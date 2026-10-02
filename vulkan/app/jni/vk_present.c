/*
 * cityman-vk-present — Vulkan 1.0 FIFO present on a SurfaceView ANativeWindow.
 *
 * Own Surface via ANativeWindow_fromSurface (not SystemUI ViewRootImpl BLAST).
 * Instance API 1.0 + VK_KHR_surface + VK_KHR_android_surface.
 * Device VK_KHR_swapchain (ICD name VK_ANDROID_native_buffer; loader remaps).
 * enumerate==0 => log and exit. No fake frames. No AHB import. No 1.1/1.4.
 */

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_0
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffu
#endif

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_th;
static int g_th_live;
static int g_stop;
static int g_init_done;
static int g_present_attempted;
static char g_line[768];
static ANativeWindow* g_win;

static void cityman_log(const char* fmt, ...)
        __attribute__((format(printf, 1, 2)));

static void cityman_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
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
    __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, buf);
}

static void set_line(const char* s) {
    pthread_mutex_lock(&g_mu);
    snprintf(g_line, sizeof(g_line), "%s", s);
    g_init_done = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    cityman_log("%s", s);
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
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
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

static int stopped(void) {
    int s;

    pthread_mutex_lock(&g_mu);
    s = g_stop;
    pthread_mutex_unlock(&g_mu);
    return s;
}

static int has_instance_ext(const char* name) {
    uint32_t n = 0;
    VkExtensionProperties* props;
    uint32_t i;
    int found = 0;
    VkResult r;

    r = vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0)
        return 0;
    props = (VkExtensionProperties*)calloc(n, sizeof(*props));
    if (!props)
        return 0;
    r = vkEnumerateInstanceExtensionProperties(NULL, &n, props);
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

    r = vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, NULL);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0)
        return 0;
    props = (VkExtensionProperties*)calloc(n, sizeof(*props));
    if (!props)
        return 0;
    r = vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, props);
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

    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, NULL);
    if (n == 0)
        return -1;
    props = (VkQueueFamilyProperties*)calloc(n, sizeof(*props));
    if (!props)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, props);
    for (i = 0; i < n; i++) {
        VkBool32 support = VK_FALSE;

        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &support) !=
                    VK_SUCCESS ||
            !support)
            continue;
        *family = i;
        free(props);
        return 0;
    }
    free(props);
    return -1;
}

