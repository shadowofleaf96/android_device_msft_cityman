/*
 * cityman-vk-tex — 8x8 R8G8B8A8 sample proof for Lumia 950 (MSM8992 / Adreno 418).
 *
 * Vulkan 1.0 instance. Upload a solid known-color 8x8 texture, sample it in
 * the fragment shader (combined-image sampler, nearest), draw a fullscreen
 * triangle to an offscreen color target, copy to host. Exit 0 if the
 * readback matches the known color. Exit 2 if n==0.
 * No WSI, no HWUI, no software fill pretending to be a GPU sample.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders.h"

#include <log/log.h>
#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#define TEX_W 8u
#define TEX_H 8u
#define FB_W 16u
#define FB_H 16u
#define IMG_FMT VK_FORMAT_R8G8B8A8_UNORM

/* Known texel: not a typical CLEAR and not 0. */
#define TEX_R 0xCCu
#define TEX_G 0x33u
#define TEX_B 0x99u
#define TEX_A 0xFFu

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

static VkResult make_shader(VkDevice dev, const uint32_t* words, uint32_t nw,
                            VkShaderModule* out) {
    VkShaderModuleCreateInfo ci;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)nw * sizeof(uint32_t);
    ci.pCode = words;
    return vkCreateShaderModule(dev, &ci, NULL, out);
}

static int color_match(const uint8_t* px) {
    return px[0] == TEX_R && px[1] == TEX_G && px[2] == TEX_B && px[3] == TEX_A;
}

