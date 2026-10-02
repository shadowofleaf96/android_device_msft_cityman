/*
 * cityman-wsi-check — list libvulkan instance extensions (loader WSI),
 * then after a Vulkan 1.0 instance, if a physical device exists, print
 * whether VK_KHR_swapchain is on that device.
 *
 * vkGetPhysicalDeviceQueueFamilyProperties has no present-bit without a
 * surface. Does not create ANativeWindow or VkSurfaceKHR.
 * Does not enable HWUI Vulkan. Does not hook SystemUI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifndef LOG_TAG
#define LOG_TAG "CitymanWsi"
#endif

#include <log/log.h>
#include <vulkan/vulkan.h>

static const char* kRequired[] = {
    "VK_KHR_surface",
    "VK_KHR_android_surface",
};

static const char* kOptional[] = {
    "VK_EXT_swapchain_colorspace",
    "VK_KHR_get_surface_capabilities2",
};

static void cityman_log(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void cityman_log(const char* fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("%s\n", buf);
    fflush(stdout);
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
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

static void check_device_swapchain(VkInstance inst) {
    uint32_t n = 0;
    uint32_t i;
    uint32_t q;
    VkResult r;
    VkPhysicalDevice* devs;

    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    cityman_log("vkEnumeratePhysicalDevices => %d n=%u", (int)r, n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_log("physdev enumerate fail");
        return;
    }
    if (n < 1) {
        cityman_log("no physical device; VK_KHR_swapchain not enumerated");
        return;
    }

    devs = (VkPhysicalDevice*)calloc(n, sizeof(*devs));
    if (!devs) {
        cityman_log("cityman-wsi-check fail: calloc physdev");
        return;
    }
    r = vkEnumeratePhysicalDevices(inst, &n, devs);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_log("physdev fill => %d", (int)r);
        free(devs);
        return;
    }

    for (i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        VkExtensionProperties* exts;
        VkQueueFamilyProperties* qf;
        uint32_t ne = 0;
        uint32_t qn = 0;

        memset(&p, 0, sizeof(p));
        vkGetPhysicalDeviceProperties(devs[i], &p);
        cityman_log("physdev[%u] name=%s api=0x%x", i, p.deviceName,
                    p.apiVersion);

        r = vkEnumerateDeviceExtensionProperties(devs[i], NULL, &ne, NULL);
        cityman_log("vkEnumerateDeviceExtensionProperties => %d n=%u", (int)r,
                    ne);
        exts = NULL;
        if (ne > 0 && (r == VK_SUCCESS || r == VK_INCOMPLETE)) {
            exts = (VkExtensionProperties*)calloc(ne, sizeof(*exts));
            if (exts) {
                r = vkEnumerateDeviceExtensionProperties(devs[i], NULL, &ne,
                                                         exts);
                if (r != VK_SUCCESS && r != VK_INCOMPLETE)
                    cityman_log("device ext fill => %d", (int)r);
            }
        }
        cityman_log("device ext VK_KHR_swapchain: %s",
                    has_ext(exts, exts ? ne : 0, "VK_KHR_swapchain")
                            ? "present"
                            : "absent");
        free(exts);

        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
        cityman_log("vkGetPhysicalDeviceQueueFamilyProperties n=%u", qn);
        if (qn > 0) {
            qf = (VkQueueFamilyProperties*)calloc(qn, sizeof(*qf));
            if (qf) {
                vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, qf);
                for (q = 0; q < qn; q++) {
                    cityman_log("queueFamily[%u] count=%u flags=0x%x", q,
                                qf[q].queueCount, qf[q].queueFlags);
                }
                free(qf);
            }
        }
        cityman_log("vkGetPhysicalDeviceQueueFamilyProperties present-bit "
                    "cannot be tested without a surface");
    }
    free(devs);
}

static int resolve_create_android_surface(void) {
    const char* inst_ext[] = {
        "VK_KHR_surface",
        "VK_KHR_android_surface",
    };
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    PFN_vkVoidFunction pfn;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-wsi-check";
    app.applicationVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = inst_ext;

    r = vkCreateInstance(&ci, NULL, &inst);
    cityman_log("vkCreateInstance(WSI) => %d instance=%p api=1.0", (int)r,
                (void*)inst);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE)
        return 1;

    check_device_swapchain(inst);

    pfn = vkGetInstanceProcAddr(inst, "vkCreateAndroidSurfaceKHR");
    cityman_log("vkGetInstanceProcAddr(vkCreateAndroidSurfaceKHR)=%p",
                (void*)pfn);
    cityman_log("no ANativeWindow; not calling vkCreateAndroidSurfaceKHR");

    vkDestroyInstance(inst, NULL);
    return pfn ? 0 : 1;
}

int main(void) {
    uint32_t n = 0;
    uint32_t i;
    VkResult r;
    VkExtensionProperties* props;
    int missing = 0;

    cityman_log("cityman-wsi-check start (libvulkan instance WSI)");

    r = vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    cityman_log("vkEnumerateInstanceExtensionProperties => %d n=%u", (int)r, n);
    if (r != VK_SUCCESS) {
        cityman_log("cityman-wsi-check fail: enumerate");
        return 1;
    }

    props = NULL;
    if (n > 0) {
        props = (VkExtensionProperties*)calloc(n, sizeof(*props));
        if (!props) {
            cityman_log("cityman-wsi-check fail: calloc");
            return 1;
        }
        r = vkEnumerateInstanceExtensionProperties(NULL, &n, props);
        if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
            cityman_log("enumerate fill => %d", (int)r);
            free(props);
            return 1;
        }
    }

    for (i = 0; i < n; i++) {
        cityman_log("instance ext[%u] %s spec=%u", i, props[i].extensionName,
                    props[i].specVersion);
    }

    for (i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); i++) {
        if (has_ext(props, n, kRequired[i])) {
            cityman_log("WSI required %s: present", kRequired[i]);
        } else {
            cityman_log("WSI required %s: MISSING", kRequired[i]);
            missing = 1;
        }
    }
    for (i = 0; i < sizeof(kOptional) / sizeof(kOptional[0]); i++) {
        cityman_log("WSI optional %s: %s", kOptional[i],
                    has_ext(props, n, kOptional[i]) ? "present" : "absent");
    }

    free(props);

    if (!missing)
        missing = resolve_create_android_surface();

    cityman_log("cityman-wsi-check %s", missing ? "fail" : "ok");
    return missing;
}
