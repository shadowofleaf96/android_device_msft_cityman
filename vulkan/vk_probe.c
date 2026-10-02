/*
 * cityman-vk-probe — dump Vulkan HAL + loader state on Lumia 950 (MSM8992).
 *
 * Logs to stdout and logcat tag citymanVk.
 */

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

#include <hardware/hwvulkan.h>
#include <log/log.h>

#define HAL_ICD_PATH "/vendor/lib64/hw/vulkan.msm8992.so"

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
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
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

static void print_prop(const char* key) {
    char val[PROP_VALUE_MAX];

    memset(val, 0, sizeof(val));
    if (__system_property_get(key, val) > 0)
        cityman_log("%s=%s", key, val);
    else
        cityman_log("%s=(empty)", key);
}

static void create_and_enumerate(const char* via,
                                 PFN_vkCreateInstance create_instance,
                                 PFN_vkGetInstanceProcAddr get_proc,
                                 uint32_t api_version) {
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkGetPhysicalDeviceProperties get_props;
    PFN_vkDestroyInstance destroy;
    uint32_t count = 0;
    VkPhysicalDevice* devs;
    uint32_t i;

    if (!create_instance) {
        cityman_log("%s CreateInstance pointer is NULL", via);
        return;
    }

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-probe";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-probe";
    app.engineVersion = 1;
    app.apiVersion = api_version;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    r = create_instance(&ci, NULL, &inst);
    cityman_log("%s CreateInstance => %d (%s) instance=%p", via, (int)r,
                vk_result_str(r), (void*)inst);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE)
        return;

    enumerate = NULL;
    get_props = NULL;
    destroy = NULL;
    if (get_proc) {
        enumerate = (PFN_vkEnumeratePhysicalDevices)get_proc(
                inst, "vkEnumeratePhysicalDevices");
        get_props = (PFN_vkGetPhysicalDeviceProperties)get_proc(
                inst, "vkGetPhysicalDeviceProperties");
        destroy = (PFN_vkDestroyInstance)get_proc(inst, "vkDestroyInstance");
    }
    cityman_log("%s GetInstanceProcAddr EnumeratePhysicalDevices=%p "
                "GetPhysicalDeviceProperties=%p DestroyInstance=%p",
                via, (void*)enumerate, (void*)get_props, (void*)destroy);
    if (!enumerate) {
        cityman_log("%s vkEnumeratePhysicalDevices unavailable", via);
        if (destroy)
            destroy(inst, NULL);
        return;
    }

    r = enumerate(inst, &count, NULL);
    cityman_log("%s EnumeratePhysicalDevices count-query => %d (%s) n=%u", via,
                (int)r, vk_result_str(r), count);
    if (count == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE)) {
        if (destroy)
            destroy(inst, NULL);
        return;
    }

    devs = (VkPhysicalDevice*)calloc(count, sizeof(*devs));
    if (!devs) {
        cityman_log("%s calloc(%u) failed", via, count);
        if (destroy)
            destroy(inst, NULL);
        return;
    }

    r = enumerate(inst, &count, devs);
    cityman_log("%s EnumeratePhysicalDevices fill => %d (%s) n=%u", via, (int)r,
                vk_result_str(r), count);
    if (get_props && (r == VK_SUCCESS || r == VK_INCOMPLETE)) {
        for (i = 0; i < count; i++) {
            VkPhysicalDeviceProperties props;

            memset(&props, 0, sizeof(props));
            get_props(devs[i], &props);
            cityman_log("%s physdev[%u] name=%s api=0x%x vendor=0x%x "
                        "device=0x%x type=%u",
                        via, i, props.deviceName, props.apiVersion,
                        props.vendorID, props.deviceID, props.deviceType);
        }
    }
    free(devs);

    if (destroy)
        destroy(inst, NULL);
}

