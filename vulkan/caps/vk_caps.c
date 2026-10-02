/*
 * cityman-vk-caps — dump real VkPhysicalDevice caps on Lumia 950
 * (MSM8992 / Adreno 418).
 *
 * CreateInstance 1.0, enumerate, print properties / queues / memory /
 * device extensions / first 8 format feature bits for the common UNORM
 * color formats. Exit 2 if n==0. Does not invent a device. Does not set
 * ro.hwui.use_vulkan.
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

#ifndef VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME
#define VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME "VK_ANDROID_native_buffer"
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

static void format_queue_flags(VkQueueFlags flags, char* buf, size_t buf_sz) {
    int first = 1;
    VkQueueFlags known;

    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    if (flags == 0) {
        snprintf(buf, buf_sz, "0");
        return;
    }
    if (flags & VK_QUEUE_GRAPHICS_BIT)
        append_flag(buf, buf_sz, &first, "GRAPHICS");
    if (flags & VK_QUEUE_COMPUTE_BIT)
        append_flag(buf, buf_sz, &first, "COMPUTE");
    if (flags & VK_QUEUE_TRANSFER_BIT)
        append_flag(buf, buf_sz, &first, "TRANSFER");
    if (flags & VK_QUEUE_SPARSE_BINDING_BIT)
        append_flag(buf, buf_sz, &first, "SPARSE_BINDING");
#ifdef VK_QUEUE_PROTECTED_BIT
    if (flags & VK_QUEUE_PROTECTED_BIT)
        append_flag(buf, buf_sz, &first, "PROTECTED");
#endif

    known = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
            VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT;
#ifdef VK_QUEUE_PROTECTED_BIT
    known |= VK_QUEUE_PROTECTED_BIT;
#endif
    if (flags & ~known) {
        char extra[32];

        snprintf(extra, sizeof(extra), "UNK_0x%x", (unsigned)(flags & ~known));
        append_flag(buf, buf_sz, &first, extra);
    }
}

static void format_type_flags(VkMemoryPropertyFlags flags, char* buf,
                              size_t buf_sz) {
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

static void format_heap_flags(VkMemoryHeapFlags flags, char* buf,
                              size_t buf_sz) {
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

/* First 8 VkFormatFeatureFlagBits: sampled through color-attachment. */
static const struct {
    VkFormatFeatureFlags bit;
    const char* name;
} kFirst8FormatFeatures[] = {
    {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, "SAMPLED_IMAGE"},
    {VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, "STORAGE_IMAGE"},
    {VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT, "STORAGE_IMAGE_ATOMIC"},
    {VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT, "UNIFORM_TEXEL_BUFFER"},
    {VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT, "STORAGE_TEXEL_BUFFER"},
    {VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT,
     "STORAGE_TEXEL_BUFFER_ATOMIC"},
    {VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "VERTEX_BUFFER"},
    {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, "COLOR_ATTACHMENT"},
};

static void format_first8_features(VkFormatFeatureFlags flags, char* buf,
                                   size_t buf_sz) {
    size_t i;
    int first = 1;

    if (!buf || buf_sz == 0)
        return;
    buf[0] = '\0';
    for (i = 0; i < sizeof(kFirst8FormatFeatures) / sizeof(kFirst8FormatFeatures[0]);
         i++) {
        if (flags & kFirst8FormatFeatures[i].bit)
            append_flag(buf, buf_sz, &first, kFirst8FormatFeatures[i].name);
    }
    if (first)
        snprintf(buf, buf_sz, "0");
}

static const char* format_name(VkFormat fmt) {
    switch (fmt) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return "R5G6B5_UNORM_PACK16";
        default:
            return "VkFormat";
    }
}

static int has_ext(const VkExtensionProperties* props, uint32_t n,
                   const char* name) {
    uint32_t i;

    for (i = 0; i < n; i++) {
        if (strcmp(props[i].extensionName, name) == 0)
            return 1;
    }
    return 0;
}

static void dump_queues(VkPhysicalDevice phys) {
    uint32_t n = 0;
    uint32_t i;
    VkQueueFamilyProperties* fams;

    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, NULL);
    cityman_log("queueFamilyCount=%u", n);
    if (n == 0)
        return;

    fams = (VkQueueFamilyProperties*)calloc(n, sizeof(*fams));
    if (!fams) {
        cityman_err("calloc queue families failed");
        return;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, fams);
    for (i = 0; i < n; i++) {
        char qflags[160];

        format_queue_flags(fams[i].queueFlags, qflags, sizeof(qflags));
        cityman_log("queueFamily[%u] flags=0x%x (%s) count=%u "
                    "timestampValidBits=%u minImageTransferGranularity="
                    "%ux%ux%u",
                    i, (unsigned)fams[i].queueFlags, qflags,
                    fams[i].queueCount, fams[i].timestampValidBits,
                    fams[i].minImageTransferGranularity.width,
                    fams[i].minImageTransferGranularity.height,
                    fams[i].minImageTransferGranularity.depth);
    }
    free(fams);
}

