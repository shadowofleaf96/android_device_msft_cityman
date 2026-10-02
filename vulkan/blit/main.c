/*
 * cityman-vk-blit — GPU blit proof for Lumia 950 (MSM8992 / Adreno 418).
 *
 * Vulkan 1.0 instance, 64x64 R8G8B8A8 images: clear src red, vkCmdBlitImage
 * to dst, copy dst to host, print first pixel. Exit 0 if red > 0. Exit 2 if
 * n==0. No WSI, no HWUI, no software blit.
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

static int create_opt_image(VkDevice dev, const VkPhysicalDeviceMemoryProperties* mp,
                            VkImageUsageFlags usage, VkImage* out_image,
                            VkDeviceMemory* out_mem) {
    VkImageCreateInfo ici;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    uint32_t type = 0;
    VkResult r;

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
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    r = vkCreateImage(dev, &ici, NULL, out_image);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImage => %d (%s)", (int)r, vk_result_str(r));
        *out_image = VK_NULL_HANDLE;
        return -1;
    }

    vkGetImageMemoryRequirements(dev, *out_image, &req);
    if (find_mem_type(mp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &type) != 0 &&
        find_mem_type(mp, req.memoryTypeBits, 0, &type) != 0) {
        cityman_err("no memory type for image bits=0x%x", req.memoryTypeBits);
        return -1;
    }

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, NULL, out_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(image) => %d (%s)", (int)r, vk_result_str(r));
        *out_mem = VK_NULL_HANDLE;
        return -1;
    }
    r = vkBindImageMemory(dev, *out_image, *out_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory => %d (%s)", (int)r, vk_result_str(r));
        return -1;
    }
    return 0;
}

static int blit_and_readback(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkFormatProperties fp;
    VkImage src = VK_NULL_HANDLE;
    VkImage dst = VK_NULL_HANDLE;
    VkDeviceMemory src_mem = VK_NULL_HANDLE;
    VkDeviceMemory dst_mem = VK_NULL_HANDLE;
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
    VkClearColorValue clear;
    VkImageSubresourceRange range;
    VkImageBlit blit;
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

    memset(&fp, 0, sizeof(fp));
    vkGetPhysicalDeviceFormatProperties(gpu, IMG_FMT, &fp);
    cityman_log("VK_FORMAT_R8G8B8A8_UNORM optimalFeatures=0x%x linearFeatures=0x%x",
                (unsigned)fp.optimalTilingFeatures,
                (unsigned)fp.linearTilingFeatures);
    if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0 ||
        (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
        cityman_err("R8G8B8A8_UNORM missing blit src/dst (opt=0x%x)",
                    (unsigned)fp.optimalTilingFeatures);
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

    if (create_opt_image(dev, &mp,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         &src, &src_mem) != 0) {
        cityman_err("src image create failed");
        goto out_src;
    }
    cityman_log("src image 64x64 R8G8B8A8_UNORM ok");

    if (create_opt_image(dev, &mp,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         &dst, &dst_mem) != 0) {
        cityman_err("dst image create failed");
        goto out_dst;
    }
    cityman_log("dst image 64x64 R8G8B8A8_UNORM ok");

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = host_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &host_buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_dst;
    }

    vkGetBufferMemoryRequirements(dev, host_buf, &buf_req);
    if (find_mem_type(&mp, buf_req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &buf_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type bits=0x%x",
                    buf_req.memoryTypeBits);
        goto out_buf;
    }

    memset(&buf_ai, 0, sizeof(buf_ai));
    buf_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_ai.allocationSize = buf_req.size;
    buf_ai.memoryTypeIndex = buf_type;
    r = vkAllocateMemory(dev, &buf_ai, NULL, &buf_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(host) => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf;
    }
    r = vkBindBufferMemory(dev, host_buf, buf_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf_mem;
    }

    r = vkMapMemory(dev, buf_mem, 0, host_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(prefill) => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf_mem;
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
        goto out_buf_mem;
    }

    memset(&cai, 0, sizeof(cai));
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(dev, &cai, &cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateCommandBuffers => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) {
        cityman_err("vkBeginCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    image_barrier(cmd, src, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(cmd, dst, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&clear, 0, sizeof(clear));
    clear.float32[0] = 1.0f;
    clear.float32[1] = 0.0f;
    clear.float32[2] = 0.0f;
    clear.float32[3] = 1.0f;
    memset(&range, 0, sizeof(range));
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear,
                         1, &range);
    cityman_log("vkCmdClearColorImage src (1,0,0,1)");

    image_barrier(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&blit, 0, sizeof(blit));
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = IMG_W;
    blit.srcOffsets[1].y = IMG_H;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1].x = IMG_W;
    blit.dstOffsets[1].y = IMG_H;
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_NEAREST);
    cityman_log("vkCmdBlitImage src->dst NEAREST 64x64");

    image_barrier(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = IMG_W;
    copy.imageExtent.height = IMG_H;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
        goto out_pool;
    }

    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    cityman_log("vkQueueSubmit (clear+blit+copy) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, buf_mem, 0, host_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    px = (const uint8_t*)mapped;
    cityman_log("dst first pixel RGBA=%u,%u,%u,%u", px[0], px[1], px[2], px[3]);
    if (px[0] > 0) {
        cityman_log("GPU blit ok (red channel %u)", px[0]);
        rc = 0;
    } else {
        cityman_err("red channel is 0; blit did not write dst");
        rc = 1;
    }
    vkUnmapMemory(dev, buf_mem);
    mapped = NULL;

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_buf_mem:
    vkFreeMemory(dev, buf_mem, NULL);
out_buf:
    vkDestroyBuffer(dev, host_buf, NULL);
out_dst:
    vkFreeMemory(dev, dst_mem, NULL);
    vkDestroyImage(dev, dst, NULL);
out_src:
    vkFreeMemory(dev, src_mem, NULL);
    vkDestroyImage(dev, src, NULL);
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

    cityman_log("cityman-vk-blit start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-blit";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-blit";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed; not a software blit");
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
        cityman_err("cityman-vk-blit: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not software-blitting.");
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

    rc = blit_and_readback(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-blit done rc=%d", rc);
    return rc;
}
