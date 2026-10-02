/*
 * cityman-vk-depth — color + depth framebuffer proof for Lumia 950
 * (MSM8992 / Adreno 418).
 *
 * Vulkan 1.0 instance, 64x64 R8G8B8A8 color + D16_UNORM (or first supported
 * depth format), renderpass CLEAR depth 1.0 (no draw). Exit 0 if the depth
 * image and framebuffer create. Exit 2 if n==0. Exit 1 if no depth format.
 * No WSI, no HWUI, no software depth.
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
#define COLOR_FMT VK_FORMAT_R8G8B8A8_UNORM

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

static const char* depth_fmt_name(VkFormat f) {
    switch (f) {
        case VK_FORMAT_D16_UNORM:
            return "D16_UNORM";
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return "X8_D24_UNORM_PACK32";
        case VK_FORMAT_D32_SFLOAT:
            return "D32_SFLOAT";
        case VK_FORMAT_D16_UNORM_S8_UINT:
            return "D16_UNORM_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return "D24_UNORM_S8_UINT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return "D32_SFLOAT_S8_UINT";
        default:
            return "unknown";
    }
}

static int format_has_stencil(VkFormat f) {
    return f == VK_FORMAT_D16_UNORM_S8_UINT ||
           f == VK_FORMAT_D24_UNORM_S8_UINT ||
           f == VK_FORMAT_D32_SFLOAT_S8_UINT;
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

static int pick_depth_format(VkPhysicalDevice gpu, VkFormat* out) {
    static const VkFormat cands[] = {
            VK_FORMAT_D16_UNORM,
            VK_FORMAT_X8_D24_UNORM_PACK32,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    size_t i;

    for (i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        VkFormatProperties fp;

        memset(&fp, 0, sizeof(fp));
        vkGetPhysicalDeviceFormatProperties(gpu, cands[i], &fp);
        cityman_log("%s optimalFeatures=0x%x linearFeatures=0x%x",
                    depth_fmt_name(cands[i]), (unsigned)fp.optimalTilingFeatures,
                    (unsigned)fp.linearTilingFeatures);
        if (fp.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            *out = cands[i];
            return 0;
        }
    }
    return -1;
}

static int create_bound_image(VkDevice dev,
                              const VkPhysicalDeviceMemoryProperties* mp,
                              const VkImageCreateInfo* ici, VkImage* image,
                              VkDeviceMemory* mem) {
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    uint32_t type = 0;
    VkResult r;

    r = vkCreateImage(dev, ici, NULL, image);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImage format=%d => %d (%s)", (int)ici->format, (int)r,
                    vk_result_str(r));
        *image = VK_NULL_HANDLE;
        return -1;
    }

    vkGetImageMemoryRequirements(dev, *image, &req);
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
    r = vkAllocateMemory(dev, &ai, NULL, mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory => %d (%s)", (int)r, vk_result_str(r));
        *mem = VK_NULL_HANDLE;
        return -1;
    }
    r = vkBindImageMemory(dev, *image, *mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory => %d (%s)", (int)r, vk_result_str(r));
        return -1;
    }
    return 0;
}

static int depth_attach(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkFormat depth_fmt = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags depth_aspect;
    VkImageCreateInfo color_ici;
    VkImageCreateInfo depth_ici;
    VkImage color_img = VK_NULL_HANDLE;
    VkImage depth_img = VK_NULL_HANDLE;
    VkDeviceMemory color_mem = VK_NULL_HANDLE;
    VkDeviceMemory depth_mem = VK_NULL_HANDLE;
    VkImageViewCreateInfo vci;
    VkImageView color_view = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkAttachmentDescription atts[2];
    VkAttachmentReference color_ref;
    VkAttachmentReference depth_ref;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo rpci;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkImageView fb_atts[2];
    VkFramebufferCreateInfo fbci;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearValue clears[2];
    VkRenderPassBeginInfo rpbi;
    VkSubmitInfo si;
    VkResult r;
    int created_depth = 0;
    int created_fb = 0;
    int rc = 1;

    if (find_graphics_queue(gpu, &qf) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    if (pick_depth_format(gpu, &depth_fmt) != 0) {
        cityman_err("no depth format (D16_UNORM and fallbacks lack DEPTH_STENCIL_ATTACHMENT)");
        return 1;
    }
    cityman_log("picked depth format %s (0x%x)", depth_fmt_name(depth_fmt),
                (unsigned)depth_fmt);
    depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format_has_stencil(depth_fmt))
        depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

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

    memset(&color_ici, 0, sizeof(color_ici));
    color_ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    color_ici.imageType = VK_IMAGE_TYPE_2D;
    color_ici.format = COLOR_FMT;
    color_ici.extent.width = IMG_W;
    color_ici.extent.height = IMG_H;
    color_ici.extent.depth = 1;
    color_ici.mipLevels = 1;
    color_ici.arrayLayers = 1;
    color_ici.samples = VK_SAMPLE_COUNT_1_BIT;
    color_ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    color_ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    color_ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    color_ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (create_bound_image(dev, &mp, &color_ici, &color_img, &color_mem) != 0)
        goto out_dev;
    cityman_log("vkCreateImage 64x64 R8G8B8A8_UNORM color => 0 (VK_SUCCESS)");

    memset(&depth_ici, 0, sizeof(depth_ici));
    depth_ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_ici.imageType = VK_IMAGE_TYPE_2D;
    depth_ici.format = depth_fmt;
    depth_ici.extent.width = IMG_W;
    depth_ici.extent.height = IMG_H;
    depth_ici.extent.depth = 1;
    depth_ici.mipLevels = 1;
    depth_ici.arrayLayers = 1;
    depth_ici.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depth_ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (create_bound_image(dev, &mp, &depth_ici, &depth_img, &depth_mem) != 0)
        goto out_color;
    created_depth = 1;
    cityman_log("vkCreateImage 64x64 %s depth => 0 (VK_SUCCESS)",
                depth_fmt_name(depth_fmt));

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = color_img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = COLOR_FMT;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, &color_view);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImageView(color) => %d (%s)", (int)r, vk_result_str(r));
        goto out_depth;
    }

    vci.image = depth_img;
    vci.format = depth_fmt;
    vci.subresourceRange.aspectMask = depth_aspect;
    r = vkCreateImageView(dev, &vci, NULL, &depth_view);
    cityman_log("vkCreateImageView(depth) => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_color_view;

    memset(&atts, 0, sizeof(atts));
    atts[0].format = COLOR_FMT;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    atts[1].format = depth_fmt;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[1].stencilLoadOp = format_has_stencil(depth_fmt)
                                    ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                    : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
    cityman_log("vkCreateRenderPass color+depth => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_depth_view;

    fb_atts[0] = color_view;
    fb_atts[1] = depth_view;
    memset(&fbci, 0, sizeof(fbci));
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = rp;
    fbci.attachmentCount = 2;
    fbci.pAttachments = fb_atts;
    fbci.width = IMG_W;
    fbci.height = IMG_H;
    fbci.layers = 1;
    r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    cityman_log("vkCreateFramebuffer color+depth => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_rp;
    created_fb = 1;

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

    memset(&clears, 0, sizeof(clears));
    clears[0].color.float32[0] = 0.0f;
    clears[0].color.float32[1] = 1.0f;
    clears[0].color.float32[2] = 0.0f;
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = IMG_W;
    rpbi.renderArea.extent.height = IMG_H;
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    cityman_log("vkCmdBeginRenderPass clear color+(depth=1.0) (no draw)");
    vkCmdEndRenderPass(cmd);

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
    cityman_log("vkQueueSubmit (depth clear) => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    if (created_depth && created_fb) {
        cityman_log("depth image+fb ok");
        rc = 0;
    }

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_fb:
    vkDestroyFramebuffer(dev, fb, NULL);
out_rp:
    vkDestroyRenderPass(dev, rp, NULL);
out_depth_view:
    vkDestroyImageView(dev, depth_view, NULL);
out_color_view:
    vkDestroyImageView(dev, color_view, NULL);
out_depth:
    vkFreeMemory(dev, depth_mem, NULL);
    vkDestroyImage(dev, depth_img, NULL);
out_color:
    vkFreeMemory(dev, color_mem, NULL);
    vkDestroyImage(dev, color_img, NULL);
out_dev:
    vkDestroyDevice(dev, NULL);

    if (rc != 0 && created_depth && created_fb)
        rc = 0;
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

    cityman_log("cityman-vk-depth start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-depth";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-depth";
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
        cityman_err("cityman-vk-depth: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not a software depth attach.");
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

    rc = depth_attach(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-depth done rc=%d", rc);
    return rc;
}