static int present_on_window(ANativeWindow* window) {
    const char* inst_exts[2];
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r;
    uint32_t n = 0;
    VkPhysicalDevice* gpus = NULL;
    VkPhysicalDevice gpu;
    VkPhysicalDeviceProperties props;
    PFN_vkCreateAndroidSurfaceKHR create_surf;
    VkAndroidSurfaceCreateInfoKHR sci;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t qf = 0;
    const char* swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR caps;
    VkExtent2D extent;
    uint32_t fmt_n = 0;
    VkSurfaceFormatKHR* fmts = NULL;
    VkSurfaceFormatKHR pick;
    uint32_t img_count;
    uint32_t mode_n = 0;
    VkPresentModeKHR* modes = NULL;
    int fifo_ok = 0;
    uint32_t mi;
    VkSwapchainCreateInfoKHR swci;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    uint32_t nimg = 0;
    VkImage* images = NULL;
    VkImageView* views = NULL;
    VkFramebuffer* fbs = NULL;
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
    int frames = 0;
    int rc = 1;
    char line[256];

    cityman_log("cityman-vk-present start (Vulkan 1.0, no AHB, no 1.1/1.4)");
    cityman_log("ANativeWindow=%p %dx%d", (void*)window,
                ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));

    if (!has_instance_ext(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_instance_ext(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
        set_line("FAIL missing VK_KHR_surface / VK_KHR_android_surface");
        return 1;
    }
    inst_exts[0] = VK_KHR_SURFACE_EXTENSION_NAME;
    inst_exts[1] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "cityman-vk-present";
    app.applicationVersion = 1;
    app.pEngineName = "cityman-vk-present";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = inst_exts;

    r = vkCreateInstance(&ici, NULL, &inst);
    cityman_log("vkCreateInstance => %d (%s) api=1.0 + %s + %s", (int)r,
                vk_result_str(r), inst_exts[0], inst_exts[1]);
    if (r != VK_SUCCESS || inst == VK_NULL_HANDLE) {
        set_line("FAIL vkCreateInstance");
        return 1;
    }

    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    cityman_log("vkEnumeratePhysicalDevices count-query => %d (%s) n=%u",
                (int)r, vk_result_str(r), n);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        set_line("FAIL vkEnumeratePhysicalDevices");
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    if (n == 0) {
        cityman_err("enumerate==0; no Vulkan GPU. Not faking frames.");
        set_line("enumerate==0; not faking frames. PRESENT_ATTEMPTED=0");
        vkDestroyInstance(inst, NULL);
        return 2;
    }

    gpus = (VkPhysicalDevice*)calloc(n, sizeof(*gpus));
    if (!gpus) {
        vkDestroyInstance(inst, NULL);
        set_line("FAIL oom");
        return 1;
    }
    r = vkEnumeratePhysicalDevices(inst, &n, gpus);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) {
        set_line("FAIL enumerate fill");
        free(gpus);
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    gpu = gpus[0];
    memset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceProperties(gpu, &props);
    cityman_log("physdev[0] name=%s api=0x%x vendor=0x%x device=0x%x",
                props.deviceName, props.apiVersion, props.vendorID,
                props.deviceID);

    create_surf = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(
            inst, "vkCreateAndroidSurfaceKHR");
    if (!create_surf) {
        set_line("FAIL vkCreateAndroidSurfaceKHR missing");
        goto out_inst;
    }
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    r = create_surf(inst, &sci, NULL, &surface);
    cityman_log("vkCreateAndroidSurfaceKHR => %d (%s) surface=%p", (int)r,
                vk_result_str(r), (void*)surface);
    if (r != VK_SUCCESS) {
        set_line("FAIL vkCreateAndroidSurfaceKHR");
        goto out_inst;
    }

    if (find_present_queue(gpu, surface, &qf) != 0) {
        set_line("FAIL no graphics+present queue");
        goto out_surf;
    }
    cityman_log("queueFamily=%u present=1", qf);

    if (!has_device_ext(gpu, swap_ext)) {
        cityman_err("device missing %s (ICD should export "
                    "VK_ANDROID_native_buffer; loader remaps). Not faking.",
                    swap_ext);
        set_line("FAIL missing VK_KHR_swapchain");
        goto out_surf;
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

    r = vkCreateDevice(gpu, &dci, NULL, &dev);
    cityman_log("vkCreateDevice + %s => %d (%s)", swap_ext, (int)r,
                vk_result_str(r));
    if (r != VK_SUCCESS) {
        set_line("FAIL vkCreateDevice VK_KHR_swapchain");
        goto out_surf;
    }
    vkGetDeviceQueue(dev, qf, 0, &queue);

    memset(&caps, 0, sizeof(caps));
    r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
    cityman_log("surface caps => %d (%s) current=%ux%u min=%ux%u max=%ux%u "
                "minImages=%u maxImages=%u",
                (int)r, vk_result_str(r), caps.currentExtent.width,
                caps.currentExtent.height, caps.minImageExtent.width,
                caps.minImageExtent.height, caps.maxImageExtent.width,
                caps.maxImageExtent.height, caps.minImageCount,
                caps.maxImageCount);
    if (r != VK_SUCCESS)
        goto fail_dev;

    extent = caps.currentExtent;
    if (extent.width == 0 || extent.height == 0 ||
        extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
        int ww = ANativeWindow_getWidth(window);
        int wh = ANativeWindow_getHeight(window);

        if (ww <= 0 || wh <= 0) {
            set_line("FAIL surface extent 0; not inventing a size");
            goto out_dev;
        }
        extent.width = (uint32_t)ww;
        extent.height = (uint32_t)wh;
    }
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    if (caps.maxImageExtent.width > 0 && extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    if (caps.maxImageExtent.height > 0 &&
        extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;

    r = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_n, NULL);
    if (r != VK_SUCCESS || fmt_n == 0) {
        set_line("FAIL no surface formats");
        goto out_dev;
    }
    fmts = (VkSurfaceFormatKHR*)calloc(fmt_n, sizeof(*fmts));
    if (!fmts)
        goto fail_dev;
    r = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmt_n, fmts);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE)
        goto fail_dev;
    pick = fmts[0];
    for (i = 0; i < fmt_n; i++) {
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
            fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            pick = fmts[i];
            break;
        }
    }
    cityman_log("surface format=%u colorspace=%u", (unsigned)pick.format,
                (unsigned)pick.colorSpace);

    /* FIFO is required by the spec. Use it even if the mode list is empty. */
    fifo_ok = 1;
    r = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_n, NULL);
    if (r == VK_SUCCESS && mode_n > 0) {
        modes = (VkPresentModeKHR*)calloc(mode_n, sizeof(*modes));
        if (modes) {
            r = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_n,
                                                          modes);
            if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
                fifo_ok = 0;
                for (mi = 0; mi < mode_n; mi++) {
                    if (modes[mi] == VK_PRESENT_MODE_FIFO_KHR)
                        fifo_ok = 1;
                }
            }
        }
    }
    free(modes);
    modes = NULL;
    if (!fifo_ok) {
        cityman_err("FIFO not listed; not falling back to IMMEDIATE/MAILBOX");
        set_line("FAIL FIFO not available");
        goto out_dev;
    }

    img_count = caps.minImageCount + 1;
    if (img_count < 2)
        img_count = 2;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;
    if (img_count > 3)
        img_count = 3;

    memset(&swci, 0, sizeof(swci));
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface;
    swci.minImageCount = img_count;
    swci.imageFormat = pick.format;
    swci.imageColorSpace = pick.colorSpace;
    swci.imageExtent = extent;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swci.clipped = VK_TRUE;

    r = vkCreateSwapchainKHR(dev, &swci, NULL, &swap);
    cityman_log("vkCreateSwapchainKHR FIFO => %d (%s) %ux%u images>=%u", (int)r,
                vk_result_str(r), extent.width, extent.height, img_count);
    if (r != VK_SUCCESS) {
        set_line("FAIL vkCreateSwapchainKHR");
        goto out_dev;
    }

    r = vkGetSwapchainImagesKHR(dev, swap, &nimg, NULL);
    if (r != VK_SUCCESS || nimg == 0)
        goto fail_swap;
    images = (VkImage*)calloc(nimg, sizeof(*images));
    views = (VkImageView*)calloc(nimg, sizeof(*views));
    fbs = (VkFramebuffer*)calloc(nimg, sizeof(*fbs));
    if (!images || !views || !fbs)
        goto fail_swap;
    r = vkGetSwapchainImagesKHR(dev, swap, &nimg, images);
    if (r != VK_SUCCESS)
        goto fail_swap;

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
    r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
    if (r != VK_SUCCESS)
        goto fail_swap;

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
        r = vkCreateImageView(dev, &vci, NULL, &views[i]);
        if (r != VK_SUCCESS)
            goto fail_rp;

        memset(&fbci, 0, sizeof(fbci));
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = rp;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &views[i];
        fbci.width = extent.width;
        fbci.height = extent.height;
        fbci.layers = 1;
        r = vkCreateFramebuffer(dev, &fbci, NULL, &fbs[i]);
        if (r != VK_SUCCESS)
            goto fail_rp;
    }

    memset(&pci, 0, sizeof(pci));
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qf;
    r = vkCreateCommandPool(dev, &pci, NULL, &pool);
    if (r != VK_SUCCESS)
        goto fail_rp;

    memset(&ai, 0, sizeof(ai));
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(dev, &ai, &cmd);
    if (r != VK_SUCCESS)
        goto fail_pool;

    memset(&semci, 0, sizeof(semci));
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateSemaphore(dev, &semci, NULL, &img_sem) != VK_SUCCESS ||
        vkCreateSemaphore(dev, &semci, NULL, &done_sem) != VK_SUCCESS ||
        vkCreateFence(dev, &fci, NULL, &fence) != VK_SUCCESS)
        goto fail_sync;

    cityman_log("clear color RGB(0.90, 0.10, 0.55) FIFO loop until surface gone");

    while (!stopped()) {
        uint32_t img_idx = 0;
        VkCommandBufferBeginInfo bi;
        VkClearValue clear;
        VkRenderPassBeginInfo rpbi;
        VkPipelineStageFlags wait_stage;
        VkSubmitInfo si;
        VkPresentInfoKHR pi;

        r = vkAcquireNextImageKHR(dev, swap, 1000000000ull, img_sem,
                                  VK_NULL_HANDLE, &img_idx);
        if (r == VK_TIMEOUT) {
            continue;
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            cityman_err("vkAcquireNextImageKHR => %d (%s)", (int)r,
                        vk_result_str(r));
            if (frames == 0) {
                snprintf(line, sizeof(line),
                         "FAIL vkAcquireNextImageKHR %s PRESENT_ATTEMPTED=0",
                         vk_result_str(r));
                set_line(line);
            }
            break;
        }

        vkResetCommandBuffer(cmd, 0);
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS)
            break;

        memset(&clear, 0, sizeof(clear));
        clear.color.float32[0] = 0.90f;
        clear.color.float32[1] = 0.10f;
        clear.color.float32[2] = 0.55f;
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
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            break;

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
        vkResetFences(dev, 1, &fence);
        r = vkQueueSubmit(queue, 1, &si, fence);
        if (r != VK_SUCCESS) {
            cityman_err("vkQueueSubmit => %d (%s)", (int)r, vk_result_str(r));
            break;
        }
        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

        pthread_mutex_lock(&g_mu);
        g_present_attempted = 1;
        pthread_mutex_unlock(&g_mu);

        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done_sem;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap;
        pi.pImageIndices = &img_idx;
        r = vkQueuePresentKHR(queue, &pi);
        if (frames == 0) {
            cityman_log("vkQueuePresentKHR => %d (%s) PRESENT_ATTEMPTED=1",
                        (int)r, vk_result_str(r));
            snprintf(line, sizeof(line),
                     "OK present %s %ux%u PRESENT_ATTEMPTED=1", vk_result_str(r),
                     extent.width, extent.height);
            set_line(line);
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            cityman_err("vkQueuePresentKHR => %d (%s)", (int)r, vk_result_str(r));
            break;
        }
        frames++;
        if (frames == 1 || (frames % 60) == 0)
            cityman_log("presented frames=%d", frames);
    }

    vkDeviceWaitIdle(dev);
    cityman_log("present loop end frames=%d PRESENT_ATTEMPTED=%d", frames,
                frames > 0 ? 1 : 0);
    rc = (frames > 0) ? 0 : 1;
    goto out_sync;

