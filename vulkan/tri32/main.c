/*
 * cityman-vk-tri32 — 32-bit Vulkan 1.0 offscreen CLEAR+draw+readback
 * for Lumia 950 (MSM8992 / Adreno 418).
 *
 * Window-less: instance, enumerate, vkCreateDevice, renderpass CLEAR
 * (magenta) + vkCmdDraw of cityman_tri SPIR-V, copy to HOST_VISIBLE,
 * prove a sampled GPU pixel is not 0,0,0,0. No present. No HWUI.
 *
 * Exit 2 if vkEnumeratePhysicalDevices n==0.
 * Exit 0 only if a sampled GPU pixel is not 0,0,0,0.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shaders/cityman_tri.vert.spv.h"
#include "../shaders/cityman_tri.frag.spv.h"

#include <log/log.h>
#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#define OFFSCREEN_W 64u
#define OFFSCREEN_H 64u
#define OFFSCREEN_PPM "/data/local/tmp/cityman-vk-tri32.ppm"

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
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
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

static int find_mem_type(VkPhysicalDevice gpu, uint32_t type_bits,
                         VkMemoryPropertyFlags flags, uint32_t* out_index) {
    VkPhysicalDeviceMemoryProperties mp;
    uint32_t i;

    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    for (i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) == 0)
            continue;
        if ((mp.memoryTypes[i].propertyFlags & flags) == flags) {
            *out_index = i;
            return 0;
        }
    }
    return -1;
}

static VkFormat pick_offscreen_format(VkPhysicalDevice gpu) {
    static const VkFormat cands[] = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
    };
    uint32_t i;
    VkFormat fallback = VK_FORMAT_UNDEFINED;

    for (i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        VkFormatProperties fp;
        VkImageFormatProperties ifp;
        VkResult r;

        memset(&fp, 0, sizeof(fp));
        vkGetPhysicalDeviceFormatProperties(gpu, cands[i], &fp);
        if ((fp.optimalTilingFeatures &
             VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0)
            continue;
        if (fallback == VK_FORMAT_UNDEFINED)
            fallback = cands[i];
        memset(&ifp, 0, sizeof(ifp));
        r = vkGetPhysicalDeviceImageFormatProperties(
                gpu, cands[i], VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                0, &ifp);
        if (r == VK_SUCCESS)
            return cands[i];
    }
    return fallback;
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

static int write_ppm_rgb(const char* path, uint32_t w, uint32_t h,
                         const uint8_t* rgba, VkFormat fmt) {
    FILE* f;
    uint32_t i, n;

    f = fopen(path, "wb");
    if (!f) {
        cityman_err("fopen %s failed: %s", path, strerror(errno));
        return -1;
    }
    if (fprintf(f, "P6\n%u %u\n255\n", w, h) < 0) {
        fclose(f);
        return -1;
    }
    n = w * h;
    for (i = 0; i < n; i++) {
        const uint8_t* p = rgba + i * 4;
        uint8_t rgb[3];

        if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB) {
            rgb[0] = p[2];
            rgb[1] = p[1];
            rgb[2] = p[0];
        } else {
            rgb[0] = p[0];
            rgb[1] = p[1];
            rgb[2] = p[2];
        }
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            return -1;
        }
    }
    if (fclose(f) != 0)
        return -1;
    return 0;
}

static int pixel_not_black(const uint8_t* rgba, uint32_t count) {
    uint32_t i;

    for (i = 0; i < count; i++) {
        if (rgba[i * 4 + 0] | rgba[i * 4 + 1] | rgba[i * 4 + 2] |
            rgba[i * 4 + 3])
            return 1;
    }
    return 0;
}

static int alloc_bound(VkDevice dev, VkPhysicalDevice gpu,
                       const VkMemoryRequirements* req,
                       VkMemoryPropertyFlags prefer,
                       VkMemoryPropertyFlags fallback, VkDeviceMemory* out_mem,
                       uint32_t* out_type, VkMemoryPropertyFlags* out_flags) {
    uint32_t type = 0;
    VkMemoryAllocateInfo ai;
    VkResult r;
    VkPhysicalDeviceMemoryProperties mp;

    *out_mem = VK_NULL_HANDLE;
    if (find_mem_type(gpu, req->memoryTypeBits, prefer, &type) != 0 &&
        find_mem_type(gpu, req->memoryTypeBits, fallback, &type) != 0) {
        cityman_err("no memory type bits=0x%x flags=0x%x/0x%x",
                    req->memoryTypeBits, prefer, fallback);
        return -1;
    }
    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req->size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, NULL, out_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory => %d (%s) size=%llu type=%u", (int)r,
                    vk_result_str(r), (unsigned long long)req->size, type);
        *out_mem = VK_NULL_HANDLE;
        return -1;
    }
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    *out_type = type;
    *out_flags = mp.memoryTypes[type].propertyFlags;
    return 0;
}

/*
 * Window-less: real GPU renderpass CLEAR (magenta) + triangle from
 * ../shaders/cityman_tri.*.spv.h, copy to HOST_VISIBLE, prove a non-zero
 * pixel. No present.
 */
