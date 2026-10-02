#!/bin/bash
# cityman Vulkan 1.0 present APK: install + start PresentActivity.
#
# Own SurfaceView (com.cityman.vkpresent), not SystemUI BLAST.
# Does not mka. Does not rewrite the APK. Does not reboot.
# Does not adb shell stop. Does not set ro.hwui.use_vulkan.
# HWUI stays GLES.
#
# Usage: vulkan/run-present.sh
# Serial: ANDROID_SERIAL or 9523fa36.

set -eu

SERIAL="${ANDROID_SERIAL:-9523fa36}"
TREE="/home/deck/android/los-18.1"
PRODUCT="${TREE}/out/target/product/cityman"
PKG="com.cityman.vkpresent"
COMPONENT="${PKG}/.PresentActivity"
QA="${TREE}/out/qa-vulkan-present.log"

adb_s() {
	command adb -s "$SERIAL" "$@"
}

find_apk() {
	local f
	for f in \
		"${PRODUCT}/system/app/cityman-vk-present/cityman-vk-present.apk" \
		"${PRODUCT}/product/app/cityman-vk-present/cityman-vk-present.apk" \
		"${PRODUCT}/obj/APPS/cityman-vk-present_intermediates/package.apk"
	do
		if [ -f "$f" ]; then
			printf '%s\n' "$f"
			return 0
		fi
	done
	find "${TREE}/out/soong/.intermediates/device/msft/cityman/vulkan/app" \
		-name 'cityman-vk-present.apk' -o -name 'package.apk' \
		2>/dev/null | head -n 1
}

if ! command adb -s "$SERIAL" get-state 2>/dev/null | grep -qx device; then
	echo "adb ${SERIAL} is not ready (need state=device). No reboot. No stop."
	exit 2
fi

APK="$(find_apk || true)"
if [ -z "${APK}" ] || [ ! -f "${APK}" ]; then
	cat <<EOF
cityman-vk-present APK not found under ${PRODUCT}.

Build it (this script will not start that build):

  cd ${TREE}
  source build/envsetup.sh
  lunch lineage_cityman-userdebug
  mka cityman-vk-present -j4

Then rerun: ${TREE}/device/msft/cityman/vulkan/run-present.sh

Do not set ro.hwui.use_vulkan. Do not hook SystemUI.
EOF
	exit 3
fi

echo "cityman-vk-present: install -r ${APK} on ${SERIAL}"
echo "start: am start -n ${COMPONENT}"
echo "HWUI stays GLES. ro.hwui.use_vulkan left unset."

adb_s logcat -c || true
adb_s install -r "${APK}"
adb_s shell am start -n "${COMPONENT}"

# SurfaceCreated -> native thread -> first vkQueuePresentKHR.
sleep 4

{
	echo "=== cityman-vk-present ==="
	echo "serial=${SERIAL}"
	echo "apk=${APK}"
	echo "component=${COMPONENT}"
	echo "hwui=GLES (ro.hwui.use_vulkan unset)"
	echo
	echo "=== logcat citymanVkPresent:V *:S ==="
	adb_s logcat -d citymanVkPresent:V '*:S' || true
	echo
	echo "=== dumpsys window | head ==="
	adb_s shell dumpsys window | head || true
} | tee "${QA}"

echo "wrote ${QA}"
