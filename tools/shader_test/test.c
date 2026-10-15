#include "../../hook/hook.c"
#undef NDEBUG
#include <assert.h>

static char *override_number(char *source, const char *name, const char *number)
{
    char *end = NULL;
    char *token = find_define_value(source, name, &end);
    assert(token && end);
    Str output = {0};
    str_add(&output, source, (size_t)(token - source));
    str_cstr(&output, number);
    str_cstr(&output, end);
    free(source);
    return output.p;
}

static char *shader_source(const char *name)
{
    char relative[160];
    snprintf(relative, sizeof(relative), "programs/%s.glsl", name);
    char *original = read_file(relative, NULL);
    assert(original);
    Str output = {0};
    str_cstr(&output, "#version 410 core\n#define USE_FOG 1\n#define USE_LINEAR_Z 1\n#define USE_SSAO 1\n"
                      "#define USE_EDGE_AWARE_UPSAMPLING 0\n#define USE_DEFERRED_SHADOW 1\n"
                      "#define USE_DEFERRED_SHADOW_BLURRED 1\n#define INPUT_NORMALS_IN_WS 1\n"
                      "#define CASCADE_COUNT 1\n#define NEAR 1\n#define DEFERRED 1\n"
                      "#define ALPHA_BLEND 0\n#define USE_SCENE_DATA_OVERRIDE 0\n#define ANIMATED 0\n");
    str_cstr(&output, "#define MAX_NODES_COUNT 256\n#define FIRST_PERSON_VIEW 0\n");
    str_cstr(&output, "#define EFFECT_WATER 4\n#define EFFECT_WATERENVIRONMENTCOLOR 5\n"
                      "#define EFFECT_WATERENVIRONMENTTRANSITION 6\n#define EFFECT_WIND 1\n"
                      "#define SHADING_FLAT 1\n#define SHADING_FULLBRIGHT 2\n#define SHADING_REFLECTIVE 3\n");
    expand(&output, original, 0);
    free(original);
    return output.p;
}

static int check_source(const char *name, char *source)
{
    GLuint shader = r_glCreateShader(strstr(name, "MapChunkVS") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const GLchar *pointer = source;
    r_glShaderSource(shader, 1, &pointer, NULL);
    r_glCompileShader(shader);
    GLint status = 0;
    r_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char message[8192] = {0};
        r_glGetShaderInfoLog(shader, sizeof(message) - 1, NULL, message);
        fprintf(stderr, "FAIL %s: %s\n", name, message);
        FILE *failed = fopen("build/validation/failed.glsl", "wb");
        if (failed) { fwrite(source, 1, strlen(source), failed); fclose(failed); }
    }
    r_glDeleteShader(shader);
    free(source);
    return status != 0;
}

