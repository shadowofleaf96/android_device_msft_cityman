/*
 * cityman-vk-vbo — HOST_VISIBLE coherent vertex buffer + vkCmdDraw.
 *
 * Lumia 950 RM-1104 / MSM8992 / Adreno 418. Vulkan 1.0 only.
 *
 * Three clip-space verts in a HOST_VISIBLE|HOST_COHERENT buffer (mem type
 * 2 or 4 on this ICD). Offscreen 64x64 R8G8B8A8_UNORM, bind VBO, vkCmdDraw,
 * copy to host. Exit 0 if a sampled pixel is not 0,0,0,0. Exit 2 if n==0.
 * Clear is black so a non-zero pixel is the raster, not the loadOp.
 * No WSI, no HWUI, no software fill.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shaders/cityman_tri.frag.spv.h"
#include "cityman_vbo.vert.spv.h"

#include <log/log.h>
#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#define IMG_W 64u
#define IMG_H 64u
#define IMG_FMT VK_FORMAT_R8G8B8A8_UNORM
#define VERT_COUNT 3u

struct Vert {
    float x, y, z, w;
    float r, g, b, a;
};

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

/* Prefer Adreno 418 HOST_VISIBLE|HOST_COHERENT types 2 then 4. */
static int find_hvc_type_2_or_4(const VkPhysicalDeviceMemoryProperties* mp,
                                uint32_t type_bits, uint32_t* out) {
    const uint32_t prefer[2] = {2, 4};
    const VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t i;

    for (i = 0; i < 2; i++) {
        uint32_t t = prefer[i];

        if (t >= mp->memoryTypeCount)
            continue;
        if ((type_bits & (1u << t)) == 0)
            continue;
        if ((mp->memoryTypes[t].propertyFlags & need) == need) {
            *out = t;
            return 0;
        }
    }
    return find_mem_type(mp, type_bits, need, out);
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

static int pixel_not_zero(const uint8_t* rgba, uint32_t count) {
    uint32_t i;

    for (i = 0; i < count; i++) {
        if (rgba[i * 4 + 0] | rgba[i * 4 + 1] | rgba[i * 4 + 2] |
            rgba[i * 4 + 3])
            return 1;
    }
    return 0;
}

static int vbo_draw_readback(VkPhysicalDevice gpu) {
    const VkDeviceSize pix_bytes = (VkDeviceSize)IMG_W * IMG_H * 4u;
    const VkDeviceSize vbo_bytes = (VkDeviceSize)sizeof(struct Vert) * VERT_COUNT;
    /* Fullscreen clip-space triangle; RGB from the VBO, not gl_VertexIndex. */
    const struct Vert verts[VERT_COUNT] = {
            {-1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
            {3.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
            {-1.0f, 3.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
    };
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
    VkVertexInputBindingDescription vib;
    VkVertexInputAttributeDescription via[2];
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
    VkBufferCreateInfo bci;
    VkBuffer vbo = VK_NULL_HANDLE;
    VkMemoryRequirements vbo_req;
    VkMemoryAllocateInfo vbo_ai;
    VkDeviceMemory vbo_mem = VK_NULL_HANDLE;
    uint32_t vbo_type = 0;
    void* vbo_map = NULL;
    VkBuffer host_buf = VK_NULL_HANDLE;
    VkMemoryRequirements host_req;
    VkMemoryAllocateInfo host_ai;
    VkDeviceMemory host_mem = VK_NULL_HANDLE;
    uint32_t host_type = 0;
    void* mapped = NULL;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkClearValue clear;
    VkRenderPassBeginInfo rpbi;
    VkDeviceSize vbo_off = 0;
    VkBufferImageCopy copy;
    VkBufferMemoryBarrier bb;
    VkSubmitInfo si;
    VkResult r;
    const uint8_t* px;
    uint32_t i;
    int rc = 1;

    if (find_graphics_queue(gpu, &qf) != 0) {
        cityman_err("no graphics queue family");
        return 1;
    }

    memset(&fp, 0, sizeof(fp));
    vkGetPhysicalDeviceFormatProperties(gpu, IMG_FMT, &fp);
    cityman_log("VK_FORMAT_R8G8B8A8_UNORM optimalFeatures=0x%x",
                (unsigned)fp.optimalTilingFeatures);
    if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ==
        0) {
        cityman_err("R8G8B8A8_UNORM is not a color attachment");
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
    cityman_log("memoryTypeCount=%u (HOST_VISIBLE|HOST_COHERENT expected 2,4)",
                mp.memoryTypeCount);

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
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    r = vkCreateImage(dev, &ici, NULL, &image);
    cityman_log("vkCreateImage %ux%u R8G8B8A8_UNORM => %d (%s)", IMG_W, IMG_H,
                (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

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
    cityman_log("image mem type=%u flags=0x%x size=%llu", img_type,
                mp.memoryTypes[img_type].propertyFlags,
                (unsigned long long)img_req.size);

    memset(&vci, 0, sizeof(vci));
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = IMG_FMT;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    r = vkCreateImageView(dev, &vci, NULL, &view);
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
    cityman_log("vkCreateRenderPass => %d (%s) loadOp=CLEAR black", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_view;

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

    r = make_shader(dev, cityman_vbo_vert_spv, cityman_vbo_vert_spv_word_count,
                    &vs);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateShaderModule(vert) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_fb;
    }
    r = make_shader(dev, cityman_tri_frag_spv, cityman_tri_frag_spv_word_count,
                    &fs);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateShaderModule(frag) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_vs;
    }

    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    r = vkCreatePipelineLayout(dev, &plci, NULL, &layout);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreatePipelineLayout => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_fs;
    }

    memset(&vib, 0, sizeof(vib));
    vib.binding = 0;
    vib.stride = (uint32_t)sizeof(struct Vert);
    vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    memset(via, 0, sizeof(via));
    via[0].location = 0;
    via[0].binding = 0;
    via[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    via[0].offset = 0;
    via[1].location = 1;
    via[1].binding = 0;
    via[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    via[1].offset = 16;

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
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vib;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = via;

    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)IMG_W;
    viewport.height = (float)IMG_H;
    viewport.maxDepth = 1.0f;
    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width = IMG_W;
    scissor.extent.height = IMG_H;
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
    r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
    cityman_log("vkCreateGraphicsPipelines => %d (%s) vertexBinding stride=%u",
                (int)r, vk_result_str(r), (unsigned)sizeof(struct Vert));
    if (r != VK_SUCCESS)
        goto out_layout;

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = vbo_bytes;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &vbo);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer(vbo) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pipe;
    }
    vkGetBufferMemoryRequirements(dev, vbo, &vbo_req);
    if (vbo_req.size < vbo_bytes)
        vbo_req.size = vbo_bytes;
    if (find_hvc_type_2_or_4(&mp, vbo_req.memoryTypeBits, &vbo_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type 2/4 bits=0x%x",
                    vbo_req.memoryTypeBits);
        goto out_vbo;
    }
    if (vbo_type != 2 && vbo_type != 4) {
        cityman_err("VBO mem type=%u (want 2 or 4)", vbo_type);
        goto out_vbo;
    }
    memset(&vbo_ai, 0, sizeof(vbo_ai));
    vbo_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vbo_ai.allocationSize = vbo_req.size;
    vbo_ai.memoryTypeIndex = vbo_type;
    r = vkAllocateMemory(dev, &vbo_ai, NULL, &vbo_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(vbo) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_vbo;
    }
    r = vkBindBufferMemory(dev, vbo, vbo_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory(vbo) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_vbo_mem;
    }
    cityman_log("vbo mem type=%u flags=0x%x size=%llu verts=%u HOST_VISIBLE|"
                "HOST_COHERENT",
                vbo_type, mp.memoryTypes[vbo_type].propertyFlags,
                (unsigned long long)vbo_req.size, VERT_COUNT);

    r = vkMapMemory(dev, vbo_mem, 0, vbo_bytes, 0, &vbo_map);
    if (r != VK_SUCCESS || !vbo_map) {
        cityman_err("vkMapMemory(vbo) => %d (%s)", (int)r, vk_result_str(r));
        goto out_vbo_mem;
    }
    memcpy(vbo_map, verts, (size_t)vbo_bytes);
    vkUnmapMemory(dev, vbo_mem);
    vbo_map = NULL;
    cityman_log("vkMapMemory(vbo) wrote 3 verts (no flush; HOST_COHERENT)");

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = pix_bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &host_buf);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateBuffer(readback) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_vbo_mem;
    }
    vkGetBufferMemoryRequirements(dev, host_buf, &host_req);
    if (host_req.size < pix_bytes)
        host_req.size = pix_bytes;
    if (find_hvc_type_2_or_4(&mp, host_req.memoryTypeBits, &host_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT readback type bits=0x%x",
                    host_req.memoryTypeBits);
        goto out_host;
    }
    memset(&host_ai, 0, sizeof(host_ai));
    host_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    host_ai.allocationSize = host_req.size;
    host_ai.memoryTypeIndex = host_type;
    r = vkAllocateMemory(dev, &host_ai, NULL, &host_mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory(readback) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_host;
    }
    r = vkBindBufferMemory(dev, host_buf, host_mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory(readback) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_host_mem;
    }
    cityman_log("readback mem type=%u flags=0x%x size=%llu", host_type,
                mp.memoryTypes[host_type].propertyFlags,
                (unsigned long long)host_req.size);

    r = vkMapMemory(dev, host_mem, 0, pix_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback prefill) => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_host_mem;
    }
    memset(mapped, 0, (size_t)pix_bytes);
    vkUnmapMemory(dev, host_mem);
    mapped = NULL;

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_host_mem;
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

    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = IMG_W;
    rpbi.renderArea.extent.height = IMG_H;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &vbo_off);
    vkCmdDraw(cmd, VERT_COUNT, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    cityman_log("vkCmdBindVertexBuffers + vkCmdDraw(%u)", VERT_COUNT);

    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = IMG_W;
    copy.imageExtent.height = IMG_H;
    copy.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0,
                         NULL);

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
    cityman_log("vkQueueSubmit (VBO+vkCmdDraw+readback) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, host_mem, 0, pix_bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    px = (const uint8_t*)mapped;
    for (i = 0; i < 8; i++) {
        cityman_log("px[%u]=%02x %02x %02x %02x", i, px[i * 4 + 0],
                    px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
    }
    if (pixel_not_zero(px, IMG_W * IMG_H)) {
        cityman_log("GPU VBO write ok (sampled pixel != 0,0,0,0) type=%u",
                    vbo_type);
        rc = 0;
    } else {
        cityman_err("sampled pixels are 0,0,0,0; VBO draw not proven");
        rc = 1;
    }
    vkUnmapMemory(dev, host_mem);
    mapped = NULL;

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_host_mem:
    vkFreeMemory(dev, host_mem, NULL);
out_host:
    vkDestroyBuffer(dev, host_buf, NULL);
out_vbo_mem:
    vkFreeMemory(dev, vbo_mem, NULL);
out_vbo:
    vkDestroyBuffer(dev, vbo, NULL);
out_pipe:
    vkDestroyPipeline(dev, pipe, NULL);
out_layout:
    vkDestroyPipelineLayout(dev, layout, NULL);
out_fs:
    vkDestroyShaderModule(dev, fs, NULL);
out_vs:
    vkDestroyShaderModule(dev, vs, NULL);
out_fb:
    vkDestroyFramebuffer(dev, fb, NULL);
out_rp:
    vkDestroyRenderPass(dev, rp, NULL);
out_view:
    vkDestroyImageView(dev, view, NULL);
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

    cityman_log("cityman-vk-vbo start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-vbo";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-vbo";
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
        cityman_err("cityman-vk-vbo: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not drawing from a host VBO.");
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

    rc = vbo_draw_readback(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-vbo done rc=%d", rc);
    return rc;
}
