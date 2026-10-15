#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define AU_FRAMEGEN_EXPORTS
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <initializer_list>
#include "sl.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
#include "streamline_security.h"
#include "aud_framegen.h"
#include "private_vulkan.h"

typedef unsigned GLuint, GLenum, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_RGBA8 0x8058
#define GL_BLEND 0x0be2
#define GL_DEPTH_TEST 0x0b71
#define GL_SCISSOR_TEST 0x0c11
#define GL_CULL_FACE 0x0b44
#define GL_STENCIL_TEST 0x0b90
#define GL_VIEWPORT 0x0ba2
#define GL_TRIANGLES 0x0004
#define GL_READ_BUFFER 0x0c02
#define GL_BACK 0x0405
#define GL_COLOR_BUFFER_BIT 0x4000

static AfgLog logger;
static HMODULE interposer;
static HWND outputWindow;
static HWND presentationWindow;
static HANDLE presentationThread, presentationReady;
static HINSTANCE presentationModule;
static char statusText[160] = "Off";
static bool initialized;
static bool ready;
static unsigned presentedFrames;
static sl::ViewportHandle viewport(0);
static sl::FrameToken *frameToken;
#define FG_LOG(...) do { if (logger) logger(__VA_ARGS__); } while (0)

static LRESULT CALLBACK presentation_proc(HWND window, UINT message, WPARAM word, LPARAM parameter)
{
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, word, parameter);
}

static DWORD WINAPI presentation_owner(void *)
{
    const wchar_t *className = L"AuroraFrameGenerationSurface";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = presentation_proc;
    windowClass.hInstance = presentationModule; windowClass.lpszClassName = className;
    bool registered = RegisterClassW(&windowClass) != 0;
    RECT client{};
    if ((registered || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) && GetClientRect(outputWindow, &client)) {
        presentationWindow = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, className, L"Aurora Frame Generation", WS_CHILD, 0, 0, client.right, client.bottom, outputWindow, nullptr, presentationModule, nullptr);
        if (presentationWindow) EnableWindow(presentationWindow, FALSE);
    }
    SetEvent(presentationReady);
    if (presentationWindow) {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (registered) UnregisterClassW(className, presentationModule);
    return 0;
}

static DWORD wait_presentation_thread(HANDLE event, DWORD timeout)
{
    ULONGLONG deadline = GetTickCount64() + timeout;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return WAIT_TIMEOUT;
        DWORD result = MsgWaitForMultipleObjects(1, &event, FALSE, (DWORD)(deadline - now), QS_SENDMESSAGE);
        if (result != WAIT_OBJECT_0 + 1) return result;
        MSG message;
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    }
}

static bool create_presentation_window()
{
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(presentation_proc), &presentationModule)) return false;
    presentationReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!presentationReady) return false;
    presentationThread = CreateThread(nullptr, 0, presentation_owner, nullptr, 0, nullptr);
    bool success = presentationThread && wait_presentation_thread(presentationReady, 5000) == WAIT_OBJECT_0 && presentationWindow;
    if (success || !presentationThread) { CloseHandle(presentationReady); presentationReady = nullptr; }
    return success;
}
#define SL_FUNCTIONS(ENTRY) ENTRY(slInit) ENTRY(slShutdown) ENTRY(slIsFeatureSupported) ENTRY(slGetFeatureFunction) ENTRY(slGetNewFrameToken) ENTRY(slSetConstants) ENTRY(slSetTagForFrame)
#define SL_DECLARE(name) static PFun_##name *name##_;
SL_FUNCTIONS(SL_DECLARE)
static PFun_slDLSSGSetOptions *setOptions;
static PFun_slDLSSGGetState *getState;
static PFun_slReflexSetOptions *setReflexOptions;
static PFun_slReflexSleep *reflexSleep;
static PFun_slPCLSetMarker *setMarker;

static bool sl_ok(sl::Result result, const char *operation)
{
    if (result == sl::Result::eOk) return true;
    snprintf(statusText, sizeof(statusText), "%s failed (%d)", operation, (int)result);
    FG_LOG("framegen: %s", statusText);
    return false;
}