static int offscreen_clear_draw(VkPhysicalDevice gpu) {
    const uint32_t w = OFFSCREEN_W;
    const uint32_t h = OFFSCREEN_H;
    const VkDeviceSize pix_bytes = (VkDeviceSize)w * h * 4u;
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkFormat fmt;
    VkImageCreateInfo ici;
    VkImage image = VK_NULL_HANDLE;
    VkMemoryRequirements img_req;
    VkDeviceMemory img_mem = VK_NULL_HANDLE;
    uint32_t img_type = 0;
    VkMemoryPropertyFlags img_flags = 0;
    VkImageViewCreateInfo vci;
    VkImageView view = VK_NULL_HANDLE;
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
    int drew_tri = 0;
    VkBufferCreateInfo bci;
    VkBuffer buf = VK_NULL_HANDLE;
    VkMemoryRequirements buf_req;
    VkDeviceMemory buf_mem = VK_NULL_HANDLE;
    uint32_t buf_type = 0;
    VkMemoryPropertyFlags buf_flags = 0;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearValue clear;
    VkRenderPassBeginInfo rpbi;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier host_bar;
    VkSubmitInfo si;
    VkResult r;
    void* mapped = NULL;
    uint8_t* pixels = NULL;
    uint32_t i;
    int rc = 1;

    if (find_graphics_queue(gpu, &qf) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    fmt = pick_offscreen_format(gpu);
    if (fmt == VK_FORMAT_UNDEFINED) {
        cityman_err("no color-renderable format");
        return 1;
    }
    cityman_log("offscreen format=%u (%s) %ux%u", (unsigned)fmt,
                fmt == VK_FORMAT_R8G8B8A8_UNORM ? "R8G8B8A8_UNORM" : "other",
                w, h);

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

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent.width = w;
    ici.extent.height = h;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    r = vkCreateImage(dev, &ici, NULL, &image);
    cityman_log("vkCreateImage => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    vkGetImageMemoryRequirements(dev, image, &img_req);
    if (alloc_bound(dev, gpu, &img_req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                    &img_mem, &img_type, &img_flags) != 0)
        goto out_image;
    cityman_log("image mem type=%u flags=0x%x size=%llu", img_type, img_flags,
                (unsigned long long)img_req.size);
    r = vkBindImageMemory(dev, image, img_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindImageMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_image;
    }

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, &view);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateImageView => %d (%s)", (int)r, vk_result_str(r));
        goto out_image;
    }

    memset(&att, 0, sizeof(att));
    att.format = fmt;
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
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    cityman_log("vkCreateRenderPass => %d (%s) loadOp=CLEAR magenta", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_view;

    memset(&fbci, 0, sizeof(fbci));
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = rp;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &view;
    fbci.width = w;
    fbci.height = h;
    fbci.layers = 1;
    r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateFramebuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_rp;
    }

    if (make_shader(dev, cityman_tri_vert_spv, cityman_tri_vert_spv_word_count,
                    &vs) == VK_SUCCESS &&
        make_shader(dev, cityman_tri_frag_spv, cityman_tri_frag_spv_word_count,
                    &fs) == VK_SUCCESS) {
        memset(&plci, 0, sizeof(plci));
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(dev, &plci, NULL, &layout) == VK_SUCCESS) {
            memset(stages, 0, sizeof(stages));
            stages[0].sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs;
            stages[0].pName = "main";
            stages[1].sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs;
            stages[1].pName = "main";

            memset(&vi, 0, sizeof(vi));
            vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            memset(&ia, 0, sizeof(ia));
            ia.sType =
                    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            memset(&viewport, 0, sizeof(viewport));
            viewport.width = (float)w;
            viewport.height = (float)h;
            viewport.maxDepth = 1.0f;
            memset(&scissor, 0, sizeof(scissor));
            scissor.extent.width = w;
            scissor.extent.height = h;
            memset(&vp, 0, sizeof(vp));
            vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vp.viewportCount = 1;
            vp.pViewports = &viewport;
            vp.scissorCount = 1;
            vp.pScissors = &scissor;

            memset(&rs, 0, sizeof(rs));
            rs.sType =
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1.0f;

            memset(&ms, 0, sizeof(ms));
            ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            memset(&ds, 0, sizeof(ds));
            ds.sType =
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

            memset(&blend_att, 0, sizeof(blend_att));
            blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                       VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT |
                                       VK_COLOR_COMPONENT_A_BIT;
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
            r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL,
                                          &pipe);
            cityman_log("vkCreateGraphicsPipelines => %d (%s)", (int)r,
                        vk_result_str(r));
            if (r == VK_SUCCESS)
                drew_tri = 1;
        }
    } else {
        cityman_err("vkCreateShaderModule failed; CLEAR-only");
    }

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = pix_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_pipe;
    }
    vkGetBufferMemoryRequirements(dev, buf, &buf_req);
    if (buf_req.size < pix_bytes)
        buf_req.size = pix_bytes;
    if (alloc_bound(dev, gpu, &buf_req,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &buf_mem, &buf_type,
                    &buf_flags) != 0)
        goto out_buf;
    r = vkBindBufferMemory(dev, buf, buf_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf;
    }
    cityman_log("readback mem type=%u flags=0x%x size=%llu", buf_type,
                buf_flags, (unsigned long long)buf_req.size);

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf;
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

    memset(&clear, 0, sizeof(clear));
    clear.color.float32[0] = 1.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 1.0f;
    clear.color.float32[3] = 1.0f;

    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = w;
    rpbi.renderArea.extent.height = h;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    if (drew_tri) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);

    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = w;
    copy.imageExtent.height = h;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buf, 1, &copy);

    memset(&host_bar, 0, sizeof(host_bar));
    host_bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host_bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_bar.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host_bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host_bar.buffer = buf;
    host_bar.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host_bar,
                         0, NULL);

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
    cityman_log("vkQueueSubmit (offscreen CLEAR%s) => %d (%s)",
                drew_tri ? "+vkCmdDraw" : "", (int)r, vk_result_str(r));
    if (r == VK_SUCCESS)
        r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("offscreen submit/wait => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_pool;
    }
    r = vkDeviceWaitIdle(dev);
    if (r != VK_SUCCESS) {
        cityman_err("vkDeviceWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, buf_mem, 0, pix_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }
    if ((buf_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange range;

        memset(&range, 0, sizeof(range));
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = buf_mem;
        range.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(dev, 1, &range);
    }

    pixels = (uint8_t*)malloc((size_t)pix_bytes);
    if (!pixels) {
        vkUnmapMemory(dev, buf_mem);
        goto out_pool;
    }
    memcpy(pixels, mapped, (size_t)pix_bytes);
    vkUnmapMemory(dev, buf_mem);

    for (i = 0; i < 16 && i < w * h; i++) {
        cityman_log("px[%u]=%02x %02x %02x %02x", i, pixels[i * 4 + 0],
                    pixels[i * 4 + 1], pixels[i * 4 + 2], pixels[i * 4 + 3]);
    }

    if (write_ppm_rgb(OFFSCREEN_PPM, w, h, pixels, fmt) == 0)
        cityman_log("wrote %s", OFFSCREEN_PPM);

    if (pixel_not_black(pixels, w * h)) {
        cityman_log("GPU write proven (sampled pixel != 0,0,0,0) tri=%d",
                    drew_tri);
        rc = 0;
    } else {
        cityman_err("sampled pixels are 0,0,0,0; GPU write not proven");
        rc = 1;
    }

    free(pixels);
    pixels = NULL;

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_buf:
    if (buf)
        vkDestroyBuffer(dev, buf, NULL);
    if (buf_mem)
        vkFreeMemory(dev, buf_mem, NULL);
out_pipe:
    if (pipe)
        vkDestroyPipeline(dev, pipe, NULL);
    if (layout)
        vkDestroyPipelineLayout(dev, layout, NULL);
    if (fs)
        vkDestroyShaderModule(dev, fs, NULL);
    if (vs)
        vkDestroyShaderModule(dev, vs, NULL);
    if (fb)
        vkDestroyFramebuffer(dev, fb, NULL);
out_rp:
    if (rp)
        vkDestroyRenderPass(dev, rp, NULL);
out_view:
    if (view)
        vkDestroyImageView(dev, view, NULL);
out_image:
    if (image)
        vkDestroyImage(dev, image, NULL);
    if (img_mem)
        vkFreeMemory(dev, img_mem, NULL);
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

    cityman_log("cityman-vk-tri32 start (Vulkan 1.0) ptr=%u",
                (unsigned)sizeof(void*));

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-tri32";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-tri32";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed; not a software clear");
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
        cityman_err(
                "cityman-vk-tri32: vkEnumeratePhysicalDevices returned 0");
        cityman_err(
                "No Vulkan GPU (ICD loaded but enumerated nothing).");
        cityman_err("Not faking a swapchain. Not software-clearing.");
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

    rc = offscreen_clear_draw(gpus[0]);

    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-tri32 done rc=%d", rc);
    return rc;
}
