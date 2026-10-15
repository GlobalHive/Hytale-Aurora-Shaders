#ifndef AUD_VULKAN_H
#define AUD_VULKAN_H
typedef struct AudVulkanContext {
    void *instance;
    void *physicalDevice;
    void *device;
    void *queue;
    unsigned queueFamily;
    void *getInstanceProcAddr;
    void *getDeviceProcAddr;
} AudVulkanContext;
#endif
