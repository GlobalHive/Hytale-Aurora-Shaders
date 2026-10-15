#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <wchar.h>
#include <stdio.h>
#include <initializer_list>
#include "../../hook/private_vulkan.h"

static VkResult expectedResult;
static bool filtered;
static VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *)
{
    wchar_t environment[512];
    GetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", environment, 512);
    filtered = wcsstr(environment, L"VK_LAYER_RTSS") && wcsstr(environment, L"VK_LAYER_VALVE_steam_overlay");
    return expectedResult;
}

int main()
{
    const wchar_t *variable = L"VK_LOADER_LAYERS_DISABLE";
    wchar_t original[32768];
    DWORD length = GetEnvironmentVariableW(variable, original, 32768);
    bool valid = true;
    for (const wchar_t *setting : {L"my_existing_filter", (const wchar_t *)nullptr}) {
        for (VkResult result : {VK_SUCCESS, VK_ERROR_INITIALIZATION_FAILED}) {
            SetEnvironmentVariableW(variable, setting);
            expectedResult = result; filtered = false;
            VkInstance instance = VK_NULL_HANDLE;
            VkResult actual = private_vk_create_instance(create_instance, nullptr, nullptr, &instance);
            wchar_t restored[512];
            DWORD restoredLength = GetEnvironmentVariableW(variable, restored, 512);
            valid &= filtered && actual == result && (setting ? restoredLength && !wcscmp(setting, restored) : !restoredLength);
        }
    }
    SetEnvironmentVariableW(variable, length ? original : nullptr);
    puts(valid ? "PASS: private Vulkan overlay filter restores process environment on success and failure" : "FAIL: scoped Vulkan overlay filter");
    return valid ? 0 : 1;
}
