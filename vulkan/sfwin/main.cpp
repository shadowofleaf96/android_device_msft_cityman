/*
 * cityman-vk-sfwin — one-shot Vulkan 1.0 present on a real SF Surface.
 *
 * As root/shell: SurfaceComposerClient + SurfaceControl (Android 11),
 * ANativeWindow from getSurface(), instance 1.0 with VK_KHR_surface +
 * VK_KHR_android_surface, device VK_KHR_swapchain, FIFO, clear magenta,
 * vkQueuePresentKHR once, then destroy.
 *
 * No SystemUI hook. No ro.hwui.use_vulkan. Do not fake present.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <binder/ProcessState.h>
#include <gui/ISurfaceComposerClient.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <log/log.h>
#include <ui/DisplayConfig.h>
#include <ui/DisplayState.h>
#include <ui/PixelFormat.h>
#include <utils/Errors.h>
#include <utils/String8.h>

#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

using android::DisplayConfig;
using android::ISurfaceComposerClient;
using android::NO_ERROR;
using android::ProcessState;
using android::sp;
using android::status_t;
using android::String8;
using android::Surface;
using android::SurfaceComposerClient;
using android::SurfaceControl;
using android::ui::DisplayState;

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
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
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
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        default:
            return "VkResult";
    }
}

static int has_instance_ext(const char* name) {
    uint32_t n = 0;
    VkExtensionProperties* props;
    uint32_t i;
    int found = 0;
    VkResult r;

    r = vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0)
        return 0;
    props = static_cast<VkExtensionProperties*>(calloc(n, sizeof(*props)));
    if (!props)
        return 0;
    r = vkEnumerateInstanceExtensionProperties(nullptr, &n, props);
    if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
        for (i = 0; i < n; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
        }
    }
    free(props);
    return found;
}

static int has_device_ext(VkPhysicalDevice gpu, const char* name) {
    uint32_t n = 0;
    VkExtensionProperties* props;
    uint32_t i;
    int found = 0;
    VkResult r;

    r = vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, nullptr);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0)
        return 0;
    props = static_cast<VkExtensionProperties*>(calloc(n, sizeof(*props)));
    if (!props)
        return 0;
    r = vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, props);
    if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
        for (i = 0; i < n; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                found = 1;
                break;
            }
        }
    }
    free(props);
    return found;
}

static int find_present_queue(VkPhysicalDevice gpu, VkSurfaceKHR surface,
                              uint32_t* family) {
    uint32_t n = 0;
    uint32_t i;
    VkQueueFamilyProperties* props;
    int fallback = -1;

    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, nullptr);
    if (n == 0)
        return -1;
    props = static_cast<VkQueueFamilyProperties*>(calloc(n, sizeof(*props)));
    if (!props)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, props);
    for (i = 0; i < n; i++) {
        VkBool32 support = VK_FALSE;
        VkResult r = vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface,
                                                          &support);
        if (r != VK_SUCCESS || !support)
            continue;
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *family = i;
            free(props);
            return 0;
        }
        if (fallback < 0)
            fallback = static_cast<int>(i);
    }
    free(props);
    if (fallback >= 0) {
        *family = static_cast<uint32_t>(fallback);
        return 0;
    }
    return -1;
}

static int make_sf_window(sp<SurfaceComposerClient>* out_client,
                          sp<SurfaceControl>* out_sc, sp<Surface>* out_surface,
                          ANativeWindow** out_window, uint32_t* out_w,
                          uint32_t* out_h) {
    status_t err;
    uint32_t w = 256;
    uint32_t h = 256;
    android::ui::LayerStack layerStack = 0;
    int have_stack = 0;

    ProcessState::self()->startThreadPool();

    sp<SurfaceComposerClient> client = new SurfaceComposerClient();
    err = client->initCheck();
    if (err != NO_ERROR) {
        cityman_err("SurfaceComposerClient::initCheck => %d (0x%x)", (int)err,
                    (unsigned)err);
        return -1;
    }
    cityman_log("SurfaceComposerClient initCheck OK");

    const sp<android::IBinder> dpy =
            SurfaceComposerClient::getInternalDisplayToken();
    if (dpy == nullptr) {
        cityman_err("getInternalDisplayToken failed; using 256x256");
    } else {
        DisplayConfig config;
        err = SurfaceComposerClient::getActiveDisplayConfig(dpy, &config);
        if (err == NO_ERROR && config.resolution.isValid() &&
            config.resolution.getWidth() > 0 &&
            config.resolution.getHeight() > 0) {
            w = static_cast<uint32_t>(config.resolution.getWidth());
            h = static_cast<uint32_t>(config.resolution.getHeight());
            cityman_log("display config %ux%u @ %.2f Hz", w, h,
                        config.refreshRate);
        } else {
            cityman_log("getActiveDisplayConfig => %d; using 256x256",
                        (int)err);
        }
        DisplayState ds;
        if (SurfaceComposerClient::getDisplayState(dpy, &ds) == NO_ERROR &&
            ds.layerStack != android::ui::NO_LAYER_STACK) {
            layerStack = ds.layerStack;
            have_stack = 1;
        }
    }

    sp<SurfaceControl> sc = client->createSurface(
            String8("cityman-vk-sfwin"), w, h, android::PIXEL_FORMAT_RGBA_8888,
            ISurfaceComposerClient::eOpaque);
    if (sc == nullptr || !sc->isValid()) {
        cityman_err("createSurface failed");
        return -1;
    }

    SurfaceComposerClient::Transaction t;
    t.setLayer(sc, 0x7FFFFFFF).setPosition(sc, 0, 0).show(sc);
    if (have_stack)
        t.setLayerStack(sc, layerStack);
    t.apply();

    sp<Surface> surface = sc->getSurface();
    if (surface == nullptr) {
        cityman_err("SurfaceControl::getSurface returned null");
        return -1;
    }

    ANativeWindow* window = surface.get();
    cityman_log("SurfaceControl cityman-vk-sfwin %ux%u ANativeWindow=%p", w, h,
                static_cast<void*>(window));

    *out_client = client;
    *out_sc = sc;
    *out_surface = surface;
    *out_window = window;
    *out_w = w;
    *out_h = h;
    return 0;
}

static void destroy_sf_window(const sp<SurfaceControl>& sc) {
    if (sc == nullptr)
        return;
    SurfaceComposerClient::Transaction t;
    t.hide(sc).apply();
}

static int present_magenta(ANativeWindow* window, uint32_t win_w,
                           uint32_t win_h) {
    const char* inst_exts[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };
    const char* swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t n = 0;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    VkAndroidSurfaceCreateInfoKHR asci;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t qf = 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR caps;
    uint32_t fmt_n = 0;
    VkSurfaceFormatKHR* fmts = nullptr;
    VkSurfaceFormatKHR pick;
    uint32_t pm_n = 0;
    VkPresentModeKHR* pms = nullptr;
    int have_fifo = 0;
    uint32_t img_count;
    VkExtent2D extent;
    VkSwapchainCreateInfoKHR swci;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    uint32_t nimg = 0;
    VkImage* images = nullptr;
    VkImageView* views = nullptr;
    VkFramebuffer* fbs = nullptr;
    uint32_t i;
    VkAttachmentDescription att;
    VkAttachmentReference color_ref;
    VkSubpassDescription sub;
    VkSubpassDependency dep;
    VkRenderPassCreateInfo rpci;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pci;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo semci;
    VkFenceCreateInfo fci;
    VkSemaphore img_sem = VK_NULL_HANDLE;
    VkSemaphore done_sem = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t img_idx = 0;
    VkCommandBufferBeginInfo bi;
    VkClearValue clear;
    VkRenderPassBeginInfo rpbi;
    VkPipelineStageFlags wait_stage;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    int rc = 1;

    if (!has_instance_ext(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_instance_ext(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
        cityman_err("instance missing VK_KHR_surface / VK_KHR_android_surface");
        return 1;
    }

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-sfwin";
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = inst_exts;

    r = vkCreateInstance(&ici, nullptr, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0 + %s + %s", (int)r,
                vk_result_str(r), VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    if (r != VK_SUCCESS)
        return 1;

    r = vkEnumeratePhysicalDevices(inst, &n, nullptr);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (r != VK_SUCCESS || n == 0) {
        cityman_err("no physical devices; not faking present");
        vkDestroyInstance(inst, nullptr);
        return 2;
    }
    r = vkEnumeratePhysicalDevices(inst, &n, &gpu);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        vkDestroyInstance(inst, nullptr);
        return 1;
    }
    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    cityman_log("physdev[0] name=%s api=0x%x", props.deviceName,
                props.apiVersion);

    memset(&asci, 0, sizeof(asci));
    asci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    asci.window = window;
    r = vkCreateAndroidSurfaceKHR(inst, &asci, nullptr, &surface);
    cityman_log("vkCreateAndroidSurfaceKHR => %d (%s)", (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS) {
        vkDestroyInstance(inst, nullptr);
        return 1;
    }

    if (find_present_queue(gpu, surface, &qf) != 0) {
        cityman_err("no queue family can present to this ANativeWindow");
        vkDestroySurfaceKHR(inst, surface, nullptr);
        vkDestroyInstance(inst, nullptr);
        return 1;
    }
    cityman_log("present+graphics queueFamily=%u", qf);

    if (!has_device_ext(gpu, swap_ext)) {
        cityman_err("device missing %s; not faking a swapchain", swap_ext);
        vkDestroySurfaceKHR(inst, surface, nullptr);
        vkDestroyInstance(inst, nullptr);
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
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &swap_ext;

    r = vkCreateDevice(gpu, &dci, nullptr, &dev);
    cityman_log("vkCreateDevice => %d (%s) + %s", (int)r, vk_result_str(r),
                swap_ext);
    if (r != VK_SUCCESS) {
        vkDestroySurfaceKHR(inst, surface, nullptr);
        vkDestroyInstance(inst, nullptr);
        return 1;
    }
    vkGetDeviceQueue(dev, qf, 0, &queue);

    memset(&caps, 0, sizeof(caps));
    r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    if (r != VK_SUCCESS) {
        cityman_err("vkGetPhysicalDeviceSurfaceCapabilitiesKHR => %d (%s)",
                    (int)r, vk_result_str(r));
        goto out_dev;
    }

    if (caps.currentExtent.width == 0xFFFFFFFFu) {
        extent.width = win_w;
        extent.height = win_h;
        if (caps.minImageExtent.width > extent.width)
            extent.width = caps.minImageExtent.width;
        if (caps.minImageExtent.height > extent.height)
            extent.height = caps.minImageExtent.height;
        if (caps.maxImageExtent.width != 0 &&
            caps.maxImageExtent.width < extent.width)
            extent.width = caps.maxImageExtent.width;
        if (caps.maxImageExtent.height != 0 &&
            caps.maxImageExtent.height < extent.height)
            extent.height = caps.maxImageExtent.height;
    } else {
        extent = caps.currentExtent;
    }
    if (extent.width == 0 || extent.height == 0) {
        cityman_err("surface extent is 0x0; not inventing a swapchain size");
        goto out_dev;
    }
    cityman_log("surface caps %ux%u minImages=%u maxImages=%u", extent.width,
                extent.height, caps.minImageCount, caps.maxImageCount);

    r = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_n, nullptr);
    if (r != VK_SUCCESS || fmt_n == 0) {
        cityman_err("no surface formats");
        goto out_dev;
    }
    fmts = static_cast<VkSurfaceFormatKHR*>(calloc(fmt_n, sizeof(*fmts)));
    if (!fmts)
        goto out_dev;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_n, fmts);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkGetPhysicalDeviceSurfaceFormatsKHR => %d (%s)", (int)r,
                    vk_result_str(r));
        goto out_dev;
    }
    pick = fmts[0];
    for (i = 0; i < fmt_n; i++) {
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
            fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            pick = fmts[i];
            break;
        }
    }
    cityman_log("swapchain format=%u colorSpace=%u", (unsigned)pick.format,
                (unsigned)pick.colorSpace);

    r = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pm_n, nullptr);
    if (r != VK_SUCCESS || pm_n == 0) {
        cityman_err("no present modes");
        goto out_dev;
    }
    pms = static_cast<VkPresentModeKHR*>(calloc(pm_n, sizeof(*pms)));
    if (!pms)
        goto out_dev;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pm_n, pms);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        cityman_err("vkGetPhysicalDeviceSurfacePresentModesKHR => %d (%s)",
                    (int)r, vk_result_str(r));
        goto out_dev;
    }
    for (i = 0; i < pm_n; i++) {
        if (pms[i] == VK_PRESENT_MODE_FIFO_KHR)
            have_fifo = 1;
    }
    if (!have_fifo) {
        cityman_err("FIFO not advertised; not substituting another mode");
        goto out_dev;
    }

    img_count = caps.minImageCount;
    if (img_count < 2)
        img_count = 2;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    memset(&swci, 0, sizeof(swci));
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface;
    swci.minImageCount = img_count;
    swci.imageFormat = pick.format;
    swci.imageColorSpace = pick.colorSpace;
    swci.imageExtent = extent;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        cityman_err("surface lacks COLOR_ATTACHMENT usage; not faking");
        goto out_dev;
    }
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;

    r = vkCreateSwapchainKHR(dev, &swci, nullptr, &swap);
    cityman_log("vkCreateSwapchainKHR => %d (%s) %ux%u FIFO images>=%u", (int)r,
                vk_result_str(r), extent.width, extent.height, img_count);
    if (r != VK_SUCCESS)
        goto out_dev;

    r = vkGetSwapchainImagesKHR(dev, swap, &nimg, nullptr);
    if (r != VK_SUCCESS || nimg == 0)
        goto out_swap;
    images = static_cast<VkImage*>(calloc(nimg, sizeof(*images)));
    views = static_cast<VkImageView*>(calloc(nimg, sizeof(*views)));
    fbs = static_cast<VkFramebuffer*>(calloc(nimg, sizeof(*fbs)));
    if (!images || !views || !fbs)
        goto out_swap;
    r = vkGetSwapchainImagesKHR(dev, swap, &nimg, images);
    if (r != VK_SUCCESS)
        goto out_swap;
    cityman_log("swapchain images=%u", nimg);

    memset(&att, 0, sizeof(att));
    att.format = pick.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    memset(&rpci, 0, sizeof(rpci));
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    r = vkCreateRenderPass(dev, &rpci, nullptr, &rp);
    if (r != VK_SUCCESS) {
        cityman_err("vkCreateRenderPass => %d (%s)", (int)r, vk_result_str(r));
        goto out_swap;
    }

    for (i = 0; i < nimg; i++) {
        VkImageViewCreateInfo vci;
        VkFramebufferCreateInfo fbci;

        memset(&vci, 0, sizeof(vci));
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = pick.format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        r = vkCreateImageView(dev, &vci, nullptr, &views[i]);
        if (r != VK_SUCCESS) {
            cityman_err("vkCreateImageView => %d (%s)", (int)r,
                        vk_result_str(r));
            goto out_rp;
        }

        memset(&fbci, 0, sizeof(fbci));
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = rp;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &views[i];
        fbci.width = extent.width;
        fbci.height = extent.height;
        fbci.layers = 1;
        r = vkCreateFramebuffer(dev, &fbci, nullptr, &fbs[i]);
        if (r != VK_SUCCESS) {
            cityman_err("vkCreateFramebuffer => %d (%s)", (int)r,
                        vk_result_str(r));
            goto out_rp;
        }
    }

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, nullptr, &pool);
    if (r != VK_SUCCESS)
        goto out_rp;

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(dev, &ai, &cmd);
    if (r != VK_SUCCESS)
        goto out_pool;

    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateSemaphore(dev, &semci, nullptr, &img_sem) != VK_SUCCESS ||
        vkCreateSemaphore(dev, &semci, nullptr, &done_sem) != VK_SUCCESS ||
        vkCreateFence(dev, &fci, nullptr, &fence) != VK_SUCCESS)
        goto out_sync;

    r = vkAcquireNextImageKHR(dev, swap, UINT64_MAX, img_sem, VK_NULL_HANDLE,
                              &img_idx);
    cityman_log("vkAcquireNextImageKHR => %d (%s) idx=%u", (int)r,
                vk_result_str(r), img_idx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        goto out_sync;

    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS)
        goto out_sync;

    memset(&clear, 0, sizeof(clear));
    clear.color.float32[0] = 1.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 1.0f;
    clear.color.float32[3] = 1.0f;
    memset(&rpbi, 0, sizeof(rpbi));
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = rp;
    rpbi.framebuffer = fbs[img_idx];
    rpbi.renderArea.extent = extent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        cityman_err("vkEndCommandBuffer => %d (%s)", (int)r, vk_result_str(r));
        goto out_sync;
    }
    cityman_log("renderpass CLEAR magenta RGBA 1,0,1,1");

    wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &img_sem;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &done_sem;
    r = vkQueueSubmit(queue, 1, &si, fence);
    cityman_log("vkQueueSubmit => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS)
        goto out_sync;
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done_sem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap;
    pi.pImageIndices = &img_idx;
    r = vkQueuePresentKHR(queue, &pi);
    cityman_log("vkQueuePresentKHR => %d (%s)", (int)r, vk_result_str(r));
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        goto out_sync;

    vkQueueWaitIdle(queue);
    usleep(250000);
    cityman_log("presented magenta once (FIFO); destroying");
    rc = 0;

out_sync:
    if (fence)
        vkDestroyFence(dev, fence, nullptr);
    if (done_sem)
        vkDestroySemaphore(dev, done_sem, nullptr);
    if (img_sem)
        vkDestroySemaphore(dev, img_sem, nullptr);
out_pool:
    vkDestroyCommandPool(dev, pool, nullptr);
out_rp:
    if (fbs) {
        for (i = 0; i < nimg; i++) {
            if (fbs[i])
                vkDestroyFramebuffer(dev, fbs[i], nullptr);
        }
    }
    if (views) {
        for (i = 0; i < nimg; i++) {
            if (views[i])
                vkDestroyImageView(dev, views[i], nullptr);
        }
    }
    if (rp)
        vkDestroyRenderPass(dev, rp, nullptr);
out_swap:
    if (swap)
        vkDestroySwapchainKHR(dev, swap, nullptr);
out_dev:
    free(pms);
    free(fmts);
    free(fbs);
    free(views);
    free(images);
    vkDestroyDevice(dev, nullptr);
    vkDestroySurfaceKHR(inst, surface, nullptr);
    vkDestroyInstance(inst, nullptr);
    return rc;
}

int main(void) {
    sp<SurfaceComposerClient> client;
    sp<SurfaceControl> sc;
    sp<Surface> surface;
    ANativeWindow* window = nullptr;
    uint32_t w = 0;
    uint32_t h = 0;
    int rc;

    cityman_log("cityman-vk-sfwin start (Vulkan 1.0 + SF SurfaceControl)");

    if (make_sf_window(&client, &sc, &surface, &window, &w, &h) != 0) {
        cityman_err("libgui Surface create failed; not faking present");
        return 1;
    }

    rc = present_magenta(window, w, h);

    destroy_sf_window(sc);
    surface.clear();
    sc.clear();
    client.clear();

    cityman_log("cityman-vk-sfwin done rc=%d", rc);
    return rc;
}
