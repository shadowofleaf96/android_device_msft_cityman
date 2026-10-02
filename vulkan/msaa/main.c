/*
 * cityman-vk-msaa — R8G8B8A8 color sampleCounts + optional 4x resolve
 * for Lumia 950 (MSM8992 / Adreno 418).
 *
 * Vulkan 1.0 instance. Query ImageFormatProperties.sampleCounts for
 * R8G8B8A8_UNORM color. If 4x is present, create a 4x target, clear it,
 * resolve to 1x, read back the 1x image. If only 1x, log that and exit 0
 * with samples=1 (honest; do not invent 4x). Exit 2 if n==0.
 * No WSI, no HWUI, no software MSAA.
 */

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

static unsigned sample_count_value(VkSampleCountFlags mask) {
    if (mask & VK_SAMPLE_COUNT_64_BIT)
        return 64;
    if (mask & VK_SAMPLE_COUNT_32_BIT)
        return 32;
    if (mask & VK_SAMPLE_COUNT_16_BIT)
        return 16;
    if (mask & VK_SAMPLE_COUNT_8_BIT)
        return 8;
    if (mask & VK_SAMPLE_COUNT_4_BIT)
        return 4;
    if (mask & VK_SAMPLE_COUNT_2_BIT)
        return 2;
    if (mask & VK_SAMPLE_COUNT_1_BIT)
        return 1;
    return 0;
}

static void log_sample_mask(const char* label, VkSampleCountFlags mask) {
    cityman_log("%s=0x%x 1x=%u 2x=%u 4x=%u 8x=%u 16x=%u 32x=%u 64x=%u",
                label, (unsigned)mask,
                (mask & VK_SAMPLE_COUNT_1_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_2_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_4_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_8_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_16_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_32_BIT) ? 1u : 0u,
                (mask & VK_SAMPLE_COUNT_64_BIT) ? 1u : 0u);
}

static int find_graphics_queue(VkPhysicalDevice gpu, uint32_t* family) {
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

static int create_bound_image(VkDevice dev,
                              const VkPhysicalDeviceMemoryProperties* mp,
                              const VkImageCreateInfo* ici, const char* tag,
                              VkImage* out_img, VkDeviceMemory* out_mem) {
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    uint32_t type = 0;
    VkResult r;

    r = vkCreateImage(dev, ici, NULL, out_img);
    cityman_log("vkCreateImage %s %ux%u samples=%u usage=0x%x => %d (%s)", tag,
                ici->extent.width, ici->extent.height, (unsigned)ici->samples,
                (unsigned)ici->usage, (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        return -1;

    vkGetImageMemoryRequirements(dev, *out_img, &req);
    if (find_mem_type(mp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &type) != 0 &&
        find_mem_type(mp, req.memoryTypeBits, 0, &type) != 0) {
        cityman_err("no memory type for %s bits=0x%x", tag, req.memoryTypeBits);
        return -1;
    }

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, NULL, out_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(%s) => %d (%s)", tag, (int)r,
                    vk_result_str(r));
        return -1;
    }
    r = vkBindImageMemory(dev, *out_img, *out_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory(%s) => %d (%s)", tag, (int)r,
                    vk_result_str(r));
        return -1;
    }
    return 0;
}

static int create_color_view(VkDevice dev, VkImage image, VkImageView* out) {
    VkImageViewCreateInfo vci;
    VkResult r;

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = IMG_FMT;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, out);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImageView => %d (%s)", (int)r, vk_result_str(r));
        return -1;
    }
    return 0;
}

/*
 * 4x color target, loadOp CLEAR, resolve into a 1x attachment, then
 * copy the 1x image to a host-visible buffer.
 */
