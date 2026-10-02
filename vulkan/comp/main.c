/*
 * cityman-vk-comp — Vulkan 1.0 compute fill on Lumia 950 (MSM8992 / Adreno 418).
 *
 * Instance 1.0, queue family 0 GRAPHICS|COMPUTE, 64-uint storage buffer,
 * vkCreateComputePipelines + dispatch, host readback. Exit 0 if word0 is
 * 0xA5A5A5A5. Exit 2 if n==0. Pipeline create failure logs VkResult and
 * exits 1 — no CPU fill. No WSI, no HWUI.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <log/log.h>
#include <vulkan/vulkan.h>

#include "fill.comp.spv.h"

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#define WORD_COUNT 64u
#define FILL_WORD 0xA5A5A5A5u
#define PREFILL_WORD 0x11111111u

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
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        default:
            return "VkResult";
    }
}

static void format_queue_flags(VkQueueFlags flags, char* buf, size_t buf_sz) {
    int first = 1;
    size_t used;

    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    if (flags == 0) {
        snprintf(buf, buf_sz, "0");
        return;
    }
#define APPEND_Q(bit, name)                                                   \
    do {                                                                      \
        if (flags & (bit)) {                                                  \
            used = strlen(buf);                                               \
            if (used + 1 >= buf_sz)                                           \
                break;                                                        \
            if (!first) {                                                     \
                buf[used++] = '|';                                            \
                buf[used] = '\0';                                             \
            }                                                                 \
            first = 0;                                                        \
            strncat(buf, (name), buf_sz - strlen(buf) - 1);                   \
        }                                                                     \
    } while (0)
    APPEND_Q(VK_QUEUE_GRAPHICS_BIT, "GRAPHICS");
    APPEND_Q(VK_QUEUE_COMPUTE_BIT, "COMPUTE");
    APPEND_Q(VK_QUEUE_TRANSFER_BIT, "TRANSFER");
    APPEND_Q(VK_QUEUE_SPARSE_BINDING_BIT, "SPARSE_BINDING");
#undef APPEND_Q
    if (first)
        snprintf(buf, buf_sz, "0x%x", (unsigned)flags);
}

static int find_compute_queue(VkPhysicalDevice gpu, uint32_t* family,
                              VkQueueFlags* flags_out) {
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
        cityman_log("queueFamily[%u] flags=0x%x count=%u", i,
                    (unsigned)props[i].queueFlags, props[i].queueCount);
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            *family = i;
            if (flags_out)
                *flags_out = props[i].queueFlags;
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

static int compute_fill_and_readback(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    VkQueueFlags qflags = 0;
    char qflagbuf[96];
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkBufferCreateInfo bci;
    VkBuffer buf = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint32_t mem_type = 0;
    const VkDeviceSize bytes = (VkDeviceSize)WORD_COUNT * sizeof(uint32_t);
    void* mapped = NULL;
    uint32_t* words;
    uint32_t i;
    uint32_t match = 0;
    VkDescriptorSetLayoutBinding bind;
    VkDescriptorSetLayoutCreateInfo dslci;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayoutCreateInfo plci;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci;
    VkShaderModule sm = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stage;
    VkComputePipelineCreateInfo cpci;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkDescriptorPoolSize dps;
    VkDescriptorPoolCreateInfo dpci;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkDescriptorBufferInfo dbi;
    VkWriteDescriptorSet wds;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo bi;
    VkMemoryBarrier mb;
    VkSubmitInfo si;
    VkResult r;
    int rc = 1;

    if (find_compute_queue(gpu, &qf, &qflags) != 0) {
        cityman_err("no compute queue family");
        return 1;
    }
    format_queue_flags(qflags, qflagbuf, sizeof(qflagbuf));
    cityman_log("using queueFamily=%u flags=0x%x (%s)", qf, (unsigned)qflags,
                qflagbuf);

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

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &buf);
    cityman_log("vkCreateBuffer 64-uint STORAGE => %d (%s) size=%u", (int)r,
                vk_result_str(r), (unsigned)bytes);
    if (r != VK_SUCCESS)
        goto out_dev;

    vkGetBufferMemoryRequirements(dev, buf, &req);
    if (find_mem_type(&mp, req.memoryTypeBits,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &mem_type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type bits=0x%x",
                    req.memoryTypeBits);
        goto out_buf;
    }
    cityman_log("buffer memType=%u req.size=%u align=%u bits=0x%x", mem_type,
                (unsigned)req.size, (unsigned)req.alignment, req.memoryTypeBits);

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = mem_type;
    r = vkAllocateMemory(dev, &ai, NULL, &mem);
    if (r != VK_SUCCESS) {
        cityman_err("vkAllocateMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_buf;
    }
    r = vkBindBufferMemory(dev, buf, mem, 0);
    if (r != VK_SUCCESS) {
        cityman_err("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
        goto out_mem;
    }

    r = vkMapMemory(dev, mem, 0, bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(prefill) => %d (%s)", (int)r, vk_result_str(r));
        goto out_mem;
    }
    words = (uint32_t*)mapped;
    for (i = 0; i < WORD_COUNT; i++)
        words[i] = PREFILL_WORD;
    vkUnmapMemory(dev, mem);
    mapped = NULL;
    cityman_log("host prefill 64 words with 0x%08x (not a GPU fill)",
                PREFILL_WORD);

    memset(&bind, 0, sizeof(bind));
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    memset(&dslci, 0, sizeof(dslci));
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &bind;
    r = vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateDescriptorSetLayout => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_mem;
    }

    memset(&plci, 0, sizeof(plci));
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;
    r = vkCreatePipelineLayout(dev, &plci, NULL, &pl);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreatePipelineLayout => %d (%s)", (int)r, vk_result_str(r));
        goto out_dsl;
    }

    memset(&smci, 0, sizeof(smci));
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = (size_t)cityman_fill_comp_spv_word_count * sizeof(uint32_t);
    smci.pCode = cityman_fill_comp_spv;
    r = vkCreateShaderModule(dev, &smci, NULL, &sm);
    cityman_log("vkCreateShaderModule SPIR-V 1.0 words=%u => %d (%s)",
                cityman_fill_comp_spv_word_count, (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pl;

    memset(&stage, 0, sizeof(stage));
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = sm;
    stage.pName = "main";

    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = pl;

    r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe);
    cityman_log("vkCreateComputePipelines => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateComputePipelines failed VkResult=%d (%s); "
                    "not a CPU fill",
                    (int)r, vk_result_str(r));
        rc = 1;
        goto out_sm;
    }

    memset(&dps, 0, sizeof(dps));
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps.descriptorCount = 1;
    memset(&dpci, 0, sizeof(dpci));
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    r = vkCreateDescriptorPool(dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateDescriptorPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_pipe;
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

    memset(&dbi, 0, sizeof(dbi));
    dbi.buffer = buf;
    dbi.range = bytes;
    memset(&wds, 0, sizeof(wds));
    wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet = dset;
    wds.dstBinding = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateCommandPool => %d (%s)", (int)r, vk_result_str(r));
        goto out_dpool;
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &dset,
                            0, NULL);
    vkCmdDispatch(cmd, 1, 1, 1);
    cityman_log("vkCmdDispatch 1x1x1 local_size=64 (64 uints)");

    memset(&mb, 0, sizeof(mb));
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0,
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
    cityman_log("vkQueueSubmit (compute fill) => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_pool;
    r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        cityman_err("vkQueueWaitIdle => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    r = vkMapMemory(dev, mem, 0, bytes, 0, &mapped);
    if (r != VK_SUCCESS || !mapped) {
        cityman_err("vkMapMemory(readback) => %d (%s)", (int)r, vk_result_str(r));
        goto out_pool;
    }

    words = (uint32_t*)mapped;
    for (i = 0; i < WORD_COUNT; i++) {
        if (words[i] == FILL_WORD)
            match++;
    }
    cityman_log("readback word0=0x%08x word63=0x%08x match=%u/%u", words[0],
                words[WORD_COUNT - 1], match, WORD_COUNT);
    if (words[0] == FILL_WORD) {
        cityman_log("GPU compute fill ok (word0 0x%08x)", words[0]);
        rc = 0;
    } else {
        cityman_err("word0=0x%08x expected 0x%08x; not treating as a GPU fill",
                    words[0], FILL_WORD);
        rc = 1;
    }
    vkUnmapMemory(dev, mem);
    mapped = NULL;

out_pool:
    vkDestroyCommandPool(dev, pool, NULL);
out_dpool:
    vkDestroyDescriptorPool(dev, dpool, NULL);
out_pipe:
    vkDestroyPipeline(dev, pipe, NULL);
out_sm:
    vkDestroyShaderModule(dev, sm, NULL);
out_pl:
    vkDestroyPipelineLayout(dev, pl, NULL);
out_dsl:
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
out_mem:
    vkFreeMemory(dev, mem, NULL);
out_buf:
    vkDestroyBuffer(dev, buf, NULL);
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

    cityman_log("cityman-vk-comp start (Vulkan 1.0 compute)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-comp";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-comp";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("vkCreateInstance failed; not a CPU fill");
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
        cityman_err("cityman-vk-comp: vkEnumeratePhysicalDevices returned 0");
        cityman_err("No Vulkan GPU. Not CPU-filling.");
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
    cityman_log("limits maxComputeWorkGroupInvocations=%u size=%u,%u,%u",
                props.limits.maxComputeWorkGroupInvocations,
                props.limits.maxComputeWorkGroupSize[0],
                props.limits.maxComputeWorkGroupSize[1],
                props.limits.maxComputeWorkGroupSize[2]);

    rc = compute_fill_and_readback(gpus[0]);
    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-comp done rc=%d", rc);
    return rc;
}
