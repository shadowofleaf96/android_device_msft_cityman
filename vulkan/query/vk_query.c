/*
 * cityman-vk-query — Vulkan 1.0 query-pool probe on Lumia 950
 * (MSM8992 / Adreno 418).
 *
 * Try VK_QUERY_TYPE_OCCLUSION, then VK_QUERY_TYPE_TIMESTAMP.
 * If vkCreateQueryPool fails, print VkResult and exit 1. Do not invent
 * timestamps or sample counts. If a pool creates, begin/end (or write
 * timestamps) around vkCmdClearColorImage, vkGetQueryPoolResults, exit 0.
 * No WSI, no HWUI, no host-clock substitution.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#define IMG_W 64
#define IMG_H 64
#define IMG_FMT VK_FORMAT_R8G8B8A8_UNORM

static void cityman_log(const char* fmt, ...)
        __attribute__((format(printf, 1, 2)));

static void cityman_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("%s\n", buf);
    fflush(stdout);
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
}

static void cityman_err(const char* fmt, ...)
        __attribute__((format(printf, 1, 2)));

static void cityman_err(const char* fmt, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s\n", buf);
    fflush(stderr);
    __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, buf);
}

static const char* vk_result_str(VkResult r) {
    switch (r) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        default:
            return "VkResult";
    }
}

static const char* query_type_str(VkQueryType t) {
    switch (t) {
        case VK_QUERY_TYPE_OCCLUSION:
            return "VK_QUERY_TYPE_OCCLUSION";
        case VK_QUERY_TYPE_TIMESTAMP:
            return "VK_QUERY_TYPE_TIMESTAMP";
        case VK_QUERY_TYPE_PIPELINE_STATISTICS:
            return "VK_QUERY_TYPE_PIPELINE_STATISTICS";
        default:
            return "VkQueryType";
    }
}

static int find_graphics_queue(VkPhysicalDevice gpu, uint32_t* family,
                               uint32_t* timestamp_bits) {
    uint32_t n = 0;
    uint32_t i;
    VkQueueFamilyProperties* props;

    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, NULL);
    if (n == 0)
        return -1;
    props = (VkQueueFamilyProperties*)calloc(n, sizeof(*props));
    if (!props)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, props);
    for (i = 0; i < n; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *family = i;
            if (timestamp_bits)
                *timestamp_bits = props[i].timestampValidBits;
            free(props);
            return 0;
        }
    }
    free(props);
    return -1;
}

static int find_mem_type(const VkPhysicalDeviceMemoryProperties* mp,
                         uint32_t type_bits, VkMemoryPropertyFlags need,
                         uint32_t* out) {
    uint32_t i;

    for (i = 0; i < mp->memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) == 0)
            continue;
        if ((mp->memoryTypes[i].propertyFlags & need) == need) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

static void image_barrier(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier b;

    memset(&b, 0, sizeof(b));
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static VkResult try_create_pool(VkDevice dev, VkQueryType type, uint32_t count,
                                VkQueryPool* out) {
    VkQueryPoolCreateInfo qci;
    VkResult r;

    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qci.queryType = type;
    qci.queryCount = count;
    r = vkCreateQueryPool(dev, &qci, NULL, out);
    cityman_log("vkCreateQueryPool %s count=%u => %d (%s) pool=%p",
                query_type_str(type), count, (int)r, vk_result_str(r),
                (void*)*out);
    if (r != VK_SUCCESS)
        *out = VK_NULL_HANDLE;
    return r;
}

static int run_query(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    uint32_t ts_bits = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures feats;
    VkPhysicalDeviceMemoryProperties mp;
    VkImageCreateInfo ici;
    VkImage image = VK_NULL_HANDLE;
    VkMemoryRequirements img_req;
    VkMemoryAllocateInfo img_ai;
    VkDeviceMemory img_mem = VK_NULL_HANDLE;
    uint32_t img_type = 0;
    VkImageViewCreateInfo ivci;
    VkImageView view = VK_NULL_HANDLE;
    VkAttachmentDescription att;
    VkAttachmentReference att_ref;
    VkSubpassDescription sub;
    VkRenderPassCreateInfo rpci;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebufferCreateInfo fbci;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkRenderPassBeginInfo rpbi;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearColorValue clear;
    VkImageSubresourceRange range;
    VkQueryPool occ_pool = VK_NULL_HANDLE;
    VkQueryPool ts_pool = VK_NULL_HANDLE;
    VkResult occ_create;
    VkResult ts_create;
    VkQueryType used = VK_QUERY_TYPE_OCCLUSION;
    VkQueryPool used_pool = VK_NULL_HANDLE;
    uint32_t used_count = 0;
    VkSubmitInfo si;
    VkResult r;
    uint64_t results[2];
    int rc = 1;

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    memset(&feats, 0, sizeof(feats));
    vkGetPhysicalDeviceFeatures(gpu, &feats);

    if (find_graphics_queue(gpu, &qf, &ts_bits) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    cityman_log("limits.timestampPeriod=%g ns/tick",
                (double)props.limits.timestampPeriod);
    cityman_log("queueFamily=%u timestampValidBits=%u", qf, ts_bits);
    cityman_log("features.occlusionQueryPrecise=%u pipelineStatisticsQuery=%u",
                feats.occlusionQueryPrecise ? 1u : 0u,
                feats.pipelineStatisticsQuery ? 1u : 0u);

    memset(&dqci, 0, sizeof(dqci));
    dqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    dqci.queueFamilyIndex = qf;
    dqci.queueCount = 1;
    dqci.pQueuePriorities = &prio;

    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqci;

    r = vkCreateDevice(gpu, &dci, NULL, &dev);
    cityman_log("vkCreateDevice => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        return 1;

    vkGetDeviceQueue(dev, qf, 0, &queue);
    memset(&mp, 0, sizeof(mp));
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);

    occ_create = try_create_pool(dev, VK_QUERY_TYPE_OCCLUSION, 1, &occ_pool);
    ts_create = VK_ERROR_FEATURE_NOT_PRESENT;
    if (ts_bits == 0) {
        cityman_log("skip TIMESTAMP: timestampValidBits=0 (not faking ticks)");
        ts_pool = VK_NULL_HANDLE;
    } else {
        ts_create = try_create_pool(dev, VK_QUERY_TYPE_TIMESTAMP, 2, &ts_pool);
    }

    if (occ_create == VK_SUCCESS && occ_pool != VK_NULL_HANDLE) {
        used = VK_QUERY_TYPE_OCCLUSION;
        used_pool = occ_pool;
        used_count = 1;
        cityman_log("using %s (begin/end around clear)", query_type_str(used));
    } else if (ts_create == VK_SUCCESS && ts_pool != VK_NULL_HANDLE) {
        used = VK_QUERY_TYPE_TIMESTAMP;
        used_pool = ts_pool;
        used_count = 2;
        cityman_log("using %s (write around clear; occlusion create failed)",
                    query_type_str(used));
    } else {
        cityman_err("vkCreateQueryPool OCCLUSION => %d (%s)", (int)occ_create,
                    vk_result_str(occ_create));
        cityman_err("vkCreateQueryPool TIMESTAMP => %d (%s)", (int)ts_create,
                    vk_result_str(ts_create));
        cityman_err("no query pool; not inventing results");
        goto out_pools;
    }

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = IMG_FMT;
    ici.extent.width = IMG_W;
    ici.extent.height = IMG_H;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    r = vkCreateImage(dev, &ici, NULL, &image);
    cityman_log("vkCreateImage 64x64 R8G8B8A8_UNORM => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pools;

    vkGetImageMemoryRequirements(dev, image, &img_req);
    if (find_mem_type(&mp, img_req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &img_type) != 0 &&
        find_mem_type(&mp, img_req.memoryTypeBits, 0, &img_type) != 0) {
        cityman_err("no memory type for image bits=0x%x", img_req.memoryTypeBits);
        goto out_image;
    }

    memset(&img_ai, 0, sizeof(img_ai));
    img_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_ai.allocationSize = img_req.size;
    img_ai.memoryTypeIndex = img_type;
    r = vkAllocateMemory(dev, &img_ai, NULL, &img_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(image) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_image;
    }
    r = vkBindImageMemory(dev, image, img_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_img_mem;
    }

    if (used == VK_QUERY_TYPE_OCCLUSION) {
        memset(&ivci, 0, sizeof(ivci));
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = IMG_FMT;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        r = vkCreateImageView(dev, &ivci, NULL, &view);
        if (r != VK_SUCCESS) {
            cityman_err("vkCreateImageView => %d (%s)", (int)r, vk_result_str(r));
            goto out_img_mem;
        }

        memset(&att, 0, sizeof(att));
        att.format = IMG_FMT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        memset(&att_ref, 0, sizeof(att_ref));
        att_ref.attachment = 0;
        att_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        memset(&sub, 0, sizeof(sub));
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &att_ref;

        memset(&rpci, 0, sizeof(rpci));
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
        if (r != VK_SUCCESS) {
            cityman_err("vkCreateRenderPass => %d (%s)", (int)r, vk_result_str(r));
            goto out_view;
        }

        memset(&fbci, 0, sizeof(fbci));
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = rp;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &view;
        fbci.width = IMG_W;
        fbci.height = IMG_H;
        fbci.layers = 1;
        r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
        if (r != VK_SUCCESS) {
            cityman_err("vkCreateFramebuffer => %d (%s)", (int)r, vk_result_str(r));
            goto out_rp;
        }
    }

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_fb;
    }

    memset(&cai, 0, sizeof(cai));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(dev, &cai, &cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateCommandBuffers => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_cmd_pool;
    }

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) {
        cityman_err("vkBeginCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_cmd_pool;
    }

    vkCmdResetQueryPool(cmd, used_pool, 0, used_count);

    memset(&clear, 0, sizeof(clear));
    clear.float32[0] = 0.0f;
    clear.float32[1] = 1.0f;
    clear.float32[2] = 0.0f;
    clear.float32[3] = 1.0f;

    if (used == VK_QUERY_TYPE_OCCLUSION) {
        memset(&rpbi, 0, sizeof(rpbi));
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = rp;
        rpbi.framebuffer = fb;
        rpbi.renderArea.extent.width = IMG_W;
        rpbi.renderArea.extent.height = IMG_H;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = (const VkClearValue*)&clear;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBeginQuery(cmd, used_pool, 0, 0);
        {
            VkClearAttachment ca;
            VkClearRect cr;

            memset(&ca, 0, sizeof(ca));
            ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ca.colorAttachment = 0;
            ca.clearValue.color = clear;
            memset(&cr, 0, sizeof(cr));
            cr.rect.extent.width = IMG_W;
            cr.rect.extent.height = IMG_H;
            cr.layerCount = 1;
            vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
        }
        vkCmdEndQuery(cmd, used_pool, 0);
        vkCmdEndRenderPass(cmd);
        cityman_log("occlusion begin/end around vkCmdClearAttachments (0,1,0,1)");
    } else {
        image_barrier(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, used_pool,
                            0);
        memset(&range, 0, sizeof(range));
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            used_pool, 1);
        cityman_log("timestamp write around vkCmdClearColorImage (0,1,0,1)");
    }

    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkEndCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_cmd_pool;
    }

    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    cityman_log("vkQueueSubmit (query+clear) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_cmd_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_cmd_pool;
    }

    memset(results, 0, sizeof(results));
    r = vkGetQueryPoolResults(dev, used_pool, 0, used_count, sizeof(results),
                              results, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    cityman_log("vkGetQueryPoolResults %s count=%u => %d (%s)",
                query_type_str(used), used_count, (int)r, vk_result_str(r));
    if (r != VK_SUCCESS) {
        cityman_err("vkGetQueryPoolResults failed; not inventing values");
        goto out_cmd_pool;
    }

    if (used == VK_QUERY_TYPE_OCCLUSION) {
        cityman_log("occlusion samples=%" PRIu64
                    " (vkGetQueryPoolResults; 64x64 attachment)",
                    results[0]);
    } else {
        uint64_t mask;
        uint64_t t0;
        uint64_t t1;
        uint64_t delta;

        if (ts_bits >= 64)
            mask = ~0ull;
        else
            mask = (ts_bits == 0) ? 0ull : ((1ull << ts_bits) - 1ull);
        t0 = results[0] & mask;
        t1 = results[1] & mask;
        delta = (t1 - t0) & mask;
        cityman_log("timestamp[0]=%" PRIu64 " timestamp[1]=%" PRIu64
                    " (raw GPU ticks, masked to %u bits)",
                    t0, t1, ts_bits);
        cityman_log("timestamp delta=%" PRIu64 " ticks * period=%g ns => %g ns",
                    delta, (double)props.limits.timestampPeriod,
                    (double)delta * (double)props.limits.timestampPeriod);
        cityman_log("timestamps are vkGetQueryPoolResults, not clock_gettime");
    }

    cityman_log("query ok type=%s", query_type_str(used));
    rc = 0;

out_cmd_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_fb:
    if (fb != VK_NULL_HANDLE)
        vkDestroyFramebuffer(dev, fb, NULL);
out_rp:
    if (rp != VK_NULL_HANDLE)
        vkDestroyRenderPass(dev, rp, NULL);
out_view:
    if (view != VK_NULL_HANDLE)
        vkDestroyImageView(dev, view, NULL);
out_img_mem:
    vkFreeMemory(dev, img_mem, NULL);
out_image:
    vkDestroyImage(dev, image, NULL);
out_pools:
    if (occ_pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(dev, occ_pool, NULL);
    if (ts_pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(dev, ts_pool, NULL);
    vkDestroyDevice(dev, NULL);
    return rc;
}

int main(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t n = 0;
    VkPhysicalDevice* gpus = NULL;
    VkPhysicalDeviceProperties props;
    int rc;

    cityman_log("cityman-vk-query start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-query";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-query";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed");
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkEnumeratePhysicalDevices failed");
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    if (n == 0) {
        cityman_err("cityman-vk-query: vkEnumeratePhysicalDevices n==0");
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    gpus = (VkPhysicalDevice*)calloc(n, sizeof(*gpus));
    if (!gpus) {
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    r = vkEnumeratePhysicalDevices(inst, &n, gpus);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkEnumeratePhysicalDevices fill failed");
        free(gpus);
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpus[0], &props);
    cityman_log("physdev[0] name=%s api=0x%x vendor=0x%x device=0x%x type=%u",
                props.deviceName, props.apiVersion, props.vendorID,
                props.deviceID, props.deviceType);

    rc = run_query(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-query done rc=%d", rc);
    return rc;
}