fail_sync:
    set_line("FAIL sync objects");
    goto out_sync;
fail_pool:
    set_line("FAIL command pool");
    goto out_pool;
fail_rp:
    set_line("FAIL renderpass/views");
    goto out_rp;
fail_swap:
    set_line("FAIL swapchain images");
    goto out_swap;
fail_dev:
    set_line("FAIL device/surface query");
    goto out_dev;

out_sync:
    if (fence)
        vkDestroyFence(dev, fence, NULL);
    if (done_sem)
        vkDestroySemaphore(dev, done_sem, NULL);
    if (img_sem)
        vkDestroySemaphore(dev, img_sem, NULL);
out_pool:
    if (pool)
        vkDestroyCommandPool(dev, pool, NULL);
out_rp:
    if (fbs) {
        for (i = 0; i < nimg; i++) {
            if (fbs[i])
                vkDestroyFramebuffer(dev, fbs[i], NULL);
        }
    }
    if (views) {
        for (i = 0; i < nimg; i++) {
            if (views[i])
                vkDestroyImageView(dev, views[i], NULL);
        }
    }
    if (rp)
        vkDestroyRenderPass(dev, rp, NULL);
out_swap:
    free(fbs);
    free(views);
    free(images);
    if (swap)
        vkDestroySwapchainKHR(dev, swap, NULL);
