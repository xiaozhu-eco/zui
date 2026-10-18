#ifndef ZUI_SOURCE_BACKENDS_VULKAN_VK_UTIL_H
#define ZUI_SOURCE_BACKENDS_VULKAN_VK_UTIL_H

// Shared Vulkan helper macro. A failed call logs and returns false, which is what every create
// path in this backend wants.

#include <vulkan/vulkan.h>

#include "log/log.h"

#define ZUI_VK(expr)                                                                               \
    do {                                                                                           \
        VkResult _r = (expr);                                                                      \
        if (_r != VK_SUCCESS) {                                                                    \
            ZUI_ERROR("vulkan error {} at {}", static_cast<int>(_r), #expr);                       \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#endif
