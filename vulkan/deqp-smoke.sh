#!/bin/bash
# cityman one-case Vulkan dEQP smoke (LOS 18.1 / Android 11).
#
# Runs ONLY dEQP-VK.api.smoke.triangle. That is a Vulkan 1.0 instance +
# device + queue + command buffer + clear + draw. Not 1.1. Not 1.4.
#
# Does not mka. Does not add com.drawelements.deqp to device.mk.
# Does not set ro.hwui.use_vulkan. Does not reboot.
#
# Usage: vulkan/deqp-smoke.sh
# Serial: ANDROID_SERIAL or 9523fa36.

set -eu

SERIAL="${ANDROID_SERIAL:-9523fa36}"
PKG="com.drawelements.deqp"
INSTR="${PKG}/com.drawelements.deqp.testercore.DeqpInstrumentation"
CASE="dEQP-VK.api.smoke.triangle"
# DeqpInstrumentation default + A11 CTS runner APP_DIR/LOG_FILE_NAME.
QPA="/sdcard/TestLog.qpa"
# deqp-vk args used by vulkancts README (Android) and A11 DeqpTestRunner.
DEQP_CMDLINE="--deqp-case=${CASE} --deqp-log-images=disable --deqp-log-shader-sources=disable --deqp-watchdog=enable"
TREE="/home/deck/android/los-18.1"
APK_HINT="${TREE}/out/target/product/cityman/testcases/com.drawelements.deqp"

adb_s() {
	command adb -s "$SERIAL" "$@"
}

print_mka_howto() {
	cat <<EOF
com.drawelements.deqp is not installed on ${SERIAL}.

Build the APK yourself (this script will not start that build):

  cd ${TREE}
  source build/envsetup.sh
  lunch lineage_cityman-userdebug
  mka com.drawelements.deqp

Then install (owner, no reboot):

  adb -s ${SERIAL} install -r -g \\
    ${APK_HINT}/arm64/${PKG}.apk

If the arm64 path is missing, look under ${APK_HINT}/.

Do not add ${PKG} to device.mk / PRODUCT_PACKAGES.
Do not set ro.hwui.use_vulkan. Do not claim Vulkan 1.4.
EOF
}

if ! command adb -s "$SERIAL" get-state 2>/dev/null | grep -qx device; then
	echo "adb ${SERIAL} is not ready (need state=device). No reboot. Not building dEQP."
	exit 2
fi

pkg_path="$(adb_s shell pm path "$PKG" 2>/dev/null | tr -d '\r' || true)"
if [ -z "$pkg_path" ]; then
	print_mka_howto
	exit 3
fi

echo "cityman dEQP smoke: ${CASE} on ${SERIAL}"
echo "package: ${pkg_path}"
echo "instrument: am instrument -w -e deqpLogFilename ${QPA} -e deqpCmdLine \"${DEQP_CMDLINE}\" ${INSTR}"
echo "Vulkan 1.0 smoke only. Not 1.4. ro.hwui.use_vulkan left unset."

# RemoteAPI appends --deqp-log-filename from deqpLogFilename.
# Extra name is deqpLogFilename (DeqpInstrumentation.java), not deqpLogFileName.
out="$(mktemp)"
trap 'rm -f "$out"' EXIT

set +e
if command -v timeout >/dev/null 2>&1; then
	timeout 180 adb -s "$SERIAL" shell am instrument -w \
		-e deqpLogFilename "$QPA" \
		-e deqpCmdLine "$DEQP_CMDLINE" \
		"$INSTR" 2>&1 | tee "$out"
	rc="${PIPESTATUS[0]}"
else
	adb_s shell am instrument -w \
		-e deqpLogFilename "$QPA" \
		-e deqpCmdLine "$DEQP_CMDLINE" \
		"$INSTR" 2>&1 | tee "$out"
	rc="${PIPESTATUS[0]}"
fi
set -e

code="$(tr -d '\r' < "$out" | sed -n 's/^INSTRUMENTATION_STATUS: dEQP-TestCaseResult-Code=//p' | tail -n 1 || true)"
echo "dEQP-TestCaseResult-Code=${code:-<none>}"

if [ "$rc" -eq 0 ] && [ "$code" = "Pass" ]; then
	echo "PASS ${CASE} (Vulkan 1.0 smoke, not 1.4)"
	exit 0
fi

if [ "$rc" -eq 124 ]; then
	echo "FAIL ${CASE}: host timeout 180s"
	exit 1
fi

echo "FAIL ${CASE}: instrument rc=${rc} code=${code:-<none>}"
exit 1