int main(int argumentCount, char **arguments)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    assert(argumentCount == 3);
    MultiByteToWideChar(CP_UTF8, 0, arguments[1], -1, g_packDir, MAX_PATH);
    HMODULE sdl = LoadLibraryA(arguments[2]);
    assert(sdl);
    bool (*initialize)(uint32_t) = (bool (*)(uint32_t))GetProcAddress(sdl, "SDL_Init");
    bool (*setAttribute)(int, int) = (bool (*)(int, int))GetProcAddress(sdl, "SDL_GL_SetAttribute");
    void *(*createWindow)(const char *, int, int, uint64_t) = (void *(*)(const char *, int, int, uint64_t))GetProcAddress(sdl, "SDL_CreateWindow");
    void *(*createContext)(void *) = (void *(*)(void *))GetProcAddress(sdl, "SDL_GL_CreateContext");
    bool (*loadGL)(const char *) = (bool (*)(const char *))GetProcAddress(sdl, "SDL_GL_LoadLibrary");
    const char *(*getError)(void) = (const char *(*)(void))GetProcAddress(sdl, "SDL_GetError");
    assert(initialize && setAttribute && createWindow && createContext && loadGL);
    if (!initialize(0x20)) { fprintf(stderr, "%s\n", getError()); return 1; }
    wchar_t systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    wcscat(systemPath, L"\\opengl32.dll");
    char library[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, systemPath, -1, library, sizeof(library), NULL, NULL);
    if (!loadGL(library)) { fprintf(stderr, "%s\n", getError()); return 1; }
    setAttribute(17, 4);
    setAttribute(18, 1);
    setAttribute(21, 1);
    void *window = createWindow("Aurora shader validation", 64, 64, 0x0a);
    assert(window);
    void *context = createContext(window);
    if (!context) { fprintf(stderr, "%s\n", getError()); return 1; }
    g_real = LoadLibraryW(systemPath);
    real_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(g_real, "wglGetProcAddress");
    #define GET_GL(name) r_##name = (PFN_##name)real_wglGetProcAddress(#name); assert(r_##name)
    GET_GL(glCreateShader);
    GET_GL(glShaderSource);
    GET_GL(glCompileShader);
    GET_GL(glGetShaderiv);
    GET_GL(glGetShaderInfoLog);
    GET_GL(glDeleteShader);
    GET_GL(glAttachShader);
    GET_GL(glLinkProgram);
    GET_GL(glGetProgramiv);
    GET_GL(glGetUniformLocation);
    GET_GL(glUseProgram);
    GET_GL(glUniformMatrix4fv);
    GET_GL(glUniform2f);
    GET_GL(glUniform1i);
    GET_GL(glUniform1f);
    int tested = 0, failures = 0;
    for (int preset = 1; preset <= 2; preset++)
    for (int tracing = 0; tracing < 2; tracing++)
    for (int lights = 0; lights < 2; lights++)
    for (int temporal = 0; temporal < 2; temporal++)
    for (int backend = 1; backend <= 2; backend++) {
        char *text = shader_source("DeferredFS");
        text = override_number(text, "AU_PRESET", preset == 1 ? "1" : "2");
        text = override_number(text, "AU_PATH_TRACING", tracing ? "1" : "0");
        text = override_number(text, "AU_LOCAL_LIGHTS", lights ? "1" : "0");
        text = override_number(text, "AU_GI_TEMPORAL_FILTER", temporal ? "1" : "0");
        text = override_number(text, "AU_GI_DENOISER", backend == 1 ? "1" : "2");
        char label[100]; snprintf(label, sizeof(label), "Deferred preset=%d PT=%d local=%d temporal=%d backend=%d", preset, tracing, lights, temporal, backend);
        failures += !check_source(label, text); tested++;
    }
    for (int taps = 1; taps <= 9; taps += taps == 1 ? 3 : 5) {
        char value[8]; snprintf(value, sizeof(value), "%d", taps);
        char *text = shader_source("DeferredFS");
        text = override_number(text, "AU_GI_HISTORY_TAPS", value);
        failures += !check_source("Original history taps", text); tested++;
    }
    for (int method = 0; method < 2; method++)
    for (int enabled = 0; enabled < 2; enabled++) {
        char *text = shader_source("DeferredFS");
        text = override_number(text, "AU_SSS", enabled ? "1" : "0");
        text = override_number(text, "AU_SSS_VOXEL", method ? "1" : "0");
        text = override_number(text, "AU_LOCAL_LIGHTS", "0");
        text = override_number(text, "AU_PATH_TRACING", "0");
        failures += !check_source("SSS isolated grid variants", text); tested++;
    }
    const char *programs[] = {"MapChunkVS", "MapChunkFS", "PostEffectFS", "TemporalAAFS", "BlockyModelFS"};
    for (int program = 0; program < 5; program++)
    for (int dlss = 0; dlss < 2; dlss++) {
        char *text = shader_source(programs[program]);
        text = override_number(text, "AU_DLSS", dlss ? "1" : "0");
        failures += !check_source(programs[program], text); tested++;
    }
    for (int blend = 0; blend < 2; blend++) {
        char *text = shader_source("MapChunkFS");
        text = override_number(text, "DEFERRED", "0");
        text = override_number(text, "ALPHA_BLEND", blend ? "1" : "0");
        failures += !check_source("Forward native emission", text); tested++;
    }
    for (int animated = 0; animated < 2; animated++)
    for (int deferred = 0; deferred < 2; deferred++) {
        char *vertex = shader_source("MapChunkVS");
        vertex = override_number(vertex, "ANIMATED", animated ? "1" : "0");
        vertex = override_number(vertex, "DEFERRED", deferred ? "1" : "0");
        failures += !check_source("MapChunkVS emitter variants", vertex); tested++;
        char *fragment = shader_source("MapChunkFS");
        fragment = override_number(fragment, "ANIMATED", animated ? "1" : "0");
        fragment = override_number(fragment, "DEFERRED", deferred ? "1" : "0");
        failures += !check_source("MapChunkFS emitter variants", fragment); tested++;
    }
    for (int deferred = 0; deferred < 2; deferred++)
    for (int firstPerson = 0; firstPerson < 2; firstPerson++) {
        char *text = shader_source("BlockyModelFS");
        text = override_number(text, "DEFERRED", deferred ? "1" : "0");
        text = override_number(text, "FIRST_PERSON_VIEW", firstPerson ? "1" : "0");
        failures += !check_source("BlockyModelFS emitter variants", text); tested++;
    }
    printf("GPU GLSL: %d variants, %d failures\n", tested, failures);
    return failures ? 1 : 0;
}
