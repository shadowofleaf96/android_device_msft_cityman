/*
 * cityman-vk-mem — print VkPhysicalDeviceMemoryProperties on Lumia 950
 * (MSM8992 / Adreno 418).
 *
 * Flags types that are HOST_VISIBLE|HOST_COHERENT (map for staging / host
 * copies without vkFlushMappedMemoryRanges) and DEVICE_LOCAL (GPU-optimal).
 * These heaps are system DRAM. GMEM (tile memory) is not a Vulkan type.
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

static const char* vk_result_str(VkResult r) {
    switch (r) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        default:
            return "VkResult";
    }
}

static void append_flag(char* buf, size_t buf_sz, int* first, const char* name) {
    size_t used;

    if (!buf || buf_sz == 0)
        return;
    used = strlen(buf);
    if (used + 1 >= buf_sz)
        return;
    if (!*first) {
        buf[used++] = '|';
        buf[used] = '\0';
        if (used + 1 >= buf_sz)
            return;
    }
    *first = 0;
    strncat(buf, name, buf_sz - used - 1);
}

static void format_type_flags(VkMemoryPropertyFlags flags, char* buf, size_t buf_sz) {
    int first = 1;
    VkMemoryPropertyFlags known;

    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    if (flags == 0) {
        snprintf(buf, buf_sz, "0");
        return;
    }
    if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        append_flag(buf, buf_sz, &first, "DEVICE_LOCAL");
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        append_flag(buf, buf_sz, &first, "HOST_VISIBLE");
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        append_flag(buf, buf_sz, &first, "HOST_COHERENT");
    if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
        append_flag(buf, buf_sz, &first, "HOST_CACHED");
    if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
        append_flag(buf, buf_sz, &first, "LAZILY_ALLOCATED");
#ifdef VK_MEMORY_PROPERTY_PROTECTED_BIT
    if (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT)
        append_flag(buf, buf_sz, &first, "PROTECTED");
#endif

    known = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
            VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
#ifdef VK_MEMORY_PROPERTY_PROTECTED_BIT
    known |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
#endif
    if (flags & ~known) {
        char extra[32];

        snprintf(extra, sizeof(extra), "UNK_0x%x", (unsigned)(flags & ~known));
        append_flag(buf, buf_sz, &first, extra);
    }
}

static void format_heap_flags(VkMemoryHeapFlags flags, char* buf, size_t buf_sz) {
    int first = 1;
    VkMemoryHeapFlags known;

    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    if (flags == 0) {
        snprintf(buf, buf_sz, "0");
        return;
    }
    if (flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        append_flag(buf, buf_sz, &first, "DEVICE_LOCAL");
#ifdef VK_MEMORY_HEAP_MULTI_INSTANCE_BIT
    if (flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
        append_flag(buf, buf_sz, &first, "MULTI_INSTANCE");
#endif

    known = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
#ifdef VK_MEMORY_HEAP_MULTI_INSTANCE_BIT
    known |= VK_MEMORY_HEAP_MULTI_INSTANCE_BIT;
#endif
    if (flags & ~known) {
        char extra[32];

        snprintf(extra, sizeof(extra), "UNK_0x%x", (unsigned)(flags & ~known));
        append_flag(buf, buf_sz, &first, extra);
    }
}

static int type_host_visible_coherent(VkMemoryPropertyFlags flags) {
    return (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
           (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static int type_device_local(VkMemoryPropertyFlags flags) {
    return (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
}

static void dump_memory_properties(uint32_t index, VkPhysicalDevice gpu) {
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t i;
    char flagbuf[256];
    uint32_t n_hvc = 0;
    uint32_t n_dl = 0;
    uint32_t n_hvc_dl = 0;
    uint32_t n_hvc_only = 0;
    uint32_t n_dl_only = 0;

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    cityman_log("physdev[%u] name=%s api=0x%x vendor=0x%x device=0x%x type=%u",
                index, props.deviceName, props.apiVersion, props.vendorID,
                props.deviceID, props.deviceType);

    memset(&mem, 0, sizeof(mem));
    vkGetPhysicalDeviceMemoryProperties(gpu, &mem);

    cityman_log("VkPhysicalDeviceMemoryProperties memoryHeapCount=%u memoryTypeCount=%u",
                mem.memoryHeapCount, mem.memoryTypeCount);
    cityman_log("--- heaps (system DRAM; not GMEM) ---");
    for (i = 0; i < mem.memoryHeapCount; i++) {
        uint64_t sz = (uint64_t)mem.memoryHeaps[i].size;

        format_heap_flags(mem.memoryHeaps[i].flags, flagbuf, sizeof(flagbuf));
        cityman_log("heap[%u] size=0x%" PRIx64 " (%" PRIu64 " bytes, %" PRIu64
                    " MiB) flags=0x%x %s",
                    i, sz, sz, sz / (1024u * 1024u),
                    (unsigned)mem.memoryHeaps[i].flags, flagbuf);
    }

    cityman_log("--- types ---");
    cityman_log("mark HOST_VISIBLE|HOST_COHERENT = staging / host copies "
                "(no flush); DEVICE_LOCAL = GPU-optimal");
    for (i = 0; i < mem.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = mem.memoryTypes[i].propertyFlags;
        int hvc = type_host_visible_coherent(f);
        int dl = type_device_local(f);

        format_type_flags(f, flagbuf, sizeof(flagbuf));
        cityman_log("type[%u] heap=%u flags=0x%x %s  HOST_VISIBLE|HOST_COHERENT=%s  "
                    "DEVICE_LOCAL=%s",
                    i, mem.memoryTypes[i].heapIndex, (unsigned)f, flagbuf,
                    hvc ? "YES" : "no", dl ? "YES" : "no");

        if (hvc)
            n_hvc++;
        if (dl)
            n_dl++;
        if (hvc && dl)
            n_hvc_dl++;
        if (hvc && !dl)
            n_hvc_only++;
        if (dl && !hvc && !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            n_dl_only++;
    }

    cityman_log("--- summary ---");
    cityman_log("HOST_VISIBLE|HOST_COHERENT types:");
    for (i = 0; i < mem.memoryTypeCount; i++) {
        if (type_host_visible_coherent(mem.memoryTypes[i].propertyFlags))
            cityman_log("  type[%u]", i);
    }
    if (n_hvc == 0)
        cityman_log("  (none)");

    cityman_log("DEVICE_LOCAL types:");
    for (i = 0; i < mem.memoryTypeCount; i++) {
        if (type_device_local(mem.memoryTypes[i].propertyFlags))
            cityman_log("  type[%u]", i);
    }
    if (n_dl == 0)
        cityman_log("  (none)");

    cityman_log("counts HOST_VISIBLE|HOST_COHERENT=%u DEVICE_LOCAL=%u "
                "both=%u host-coherent-not-device-local=%u "
                "device-local-not-host-visible=%u",
                n_hvc, n_dl, n_hvc_dl, n_hvc_only, n_dl_only);
    if (n_hvc_dl > 0)
        cityman_log("UMA: host-coherent types sit on DEVICE_LOCAL heap; "
                    "mapped copies do not need a separate staging heap");
    else if (n_hvc_only > 0)
        cityman_log("discrete-style: host-coherent types are not DEVICE_LOCAL; "
                    "use them as staging, copy into DEVICE_LOCAL");
    cityman_log("GMEM is tile memory and is not listed in these types");
}

int main(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t count = 0;
    VkPhysicalDevice* devs;
    uint32_t i;

    cityman_log("cityman-vk-mem start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-mem";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-mem";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    r = vkCreateInstance(&ci, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) instance=%p", (int)r,
                vk_result_str(r), (void*)inst);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE)
        return 1;

    r = vkEnumeratePhysicalDevices(inst, &count, NULL);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), count);
    if (count == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE)) {
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    devs = (VkPhysicalDevice*)calloc(count, sizeof(*devs));
    if (!devs) {
        cityman_log("calloc(%u) failed", count);
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &count, devs);
    cityman_log("vkEnumeratePhysicalDevices fill => %d (%s) n=%u", (int)r,
                vk_result_str(r), count);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        free(devs);
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    for (i = 0; i < count; i++)
        dump_memory_properties(i, devs[i]);

    free(devs);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-mem done");
    return 0;
}
