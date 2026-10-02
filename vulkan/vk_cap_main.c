/*
 * cityman-vk-cap — print cityman_vulkan_device_count() from libcityman-vkcap.
 * Does not invent a physical device. Does not set ro.hwui.use_vulkan.
 */

#include <stdio.h>

#include "VkCap.h"

int main(void)
{
    printf("%d\n", cityman_vulkan_device_count());
    return 0;
}
