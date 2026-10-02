/*
 * cityman-vk-copy — color image + vkCmdCopyImageToBuffer on Lumia 950
 * (MSM8992 / Adreno 418).
 *
 * Vulkan 1.0. Clear a 64x64 R8G8B8A8 image, then copy to host twice:
 *   bufferRowLength = 0  (tightly packed; implied row texels = width)
 *   bufferRowLength = 80 (padded; vs tightly packed rowBytes)
 * Exit 0 if the copy succeeds and the first pixel is valid after the clear.
 * Exit 2 if vkEnumeratePhysicalDevices n==0.
 * No WSI, no HWUI, no software fill of the image.
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
#define BPP 4u
#define PAD_ROW_TEXELS 80u
#define PREFILL 0xCDu

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

static int make_host_buffer(VkDevice dev, const VkPhysicalDeviceMemoryProperties* mp,
                            VkDeviceSize bytes, VkBuffer* out_buf,
                            VkDeviceMemory* out_mem) {
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    uint32_t type = 0;
    VkResult r;
    void* mapped = NULL;

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, out_buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer(%llu) => %d (%s)",
                    (unsigned long long)bytes, (int)r, vk_result_str(r));
        return -1;
    }

    vkGetBufferMemoryRequirements(dev, *out_buf, &req);
    if (find_mem_type(mp, req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type bits=0x%x",
                    req.memoryTypeBits);
        vkDestroyBuffer(dev, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        return -1;
    }

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, NULL, out_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(host) => %d (%s)", (int)r, vk_result_str(r));
        vkDestroyBuffer(dev, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        return -1;
    }
    r = vkBindBufferMemory(dev, *out_buf, *out_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
        vkFreeMemory(dev, *out_mem, NULL);
        vkDestroyBuffer(dev, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        *out_mem = VK_NULL_HANDLE;
        return -1;
    }

    r = vkMapMemory(dev, *out_mem, 0, bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(prefill) => %d (%s)", (int)r, vk_result_str(r));
        vkFreeMemory(dev, *out_mem, NULL);
        vkDestroyBuffer(dev, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        *out_mem = VK_NULL_HANDLE;
        return -1;
    }
    memset(mapped, PREFILL, (size_t)bytes);
    vkUnmapMemory(dev, *out_mem);
    return 0;
}

static void fill_copy(VkBufferImageCopy* copy, uint32_t row_texels) {
    memset(copy, 0, sizeof(*copy));
    copy->bufferRowLength = row_texels;
    copy->bufferImageHeight = 0;
    copy->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy->imageSubresource.layerCount = 1;
    copy->imageExtent.width = IMG_W;
    copy->imageExtent.height = IMG_H;
    copy->imageExtent.depth = 1;
}

static int pixel_valid(const uint8_t* px) {
    /* Clear was (0,1,0,1) on R8G8B8A8_UNORM. */
    return px[0] == 0 && px[1] == 255 && px[2] == 0 && px[3] == 255;
}

