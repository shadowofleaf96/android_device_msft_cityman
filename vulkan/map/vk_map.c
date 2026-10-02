/*
 * cityman-vk-map — HOST_VISIBLE|HOST_COHERENT map/unmap/remap on Lumia 950.
 *
 * Instance 1.0, vkCreateDevice, allocate type 2 or 4 (Adreno 418 HVC),
 * vkMapMemory, write 256-byte pattern, unmap, remap, verify.
 * Exit 0 on match. Exit 2 if vkEnumeratePhysicalDevices n==0.
 * Does not set ro.hwui.use_vulkan.
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

#define MAP_BYTES 256u

static const uint32_t k_hvc_types[] = { 2u, 4u };

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
        default:
            return "VkResult";
    }
}

static int type_host_visible_coherent(VkMemoryPropertyFlags flags) {
    return (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
           (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

static void fill_pattern(uint8_t* p, size_t n) {
    size_t i;

    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
}

static int pick_hvc_type(const VkPhysicalDeviceMemoryProperties* mp,
                         uint32_t type_bits, uint32_t* out) {
    size_t i;

    for (i = 0; i < sizeof(k_hvc_types) / sizeof(k_hvc_types[0]); i++) {
        uint32_t t = k_hvc_types[i];
        VkMemoryPropertyFlags f;

        if (t >= mp->memoryTypeCount)
            continue;
        if ((type_bits & (1u << t)) == 0)
            continue;
        f = mp->memoryTypes[t].propertyFlags;
        if (!type_host_visible_coherent(f))
            continue;
        *out = t;
        return 0;
    }
    return -1;
}

static int map_unmap_verify(VkPhysicalDevice gpu) {
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mp;
    VkPhysicalDeviceProperties props;
    VkBufferCreateInfo bci;
    VkBuffer buf = VK_NULL_HANDLE;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint32_t type = 0;
    void* mapped = NULL;
    uint8_t expect[MAP_BYTES];
    VkResult r;
    int rc = 1;
    size_t i;
    size_t mismatches = 0;

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

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    cityman_log("minMemoryMapAlignment=%zu nonCoherentAtomSize=%zu",
                (size_t)props.limits.minMemoryMapAlignment,
                (size_t)props.limits.nonCoherentAtomSize);

    memset(&mp, 0, sizeof(mp));
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    cityman_log("memoryTypeCount=%u (HVC candidates type 2 and 4)",
                mp.memoryTypeCount);
    for (i = 0; i < sizeof(k_hvc_types) / sizeof(k_hvc_types[0]); i++) {
        uint32_t t = k_hvc_types[i];

        if (t >= mp.memoryTypeCount) {
            cityman_log("type[%u] missing", t);
            continue;
        }
        cityman_log("type[%u] heap=%u flags=0x%x HOST_VISIBLE|HOST_COHERENT=%s",
                    t, mp.memoryTypes[t].heapIndex,
                    (unsigned)mp.memoryTypes[t].propertyFlags,
                    type_host_visible_coherent(mp.memoryTypes[t].propertyFlags)
                            ? "YES"
                            : "no");
    }

    memset(&bci, 0, sizeof(bci));
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = MAP_BYTES;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(dev, &bci, NULL, &buf);
    cityman_log("vkCreateBuffer size=%u => %d (%s)", MAP_BYTES, (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_dev;

    memset(&req, 0, sizeof(req));
    vkGetBufferMemoryRequirements(dev, buf, &req);
    cityman_log("buffer req size=%llu align=%llu memoryTypeBits=0x%x",
                (unsigned long long)req.size, (unsigned long long)req.alignment,
                (unsigned)req.memoryTypeBits);

    if (pick_hvc_type(&mp, req.memoryTypeBits, &type) != 0) {
        cityman_err("no HOST_VISIBLE|HOST_COHERENT type 2 or 4 in bits=0x%x",
                    (unsigned)req.memoryTypeBits);
        goto out_buf;
    }
    cityman_log("allocate HOST_VISIBLE|HOST_COHERENT type=%u flags=0x%x "
                "size=%llu",
                type, (unsigned)mp.memoryTypes[type].propertyFlags,
                (unsigned long long)req.size);

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(dev, &ai, NULL, &mem);
    cityman_log("vkAllocateMemory => %d (%s) mem=%p", (int)r, vk_result_str(r),
                (void*)mem);
    if (r != VK_SUCCESS)
        goto out_buf;

    r = vkBindBufferMemory(dev, buf, mem, 0);
    cityman_log("vkBindBufferMemory => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_mem;

    mapped = NULL;
    r = vkMapMemory(dev, mem, 0, MAP_BYTES, 0, &mapped);
    cityman_log("vkMapMemory (write) => %d (%s) ptr=%p", (int)r,
                vk_result_str(r), mapped);
    if (r != VK_SUCCESS || !mapped)
        goto out_mem;

    fill_pattern((uint8_t*)mapped, MAP_BYTES);
    cityman_log("wrote %u-byte pattern 0xA5^i first=0x%02x last=0x%02x",
                MAP_BYTES, (unsigned)((uint8_t*)mapped)[0],
                (unsigned)((uint8_t*)mapped)[MAP_BYTES - 1]);
    vkUnmapMemory(dev, mem);
    mapped = NULL;
    cityman_log("vkUnmapMemory");

    r = vkMapMemory(dev, mem, 0, MAP_BYTES, 0, &mapped);
    cityman_log("vkMapMemory (remap) => %d (%s) ptr=%p", (int)r,
                vk_result_str(r), mapped);
    if (r != VK_SUCCESS || !mapped)
        goto out_mem;

    fill_pattern(expect, MAP_BYTES);
    for (i = 0; i < MAP_BYTES; i++) {
        if (((uint8_t*)mapped)[i] != expect[i]) {
            if (mismatches < 4)
                cityman_err("mismatch @%zu got=0x%02x want=0x%02x", i,
                            (unsigned)((uint8_t*)mapped)[i],
                            (unsigned)expect[i]);
            mismatches++;
        }
    }
    if (mismatches == 0) {
        cityman_log("remap verify MATCH %u bytes type=%u first=0x%02x last=0x%02x",
                    MAP_BYTES, type, (unsigned)((uint8_t*)mapped)[0],
                    (unsigned)((uint8_t*)mapped)[MAP_BYTES - 1]);
        rc = 0;
    } else {
        cityman_err("remap verify FAIL mismatches=%zu / %u", mismatches,
                    MAP_BYTES);
    }

    vkUnmapMemory(dev, mem);
    mapped = NULL;

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

    cityman_log("cityman-vk-map start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-map";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-map";
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
        cityman_err("cityman-vk-map: vkEnumeratePhysicalDevices n==0");
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
        cityman_err("cityman-vk-map: vkEnumeratePhysicalDevices n==0");
        free(gpus);
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpus[0], &props);
    cityman_log("physdev[0] name=%s api=0x%x vendor=0x%x device=0x%x type=%u",
                props.deviceName, props.apiVersion, props.vendorID,
                props.deviceID, props.deviceType);

    rc = map_unmap_verify(gpus[0]);

    free(gpus);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-map done rc=%d%s", rc, rc == 0 ? " MATCH" : "");
    return rc;
}
