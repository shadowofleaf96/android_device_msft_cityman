#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "pm_wrapper"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void* handle = NULL;

static void init_handle() {
    if (!handle) {
        handle = dlopen("libperipheral_client_vendor.so", RTLD_NOW);
        if (!handle) {
            LOGE("Failed to load libperipheral_client_vendor.so: %s", dlerror());
        }
    }
}

void* pm_client_register(void* cb, void* cookie, const char* name, const char* client_name) {
    init_handle();
    if (!handle) return NULL;

    void* (*orig)(void*, void*, const char*, const char*) = dlsym(handle, "pm_client_register");
    if (!orig) return NULL;

    const char* target_name = name;
    if (name && strcmp(name, "modem") == 0) {
        LOGD("Intercepted pm_client_register for 'modem', changing to 'MPSS'");
        target_name = "MPSS";
    }

    return orig(cb, cookie, target_name, client_name);
}

void pm_client_unregister(void* client) {
    init_handle();
    if (!handle) return;
    void (*orig)(void*) = dlsym(handle, "pm_client_unregister");
    if (orig) orig(client);
}

void pm_client_connect(void* client) {
    init_handle();
    if (!handle) return;
    void (*orig)(void*) = dlsym(handle, "pm_client_connect");
    if (orig) orig(client);
}

void pm_client_disconnect(void* client) {
    init_handle();
    if (!handle) return;
    void (*orig)(void*) = dlsym(handle, "pm_client_disconnect");
    if (orig) orig(client);
}

void pm_client_event_acknowledge(void* client, int event) {
    init_handle();
    if (!handle) return;
    void (*orig)(void*, int) = dlsym(handle, "pm_client_event_acknowledge");
    if (orig) orig(client, event);
}