static void dump_memory(VkPhysicalDevice phys) {
    VkPhysicalDeviceMemoryProperties mem;
    uint32_t i;
    char flags[160];

    memset(&mem, 0, sizeof(mem));
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);

    cityman_log("memoryHeapCount=%u", mem.memoryHeapCount);
    for (i = 0; i < mem.memoryHeapCount; i++) {
        format_heap_flags(mem.memoryHeaps[i].flags, flags, sizeof(flags));
        cityman_log("heap[%u] size=%" PRIu64 " (0x%" PRIx64 ") flags=0x%x (%s)",
                    i, (uint64_t)mem.memoryHeaps[i].size,
                    (uint64_t)mem.memoryHeaps[i].size,
                    (unsigned)mem.memoryHeaps[i].flags, flags);
    }

    cityman_log("memoryTypeCount=%u", mem.memoryTypeCount);
    for (i = 0; i < mem.memoryTypeCount; i++) {
        format_type_flags(mem.memoryTypes[i].propertyFlags, flags,
                          sizeof(flags));
        cityman_log("type[%u] heap=%u flags=0x%x (%s)", i,
                    mem.memoryTypes[i].heapIndex,
                    (unsigned)mem.memoryTypes[i].propertyFlags, flags);
    }
}

static void dump_device_extensions(VkPhysicalDevice phys) {
    uint32_t n = 0;
    uint32_t i;
    VkResult r;
    VkExtensionProperties* props;
    int have_swapchain;
    int have_anb;

    r = vkEnumerateDeviceExtensionProperties(phys, NULL, &n, NULL);
    cityman_log("EnumerateDeviceExtensionProperties count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (n == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE))
        return;

    props = (VkExtensionProperties*)calloc(n, sizeof(*props));
    if (!props) {
        cityman_err("calloc device extensions failed");
        return;
    }
    r = vkEnumerateDeviceExtensionProperties(phys, NULL, &n, props);
    cityman_log("EnumerateDeviceExtensionProperties fill => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        free(props);
        return;
    }

    have_swapchain = has_ext(props, n, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    have_anb = has_ext(props, n, VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME);
    cityman_log("ext %s=%s", VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                have_swapchain ? "yes" : "no");
    cityman_log("ext %s=%s", VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME,
                have_anb ? "yes" : "no");

    for (i = 0; i < n; i++) {
        cityman_log("deviceExt[%u] %s spec=0x%x", i, props[i].extensionName,
                    props[i].specVersion);
    }
    free(props);
}

static void dump_format(VkPhysicalDevice phys, VkFormat fmt) {
    VkFormatProperties fp;
    char linear[192];
    char optimal[192];
    char buffer[192];

    memset(&fp, 0, sizeof(fp));
    vkGetPhysicalDeviceFormatProperties(phys, fmt, &fp);

    format_first8_features(fp.linearTilingFeatures, linear, sizeof(linear));
    format_first8_features(fp.optimalTilingFeatures, optimal, sizeof(optimal));
    format_first8_features(fp.bufferFeatures, buffer, sizeof(buffer));

    cityman_log("format %s (0x%x)", format_name(fmt), (unsigned)fmt);
    cityman_log("  linearTilingFeatures=0x%x first8=%s sampled=%u colorAtt=%u",
                (unsigned)fp.linearTilingFeatures, linear,
                (fp.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
                        ? 1u
                        : 0u,
                (fp.linearTilingFeatures &
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                        ? 1u
                        : 0u);
    cityman_log("  optimalTilingFeatures=0x%x first8=%s sampled=%u colorAtt=%u",
                (unsigned)fp.optimalTilingFeatures, optimal,
                (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
                        ? 1u
                        : 0u,
                (fp.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                        ? 1u
                        : 0u);
    cityman_log("  bufferFeatures=0x%x first8=%s", (unsigned)fp.bufferFeatures,
                buffer);
}

static void dump_physical_device(uint32_t index, VkPhysicalDevice phys) {
    VkPhysicalDeviceProperties props;

    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(phys, &props);

    cityman_log("physdev[%u] deviceName=%s", index, props.deviceName);
    cityman_log("physdev[%u] apiVersion=0x%x (%u.%u.%u)", index,
                props.apiVersion, VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion),
                VK_VERSION_PATCH(props.apiVersion));
    cityman_log("physdev[%u] vendorID=0x%x deviceID=0x%x type=%u", index,
                props.vendorID, props.deviceID, props.deviceType);

    dump_queues(phys);
    dump_memory(phys);
    dump_device_extensions(phys);

    cityman_log("--- format features (first 8: sampled..color-attachment) ---");
    dump_format(phys, VK_FORMAT_R8G8B8A8_UNORM);
    dump_format(phys, VK_FORMAT_B8G8R8A8_UNORM);
    dump_format(phys, VK_FORMAT_R5G6B5_UNORM_PACK16);
}

int main(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t n = 0;
    uint32_t i;
    VkPhysicalDevice* devs;
    int rc = 1;

    cityman_log("cityman-vk-caps start (Vulkan 1.0)");

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-caps";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-caps";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    r = vkCreateInstance(&ci, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) instance=%p api=1.0", (int)r,
                vk_result_str(r), (void*)inst);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        cityman_err("CreateInstance failed");
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (n == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE)) {
        cityman_err("no physical device (n=%u) — not faking one", n);
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    devs = (VkPhysicalDevice*)calloc(n, sizeof(*devs));
    if (!devs) {
        cityman_err("calloc(%u) failed", n);
        vkDestroyInstance(inst, NULL);
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &n, devs);
    cityman_log("vkEnumeratePhysicalDevices fill => %d (%s) n=%u", (int)r,
                vk_result_str(r), n);
    if (n == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE)) {
        cityman_err("enumerate fill empty (n=%u) — not faking one", n);
        free(devs);
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    for (i = 0; i < n; i++)
        dump_physical_device(i, devs[i]);

    free(devs);
    vkDestroyInstance(inst, NULL);
    cityman_log("cityman-vk-caps done rc=0");
    rc = 0;
    return rc;
}
