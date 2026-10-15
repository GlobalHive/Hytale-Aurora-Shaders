#ifndef AU_PRIVATE_VULKAN_H
#define AU_PRIVATE_VULKAN_H

static VkResult private_vk_create_instance(PFN_vkCreateInstance createInstance, const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator, VkInstance *instance)
{
    const wchar_t *variable = L"VK_LOADER_LAYERS_DISABLE";
    DWORD length = GetEnvironmentVariableW(variable, NULL, 0);
    wchar_t *previous = length ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, length * sizeof(wchar_t)) : NULL;
    if (length && (!previous || GetEnvironmentVariableW(variable, previous, length) >= length)) {
        if (previous) HeapFree(GetProcessHeap(), 0, previous);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const wchar_t *filter = L"VK_LAYER_RTSS,VK_LAYER_VALVE_steam_overlay,VK_LAYER_reshade";
    size_t capacity = (previous ? wcslen(previous) + 1 : 0) + wcslen(filter) + 1;
    wchar_t *disabled = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(wchar_t));
    if (!disabled) {
        if (previous) HeapFree(GetProcessHeap(), 0, previous);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    disabled[0] = 0;
    if (previous && previous[0]) { wcscpy(disabled, previous); wcscat(disabled, L","); }
    wcscat(disabled, filter);
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (SetEnvironmentVariableW(variable, disabled)) {
        result = createInstance(info, allocator, instance);
        SetEnvironmentVariableW(variable, previous);
    }
    HeapFree(GetProcessHeap(), 0, disabled);
    if (previous) HeapFree(GetProcessHeap(), 0, previous);
    return result;
}

#endif