static int copy_and_readback(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkFormatProperties fp;
    VkImageCreateInfo ici;
    VkImage image = VK_NULL_HANDLE;
    VkMemoryRequirements img_req;
    VkMemoryAllocateInfo img_ai;
    VkDeviceMemory img_mem = VK_NULL_HANDLE;
    uint32_t img_type = 0;
    const VkDeviceSize tight_bytes = (VkDeviceSize)IMG_W * IMG_H * BPP;
    const VkDeviceSize pad_bytes = (VkDeviceSize)PAD_ROW_TEXELS * IMG_H * BPP;
    const uint32_t tight_row_bytes = IMG_W * BPP;
    const uint32_t pad_row_bytes = PAD_ROW_TEXELS * BPP;
    VkBuffer tight_buf = VK_NULL_HANDLE;
    VkDeviceMemory tight_mem = VK_NULL_HANDLE;
    VkBuffer pad_buf = VK_NULL_HANDLE;
    VkDeviceMemory pad_mem = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearColorValue clear;
    VkImageSubresourceRange range;
    VkBufferImageCopy copy_tight;
    VkBufferImageCopy copy_pad;
    VkBufferMemoryBarrier bb[2];
    VkSubmitInfo si;
    VkResult r;
    void* mapped = NULL;
    const uint8_t* px;
    uint32_t gap_ok = 1;
    uint32_t gi;
    int tight_ok = 0;
    int pad_ok = 0;
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
    if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0 &&
        (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0 &&
        (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
        cityman_err("R8G8B8A8_UNORM has no clear/transfer-dst features");
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
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    r = vkCreateImage(dev, &ici, NULL, &image);
    cityman_log("vkCreateImage color 64x64 R8G8B8A8_UNORM => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    vkGetImageMemoryRequirements(dev, image, &img_req);
    if (find_mem_type(&mp, img_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &img_type) != 0 &&
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
        cityman_err("vkAllocateMemory(image) => %d (%s)", (int)r, vk_result_str(r));
        goto out_image;
    }
    r = vkBindImageMemory(dev, image, img_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_img_mem;
    }

    if (make_host_buffer(dev, &mp, tight_bytes, &tight_buf, &tight_mem) != 0)
        goto out_img_mem;
    if (make_host_buffer(dev, &mp, pad_bytes, &pad_buf, &pad_mem) != 0)
        goto out_tight;

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_pad;
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

    image_barrier(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&clear, 0, sizeof(clear));
    clear.float32[0] = 0.0f;
    clear.float32[1] = 1.0f;
    clear.float32[2] = 0.0f;
    clear.float32[3] = 1.0f;
    memset(&range, 0, sizeof(range));
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear,
                         1, &range);
    cityman_log("vkCmdClearColorImage (0,1,0,1)");

    image_barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    fill_copy(&copy_tight, 0);
    fill_copy(&copy_pad, PAD_ROW_TEXELS);
    cityman_log("bufferRowLength=0 tightlyPackedRowTexels=%u rowBytes=%u",
                IMG_W, tight_row_bytes);
    cityman_log("bufferRowLength=%u rowBytes=%u vs tightly packed rowBytes=%u "
                "(delta=%u texels, %u bytes)",
                PAD_ROW_TEXELS, pad_row_bytes, tight_row_bytes,
                PAD_ROW_TEXELS - IMG_W, pad_row_bytes - tight_row_bytes);

    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           tight_buf, 1, &copy_tight);
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           pad_buf, 1, &copy_pad);
    cityman_log("vkCmdCopyImageToBuffer x2 (tight + padded)");

    memset(bb, 0, sizeof(bb));
    bb[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb[0].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb[0].buffer = tight_buf;
    bb[0].size = VK_WHOLE_SIZE;
    bb[1] = bb[0];
    bb[1].buffer = pad_buf;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 2, bb, 0, NULL);

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
    cityman_log("vkQueueSubmit (clear+copy) => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, tight_mem, 0, tight_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(tight) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }
    px = (const uint8_t*)mapped;
    cityman_log("tight first pixel RGBA=%u,%u,%u,%u", px[0], px[1], px[2], px[3]);
    if (IMG_H > 1) {
        const uint8_t* row1 = px + tight_row_bytes;
        cityman_log("tight row1@%u RGBA=%u,%u,%u,%u", tight_row_bytes, row1[0],
                    row1[1], row1[2], row1[3]);
    }
    tight_ok = pixel_valid(px);
    vkUnmapMemory(dev, tight_mem);
    mapped = NULL;

    r = vkMapMemory(dev, pad_mem, 0, pad_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(pad) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }
    px = (const uint8_t*)mapped;
    cityman_log("pad first pixel RGBA=%u,%u,%u,%u", px[0], px[1], px[2], px[3]);
    if (IMG_H > 1) {
        const uint8_t* row1_pad = px + pad_row_bytes;
        const uint8_t* row1_if_tight = px + tight_row_bytes;
        cityman_log("pad row1@%u (bufferRowLength) RGBA=%u,%u,%u,%u",
                    pad_row_bytes, row1_pad[0], row1_pad[1], row1_pad[2],
                    row1_pad[3]);
        cityman_log("pad @tight-rowBytes=%u RGBA=%u,%u,%u,%u (expect prefill "
                    "0xcd if ICD honors bufferRowLength)",
                    tight_row_bytes, row1_if_tight[0], row1_if_tight[1],
                    row1_if_tight[2], row1_if_tight[3]);
    }
    for (gi = tight_row_bytes; gi < pad_row_bytes; gi++) {
        if (px[gi] != PREFILL) {
            gap_ok = 0;
            break;
        }
    }
    cityman_log("pad row0 gap [%u..%u) stays 0xcd=%s", tight_row_bytes,
                pad_row_bytes, gap_ok ? "YES" : "NO");
    pad_ok = pixel_valid(px);
    vkUnmapMemory(dev, pad_mem);
    mapped = NULL;

    if (tight_ok) {
        cityman_log("copy ok; first pixel valid after clear (tight "
                    "bufferRowLength=0)");
        if (pad_ok)
            cityman_log("padded bufferRowLength=%u first pixel also valid",
                        PAD_ROW_TEXELS);
        rc = 0;
    } else {
        cityman_err("first pixel not valid after clear (want 0,255,0,255)");
        rc = 1;
    }

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_pad:
    vkFreeMemory(dev, pad_mem, NULL);
    vkDestroyBuffer(dev, pad_buf, NULL);
out_tight:
    vkFreeMemory(dev, tight_mem, NULL);
    vkDestroyBuffer(dev, tight_buf, NULL);
out_img_mem:
    vkFreeMemory(dev, img_mem, NULL);
out_image:
    vkDestroyImage(dev, image, NULL);
out_dev:
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

    cityman_log("cityman-vk-copy start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-copy";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-copy";
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
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u", (int)r,
                vk_result_str(r), n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkEnumeratePhysicalDevices failed");
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    if (n == 0) {
        cityman_err("cityman-vk-copy: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU.");
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

    rc = copy_and_readback(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-copy done rc=%d", rc);
    return rc;
}