static void probe_hal_icd(void) {
    void* dso;
    const char* err;
    struct hw_module_t* hmi;
    hw_device_t* hwdev = NULL;
    hwvulkan_device_t* vkdev;
    int rc;
    uint32_t ext_count = 0;
    VkResult vr;

    cityman_log("--- HAL ICD %s ---", HAL_ICD_PATH);
    (void)dlerror();
    dso = dlopen(HAL_ICD_PATH, RTLD_LOCAL | RTLD_NOW);
    err = dlerror();
    cityman_log("dlopen handle=%p dlerror=%s", dso, err ? err : "(null)");
    if (!dso)
        return;

    (void)dlerror();
    hmi = (struct hw_module_t*)dlsym(dso, HAL_MODULE_INFO_SYM_AS_STR);
    err = dlerror();
    cityman_log("dlsym %s => %p dlerror=%s", HAL_MODULE_INFO_SYM_AS_STR,
                (void*)hmi, err ? err : "(null)");
    if (!hmi) {
        (void)dlerror();
        hmi = (struct hw_module_t*)dlsym(dso, "HMI");
        err = dlerror();
        cityman_log("dlsym HMI => %p dlerror=%s", (void*)hmi,
                    err ? err : "(null)");
    }
    if (!hmi)
        return;

    cityman_log("module id=%s name=%s author=%s api=0x%x hal_api=0x%x",
                hmi->id ? hmi->id : "(null)", hmi->name ? hmi->name : "(null)",
                hmi->author ? hmi->author : "(null)", hmi->module_api_version,
                hmi->hal_api_version);

    if (!hmi->methods || !hmi->methods->open) {
        cityman_log("module has no open()");
        return;
    }

    rc = hmi->methods->open(hmi, HWVULKAN_DEVICE_0, &hwdev);
    cityman_log("open(%s) rc=%d (%s) dev=%p", HWVULKAN_DEVICE_0, rc,
                strerror(rc < 0 ? -rc : rc), (void*)hwdev);
    if (rc != 0 || !hwdev)
        return;

    vkdev = (hwvulkan_device_t*)hwdev;
    cityman_log("HAL fn EnumerateInstanceExtensionProperties=%p "
                "CreateInstance=%p GetInstanceProcAddr=%p",
                (void*)vkdev->EnumerateInstanceExtensionProperties,
                (void*)vkdev->CreateInstance, (void*)vkdev->GetInstanceProcAddr);

    if (vkdev->EnumerateInstanceExtensionProperties) {
        vr = vkdev->EnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
        cityman_log("HAL EnumerateInstanceExtensionProperties => %d (%s) n=%u",
                    (int)vr, vk_result_str(vr), ext_count);
    }

    create_and_enumerate("HAL-1.0", vkdev->CreateInstance,
                        vkdev->GetInstanceProcAddr, VK_API_VERSION_1_0);
    create_and_enumerate("HAL-1.1", vkdev->CreateInstance,
                        vkdev->GetInstanceProcAddr, VK_API_VERSION_1_1);
}

static void* loader_sym(void* lib, const char* name) {
    void* sym;
    const char* err;

    (void)dlerror();
    sym = dlsym(lib, name);
    err = dlerror();
    cityman_log("dlsym %s => %p dlerror=%s", name, sym, err ? err : "(null)");
    return sym;
}

static void probe_loader(void) {
    void* lib;
    const char* err;
    PFN_vkCreateInstance create_instance;
    PFN_vkGetInstanceProcAddr get_proc;
    PFN_vkEnumeratePhysicalDevices enumerate;

    cityman_log("--- loader libvulkan.so ---");
    (void)dlerror();
    lib = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_NOW);
    err = dlerror();
    cityman_log("dlopen libvulkan.so handle=%p dlerror=%s", lib,
                err ? err : "(null)");
    if (!lib)
        return;

    create_instance = (PFN_vkCreateInstance)loader_sym(lib, "vkCreateInstance");
    get_proc = (PFN_vkGetInstanceProcAddr)loader_sym(lib, "vkGetInstanceProcAddr");
    enumerate = (PFN_vkEnumeratePhysicalDevices)loader_sym(
            lib, "vkEnumeratePhysicalDevices");
    cityman_log("loader exported EnumeratePhysicalDevices=%p", (void*)enumerate);

    create_and_enumerate("loader-1.0", create_instance, get_proc,
                        VK_API_VERSION_1_0);
    create_and_enumerate("loader-1.1", create_instance, get_proc,
                        VK_API_VERSION_1_1);
}

int main(void) {
    cityman_log("cityman-vk-probe start");
    print_prop("ro.hardware.vulkan");
    print_prop("ro.board.platform");
    probe_hal_icd();
    probe_loader();
    cityman_log("cityman-vk-probe done");
    return 0;
}