out_dev:
    free(fmts);
    vkDestroyDevice(dev, NULL);
out_surf:
    vkDestroySurfaceKHR(inst, surface, NULL);
out_inst:
    free(gpus);
    vkDestroyInstance(inst, NULL);
    if (!g_init_done)
        set_line("FAIL present path");
    return rc;
}

static void* vk_thread(void* arg) {
    ANativeWindow* win = (ANativeWindow*)arg;

    present_on_window(win);
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_cityman_vkpresent_PresentActivity_nativeOnSurfaceCreated(
        JNIEnv* env, jobject thiz, jobject surface) {
    ANativeWindow* win;

    (void)thiz;
    if (!surface) {
        set_line("FAIL null Surface");
        return;
    }
    win = ANativeWindow_fromSurface(env, surface);
    cityman_log("ANativeWindow_fromSurface => %p", (void*)win);
    if (!win) {
        set_line("FAIL ANativeWindow_fromSurface");
        return;
    }

    pthread_mutex_lock(&g_mu);
    if (g_th_live) {
        pthread_mutex_unlock(&g_mu);
        ANativeWindow_release(win);
        cityman_err("present thread already live");
        return;
    }
    g_win = win;
    g_stop = 0;
    g_init_done = 0;
    g_present_attempted = 0;
    g_line[0] = '\0';
    if (pthread_create(&g_th, NULL, vk_thread, win) != 0) {
        g_win = NULL;
        pthread_mutex_unlock(&g_mu);
        ANativeWindow_release(win);
        set_line("FAIL pthread_create");
        return;
    }
    g_th_live = 1;
    pthread_mutex_unlock(&g_mu);
}

JNIEXPORT void JNICALL
Java_com_cityman_vkpresent_PresentActivity_nativeOnSurfaceDestroyed(
        JNIEnv* env, jobject thiz) {
    ANativeWindow* win = NULL;
    int live = 0;

    (void)env;
    (void)thiz;
    pthread_mutex_lock(&g_mu);
    g_stop = 1;
    live = g_th_live;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    if (live)
        pthread_join(g_th, NULL);
    pthread_mutex_lock(&g_mu);
    g_th_live = 0;
    win = g_win;
    g_win = NULL;
    pthread_mutex_unlock(&g_mu);
    if (win)
        ANativeWindow_release(win);
    cityman_log("surface destroyed; window released");
}

JNIEXPORT jstring JNICALL
Java_com_cityman_vkpresent_PresentActivity_nativeAwaitInit(JNIEnv* env,
                                                           jobject thiz) {
    char copy[768];

    (void)thiz;
    pthread_mutex_lock(&g_mu);
    while (!g_init_done && !g_stop)
        pthread_cond_wait(&g_cv, &g_mu);
    if (!g_init_done)
        snprintf(copy, sizeof(copy), "FAIL stopped before init PRESENT_ATTEMPTED=%d",
                 g_present_attempted);
    else
        snprintf(copy, sizeof(copy), "%s", g_line);
    pthread_mutex_unlock(&g_mu);
    return (*env)->NewStringUTF(env, copy);
}