#define VK_FUNCTIONS(ENTRY) \
    ENTRY(vkCreateInstance) ENTRY(vkDestroyInstance) ENTRY(vkEnumeratePhysicalDevices) ENTRY(vkGetPhysicalDeviceProperties2) \
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties) ENTRY(vkGetPhysicalDeviceMemoryProperties) ENTRY(vkGetPhysicalDeviceFeatures2) \
    ENTRY(vkEnumerateDeviceExtensionProperties) \
    ENTRY(vkCreateDevice) ENTRY(vkDestroyDevice) ENTRY(vkGetDeviceProcAddr) ENTRY(vkGetDeviceQueue) ENTRY(vkDeviceWaitIdle) \
    ENTRY(vkCreateWin32SurfaceKHR) ENTRY(vkDestroySurfaceKHR) ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR) \
    ENTRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) ENTRY(vkGetPhysicalDeviceSurfaceFormatsKHR) ENTRY(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    ENTRY(vkCreateSwapchainKHR) ENTRY(vkDestroySwapchainKHR) ENTRY(vkGetSwapchainImagesKHR) ENTRY(vkAcquireNextImageKHR) ENTRY(vkQueuePresentKHR) \
    ENTRY(vkCreateCommandPool) ENTRY(vkDestroyCommandPool) ENTRY(vkAllocateCommandBuffers) ENTRY(vkResetCommandBuffer) \
    ENTRY(vkBeginCommandBuffer) ENTRY(vkEndCommandBuffer) ENTRY(vkQueueSubmit) ENTRY(vkCreateFence) ENTRY(vkDestroyFence) ENTRY(vkWaitForFences) ENTRY(vkResetFences) \
    ENTRY(vkCreateImage) ENTRY(vkDestroyImage) ENTRY(vkGetImageMemoryRequirements) ENTRY(vkAllocateMemory) ENTRY(vkFreeMemory) \
    ENTRY(vkBindImageMemory) ENTRY(vkCreateImageView) ENTRY(vkDestroyImageView) ENTRY(vkGetMemoryWin32HandleKHR) \
    ENTRY(vkCreateSemaphore) ENTRY(vkDestroySemaphore) ENTRY(vkGetSemaphoreWin32HandleKHR) ENTRY(vkCmdPipelineBarrier) ENTRY(vkCmdBlitImage)
#define VK_DECLARE(name) static PFN_##name name##_;
VK_FUNCTIONS(VK_DECLARE)
static PFN_vkGetInstanceProcAddr instanceProc;
static VkInstance instance;
static VkPhysicalDevice physicalDevice;
static VkDevice device;
static VkQueue queue;
static uint32_t queueFamily;
static VkPhysicalDeviceMemoryProperties memoryProperties;
static VkSurfaceKHR surface;
static VkSwapchainKHR swapchain;
static VkFormat swapchainFormat;
static VkImage swapchainImages[8];
static uint32_t swapchainCount;
static VkSemaphore acquireSemaphore, presentSemaphores[8], glReady, vkReady;
static VkCommandPool commandPool;
static VkCommandBuffer command;
static VkFence fence;
static int renderWidth, renderHeight, outputWidth, outputHeight;
static bool inputsCaptured, hudCaptured;
static bool vkPending;

#define GL_FUNCTIONS(ENTRY) \
    ENTRY(void, GetIntegerv, (GLenum, GLint *)) ENTRY(GLboolean, IsEnabled, (GLenum)) \
    ENTRY(void, Enable, (GLenum)) ENTRY(void, Disable, (GLenum)) ENTRY(void, ActiveTexture, (GLenum)) \
    ENTRY(void, GenTextures, (GLsizei, GLuint *)) ENTRY(void, DeleteTextures, (GLsizei, const GLuint *)) \
    ENTRY(void, BindTexture, (GLenum, GLuint)) ENTRY(void, TexParameteri, (GLenum, GLenum, GLint)) \
    ENTRY(void, CreateMemoryObjectsEXT, (GLsizei, GLuint *)) ENTRY(void, DeleteMemoryObjectsEXT, (GLsizei, const GLuint *)) \
    ENTRY(void, MemoryObjectParameterivEXT, (GLuint, GLenum, const GLint *)) \
    ENTRY(void, ImportMemoryWin32HandleEXT, (GLuint, unsigned long long, GLenum, void *)) \
    ENTRY(void, TexStorageMem2DEXT, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, unsigned long long)) \
    ENTRY(void, GenSemaphoresEXT, (GLsizei, GLuint *)) ENTRY(void, DeleteSemaphoresEXT, (GLsizei, const GLuint *)) \
    ENTRY(void, ImportSemaphoreWin32HandleEXT, (GLuint, GLenum, void *)) \
    ENTRY(void, SignalSemaphoreEXT, (GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *)) \
    ENTRY(void, WaitSemaphoreEXT, (GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *)) \
    ENTRY(void, GetUnsignedBytei_vEXT, (GLenum, GLuint, unsigned char *)) ENTRY(void, Flush, ()) \
    ENTRY(void, Finish, ()) \
    ENTRY(void, GenFramebuffers, (GLsizei, GLuint *)) ENTRY(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
    ENTRY(void, BindFramebuffer, (GLenum, GLuint)) ENTRY(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    ENTRY(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    ENTRY(void, DrawBuffers, (GLsizei, const GLenum *)) ENTRY(void, ReadBuffer, (GLenum)) \
    ENTRY(GLuint, CreateShader, (GLenum)) ENTRY(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
    ENTRY(void, CompileShader, (GLuint)) ENTRY(GLuint, CreateProgram, ()) ENTRY(void, AttachShader, (GLuint, GLuint)) \
    ENTRY(void, LinkProgram, (GLuint)) ENTRY(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    ENTRY(void, DeleteProgram, (GLuint)) ENTRY(void, DeleteShader, (GLuint)) ENTRY(void, UseProgram, (GLuint)) \
    ENTRY(GLint, GetUniformLocation, (GLuint, const char *)) ENTRY(void, Uniform1i, (GLint, GLint)) \
    ENTRY(void, Uniform2f, (GLint, GLfloat, GLfloat)) ENTRY(void, BindSampler, (GLuint, GLuint)) \
    ENTRY(void, GetIntegeri_v, (GLenum, GLuint, GLint *)) ENTRY(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    ENTRY(void, GenVertexArrays, (GLsizei, GLuint *)) ENTRY(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
    ENTRY(void, BindVertexArray, (GLuint)) ENTRY(void, DrawArrays, (GLenum, GLint, GLsizei))
#define GL_DECLARE(result, name, arguments) static result (APIENTRY *gl##name) arguments;
GL_FUNCTIONS(GL_DECLARE)
static GLuint glReadyName, vkReadyName, copyFbo, inputFbo, conversionProgram, conversionVAO;

struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkFormat format{};
    GLuint texture{}, memoryObject{};
    int width{}, height{};
};
enum { RawColor, RawDepth, RawMotion, Depth, Motion, Backbuffer, Hudless, HudlessTop, ImageCount };
static Image images[ImageCount];

static bool vk_ok(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return true;
    snprintf(statusText, sizeof(statusText), "%s failed (%d)", operation, (int)result);
    FG_LOG("framegen: %s", statusText);
    return false;
}

static void barrier(VkImage image, VkImageLayout before, VkImageLayout after, uint32_t from = VK_QUEUE_FAMILY_IGNORED, uint32_t to = VK_QUEUE_FAMILY_IGNORED)
{
    VkImageMemoryBarrier transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    transition.oldLayout = before;
    transition.newLayout = after;
    transition.srcQueueFamilyIndex = from;
    transition.dstQueueFamilyIndex = to;
    transition.image = image;
    transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    transition.srcAccessMask = before == VK_IMAGE_LAYOUT_UNDEFINED || from == VK_QUEUE_FAMILY_EXTERNAL ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    transition.dstAccessMask = after == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR || to == VK_QUEUE_FAMILY_EXTERNAL ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier_(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &transition);
}

static void destroy_image(Image &image)
{
    if (image.texture) glDeleteTextures(1, &image.texture);
    if (image.memoryObject) glDeleteMemoryObjectsEXT(1, &image.memoryObject);
    if (image.view) vkDestroyImageView_(device, image.view, nullptr);
    if (image.image) vkDestroyImage_(device, image.image, nullptr);
    if (image.memory) vkFreeMemory_(device, image.memory, nullptr);
    image = {};
}

static bool create_image(Image &image, int width, int height, VkFormat format, GLenum glFormat)
{
    image.width = width; image.height = height; image.format = format;
    VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.pNext = glFormat ? &external : nullptr;
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
    info.extent = {(uint32_t)width, (uint32_t)height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (!vk_ok(vkCreateImage_(device, &info, nullptr, &image.image), "create image")) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements_(device, image.image, &requirements);
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = image.image;
    VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    exportInfo.pNext = &dedicated; exportInfo.handleTypes = external.handleTypes;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = glFormat ? (void *)&exportInfo : (void *)&dedicated;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = UINT32_MAX;
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; index++)
        if ((requirements.memoryTypeBits & (1u << index)) && (memoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            allocation.memoryTypeIndex = index; break;
        }
    if (allocation.memoryTypeIndex == UINT32_MAX || !vk_ok(vkAllocateMemory_(device, &allocation, nullptr, &image.memory), "allocate image memory")) return false;
    if (!vk_ok(vkBindImageMemory_(device, image.image, image.memory, 0), "bind image memory")) return false;
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!vk_ok(vkCreateImageView_(device, &viewInfo, nullptr, &image.view), "create image view")) return false;
    if (glFormat) {
        VkMemoryGetWin32HandleInfoKHR handleInfo{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
        handleInfo.memory = image.memory; handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        HANDLE handle = nullptr;
        if (!vk_ok(vkGetMemoryWin32HandleKHR_(device, &handleInfo, &handle), "export image")) return false;
        glCreateMemoryObjectsEXT(1, &image.memoryObject);
        GLint enabled = 1;
        glMemoryObjectParameterivEXT(image.memoryObject, 0x9581, &enabled);
        glImportMemoryWin32HandleEXT(image.memoryObject, requirements.size, 0x9587, handle);
        CloseHandle(handle);
        GLint previousTexture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGenTextures(1, &image.texture);
        glBindTexture(GL_TEXTURE_2D, image.texture);
        glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, glFormat, width, height, image.memoryObject, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812f);
        glBindTexture(GL_TEXTURE_2D, previousTexture);
    }
    return true;
}

static bool shared_semaphore(VkSemaphore &semaphore, GLuint &name)
{
    VkExportSemaphoreCreateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &exportInfo};
    if (!vk_ok(vkCreateSemaphore_(device, &info, nullptr, &semaphore), "create shared semaphore")) return false;
    VkSemaphoreGetWin32HandleInfoKHR handleInfo{VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR};
    handleInfo.semaphore = semaphore; handleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE handle = nullptr;
    if (!vk_ok(vkGetSemaphoreWin32HandleKHR_(device, &handleInfo, &handle), "export semaphore")) return false;
    glGenSemaphoresEXT(1, &name);
    glImportSemaphoreWin32HandleEXT(name, 0x9587, handle);
    CloseHandle(handle);
    return true;
}

static bool create_conversion()
{
    const char *sources[] = {
        "#version 410 core\nout vec2 uv;void main(){vec2 pos=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=pos;gl_Position=vec4(pos*2-1,0,1);}",
        "#version 410 core\nin vec2 uv;layout(location=0)out float depth;layout(location=1)out vec2 motion;"
        "uniform sampler2D depthInput,motionInput;uniform bool linearDepth;uniform vec2 projectionDepth;"
        "void main(){vec2 coord=vec2(uv.x,1-uv.y);float value=texture(depthInput,coord).r;"
        "depth=linearDepth?clamp((-projectionDepth.x+projectionDepth.y/max(value,0.00001))*0.5+0.5,0,1):value;"
        "motion=texture(motionInput,coord).rg*vec2(1,-1);}"};
    conversionProgram = glCreateProgram();
    for (int index = 0; index < 2; index++) {
        GLuint shader = glCreateShader(index ? 0x8b30 : 0x8b31);
        glShaderSource(shader, 1, &sources[index], nullptr);
        glCompileShader(shader); glAttachShader(conversionProgram, shader); glDeleteShader(shader);
    }
    glLinkProgram(conversionProgram);
    GLint linked = 0;
    glGetProgramiv(conversionProgram, 0x8b82, &linked);
    glGenVertexArrays(1, &conversionVAO);
    glGenFramebuffers(1, &copyFbo); glGenFramebuffers(1, &inputFbo);
    return linked != 0;
}

extern "C" int afg_init(AfgLog log, AfgGetProc getProc, const wchar_t *directory, HWND window)
{
    if (initialized) return ready && outputWindow == window && IsWindow(presentationWindow);
    logger = log; outputWindow = window;
    wchar_t path[MAX_PATH];
    if (!window || swprintf(path, MAX_PATH, L"%ls\\sl.interposer.dll", directory) < 0) return 0;
    if (!sl::security::verifyEmbeddedSignature(path)) {
        snprintf(statusText, sizeof(statusText), "NVIDIA signature verification failed"); return 0;
    }
    interposer = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!interposer) { snprintf(statusText, sizeof(statusText), "Streamline runtime missing (%lu)", GetLastError()); return 0; }
#define SL_LOAD(name) name##_ = reinterpret_cast<PFun_##name *>(GetProcAddress(interposer, #name)); if (!name##_) return 0;
    SL_FUNCTIONS(SL_LOAD)
    static const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    sl::Preferences preferences;
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    preferences.flags |= sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.engineVersion = "Aurora-1.1";
    preferences.projectId = "5b33cc7e-861e-4d5a-bf38-7708f702b286";
    wchar_t dlssDirectory[MAX_PATH];
    swprintf(dlssDirectory, MAX_PATH, L"%ls", directory);
    wchar_t *parent = wcsrchr(dlssDirectory, L'\\');
    bool hasSibling = parent && parent - dlssDirectory + 6 < MAX_PATH;
    if (hasSibling) wcscpy(parent, L"\\dlss");
    const wchar_t *runtimePaths[] = {directory, dlssDirectory};
    preferences.pathsToPlugins = runtimePaths; preferences.numPathsToPlugins = hasSibling ? 2 : 1;
    preferences.pathToLogsAndData = directory;
    preferences.featuresToLoad = features; preferences.numFeaturesToLoad = 3;
    if (!sl_ok(slInit_(preferences, sl::kSDKVersion), "Streamline initialization")) return 0;
    initialized = true;
    if (!create_presentation_window()) { snprintf(statusText, sizeof(statusText), "Frame Generation surface creation failed"); return 0; }
#define GL_LOAD(result, name, arguments) gl##name = reinterpret_cast<decltype(gl##name)>(getProc("gl" #name)); if (!gl##name) { snprintf(statusText, sizeof(statusText), "Missing gl%s", #name); return 0; }
    GL_FUNCTIONS(GL_LOAD)
    instanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(interposer, "vkGetInstanceProcAddr"));
    if (!instanceProc) return 0;
    vkCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(instanceProc(nullptr, "vkCreateInstance"));
    const char *instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Aurora Frame Generation"; application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &application; instanceInfo.enabledExtensionCount = 4; instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    if (!vk_ok(private_vk_create_instance(vkCreateInstance_, &instanceInfo, nullptr, &instance), "create instance")) return 0;
#define VK_LOAD_INSTANCE(name) name##_ = reinterpret_cast<PFN_##name>(instanceProc(instance, #name));
    VK_FUNCTIONS(VK_LOAD_INSTANCE)
    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hinstance = presentationModule; surfaceInfo.hwnd = presentationWindow;
    if (!vk_ok(vkCreateWin32SurfaceKHR_(instance, &surfaceInfo, nullptr, &surface), "create window surface")) return 0;
    uint32_t count = 8;
    VkPhysicalDevice devices[8];
    if (!vk_ok(vkEnumeratePhysicalDevices_(instance, &count, devices), "enumerate GPUs")) return 0;
    unsigned char glUUID[16];
    glGetUnsignedBytei_vEXT(0x9597, 0, glUUID);
    for (uint32_t index = 0; index < count; index++) {
        VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &identity};
        vkGetPhysicalDeviceProperties2_(devices[index], &properties);
        if (!memcmp(identity.deviceUUID, glUUID, 16)) { physicalDevice = devices[index]; FG_LOG("framegen: GPU %s", properties.properties.deviceName); break; }
    }
    if (!physicalDevice) { snprintf(statusText, sizeof(statusText), "OpenGL/Vulkan GPU mismatch"); return 0; }
    sl::AdapterInfo adapter; adapter.vkPhysicalDevice = physicalDevice;
    if (!sl_ok(slIsFeatureSupported_(sl::kFeatureDLSS_G, adapter), "Frame Generation capability")) return 0;
    uint32_t familyCount = 32;
    VkQueueFamilyProperties families[32];
    vkGetPhysicalDeviceQueueFamilyProperties_(physicalDevice, &familyCount, families);
    queueFamily = UINT32_MAX;
    for (uint32_t index = 0; index < familyCount; index++) {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR_(physicalDevice, index, surface, &supported);
        if (supported && (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)) { queueFamily = index; break; }
    }
    if (queueFamily == UINT32_MAX) { snprintf(statusText, sizeof(statusText), "No graphics/present queue"); return 0; }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
    const char *deviceExtensions[64] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
    uint32_t extensionCount = 5, availableCount = 0;
    vkEnumerateDeviceExtensionProperties_(physicalDevice, nullptr, &availableCount, nullptr);
    auto available = static_cast<VkExtensionProperties *>(calloc(availableCount, sizeof(VkExtensionProperties)));
    if (!available) return 0;
    vkEnumerateDeviceExtensionProperties_(physicalDevice, nullptr, &availableCount, available);
    const char *extraExtensions[] = {"VK_NVX_binary_import", "VK_NVX_image_view_handle"};
    unsigned ngxInstanceCount = 0, ngxDeviceCount = 0;
    const char **ngxInstanceExtensions = nullptr, **ngxDeviceExtensions = nullptr;
    using RequiredExtensions = unsigned (*)(unsigned *, const char ***, unsigned *, const char ***);
    HMODULE core = GetModuleHandleW(L"_nvngx.dll");
    auto required = core ? reinterpret_cast<RequiredExtensions>(GetProcAddress(core, "NVSDK_NGX_VULKAN_RequiredExtensions")) : nullptr;
    if (required && required(&ngxInstanceCount, &ngxInstanceExtensions, &ngxDeviceCount, &ngxDeviceExtensions) != 1) ngxDeviceCount = 0;
    for (unsigned candidate = 0; candidate < ngxDeviceCount + 2 && extensionCount < 64; candidate++) {
        const char *name = candidate < ngxDeviceCount ? ngxDeviceExtensions[candidate] : extraExtensions[candidate - ngxDeviceCount];
        bool supported = false, duplicate = false;
        for (unsigned index = 0; index < availableCount; index++) if (!strcmp(name, available[index].extensionName)) supported = true;
        for (unsigned index = 0; index < extensionCount; index++) if (!strcmp(name, deviceExtensions[index])) duplicate = true;
        if (supported && !duplicate) deviceExtensions[extensionCount++] = name;
    }
    free(available);
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &features13};
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &features12};
    VkPhysicalDeviceFeatures2 deviceFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features11};
    vkGetPhysicalDeviceFeatures2_(physicalDevice, &deviceFeatures);
    deviceFeatures.features.robustBufferAccess = VK_FALSE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features11};
    deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = extensionCount; deviceInfo.ppEnabledExtensionNames = deviceExtensions; deviceInfo.pEnabledFeatures = &deviceFeatures.features;
    if (!vk_ok(vkCreateDevice_(physicalDevice, &deviceInfo, nullptr, &device), "create device")) return 0;