static int sample_and_readback(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkFormatProperties fp;
    VkImageCreateInfo ici;
    VkImage tex = VK_NULL_HANDLE;
    VkImage fb_img = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkDeviceMemory tex_mem = VK_NULL_HANDLE;
    VkDeviceMemory fb_mem = VK_NULL_HANDLE;
    VkDeviceMemory stage_mem = VK_NULL_HANDLE;
    VkDeviceMemory host_mem = VK_NULL_HANDLE;
    uint32_t tex_type = 0;
    uint32_t fb_type = 0;
    uint32_t stage_type = 0;
    uint32_t host_type = 0;
    VkBufferCreateInfo bci;
    VkBuffer stage = VK_NULL_HANDLE;
    VkBuffer host_buf = VK_NULL_HANDLE;
    const VkDeviceSize tex_bytes = (VkDeviceSize)TEX_W * TEX_H * 4u;
    const VkDeviceSize fb_bytes = (VkDeviceSize)FB_W * FB_H * 4u;
    void* mapped = NULL;
    uint8_t* fill;
    uint32_t i;
    VkImageViewCreateInfo vci;
    VkImageView tex_view = VK_NULL_HANDLE;
    VkImageView fb_view = VK_NULL_HANDLE;
    VkSamplerCreateInfo sci;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayoutBinding bind;
    VkDescriptorSetLayoutCreateInfo dslci;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPoolSize dps;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkDescriptorImageInfo dii;
    VkWriteDescriptorSet write;
    VkAttachmentDescription att;
    VkAttachmentReference color_ref;
    VkSubpassDescription sub;
    VkSubpassDependency deps[2];
    VkRenderPassCreateInfo rpci;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebufferCreateInfo fbci;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkViewport viewport;
    VkRect2D scissor;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState blend_att;
    VkPipelineColorBlendStateCreateInfo cb;
    VkGraphicsPipelineCreateInfo gpci;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkBufferImageCopy bic;
    VkClearValue clear;
    VkRenderPassBeginInfo rpbi;
    VkBufferMemoryBarrier bb;
    VkSubmitInfo si;
    VkResult r;
    const uint8_t* px;
    uint32_t match_n = 0;
    int rc = 1;

    if (find_graphics_queue(gpu, &qf) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    memset(&fp, 0, sizeof(fp));
    vkGetPhysicalDeviceFormatProperties(gpu, IMG_FMT, &fp);
    cityman_log("VK_FORMAT_R8G8B8A8_UNORM optimal=0x%x linear=0x%x",
                (unsigned)fp.optimalTilingFeatures,
                (unsigned)fp.linearTilingFeatures);
    if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        cityman_err("R8G8B8A8_UNORM has no SAMPLED_IMAGE");
        return 1;
    }
    if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
        cityman_err("R8G8B8A8_UNORM has no COLOR_ATTACHMENT");
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
    ici.extent.width = TEX_W;
    ici.extent.height = TEX_H;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    r = vkCreateImage(dev, &ici, NULL, &tex);
    cityman_log("vkCreateImage tex 8x8 R8G8B8A8_UNORM => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    vkGetImageMemoryRequirements(dev, tex, &req);
    if (find_mem_type(&mp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &tex_type) != 0 &&
        find_mem_type(&mp, req.memoryTypeBits, 0, &tex_type) != 0) {
        cityman_err("no memory type for tex bits=0x%x", req.memoryTypeBits);
        goto out_tex;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = tex_type;
    r = vkAllocateMemory(dev, &ai, NULL, &tex_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(tex) => %d (%s)", (int)r, vk_result_str(r));
        goto out_tex;
    }
    r = vkBindImageMemory(dev, tex, tex_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory(tex) => %d (%s)", (int)r, vk_result_str(r));
        goto out_tex_mem;
    }

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = IMG_FMT;
    ici.extent.width = FB_W;
    ici.extent.height = FB_H;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    r = vkCreateImage(dev, &ici, NULL, &fb_img);
    cityman_log("vkCreateImage fb %ux%u R8G8B8A8_UNORM => %d (%s)", FB_W, FB_H,
                (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_tex_mem;

    vkGetImageMemoryRequirements(dev, fb_img, &req);
    if (find_mem_type(&mp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      &fb_type) != 0 &&
        find_mem_type(&mp, req.memoryTypeBits, 0, &fb_type) != 0) {
        cityman_err("no memory type for fb bits=0x%x", req.memoryTypeBits);
        goto out_fb;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = fb_type;
    r = vkAllocateMemory(dev, &ai, NULL, &fb_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(fb) => %d (%s)", (int)r, vk_result_str(r));
        goto out_fb;
    }
    r = vkBindImageMemory(dev, fb_img, fb_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory(fb) => %d (%s)", (int)r, vk_result_str(r));
        goto out_fb_mem;
    }

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = tex_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &stage);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer(stage) => %d (%s)", (int)r, vk_result_str(r));
        goto out_fb_mem;
    }
    vkGetBufferMemoryRequirements(dev, stage, &req);
    if (find_mem_type(&mp, req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &stage_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type for stage bits=0x%x",
                    req.memoryTypeBits);
        goto out_stage;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = stage_type;
    r = vkAllocateMemory(dev, &ai, NULL, &stage_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(stage) => %d (%s)", (int)r, vk_result_str(r));
        goto out_stage;
    }
    r = vkBindBufferMemory(dev, stage, stage_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory(stage) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_stage_mem;
    }

    r = vkMapMemory(dev, stage_mem, 0, tex_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(stage) => %d (%s)", (int)r, vk_result_str(r));
        goto out_stage_mem;
    }
    fill = (uint8_t*)mapped;
    for (i = 0; i < TEX_W * TEX_H; i++) {
        fill[i * 4u + 0u] = TEX_R;
        fill[i * 4u + 1u] = TEX_G;
        fill[i * 4u + 2u] = TEX_B;
        fill[i * 4u + 3u] = TEX_A;
    }
    vkUnmapMemory(dev, stage_mem);
    mapped = NULL;
    cityman_log("tex fill 8x8 RGBA=%u,%u,%u,%u", TEX_R, TEX_G, TEX_B, TEX_A);

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = fb_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &host_buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer(host) => %d (%s)", (int)r, vk_result_str(r));
        goto out_stage_mem;
    }
    vkGetBufferMemoryRequirements(dev, host_buf, &req);
    if (find_mem_type(&mp, req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &host_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type for host bits=0x%x",
                    req.memoryTypeBits);
        goto out_host;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = host_type;
    r = vkAllocateMemory(dev, &ai, NULL, &host_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(host) => %d (%s)", (int)r, vk_result_str(r));
        goto out_host;
    }
    r = vkBindBufferMemory(dev, host_buf, host_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory(host) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_host_mem;
    }
    r = vkMapMemory(dev, host_mem, 0, fb_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(host prefill) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_host_mem;
    }
    memset(mapped, 0xAA, (size_t)fb_bytes);
    vkUnmapMemory(dev, host_mem);
    mapped = NULL;

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = tex;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = IMG_FMT;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, &tex_view);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImageView(tex) => %d (%s)", (int)r, vk_result_str(r));
        goto out_host_mem;
    }

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = fb_img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = IMG_FMT;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, &fb_view);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImageView(fb) => %d (%s)", (int)r, vk_result_str(r));
        goto out_tex_view;
    }

    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    r = vkCreateSampler(dev, &sci, NULL, &sampler);
    cityman_log("vkCreateSampler NEAREST clamp => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_fb_view;

    memset(&bind, 0, sizeof(bind));
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &bind;
    r = vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateDescriptorSetLayout => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_sampler;
    }

    memset(&dps, 0, sizeof(dps));
    dps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps.descriptorCount = 1;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    r = vkCreateDescriptorPool(dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateDescriptorPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_dsl;
    }

    memset(&dsai, 0, sizeof(dsai));
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    r = vkAllocateDescriptorSets(dev, &dsai, &dset);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateDescriptorSets => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_dpool;
    }

    memset(&dii, 0, sizeof(dii));
    dii.sampler = sampler;
    dii.imageView = tex_view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dset;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);
    cityman_log("descriptor combined-image-sampler set=0 binding=0");

    memset(&att, 0, sizeof(att));
    att.format = IMG_FMT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    memset(deps, 0, sizeof(deps));
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
    cityman_log("vkCreateRenderPass => %d (%s) loadOp=CLEAR 0,0,0,0", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dpool;

    memset(&fbci, 0, sizeof(fbci));
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = rp;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &fb_view;
    fbci.width = FB_W;
    fbci.height = FB_H;
    fbci.layers = 1;
    r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateFramebuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_rp;
    }

    r = make_shader(dev, cityman_tex_vert_spv, cityman_tex_vert_spv_word_count, &vs);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateShaderModule(vert) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_fbobj;
    }
    r = make_shader(dev, cityman_tex_frag_spv, cityman_tex_frag_spv_word_count, &fs);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateShaderModule(frag) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_vs;
    }

    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;
    r = vkCreatePipelineLayout(dev, &plci, NULL, &layout);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreatePipelineLayout => %d (%s)", (int)r, vk_result_str(r));
        goto out_fs;
    }

    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)FB_W;
    viewport.height = (float)FB_H;
    viewport.maxDepth = 1.0f;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width = FB_W;
    scissor.extent.height = FB_H;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    memset(&blend_att, 0, sizeof(blend_att));
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend_att;

    memset(&gpci, 0, sizeof(gpci));
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.layout = layout;
    gpci.renderPass = rp;
    r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
    cityman_log("vkCreateGraphicsPipelines (sampler frag) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_layout;

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_pipe;
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

    image_barrier(cmd, tex, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&bic, 0, sizeof(bic));
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent.width = TEX_W;
    bic.imageExtent.height = TEX_H;
    bic.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, stage, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &bic);

    image_barrier(cmd, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    memset(&clear, 0, sizeof(clear));
    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = FB_W;
    rpbi.renderArea.extent.height = FB_H;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &dset, 0, NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    cityman_log("vkCmdDraw fullscreen triangle, sample 8x8 at vUv");

    memset(&bic, 0, sizeof(bic));
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent.width = FB_W;
    bic.imageExtent.height = FB_H;
    bic.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, fb_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           host_buf, 1, &bic);

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
    cityman_log("vkQueueSubmit (upload+sample+copy) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, host_mem, 0, fb_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    px = (const uint8_t*)mapped;
    cityman_log("first pixel RGBA=%u,%u,%u,%u expect %u,%u,%u,%u", px[0], px[1],
                px[2], px[3], TEX_R, TEX_G, TEX_B, TEX_A);
    for (i = 0; i < 8u && i < FB_W * FB_H; i++) {
        cityman_log("px[%u]=%02x %02x %02x %02x", i, px[i * 4u + 0u],
                    px[i * 4u + 1u], px[i * 4u + 2u], px[i * 4u + 3u]);
    }
    for (i = 0; i < FB_W * FB_H; i++) {
        if (color_match(px + i * 4u))
            match_n++;
    }
    cityman_log("match_n=%u / %u", match_n, FB_W * FB_H);

    if (color_match(px)) {
        cityman_log("sampled color matches known texel (fragment sampler)");
        rc = 0;
    } else {
        cityman_err("sampled color mismatch; not treating as a GPU sample");
        rc = 1;
    }
    vkUnmapMemory(dev, host_mem);
    mapped = NULL;

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_pipe:
    vkDestroyPipeline(dev, pipe, NULL);
out_layout:
    vkDestroyPipelineLayout(dev, layout, NULL);
out_fs:
    vkDestroyShaderModule(dev, fs, NULL);
out_vs:
    vkDestroyShaderModule(dev, vs, NULL);
out_fbobj:
    vkDestroyFramebuffer(dev, fb, NULL);
out_rp:
    vkDestroyRenderPass(dev, rp, NULL);
out_dpool:
    vkDestroyDescriptorPool(dev, dpool, NULL);
out_dsl:
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
out_sampler:
    vkDestroySampler(dev, sampler, NULL);
out_fb_view:
    vkDestroyImageView(dev, fb_view, NULL);
out_tex_view:
    vkDestroyImageView(dev, tex_view, NULL);
out_host_mem:
    vkFreeMemory(dev, host_mem, NULL);
out_host:
    vkDestroyBuffer(dev, host_buf, NULL);
out_stage_mem:
    vkFreeMemory(dev, stage_mem, NULL);
out_stage:
    vkDestroyBuffer(dev, stage, NULL);
out_fb_mem:
    vkFreeMemory(dev, fb_mem, NULL);
out_fb:
    vkDestroyImage(dev, fb_img, NULL);
out_tex_mem:
    vkFreeMemory(dev, tex_mem, NULL);
out_tex:
    vkDestroyImage(dev, tex, NULL);
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

    cityman_log("cityman-vk-tex start (Vulkan 1.0, 8x8 sampler)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-tex";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-tex";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed; not a software sample");
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
        cityman_err("cityman-vk-tex: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not software-sampling.");
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

    rc = sample_and_readback(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-tex done rc=%d", rc);
    return rc;
}
