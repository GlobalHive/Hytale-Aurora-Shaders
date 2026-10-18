// Tests driver parameter ABI and fd ownership without a running GPU.
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include "../../hook/aud_dlss.c"
static int exported_fd, fail_import, fail_export;
static GLenum error;
static VkResult mock_semaphore(VkDevice d, const VkSemaphoreCreateInfo *info, const VkAllocationCallbacks *a, VkSemaphore *s)
{
    const VkExportSemaphoreCreateInfo *ext = info->pNext;
    assert(ext->handleTypes == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
    *s = (VkSemaphore)(uintptr_t)1; return VK_SUCCESS;
}
static VkResult mock_fd(VkDevice d, const VkSemaphoreGetFdInfoKHR *info, int *fd)
{
    assert(info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
    if (fail_export) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    *fd = exported_fd = open("/dev/null", O_RDONLY); assert(*fd >= 0); return VK_SUCCESS;
}
static void mock_gen(GLsizei n, GLuint *s) { *s = 1; }
static void mock_import(GLuint s, GLenum type, int fd)
{
    assert(type == GL_HANDLE_TYPE_OPAQUE_FD_EXT);
    if (fail_import) error = 0x0502; else assert(close(fd) == 0);
}
static GLenum mock_error(void) { GLenum e = error; error = 0; return e; }
int main(void)
{
    vkCreateSemaphore_ = mock_semaphore; vkGetSemaphoreFdKHR_ = mock_fd;
    gl.GenSemaphores = mock_gen; gl.ImportSemaphoreHandle = mock_import; gl.GetError = mock_error;
    VkSemaphore sem; GLuint glsem;
    assert(create_semaphore(&sem, &glsem));
    assert(fcntl(exported_fd, F_GETFD) == -1 && errno == EBADF);
    fail_import = 1; assert(!create_semaphore(&sem, &glsem));
    assert(fcntl(exported_fd, F_GETFD) == -1 && errno == EBADF);
    fail_export = 1; assert(!create_semaphore(&sem, &glsem));
    puts("PASS: opaque-fd semaphore export/import and failure cleanup");
    g_ngx = load_ngx_core();
    if (!g_ngx) { puts("SKIP: NVIDIA NGX driver parameter ABI (library absent)"); return 0; }
    ngxAllocateParameters = (void *)aud_symbol(g_ngx, "NVSDK_NGX_VULKAN_AllocateParameters");
    ngxDestroyParameters = (void *)aud_symbol(g_ngx, "NVSDK_NGX_VULKAN_DestroyParameters");
    assert(ngxAllocateParameters && ngxDestroyParameters);
    NVSDK_NGX_Parameter *p = NULL;
    assert(!NVSDK_NGX_FAILED(ngxAllocateParameters(&p)) && p);
    ngx_set_i(p, "Linux.Int", -42); int i = 0;
    assert(!NVSDK_NGX_FAILED(ngx_get_i(p, "Linux.Int", &i)) && i == -42);
    ngx_set_ui(p, "Linux.UInt", 1234); unsigned u = 0;
    assert(!NVSDK_NGX_FAILED(ngx_get_ui(p, "Linux.UInt", &u)) && u == 1234);
    ngx_set_f(p, "Linux.Float", 1.25f); float f = 0;
    NVSDK_NGX_Result (*get_float)(void *, const char *, float *) = (void *)NGX_VT(p)[9];
    assert(!NVSDK_NGX_FAILED(get_float(p, "Linux.Float", &f)) && f == 1.25f);
    ngx_set_ptr(p, "Linux.Pointer", &i); void *ptr = NULL;
    NVSDK_NGX_Result (*get_ptr)(void *, const char *, void **) = (void *)NGX_VT(p)[15];
    assert(!NVSDK_NGX_FAILED(get_ptr(p, "Linux.Pointer", &ptr)) && ptr == &i);
    ngxDestroyParameters(p); dlclose(g_ngx);
    puts("PASS: installed Linux NGX driver int/unsigned/float/pointer ABI");
}
