#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/device.h"

#include "log/log.h"

#include <vector>

namespace zui {
namespace backends {

bool VkDeviceContext::create(IWindow& window) {
        uint32_t ext_count = 0;
        const char* const* exts = nullptr;
        if (!window.vulkan_instance_extensions(&ext_count, &exts)) {
            ZUI_ERROR("no Vulkan instance extensions from window");
            return false;
        }
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "zui";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = ext_count;
        ici.ppEnabledExtensionNames = exts;
        ZUI_VK(vkCreateInstance(&ici, nullptr, &instance_));

        void* surf = nullptr;
        if (!window.create_vulkan_surface(instance_, &surf)) {
            return false;
        }
        surface_ = reinterpret_cast<VkSurfaceKHR>(surf);

        uint32_t nphys = 0;
        vkEnumeratePhysicalDevices(instance_, &nphys, nullptr);
        std::vector<VkPhysicalDevice> phys(nphys);
        vkEnumeratePhysicalDevices(instance_, &nphys, phys.data());
        if (phys.empty()) {
            ZUI_ERROR("no Vulkan device");
            return false;
        }
        gpu_ = phys[0];
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu_, &nq, qf.data());
        graphics_family_ = UINT32_MAX;
        present_family_ = UINT32_MAX;
        for (uint32_t i = 0; i < nq; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, i, surface_, &present);
            if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphics_family_ == UINT32_MAX) {
                graphics_family_ = i;
            }
            if (present && present_family_ == UINT32_MAX) {
                present_family_ = i;
            }
        }
        if (graphics_family_ == UINT32_MAX || present_family_ == UINT32_MAX) {
            ZUI_ERROR("no graphics/present queue");
            return false;
        }
        graphics_compute_ = (qf[graphics_family_].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        float prio = 1.f;
        VkDeviceQueueCreateInfo qci[2]{};
        uint32_t qcount = 1;
        qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci[0].queueFamilyIndex = graphics_family_;
        qci[0].queueCount = 1;
        qci[0].pQueuePriorities = &prio;
        if (present_family_ != graphics_family_) {
            qci[1] = qci[0];
            qci[1].queueFamilyIndex = present_family_;
            qcount = 2;
        }
        const char* dev_ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = qcount;
        dci.pQueueCreateInfos = qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = dev_ext;
        ZUI_VK(vkCreateDevice(gpu_, &dci, nullptr, &device_));
        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
        vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
    return true;
}

void VkDeviceContext::wait_idle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

void VkDeviceContext::destroy() {
        if (device_) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ && instance_) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
}

uint32_t VkDeviceContext::find_memory(uint32_t bits, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(gpu_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        return 0;
    }

}  // namespace backends
}  // namespace zui

#endif
