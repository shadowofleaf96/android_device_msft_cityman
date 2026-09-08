#define RIL_SHLIB 1
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <telephony/ril.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <log/log.h>

#undef LOG_TAG
#define LOG_TAG "RIL_WRAPPER"

static const struct RIL_Env *g_original_env = NULL;
static const RIL_RadioFunctions *g_original_funcs = NULL;

static int g_faked_radio_off = 1;
static RIL_Token g_get_sim_status_token = NULL;
static int g_needs_provisioning = 1;

static void* inject_qmi_thread(void* arg) {
    sleep(1);
    ALOGI("  -> [QMI INJECT] Attempting to forcefully provision the SIM using stolen handles...");
    void* qmicci = dlopen("/vendor/lib64/libqmi_cci.so", RTLD_NOW);
    void* qcril = dlopen("/vendor/lib64/libril-qc-qmi-1.so", RTLD_NOW);
    if (!qmicci || !qcril) {
        ALOGE("  -> [QMI INJECT] Failed to load QMI libraries!");
        return NULL;
    }
    
    void* (*get_user_handle)(int) = dlsym(qcril, "qcril_qmi_client_get_user_handle");
    int (*send_raw)(void*, unsigned int, void*, unsigned int, void*, unsigned int, unsigned int*, unsigned int) = dlsym(qmicci, "qmi_client_send_raw_msg_sync");
    
    if (!get_user_handle || !send_raw) {
        ALOGE("  -> [QMI INJECT] Failed to resolve QMI symbols!");
        return NULL;
    }
    
    uint8_t req[] = {
        0x01, 0x02, 0x00, 0x00, 0x00,
        0x02, 0x01, 0x00, 0x00
    };
    uint8_t resp[256];
    
    for (int i = 0; i < 20; i++) {
        void* client = get_user_handle(i);
        if (client) {
            unsigned int resp_len = 0;
            memset(resp, 0, sizeof(resp));
            int err = send_raw(client, 0x002B, req, sizeof(req), resp, sizeof(resp), &resp_len, 4000);
            
            char hex_buf[512] = {0};
            for (unsigned int j = 0; j < resp_len && j < 128; j++) {
                sprintf(hex_buf + strlen(hex_buf), "%02X ", resp[j]);
            }
            ALOGI("  -> [QMI INJECT] handle enum %d (ptr %p) send_raw err: %d, resp_len: %d, data: %s", i, client, err, resp_len, hex_buf);
        }
    }
    return NULL;
}

static void wrapped_OnRequestComplete(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
    ALOGI("OnRequestComplete: errno=%d, responselen=%zu", (int)e, responselen);

    if (t == g_get_sim_status_token) {
        g_get_sim_status_token = NULL;
        
        if (e == RIL_E_SUCCESS && response != NULL) {
            RIL_CardStatus_v6 *p_cur = (RIL_CardStatus_v6 *)response;
            ALOGI("  -> GET_SIM_STATUS response: card_state=%d, num_apps=%d", p_cur->card_state, p_cur->num_applications);
            
            if (p_cur->card_state == 0 || p_cur->num_applications == 0 || responselen < sizeof(RIL_CardStatus_v6)) {
                ALOGI("  -> [OVERRIDE] SIM ABSENT or no apps! Faking PRESENT and 1 app...");
                
                size_t fake_len = sizeof(RIL_CardStatus_v6);
                RIL_CardStatus_v6* fake_resp = (RIL_CardStatus_v6*)malloc(fake_len);
                if (fake_resp) {
                    memset(fake_resp, 0, fake_len);
                    fake_resp->card_state = 1;
                    fake_resp->universal_pin_state = 0;
                    fake_resp->gsm_umts_subscription_app_index = 0;
                    fake_resp->cdma_subscription_app_index = -1;
                    fake_resp->ims_subscription_app_index = -1;
                    fake_resp->num_applications = 1;
                    
                    fake_resp->applications[0].app_type = 2;
                    fake_resp->applications[0].app_state = 5;
                    
                    if (g_needs_provisioning) {
                        g_needs_provisioning = 0;
                        pthread_t tid;
                        pthread_create(&tid, NULL, inject_qmi_thread, NULL);
                        pthread_detach(tid);
                    }
                    
                    g_original_env->OnRequestComplete(t, e, fake_resp, fake_len);
                    free(fake_resp);
                    return;
                }
            } else {
                if (p_cur->gsm_umts_subscription_app_index < 0) {
                    ALOGI("  -> [OVERRIDE] Forcing gsm_umts_subscription_app_index to 0!");
                    p_cur->gsm_umts_subscription_app_index = 0;
                }
                
                if (p_cur->applications[0].app_state != 5) {
                    ALOGI("  -> [OVERRIDE] Forcing app_state to 5 (READY)!");
                    p_cur->applications[0].app_state = 5;
                }
                
                if (p_cur->applications[0].app_type == 0) {
                    ALOGI("  -> [OVERRIDE] Forcing app_type to 2 (USIM)!");
                    p_cur->applications[0].app_type = 2;
                }
                
                if (g_needs_provisioning) {
                    g_needs_provisioning = 0;
                    pthread_t tid;
                    pthread_create(&tid, NULL, inject_qmi_thread, NULL);
                    pthread_detach(tid);
                }
            }
        }
    }
    
    g_original_env->OnRequestComplete(t, e, response, responselen);
}

