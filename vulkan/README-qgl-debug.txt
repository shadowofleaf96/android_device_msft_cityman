cityman QGL / Adreno-VK debug properties (userdebug, live phone).

Init fragments already live under rootdir/etc (PRODUCT_PACKAGES in
device.mk). Do not add an extra init .rc. Do not set HWUI Vulkan
(ro.hwui.use_vulkan / debug.hwui.renderer). Do not stop/start Android.

Source of names (strings):
  vendor/lib64/hw/vulkan.msm8992.so
    debug.vulkan.profiler
    debug.scope.enabled
    debug.scope.frames
    debug.prerotation.disable
    also dlopen() of libgsl.so, libadreno_utils.so, libllvm-qgl.so,
    libVKLayer_q3dtools.so (SdpServer / InitializeProfiler)
  vendor/lib64/libgsl.so
    debug.prerotation.disable
    ro.graphics.memory (read-only; do not setprop)
    also reads /data/{misc,vendor}/gpu/adreno_config.txt
    (parse_force_chip_id) — not an Android property

AOSP libvulkan.so (loader, not the ICD):
  debug.vulkan.layers
  debug.vulkan.layer.*
  debug.vulkan.enable_callback
  Leave unset unless you intend layers.

File-based QGL settings (not properties; see gist bylaws/qgl_config.txt):
  /data/misc/gpu/qgl_config.txt
  /data/vendor/gpu/qgl_config.txt
  debugPrintGroupsEnabled / debugTracingGroupsEnabled / enablebinlog
  /data/local/tmp/vulkan/

EGL-only (libEGL_adreno.so). Do not set for this ICD dump:
  debug.egl.profiler
  debug.egl.profiler.lib64
  persist.sys.qti.profiler.lib64

Exact commands (adb -s 9523fa36 shell, or on-device):

setprop debug.vulkan.profiler 1
setprop debug.scope.enabled 1
setprop debug.scope.frames 1
setprop debug.prerotation.disable 0

Then (loads vulkan.msm8992.so in GpuService; no zygote restart):

cmd gpu vkjson

logcat -d | grep -iE 'QGL|Adreno|GSL|vulkan'

Expected log tags: QGL, Adreno, Adreno-GSL, vulkan, libvulkan.
GPU SCOPE may write /data/vendor/gpu/gpu_scope_<pid>_<id>.txt.
profiler=1 may dlopen libVKLayer_q3dtools.so (missing layer is a
dlopen fail, not a chip reject).
