# Copyright 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Qualcomm blob(s) necessary for Shamu hardware
PRODUCT_COPY_FILES := \
    vendor/qcom/cityman/proprietary/ATFWD-daemon:system/bin/ATFWD-daemon:qcom \
    vendor/qcom/cityman/proprietary/athdiag:system/bin/athdiag:qcom \
    vendor/qcom/cityman/proprietary/cnd:system/bin/cnd:qcom \
    vendor/qcom/cityman/proprietary/cnss-daemon:system/bin/cnss-daemon:qcom \
    vendor/qcom/cityman/proprietary/cnss_diag:system/bin/cnss_diag:qcom \
    vendor/qcom/cityman/proprietary/diag_klog:system/bin/diag_klog:qcom \
    vendor/qcom/cityman/proprietary/diag_mdlog:system/bin/diag_mdlog:qcom \
    vendor/qcom/cityman/proprietary/diag_qshrink4_daemon:system/bin/diag_qshrink4_daemon:qcom \
    vendor/qcom/cityman/proprietary/imsdatadaemon:system/bin/imsdatadaemon:qcom \
    vendor/qcom/cityman/proprietary/imsqmidaemon:system/bin/imsqmidaemon:qcom \
    vendor/qcom/cityman/proprietary/ims_rtp_daemon:system/bin/ims_rtp_daemon:qcom \
    vendor/qcom/cityman/proprietary/irsc_util:system/bin/irsc_util:qcom \
    vendor/qcom/cityman/proprietary/location-mq:system/bin/location-mq:qcom \
    vendor/qcom/cityman/proprietary/loc_launcher:system/bin/loc_launcher:qcom \
    vendor/qcom/cityman/proprietary/lowi-server:system/bin/lowi-server:qcom \
    vendor/qcom/cityman/proprietary/msm_irqbalance:system/bin/msm_irqbalance:qcom \
    vendor/qcom/cityman/proprietary/netmgrd:system/bin/netmgrd:qcom \
    vendor/qcom/cityman/proprietary/nl_listener:system/bin/nl_listener:qcom \
    vendor/qcom/cityman/proprietary/perfd:system/bin/perfd:qcom \
    vendor/qcom/cityman/proprietary/pktlogconf:system/bin/pktlogconf:qcom \
    vendor/qcom/cityman/proprietary/PktRspTest:system/bin/PktRspTest:qcom \
    vendor/qcom/cityman/proprietary/pm-proxy:system/bin/pm-proxy:qcom \
    vendor/qcom/cityman/proprietary/pm-service:system/bin/pm-service:qcom \
    vendor/qcom/cityman/proprietary/port-bridge:system/bin/port-bridge:qcom \
    vendor/qcom/cityman/proprietary/qmakernote-xtract:system/bin/qmakernote-xtract:qcom \
    vendor/qcom/cityman/proprietary/check_system_health:system/bin/qmi-framework-tests/check_system_health:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_clnt_test_0000:system/bin/qmi-framework-tests/qmi_ping_clnt_test_0000:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_clnt_test_0001:system/bin/qmi-framework-tests/qmi_ping_clnt_test_0001:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_clnt_test_1000:system/bin/qmi-framework-tests/qmi_ping_clnt_test_1000:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_clnt_test_1001:system/bin/qmi-framework-tests/qmi_ping_clnt_test_1001:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_clnt_test_2000:system/bin/qmi-framework-tests/qmi_ping_clnt_test_2000:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_svc:system/bin/qmi-framework-tests/qmi_ping_svc:qcom \
    vendor/qcom/cityman/proprietary/qmi_ping_test:system/bin/qmi-framework-tests/qmi_ping_test:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_mt_client_init_instance:system/bin/qmi-framework-tests/qmi_test_mt_client_init_instance:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_clnt_test_0000:system/bin/qmi-framework-tests/qmi_test_service_clnt_test_0000:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_clnt_test_0001:system/bin/qmi-framework-tests/qmi_test_service_clnt_test_0001:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_clnt_test_1000:system/bin/qmi-framework-tests/qmi_test_service_clnt_test_1000:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_clnt_test_1001:system/bin/qmi-framework-tests/qmi_test_service_clnt_test_1001:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_clnt_test_2000:system/bin/qmi-framework-tests/qmi_test_service_clnt_test_2000:qcom \
    vendor/qcom/cityman/proprietary/qmi_test_service_test:system/bin/qmi-framework-tests/qmi_test_service_test:qcom \
    vendor/qcom/cityman/proprietary/qmi_simple_ril_test:system/bin/qmi_simple_ril_test:qcom \
    vendor/qcom/cityman/proprietary/qmuxd:system/bin/qmuxd:qcom \
    vendor/qcom/cityman/proprietary/radish:system/bin/radish:qcom \
    vendor/qcom/cityman/proprietary/rmt_storage:system/bin/rmt_storage:qcom \
    vendor/qcom/cityman/proprietary/subsystem_ramdump:system/bin/subsystem_ramdump:qcom \
    vendor/qcom/cityman/proprietary/test_diag:system/bin/test_diag:qcom \
    vendor/qcom/cityman/proprietary/thermal-engine:system/bin/thermal-engine:qcom \
    vendor/qcom/cityman/proprietary/MTP_Bluetooth_cal.acdb:system/etc/acdbdata/MTP/MTP_Bluetooth_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_General_cal.acdb:system/etc/acdbdata/MTP/MTP_General_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_Global_cal.acdb:system/etc/acdbdata/MTP/MTP_Global_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_Handset_cal.acdb:system/etc/acdbdata/MTP/MTP_Handset_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_Hdmi_cal.acdb:system/etc/acdbdata/MTP/MTP_Hdmi_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_Headset_cal.acdb:system/etc/acdbdata/MTP/MTP_Headset_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/MTP_Speaker_cal.acdb:system/etc/acdbdata/MTP/MTP_Speaker_cal.acdb:qcom \
    vendor/qcom/cityman/proprietary/profile.txt:system/etc/cne/profile.txt:qcom \
    vendor/qcom/cityman/proprietary/dsi_config.xml:system/etc/data/dsi_config.xml:qcom \
    vendor/qcom/cityman/proprietary/netmgr_config.xml:system/etc/data/netmgr_config.xml:qcom \
    vendor/qcom/cityman/proprietary/qmi_config.xml:system/etc/data/qmi_config.xml:qcom \
    vendor/qcom/cityman/proprietary/flp.conf:system/etc/flp.conf:qcom \
    vendor/qcom/cityman/proprietary/izat.conf:system/etc/izat.conf:qcom \
    vendor/qcom/cityman/proprietary/lowi.conf:system/etc/lowi.conf:qcom \
    vendor/qcom/cityman/proprietary/cneapiclient.xml:system/etc/permissions/cneapiclient.xml:qcom \
    vendor/qcom/cityman/proprietary/embms.xml:system/etc/permissions/embms.xml:qcom \
    vendor/qcom/cityman/proprietary/qcrilhook.xml:system/etc/permissions/qcrilhook.xml:qcom \
    vendor/qcom/cityman/proprietary/qti_permissions.xml:system/etc/permissions/qti_permissions.xml:qcom \
    vendor/qcom/cityman/proprietary/rcsservice.xml:system/etc/permissions/rcsservice.xml:qcom \
    vendor/qcom/cityman/proprietary/qcril.db:system/etc/qcril.db:qcom \
    vendor/qcom/cityman/proprietary/qmi_fw.conf:system/etc/qmi_fw.conf:qcom \
    vendor/qcom/cityman/proprietary/sap.conf:system/etc/sap.conf:qcom \
    vendor/qcom/cityman/proprietary/thermal-engine-8994.conf:system/etc/thermal-engine-8994.conf:qcom \
    vendor/qcom/cityman/proprietary/cneapiclient.jar:system/framework/cneapiclient.jar:qcom \
    vendor/qcom/cityman/proprietary/embmslibrary.jar:system/framework/embmslibrary.jar:qcom \
    vendor/qcom/cityman/proprietary/qcrilhook.jar:system/framework/qcrilhook.jar:qcom \
    vendor/qcom/cityman/proprietary/rcsimssettings.jar:system/framework/rcsimssettings.jar:qcom \
    vendor/qcom/cityman/proprietary/rcsservice.jar:system/framework/rcsservice.jar:qcom \
    vendor/qcom/cityman/proprietary/lib64/gps.msm8994.so:system/lib64/hw/gps.msm8994.so:qcom \
    vendor/qcom/cityman/proprietary/lib64/libgps.utils.so:system/lib64/libgps.utils.so:qcom \
    vendor/qcom/cityman/proprietary/lib64/libiperf.so:system/lib64/libiperf.so:qcom \
    vendor/qcom/cityman/proprietary/gps.msm8994.so:system/lib/hw/gps.msm8994.so:qcom \
    vendor/qcom/cityman/proprietary/libgps.utils.so:system/lib/libgps.utils.so:qcom \
    vendor/qcom/cityman/proprietary/libiperf.so:system/lib/libiperf.so:qcom \
    vendor/qcom/cityman/proprietary/iperf3:system/xbin/iperf3:qcom \