#define VK_LOAD_DEVICE(name) { auto entry = vkGetDeviceProcAddr_(device, #name); if (entry) name##_ = reinterpret_cast<PFN_##name>(entry); }
    VK_FUNCTIONS(VK_LOAD_DEVICE)
    vkGetDeviceQueue_(device, queueFamily, 0, &queue);
    vkGetPhysicalDeviceMemoryProperties_(physicalDevice, &memoryProperties);
#define FG_IMPORT(feature, name, target) if (!sl_ok(slGetFeatureFunction_(feature, name, reinterpret_cast<void *&>(target)), name)) return 0;
    FG_IMPORT(sl::kFeatureDLSS_G, "slDLSSGSetOptions", setOptions)
    FG_IMPORT(sl::kFeatureDLSS_G, "slDLSSGGetState", getState)
    FG_IMPORT(sl::kFeatureReflex, "slReflexSetOptions", setReflexOptions)
    FG_IMPORT(sl::kFeatureReflex, "slReflexSleep", reflexSleep)
    FG_IMPORT(sl::kFeaturePCL, "slPCLSetMarker", setMarker)
    sl::ReflexOptions reflex;
    reflex.mode = sl::ReflexMode::eLowLatency;
    if (!sl_ok(setReflexOptions(reflex), "Reflex initialization")) return 0;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily; poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (!vk_ok(vkCreateCommandPool_(device, &poolInfo, nullptr, &commandPool), "create command pool")) return 0;
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool; commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandInfo.commandBufferCount = 1;
    if (!vk_ok(vkAllocateCommandBuffers_(device, &commandInfo, &command), "allocate command buffer")) return 0;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!vk_ok(vkCreateFence_(device, &fenceInfo, nullptr, &fence), "create fence")) return 0;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!vk_ok(vkCreateSemaphore_(device, &semaphoreInfo, nullptr, &acquireSemaphore), "create acquire semaphore")) return 0;
    if (!shared_semaphore(glReady, glReadyName) || !shared_semaphore(vkReady, vkReadyName) || !create_conversion()) return 0;
    snprintf(statusText, sizeof(statusText), "Ready");
    ready = true;
    return 1;
}