static int msaa4_clear_resolve_readback(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkImageCreateInfo ici;
    VkImage msaa_img = VK_NULL_HANDLE;
    VkDeviceMemory msaa_mem = VK_NULL_HANDLE;
    VkImage resolve_img = VK_NULL_HANDLE;
    VkDeviceMemory resolve_mem = VK_NULL_HANDLE;
    VkImageView msaa_view = VK_NULL_HANDLE;
    VkImageView resolve_view = VK_NULL_HANDLE;
    VkAttachmentDescription atts[2];
    VkAttachmentReference color_ref;
    VkAttachmentReference resolve_ref;
    VkSubpassDescription sub;
    VkSubpassDependency deps[2];
    VkRenderPassCreateInfo rpci;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkImageView fb_views[2];
    VkFramebufferCreateInfo fbci;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkBufferCreateInfo bci;
    VkBuffer host_buf = VK_NULL_HANDLE;
    VkMemoryRequirements buf_req;
    VkMemoryAllocateInfo buf_ai;
    VkDeviceMemory buf_mem = VK_NULL_HANDLE;
    uint32_t buf_type = 0;
    const VkDeviceSize host_bytes = (VkDeviceSize)IMG_W * IMG_H * 4u;
    void* mapped = NULL;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearValue clear;
    VkRenderPassBeginInfo rpbi;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier bb;
    VkSubmitInfo si;
    VkResult r;
    const uint8_t* px;
    int rc = 1;

    if (find_graphics_queue(gpu, &qf) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = qf;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    r = vkCreateDevice(gpu, &dci, NULL, &dev);
    cityman_log("vkCreateDevice => %d (%s) queueFamily=%u", (int)r,
                vk_result_str(r), qf);
    if (r != VK_SUCCESS)
        return 1;

    vkGetDeviceQueue(dev, qf, 0, &queue);
    memset(&mp, 0, sizeof(mp));
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = IMG_FMT;
    ici.extent.width = IMG_W;
    ici.extent.height = IMG_H;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_4_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (create_bound_image(dev, &mp, &ici, "msaa4", &msaa_img, &msaa_mem) != 0)
        goto out_dev;

    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (create_bound_image(dev, &mp, &ici, "resolve1x", &resolve_img,
                           &resolve_mem) != 0)
        goto out_dev;

    if (create_color_view(dev, msaa_img, &msaa_view) != 0)
        goto out_dev;
    if (create_color_view(dev, resolve_img, &resolve_view) != 0)
        goto out_dev;

    memset(atts, 0, sizeof(atts));
    atts[0].format = IMG_FMT;
    atts[0].samples = VK_SAMPLE_COUNT_4_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    atts[1].format = IMG_FMT;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memset(&resolve_ref, 0, sizeof(resolve_ref));
    resolve_ref.attachment = 1;
    resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pResolveAttachments = &resolve_ref;

    memset(deps, 0, sizeof(deps));
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
    cityman_log("vkCreateRenderPass (4x clear + resolve 1x) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    fb_views[0] = msaa_view;
    fb_views[1] = resolve_view;
    memset(&fbci, 0, sizeof(fbci));
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = rp;
    fbci.attachmentCount = 2;
    fbci.pAttachments = fb_views;
    fbci.width = IMG_W;
    fbci.height = IMG_H;
    fbci.layers = 1;
    r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    cityman_log("vkCreateFramebuffer 64x64 4x+1x => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = host_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &host_buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    vkGetBufferMemoryRequirements(dev, host_buf, &buf_req);
    if (find_mem_type(&mp, buf_req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &buf_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type bits=0x%x",
                    buf_req.memoryTypeBits);
        goto out_dev;
    }

    memset(&buf_ai, 0, sizeof(buf_ai));
    buf_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_ai.allocationSize = buf_req.size;
    buf_ai.memoryTypeIndex = buf_type;
    r = vkAllocateMemory(dev, &buf_ai, NULL, &buf_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(host) => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }
    r = vkBindBufferMemory(dev, host_buf, buf_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    r = vkMapMemory(dev, buf_mem, 0, host_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(prefill) => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }
    memset(mapped, 0, (size_t)host_bytes);
    vkUnmapMemory(dev, buf_mem);
    mapped = NULL;

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    memset(&cai, 0, sizeof(cai));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(dev, &cai, &cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateCommandBuffers => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) {
        cityman_err("vkBeginCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    memset(&clear, 0, sizeof(clear));
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 1.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 1.0f;

    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = IMG_W;
    rpbi.renderArea.extent.height = IMG_H;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
    cityman_log("vkCmdBegin/EndRenderPass (clear 4x + resolve 1x)");

    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = IMG_W;
    copy.imageExtent.height = IMG_H;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, resolve_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           host_buf, 1, &copy);

    memset(&bb, 0, sizeof(bb));
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = host_buf;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);

    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkEndCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    cityman_log("vkQueueSubmit (msaa clear+resolve+copy) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    r = vkMapMemory(dev, buf_mem, 0, host_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback) => %d (%s)", (int)r, vk_result_str(r));
        goto out_dev;
    }

    px = (const uint8_t*)mapped;
    cityman_log("resolve 1x first pixel RGBA=%u,%u,%u,%u", px[0], px[1], px[2],
                px[3]);
    if (px[1] > 0) {
        cityman_log("MSAA 4x resolve ok (green channel %u)", px[1]);
        cityman_log("samples=4");
        rc = 0;
    } else {
        cityman_err("resolve 1x green channel is 0; not treating as GPU MSAA");
        rc = 1;
    }
    vkUnmapMemory(dev, buf_mem);
    mapped = NULL;

out_dev:
    if (pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(dev, pool, NULL);
    if (buf_mem != VK_NULL_HANDLE)
        vkFreeMemory(dev, buf_mem, NULL);
    if (host_buf != VK_NULL_HANDLE)
        vkDestroyBuffer(dev, host_buf, NULL);
    if (fb != VK_NULL_HANDLE)
        vkDestroyFramebuffer(dev, fb, NULL);
    if (rp != VK_NULL_HANDLE)
        vkDestroyRenderPass(dev, rp, NULL);
    if (resolve_view != VK_NULL_HANDLE)
        vkDestroyImageView(dev, resolve_view, NULL);
    if (msaa_view != VK_NULL_HANDLE)
        vkDestroyImageView(dev, msaa_view, NULL);
    if (resolve_mem != VK_NULL_HANDLE)
        vkFreeMemory(dev, resolve_mem, NULL);
    if (resolve_img != VK_NULL_HANDLE)
        vkDestroyImage(dev, resolve_img, NULL);
    if (msaa_mem != VK_NULL_HANDLE)
        vkFreeMemory(dev, msaa_mem, NULL);
    if (msaa_img != VK_NULL_HANDLE)
        vkDestroyImage(dev, msaa_img, NULL);
    vkDestroyDevice(dev, NULL);
    return rc;
}

static int query_and_maybe_msaa(VkPhysicalDevice gpu) {
    VkPhysicalDeviceProperties props;
    VkFormatProperties fp;
    VkImageFormatProperties ifp;
    VkResult r;
    VkSampleCountFlags color_samples;
    unsigned highest;

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    log_sample_mask("limits.framebufferColorSampleCounts",
                    props.limits.framebufferColorSampleCounts);

    memset(&fp, 0, sizeof(fp));
    vkGetPhysicalDeviceFormatProperties(gpu, IMG_FMT, &fp);
    cityman_log("VK_FORMAT_R8G8B8A8_UNORM optimalFeatures=0x%x colorAtt=%u",
                (unsigned)fp.optimalTilingFeatures,
                (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                        ? 1u
                        : 0u);

    memset(&ifp, 0, sizeof(ifp));
    r = vkGetPhysicalDeviceImageFormatProperties(
            gpu, IMG_FMT, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &ifp);
    cityman_log("vkGetPhysicalDeviceImageFormatProperties R8G8B8A8_UNORM "
                "2D OPTIMAL COLOR_ATTACHMENT => %d (%s)",
                (int)r, vk_result_str(r));
    if (r != VK_SUCCESS) {
        cityman_err("R8G8B8A8 color ImageFormatProperties failed; not faking 4x");
        return 1;
    }

    cityman_log("imageFormat maxExtent=%ux%ux%u maxResourceSize=%llu",
                ifp.maxExtent.width, ifp.maxExtent.height, ifp.maxExtent.depth,
                (unsigned long long)ifp.maxResourceSize);
    log_sample_mask("R8G8B8A8_UNORM color sampleCounts", ifp.sampleCounts);

    color_samples = ifp.sampleCounts;
    highest = sample_count_value(color_samples);
    if (highest == 0) {
        cityman_err("R8G8B8A8 color sampleCounts=0; not faking samples");
        return 1;
    }

    if ((color_samples & VK_SAMPLE_COUNT_4_BIT) == 0) {
        cityman_log("4x not available for R8G8B8A8 color (honest, not faking 4x)");
        cityman_log("samples=%u", highest);
        return 0;
    }

    cityman_log("4x available for R8G8B8A8 color; creating MSAA target + 1x resolve");
    return msaa4_clear_resolve_readback(gpu);
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

    cityman_log("cityman-vk-msaa start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-msaa";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-msaa";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed; not a software MSAA");
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u", (int)r,
                vk_result_str(r), n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkEnumeratePhysicalDevices failed");
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    if (n == 0) {
        cityman_err("cityman-vk-msaa: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not faking 4x MSAA.");
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
    if (n == 0) {
        cityman_err("enumerate fill empty (n=0) — not faking 4x");
        free(gpus);
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpus[0], &props);
    cityman_log("physdev[0] name=%s api=0x%x vendor=0x%x device=0x%x type=%u",
                props.deviceName, props.apiVersion, props.vendorID,
                props.deviceID, props.deviceType);

    rc = query_and_maybe_msaa(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-msaa done rc=%d", rc);
    return rc;
}
