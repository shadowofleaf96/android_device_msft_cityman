/*
 * cityman Vulkan capability helper — ICD present + enumerate count only.
 * No fake physical device. Callers must not treat 0 as "skip the ICD".
 */

#include "VkCap.h"

#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define cityman_VK_ICD_HAL "/vendor/lib64/hw/vulkan.msm8992.so"
#define cityman_VK_ICD_ALT "/vendor/lib64/vulkan.msm8992.so"

static int cityman_vulkan_icd_present(void) {
    return access(cityman_VK_ICD_HAL, R_OK) == 0 ||
           access(cityman_VK_ICD_ALT, R_OK) == 0;
}

int cityman_vulkan_device_count(void) {
    void* lib;
    PFN_vkCreateInstance create_instance;
    PFN_vkGetInstanceProcAddr get_proc;
    PFN_vkEnumeratePhysicalDevices enumerate;
    PFN_vkDestroyInstance destroy;
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t count = 0;

    if (!cityman_vulkan_icd_present())
        return 0;

    lib = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_NOW);
    if (!lib)
        return 0;

    create_instance = (PFN_vkCreateInstance)dlsym(lib, "vkCreateInstance");
    get_proc = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    if (!create_instance) {
        dlclose(lib);
        return 0;
    }

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vkcap";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vkcap";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    r = create_instance(&ci, NULL, &inst);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        dlclose(lib);
        return 0;
    }

    enumerate = NULL;
    destroy = NULL;
    if (get_proc) {
        enumerate = (PFN_vkEnumeratePhysicalDevices)get_proc(
                inst, "vkEnumeratePhysicalDevices");
        destroy = (PFN_vkDestroyInstance)get_proc(inst, "vkDestroyInstance");
    }
    if (!enumerate)
        enumerate = (PFN_vkEnumeratePhysicalDevices)dlsym(
                lib, "vkEnumeratePhysicalDevices");
    if (!destroy)
        destroy = (PFN_vkDestroyInstance)dlsym(lib, "vkDestroyInstance");

    if (!enumerate) {
        if (destroy)
            destroy(inst, NULL);
        dlclose(lib);
        return 0;
    }

    r = enumerate(inst, &count, NULL);
    if (destroy)
        destroy(inst, NULL);
    dlclose(lib);

    if (count == 0 || (r != VK_SUCCESS && r != VK_INCOMPLETE))
        return 0;

    return (int)count;
}