static void wait_inputs();

extern "C" int afg_vulkan_context(AudVulkanContext *context)
{
    if (!ready || !context) return 0;
    *context = {(void *)instance, (void *)physicalDevice, (void *)device, (void *)queue, queueFamily, (void *)instanceProc, (void *)vkGetDeviceProcAddr_};
    return 1;
}

extern "C" void afg_wait(void)
{
    if (device) vkDeviceWaitIdle_(device);
}

extern "C" void afg_stop(void)
{
    inputsCaptured = hudCaptured = false;
    if (presentationWindow) ShowWindow(presentationWindow, SW_HIDE);
    if (!device) return;
    wait_inputs();
    if (glFinish) glFinish();
    if (setOptions) { sl::DLSSGOptions options; options.mode = sl::DLSSGMode::eOff; setOptions(viewport, options); }
    vkDeviceWaitIdle_(device);
    if (swapchain) vkDestroySwapchainKHR_(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    for (uint32_t index = 0; index < 8; index++) {
        if (presentSemaphores[index]) vkDestroySemaphore_(device, presentSemaphores[index], nullptr);
        presentSemaphores[index] = VK_NULL_HANDLE;
    }
    for (Image &image : images) destroy_image(image);
    renderWidth = renderHeight = outputWidth = outputHeight = 0;
    vkPending = false;
}

extern "C" int afg_configure(int width, int height, int outputW, int outputH)
{
    if (!device || !setOptions || width < 32 || height < 32 || outputW < 512 || outputH < 512) return 0;
    if (swapchain && width == renderWidth && height == renderHeight && outputW == outputWidth && outputH == outputHeight) return 1;
    afg_stop();
    if (!SetWindowPos(presentationWindow, HWND_TOP, 0, 0, outputW, outputH, SWP_NOACTIVATE)) return 0;
    VkSurfaceCapabilitiesKHR capabilities;
    if (!vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(physicalDevice, surface, &capabilities), "surface capabilities")) return 0;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return 0;
    uint32_t formatCount = 64;
    VkSurfaceFormatKHR formats[64];
    if (!vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR_(physicalDevice, surface, &formatCount, formats), "surface formats")) return 0;
    VkSurfaceFormatKHR selected = formats[0];
    for (uint32_t index = 0; index < formatCount; index++)
        if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM && formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) selected = formats[index];
    swapchainFormat = selected.format;
    uint32_t modeCount = 32;
    VkPresentModeKHR modes[32];
    vkGetPhysicalDeviceSurfacePresentModesKHR_(physicalDevice, surface, &modeCount, modes);
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t index = 0; index < modeCount; index++) if (modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = modes[index];
    VkSwapchainCreateInfoKHR swapInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapInfo.surface = surface; swapInfo.minImageCount = capabilities.minImageCount > 3 ? capabilities.minImageCount : 3;
    if (capabilities.maxImageCount && swapInfo.minImageCount > capabilities.maxImageCount) swapInfo.minImageCount = capabilities.maxImageCount;
    swapInfo.imageFormat = selected.format; swapInfo.imageColorSpace = selected.colorSpace;
    swapInfo.imageExtent = capabilities.currentExtent.width == UINT32_MAX ? VkExtent2D{(uint32_t)outputW, (uint32_t)outputH} : capabilities.currentExtent;
    if (swapInfo.imageExtent.width != (uint32_t)outputW || swapInfo.imageExtent.height != (uint32_t)outputH) return 0;
    swapInfo.imageArrayLayers = 1; swapInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapInfo.preTransform = capabilities.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; swapInfo.presentMode = presentMode; swapInfo.clipped = VK_TRUE;
    if (!vk_ok(vkCreateSwapchainKHR_(device, &swapInfo, nullptr, &swapchain), "create swapchain")) return 0;
    swapchainCount = 0;
    if (!vk_ok(vkGetSwapchainImagesKHR_(device, swapchain, &swapchainCount, nullptr), "swapchain image count") || !swapchainCount || swapchainCount > 8) { afg_stop(); return 0; }
    if (!vk_ok(vkGetSwapchainImagesKHR_(device, swapchain, &swapchainCount, swapchainImages), "swapchain images")) { afg_stop(); return 0; }
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t index = 0; index < swapchainCount; index++)
        if (!vk_ok(vkCreateSemaphore_(device, &semaphoreInfo, nullptr, &presentSemaphores[index]), "create present semaphore")) { afg_stop(); return 0; }
    bool success = create_image(images[RawColor], width, height, VK_FORMAT_R16G16B16A16_SFLOAT, 0x881a)
        && create_image(images[RawDepth], width, height, VK_FORMAT_R32_SFLOAT, 0x822e)
        && create_image(images[RawMotion], width, height, VK_FORMAT_R16G16_SFLOAT, 0x822f)
        && create_image(images[Depth], width, height, VK_FORMAT_R32_SFLOAT, 0x822e)
        && create_image(images[Motion], width, height, VK_FORMAT_R16G16_SFLOAT, 0x822f)
        && create_image(images[Backbuffer], outputW, outputH, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8)
        && create_image(images[Hudless], outputW, outputH, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8)
        && create_image(images[HudlessTop], outputW, outputH, VK_FORMAT_R8G8B8A8_UNORM, 0);
    if (!success) { afg_stop(); return 0; }
    vkWaitForFences_(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences_(device, 1, &fence); vkResetCommandBuffer_(command, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer_(command, &begin);
    for (Image &image : images) {
        barrier(image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        if (image.texture) barrier(image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, queueFamily, VK_QUEUE_FAMILY_EXTERNAL);
    }
    vkEndCommandBuffer_(command);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    vkQueueSubmit_(queue, 1, &submit, fence); vkWaitForFences_(device, 1, &fence, VK_TRUE, UINT64_MAX);
    renderWidth = width; renderHeight = height; outputWidth = outputW; outputHeight = outputH;
    ShowWindow(presentationWindow, SW_SHOWNA);
    FG_LOG("framegen: presentation configured %dx%d -> %dx%d", width, height, outputW, outputH);
    return 1;
}

extern "C" unsigned afg_texture(int input)
{
    return input >= AFG_COLOR && input <= AFG_MOTION ? images[RawColor + input].texture : 0;
}

static void wait_inputs()
{
    if (!vkPending) return;
    GLuint textures[] = {images[Depth].texture, images[Motion].texture, images[Backbuffer].texture, images[Hudless].texture};
    const GLenum layouts[] = {0x958d, 0x958d, 0x958d, 0x958d};
    glWaitSemaphoreEXT(vkReadyName, 0, nullptr, 4, textures, layouts);
    vkPending = false;
}

extern "C" int afg_capture_inputs(unsigned depth, unsigned motion, int linearDepth, const float *projection)
{
    if (!swapchain || !depth || !motion) return 0;
    wait_inputs();
    GLint oldProgram, oldVAO, oldActive, oldDraw, oldViewport[4], oldTextures[2], oldSamplers[2];
    glGetIntegerv(0x8b8d, &oldProgram); glGetIntegerv(0x85b5, &oldVAO); glGetIntegerv(0x84e0, &oldActive);
    glGetIntegerv(0x8ca6, &oldDraw); glGetIntegerv(GL_VIEWPORT, oldViewport);
    const GLenum capabilities[] = {GL_BLEND, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_CULL_FACE, GL_STENCIL_TEST, 0x8db9};
    GLboolean enabled[6];
    for (int index = 0; index < 6; index++) { enabled[index] = glIsEnabled(capabilities[index]); glDisable(capabilities[index]); }
    for (int index = 0; index < 2; index++) {
        glActiveTexture(0x84c0 + index); glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTextures[index]);
        glGetIntegeri_v(0x8919, index, &oldSamplers[index]); glBindSampler(index, 0);
        glBindTexture(GL_TEXTURE_2D, index ? motion : depth);
    }
    glBindFramebuffer(0x8ca9, inputFbo);
    glFramebufferTexture2D(0x8ca9, 0x8ce0, GL_TEXTURE_2D, images[Depth].texture, 0);
    glFramebufferTexture2D(0x8ca9, 0x8ce1, GL_TEXTURE_2D, images[Motion].texture, 0);
    const GLenum attachments[] = {0x8ce0, 0x8ce1}; glDrawBuffers(2, attachments);
    glUseProgram(conversionProgram); glBindVertexArray(conversionVAO);
    glUniform1i(glGetUniformLocation(conversionProgram, "depthInput"), 0);
    glUniform1i(glGetUniformLocation(conversionProgram, "motionInput"), 1);
    glUniform1i(glGetUniformLocation(conversionProgram, "linearDepth"), linearDepth);
    glUniform2f(glGetUniformLocation(conversionProgram, "projectionDepth"), projection[10], projection[14]);
    glViewport(0, 0, renderWidth, renderHeight); glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(0x8ca9, oldDraw); glUseProgram(oldProgram); glBindVertexArray(oldVAO);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    for (int index = 0; index < 2; index++) { glActiveTexture(0x84c0 + index); glBindTexture(GL_TEXTURE_2D, oldTextures[index]); glBindSampler(index, oldSamplers[index]); }
    glActiveTexture(oldActive);
    for (int index = 0; index < 6; index++) if (enabled[index]) glEnable(capabilities[index]);
    inputsCaptured = true; hudCaptured = false;
    return 1;
}

static bool capture_window(Image &image)
{
    GLint oldRead, oldDraw, oldReadBuffer;
    glGetIntegerv(0x8caa, &oldRead); glGetIntegerv(0x8ca6, &oldDraw);
    glBindFramebuffer(0x8ca8, 0); glGetIntegerv(GL_READ_BUFFER, &oldReadBuffer); glReadBuffer(GL_BACK);
    glBindFramebuffer(0x8ca9, copyFbo); glFramebufferTexture2D(0x8ca9, 0x8ce0, GL_TEXTURE_2D, image.texture, 0);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST), srgb = glIsEnabled(0x8db9);
    glDisable(GL_SCISSOR_TEST); glDisable(0x8db9);
    glBlitFramebuffer(0, 0, outputWidth, outputHeight, 0, 0, outputWidth, outputHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (scissor) glEnable(GL_SCISSOR_TEST); if (srgb) glEnable(0x8db9);
    glBindFramebuffer(0x8ca9, oldDraw); glReadBuffer(oldReadBuffer); glBindFramebuffer(0x8ca8, oldRead);
    return true;
}

extern "C" int afg_capture_hudless(void)
{
    if (!inputsCaptured || hudCaptured) return 0;
    wait_inputs(); hudCaptured = capture_window(images[Hudless]); return hudCaptured;
}

static void identity(float *matrix)
{
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
}

static void multiply(float *result, const float *left, const float *right)
{
    float product[16] = {};
    for (int column = 0; column < 4; column++)
        for (int row = 0; row < 4; row++)
            for (int axis = 0; axis < 4; axis++) product[column * 4 + row] += left[axis * 4 + row] * right[column * 4 + axis];
    memcpy(result, product, sizeof(product));
}

static bool invert(float *result, const float *matrix)
{
    float rows[4][8] = {};
    for (int row = 0; row < 4; row++) { for (int column = 0; column < 4; column++) rows[row][column] = matrix[column * 4 + row]; rows[row][row + 4] = 1; }
    for (int column = 0; column < 4; column++) {
        int pivot = column;
        for (int row = column + 1; row < 4; row++) if (fabsf(rows[row][column]) > fabsf(rows[pivot][column])) pivot = row;
        if (fabsf(rows[pivot][column]) < 1e-8f) return false;
        for (int index = 0; index < 8; index++) { float saved = rows[column][index]; rows[column][index] = rows[pivot][index]; rows[pivot][index] = saved; }
        float scale = rows[column][column]; for (float &value : rows[column]) value /= scale;
        for (int row = 0; row < 4; row++) if (row != column) { float amount = rows[row][column]; for (int index = 0; index < 8; index++) rows[row][index] -= amount * rows[column][index]; }
    }
    for (int row = 0; row < 4; row++) for (int column = 0; column < 4; column++) result[column * 4 + row] = rows[row][column + 4];
    return true;
}

static void set_matrix(sl::float4x4 &target, const float *source)
{
    for (int row = 0; row < 4; row++) target[row] = sl::float4(source[row * 4], source[row * 4 + 1], source[row * 4 + 2], source[row * 4 + 3]);
}

static bool constants(sl::Constants &values, const float *scene, const float *previous, const float *jitter, int reset)
{
    float projection[16], previousProjection[16], conversion[16], inverseProjection[16], reprojection[16], inverseReprojection[16], translation[16], currentVP[16], previousVP[16], inverseVP[16];
    memcpy(projection, scene + 48, sizeof(projection)); projection[8] = projection[9] = 0;
    memcpy(previousProjection, previous + 48, sizeof(previousProjection)); previousProjection[8] = previousProjection[9] = 0;
    identity(conversion); conversion[5] = -1; conversion[10] = conversion[14] = 0.5f;
    multiply(projection, conversion, projection);
    multiply(previousProjection, conversion, previousProjection);
    if (!invert(inverseProjection, projection)) return false;
    multiply(currentVP, projection, scene + 32);
    multiply(previousVP, previousProjection, previous + 32);
    if (!invert(inverseVP, currentVP)) return false;
    identity(translation);
    for (int axis = 0; axis < 3; axis++) translation[12 + axis] = scene[132 + axis] - previous[132 + axis];
    multiply(reprojection, translation, inverseVP);
    multiply(reprojection, previousVP, reprojection);
    if (!invert(inverseReprojection, reprojection)) return false;
    set_matrix(values.cameraViewToClip, projection); set_matrix(values.clipToCameraView, inverseProjection);
    set_matrix(values.clipToPrevClip, reprojection); set_matrix(values.prevClipToClip, inverseReprojection);
    values.jitterOffset = {jitter[0] * renderWidth * 0.5f, -jitter[1] * renderHeight * 0.5f};
    values.mvecScale = {1.0f / renderWidth, 1.0f / renderHeight};
    values.cameraPinholeOffset = {0, 0}; values.cameraPos = {scene[132], scene[133], scene[134]};
    values.cameraRight = {scene[80], scene[81], scene[82]}; values.cameraUp = {scene[84], scene[85], scene[86]};
    values.cameraFwd = {-scene[88], -scene[89], -scene[90]};
    values.cameraNear = scene[144]; values.cameraFar = scene[145];
    values.cameraFOV = 2.0f * atanf(1.0f / fabsf(projection[5])); values.cameraAspectRatio = (float)renderWidth / renderHeight;
    values.depthInverted = sl::eFalse; values.cameraMotionIncluded = sl::eTrue; values.motionVectors3D = sl::eFalse;
    values.reset = reset ? sl::eTrue : sl::eFalse;
    return isfinite(values.cameraNear) && values.cameraNear > 0 && values.cameraFar > values.cameraNear;
}

static sl::Resource resource(const Image &image)
{
    sl::Resource result(sl::ResourceType::eTex2d, (void *)image.image, (void *)image.memory, (void *)image.view, VK_IMAGE_LAYOUT_GENERAL);
    result.width = image.width; result.height = image.height; result.nativeFormat = image.format;
    result.mipLevels = result.arrayLayers = 1; result.flags = 0;
    result.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return result;
}

static void blit(VkImage source, VkImage destination)
{
    VkImageBlit region{};
    region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {0, outputHeight, 0}; region.srcOffsets[1] = {outputWidth, 0, 1};
    region.dstOffsets[1] = {outputWidth, outputHeight, 1};
    vkCmdBlitImage_(command, source, VK_IMAGE_LAYOUT_GENERAL, destination, VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_NEAREST);
}

extern "C" int afg_present(unsigned frameIndex, const float *scene, const float *previous, const float *jitter, int reset, int generatedFrames)
{
    if (!swapchain || !inputsCaptured || !hudCaptured || !IsWindowVisible(outputWindow) || IsIconic(outputWindow)) return 0;
    if (!sl_ok(slGetNewFrameToken_(frameToken, &frameIndex), "frame token")) return 0;
    if (!sl_ok(reflexSleep(*frameToken), "Reflex sleep")) return 0;
    setMarker(sl::PCLMarker::eSimulationStart, *frameToken); setMarker(sl::PCLMarker::eSimulationEnd, *frameToken);
    sl::Constants values;
    if (!constants(values, scene, previous, jitter, reset) || !sl_ok(slSetConstants_(values, *frameToken, viewport), "frame constants")) return 0;
    sl::DLSSGState state;
    if (!sl_ok(getState(viewport, state, nullptr), "Frame Generation state")) return 0;
    sl::DLSSGOptions options;
    options.mode = sl::DLSSGMode::eOn;
    options.flags = sl::DLSSGFlags::eEnableFullscreenMenuDetection;
    unsigned maximum = state.numFramesToGenerateMax ? state.numFramesToGenerateMax : 1;
    options.numFramesToGenerate = generatedFrames > 0 ? (unsigned)generatedFrames : 1;
    if (options.numFramesToGenerate > maximum) options.numFramesToGenerate = maximum;
    options.numBackBuffers = swapchainCount;
    options.mvecDepthWidth = renderWidth; options.mvecDepthHeight = renderHeight;
    options.colorWidth = outputWidth; options.colorHeight = outputHeight;
    options.colorBufferFormat = swapchainFormat; options.mvecBufferFormat = VK_FORMAT_R16G16_SFLOAT;
    options.depthBufferFormat = VK_FORMAT_R32_SFLOAT; options.hudLessBufferFormat = VK_FORMAT_R8G8B8A8_UNORM;
    if (!sl_ok(setOptions(viewport, options), "Frame Generation options")) return 0;
    wait_inputs(); capture_window(images[Backbuffer]);
    uint32_t imageIndex;
    VkResult acquired = vkAcquireNextImageKHR_(device, swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) { afg_stop(); return 0; }
    if (acquired != VK_SUBOPTIMAL_KHR && !vk_ok(acquired, "acquire backbuffer")) return 0;
    GLuint textures[] = {images[Depth].texture, images[Motion].texture, images[Backbuffer].texture, images[Hudless].texture};
    const GLenum layouts[] = {0x958d, 0x958d, 0x958d, 0x958d};
    glSignalSemaphoreEXT(glReadyName, 0, nullptr, 4, textures, layouts); glFlush();
    vkWaitForFences_(device, 1, &fence, VK_TRUE, UINT64_MAX); vkResetFences_(device, 1, &fence); vkResetCommandBuffer_(command, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer_(command, &begin);
    for (int index : {Depth, Motion, Backbuffer, Hudless}) barrier(images[index].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, queueFamily);
    barrier(swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    blit(images[Backbuffer].image, swapchainImages[imageIndex]); blit(images[Hudless].image, images[HudlessTop].image);
    barrier(images[HudlessTop].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    barrier(swapchainImages[imageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer_(command);
    sl::Resource depth = resource(images[Depth]), motion = resource(images[Motion]), hudless = resource(images[HudlessTop]);
    sl::Extent inputExtent{0, 0, (uint32_t)renderWidth, (uint32_t)renderHeight};
    sl::Extent colorExtent{0, 0, (uint32_t)outputWidth, (uint32_t)outputHeight};
    sl::ResourceTag tags[] = {{&depth, sl::kBufferTypeDepth, sl::eValidUntilPresent, &inputExtent}, {&motion, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent, &inputExtent}, {&hudless, sl::kBufferTypeHUDLessColor, sl::eValidUntilPresent, &colorExtent}};
    if (!sl_ok(slSetTagForFrame_(*frameToken, viewport, tags, 3, nullptr), "frame resources")) return 0;
    VkSemaphore waits[] = {glReady, acquireSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 2; submit.pWaitSemaphores = waits; submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &presentSemaphores[imageIndex];
    setMarker(sl::PCLMarker::eRenderSubmitStart, *frameToken);
    if (!vk_ok(vkQueueSubmit_(queue, 1, &submit, fence), "submit frame")) return 0;
    setMarker(sl::PCLMarker::eRenderSubmitEnd, *frameToken); setMarker(sl::PCLMarker::ePresentStart, *frameToken);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1; present.pWaitSemaphores = &presentSemaphores[imageIndex]; present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR_(queue, &present);
    setMarker(sl::PCLMarker::ePresentEnd, *frameToken);
    if (result != VK_SUBOPTIMAL_KHR && !vk_ok(result, "present generated frames")) { afg_stop(); return 0; }
    if (!sl_ok(getState(viewport, state, nullptr), "presented frame state")) { afg_stop(); return 0; }
    presentedFrames = state.numFramesActuallyPresented;
    if (state.status != sl::DLSSGStatus::eOk) {
        snprintf(statusText, sizeof(statusText), "Runtime failed (0x%x)", (unsigned)state.status);
        FG_LOG("framegen: %s", statusText);
        afg_stop(); return 0;
    }
    vkWaitForFences_(device, 1, &fence, VK_TRUE, UINT64_MAX); vkResetFences_(device, 1, &fence); vkResetCommandBuffer_(command, 0);
    vkBeginCommandBuffer_(command, &begin);
    for (int index : {Depth, Motion, Backbuffer, Hudless}) barrier(images[index].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, queueFamily, VK_QUEUE_FAMILY_EXTERNAL);
    vkEndCommandBuffer_(command);
    VkSubmitInfo release{VK_STRUCTURE_TYPE_SUBMIT_INFO}; release.commandBufferCount = 1; release.pCommandBuffers = &command; release.signalSemaphoreCount = 1; release.pSignalSemaphores = &vkReady;
    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    VkSemaphore processingFence = (VkSemaphore)state.inputsProcessingCompletionFence;
    VkPipelineStageFlags processingStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    uint64_t signalValue = 0;
    if (processingFence && state.lastPresentInputsProcessingCompletionFenceValue) {
        timeline.waitSemaphoreValueCount = 1; timeline.pWaitSemaphoreValues = &state.lastPresentInputsProcessingCompletionFenceValue;
        timeline.signalSemaphoreValueCount = 1; timeline.pSignalSemaphoreValues = &signalValue;
        release.pNext = &timeline; release.waitSemaphoreCount = 1; release.pWaitSemaphores = &processingFence; release.pWaitDstStageMask = &processingStage;
    }
    if (!vk_ok(vkQueueSubmit_(queue, 1, &release, fence), "release shared inputs")) return 0;
    vkPending = true; inputsCaptured = hudCaptured = false;
    if (presentedFrames > 1) snprintf(statusText, sizeof(statusText), "%u presents / rendered frame", presentedFrames);
    else snprintf(statusText, sizeof(statusText), "Warming / suspended");
    return 1;
}

extern "C" void afg_shutdown(void)
{
    afg_stop();
    if (initialized && slShutdown_) slShutdown_();
    initialized = ready = false;
    if (device) {
        if (glReadyName) glDeleteSemaphoresEXT(1, &glReadyName);
        if (vkReadyName) glDeleteSemaphoresEXT(1, &vkReadyName);
        if (glReady) vkDestroySemaphore_(device, glReady, nullptr);
        if (vkReady) vkDestroySemaphore_(device, vkReady, nullptr);
        if (acquireSemaphore) vkDestroySemaphore_(device, acquireSemaphore, nullptr);
        if (fence) vkDestroyFence_(device, fence, nullptr);
        if (commandPool) vkDestroyCommandPool_(device, commandPool, nullptr);
        if (conversionProgram) glDeleteProgram(conversionProgram);
        if (conversionVAO) glDeleteVertexArrays(1, &conversionVAO);
        if (copyFbo) glDeleteFramebuffers(1, &copyFbo);
        if (inputFbo) glDeleteFramebuffers(1, &inputFbo);
        vkDestroyDevice_(device, nullptr);
    }
    if (surface) vkDestroySurfaceKHR_(instance, surface, nullptr);
    if (instance) vkDestroyInstance_(instance, nullptr);
    if (IsWindow(presentationWindow)) PostMessageW(presentationWindow, WM_CLOSE, 0, 0);
    if (presentationThread) { wait_presentation_thread(presentationThread, 5000); CloseHandle(presentationThread); }
    if (presentationReady) CloseHandle(presentationReady);
    presentationWindow = nullptr; presentationThread = presentationReady = nullptr;
    device = VK_NULL_HANDLE; instance = VK_NULL_HANDLE; surface = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE; commandPool = VK_NULL_HANDLE; command = VK_NULL_HANDLE; fence = VK_NULL_HANDLE;
    glReady = vkReady = acquireSemaphore = VK_NULL_HANDLE;
    glReadyName = vkReadyName = conversionProgram = conversionVAO = copyFbo = inputFbo = 0;
    setOptions = nullptr; getState = nullptr; setReflexOptions = nullptr; reflexSleep = nullptr; setMarker = nullptr;
    if (interposer) FreeLibrary(interposer);
    interposer = nullptr;
}

extern "C" unsigned afg_presented_frames(void) { return presentedFrames; }
extern "C" const char *afg_status(void) { return statusText; }