static void wrapped_OnUnsolicitedResponse(int unsolResponse, const void *data, size_t datalen) {
    ALOGI("OnUnsolicitedResponse: id=%d, datalen=%zu", unsolResponse, datalen);
    if (unsolResponse == RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED) {
        ALOGI("  -> UNSOL_RESPONSE_RADIO_STATE_CHANGED");
    } else if (unsolResponse == RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED) {
        ALOGI("  -> UNSOL_RESPONSE_SIM_STATUS_CHANGED");
    }
    
    if (g_faked_radio_off && unsolResponse == RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED) {
        ALOGI("  -> Suppressing UNSOL_RESPONSE_RADIO_STATE_CHANGED while faking OFF");
        return;
    }
    
    g_original_env->OnUnsolicitedResponse(unsolResponse, data, datalen);
}

static void wrapped_RequestTimedCallback(RIL_TimedCallback callback, void *param, const struct timeval *relativeTime) {
    g_original_env->RequestTimedCallback(callback, param, relativeTime);
}

static void wrapped_onRequestComplete_for_RIL_Env(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
    wrapped_OnRequestComplete(t, e, response, responselen);
}

static void wrapped_onRequest(int request, void *data, size_t datalen, RIL_Token t) {
    ALOGI("wrapped_onRequest: request=%d, datalen=%zu", request, datalen);
    if (request == RIL_REQUEST_RADIO_POWER) {
        int power_state = ((int *)data)[0];
        ALOGI("  -> RADIO_POWER (%d) sent to real RIL", power_state);
        
        if (power_state == 1) {
            g_faked_radio_off = 0;
            g_original_env->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
        } else if (power_state == 0 && g_faked_radio_off) {
            ALOGI("  -> Faking SUCCESS for RADIO_POWER(false)");
            g_original_env->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            return;
        }
    }

    if (request == RIL_REQUEST_GET_SIM_STATUS) {
        ALOGI("  -> GET_SIM_STATUS (11) sent to real RIL");
        g_get_sim_status_token = t;
    }

    g_original_funcs->onRequest(request, data, datalen, t);
}

static RIL_RadioState wrapped_onStateRequest() {
    RIL_RadioState real_state = g_original_funcs->onStateRequest();
    ALOGI("wrapped_onStateRequest: real_state=%d, g_faked_radio_off=%d", (int)real_state, g_faked_radio_off);
    if (g_faked_radio_off) {
        return RADIO_STATE_OFF;
    }
    return real_state;
}

static int wrapped_onSupports(int requestCode) {
    return g_original_funcs->supports(requestCode);
}

static void wrapped_onCancel(RIL_Token t) {
    g_original_funcs->onCancel(t);
}

static const RIL_RadioFunctions g_wrapped_funcs = {
    1,
    wrapped_onRequest,
    wrapped_onStateRequest,
    wrapped_onSupports,
    wrapped_onCancel,
    NULL
};

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv) {
    ALOGI("ril_wrapper RIL_Init called");
    
    g_original_env = env;
    
    struct RIL_Env *custom_env = malloc(sizeof(struct RIL_Env));
    custom_env->OnRequestComplete = wrapped_onRequestComplete_for_RIL_Env;
    custom_env->OnUnsolicitedResponse = wrapped_OnUnsolicitedResponse;
    custom_env->RequestTimedCallback = wrapped_RequestTimedCallback;

    void *real_ril = dlopen("/vendor/lib64/libril-qc-qmi-1.so", RTLD_NOW);
    if (!real_ril) {
        ALOGE("Failed to open real RIL: %s", dlerror());
        return NULL;
    }
    
    const RIL_RadioFunctions *(*real_ril_init)(const struct RIL_Env *, int, char **) = dlsym(real_ril, "RIL_Init");
    if (!real_ril_init) {
        ALOGE("Failed to find RIL_Init in real RIL: %s", dlerror());
        return NULL;
    }
    
    g_original_funcs = real_ril_init(custom_env, argc, argv);
    
    return &g_wrapped_funcs;
}
