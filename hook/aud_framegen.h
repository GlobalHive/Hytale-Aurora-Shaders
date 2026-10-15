#ifndef AUD_FRAMEGEN_H
#define AUD_FRAMEGEN_H
#include <windows.h>
#include "aud_vulkan.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*AfgLog)(const char *, ...);
typedef void *(*AfgGetProc)(const char *);
enum { AFG_COLOR, AFG_DEPTH, AFG_MOTION };
#ifdef AU_FRAMEGEN_EXPORTS
#define AFG_API __declspec(dllexport)
#else
#define AFG_API
#endif
AFG_API int afg_init(AfgLog log, AfgGetProc getProc, const wchar_t *runtimeDirectory, HWND window);
AFG_API int afg_configure(int renderWidth, int renderHeight, int outputWidth, int outputHeight);
AFG_API int afg_vulkan_context(AudVulkanContext *context);
AFG_API void afg_wait(void);
AFG_API unsigned afg_texture(int input);
AFG_API int afg_capture_inputs(unsigned depth, unsigned motion, int linearDepth, const float *projection);
AFG_API int afg_capture_hudless(void);
AFG_API int afg_present(unsigned frameIndex, const float *scene, const float *previousScene, const float *jitter, int reset, int generatedFrames);
AFG_API void afg_stop(void);
AFG_API void afg_shutdown(void);
AFG_API unsigned afg_presented_frames(void);
AFG_API const char *afg_status(void);
#ifdef __cplusplus
}
#endif
#endif
