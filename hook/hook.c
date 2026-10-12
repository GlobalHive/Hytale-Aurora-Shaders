// opengl32.dll proxy for Hytale: forwards everything to the system opengl32.dll and
// intercepts shader compilation so the "shaderpack" folder next to this DLL can replace
// the game's embedded GLSL. Programs are relinked live whenever a file in
// shaderpack/programs or shaderpack/include changes; uniform locations are virtualised
// so the game's cached locations stay valid across relinks.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;

#define GL_SHADER_TYPE        0x8B4F
#define GL_COMPILE_STATUS     0x8B81
#define GL_LINK_STATUS        0x8B82
#define GL_INFO_LOG_LENGTH    0x8B84
#define GL_ATTACHED_SHADERS   0x8B85
#define GL_CURRENT_PROGRAM    0x8B8D
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_GEOMETRY_SHADER    0x8DD9
#define GL_INVALID_INDEX      0xFFFFFFFFu

int stubs_resolve(HMODULE real);

static HMODULE g_real;
static wchar_t g_packDir[MAX_PATH];
static CRITICAL_SECTION g_lock;
static FILE *g_log;
static int g_dump;
static volatile LONG g_reloadRequested;
static int g_enabled = 1;

// ----------------------------------------------------------------------------------------
// Logging / files

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

static void pack_path(wchar_t *out, const char *rel)
{
    _snwprintf(out, MAX_PATH, L"%s\\%hs", g_packDir, rel);
    out[MAX_PATH - 1] = 0;
}

static char *read_file(const char *rel, size_t *outLen)
{
    wchar_t path[MAX_PATH];
    pack_path(path, rel);
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    // Strip a UTF-8 BOM.
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, got - 2);
        got -= 3;
    }
    if (outLen) *outLen = got;
    return buf;
}

static void write_file(const char *rel, const char *data, size_t n)
{
    wchar_t path[MAX_PATH];
    pack_path(path, rel);
    FILE *f = _wfopen(path, L"wb");
    if (f) { fwrite(data, 1, n, f); fclose(f); }
}

static uint64_t fnv1a(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}

// Growable string
typedef struct { char *p; size_t n, cap; } Str;
static void str_add(Str *s, const char *d, size_t n)
{
    if (s->n + n + 1 > s->cap) {
        s->cap = (s->n + n + 1) * 2;
        s->p = (char *)realloc(s->p, s->cap);
    }
    memcpy(s->p + s->n, d, n);
    s->n += n;
    s->p[s->n] = 0;
}
static void str_cstr(Str *s, const char *d) { str_add(s, d, strlen(d)); }

// ----------------------------------------------------------------------------------------
// Real GL entry points

typedef PROC (WINAPI *PFN_wglGetProcAddress)(LPCSTR);
static PFN_wglGetProcAddress real_wglGetProcAddress;

#define GLFN(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name r_##name;
GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))
GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint *))
GLFN(GLuint, glCreateShader, (GLenum))
GLFN(void, glCompileShader, (GLuint))
GLFN(void, glDeleteShader, (GLuint))
GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GLFN(void, glAttachShader, (GLuint, GLuint))
GLFN(void, glDetachShader, (GLuint, GLuint))
GLFN(void, glLinkProgram, (GLuint))
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))
GLFN(void, glGetAttachedShaders, (GLuint, GLsizei, GLsizei *, GLuint *))
GLFN(void, glDeleteProgram, (GLuint))
GLFN(void, glUseProgram, (GLuint))
GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar *))
GLFN(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *))
GLFN(void, glUniformBlockBinding, (GLuint, GLuint, GLuint))
GLFN(void, glGetActiveUniformBlockName, (GLuint, GLuint, GLsizei, GLsizei *, GLchar *))
GLFN(void, glGetIntegerv, (GLenum, GLint *))

GLFN(void, glUniform1f, (GLint, GLfloat))
GLFN(void, glUniform2f, (GLint, GLfloat, GLfloat))
GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat))
GLFN(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLFN(void, glUniform1i, (GLint, GLint))
GLFN(void, glUniform2i, (GLint, GLint, GLint))
GLFN(void, glUniform3i, (GLint, GLint, GLint, GLint))
GLFN(void, glUniform4i, (GLint, GLint, GLint, GLint, GLint))
GLFN(void, glUniform3ui, (GLint, GLuint, GLuint, GLuint))
GLFN(void, glUniform1fv, (GLint, GLsizei, const GLfloat *))
GLFN(void, glUniform2fv, (GLint, GLsizei, const GLfloat *))
GLFN(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))
GLFN(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))
GLFN(void, glUniform1iv, (GLint, GLsizei, const GLint *))
GLFN(void, glUniform1uiv, (GLint, GLsizei, const GLuint *))
GLFN(void, glUniformMatrix2fv, (GLint, GLsizei, GLboolean, const GLfloat *))
GLFN(void, glUniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *))
GLFN(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))

static int g_helpersResolved;
static void resolve_helpers(void)
{
    if (g_helpersResolved) return;
    g_helpersResolved = 1;
#define RES(name) if (!r_##name) r_##name = (PFN_##name)real_wglGetProcAddress(#name);
    RES(glShaderSource) RES(glGetShaderiv) RES(glCreateShader) RES(glCompileShader) RES(glDeleteShader)
    RES(glGetShaderInfoLog) RES(glAttachShader) RES(glDetachShader) RES(glLinkProgram) RES(glGetProgramiv)
    RES(glGetProgramInfoLog) RES(glGetAttachedShaders) RES(glDeleteProgram) RES(glUseProgram)
    RES(glGetUniformLocation) RES(glGetUniformBlockIndex) RES(glUniformBlockBinding) RES(glGetActiveUniformBlockName)
    RES(glUniform1f) RES(glUniform2f) RES(glUniform3f) RES(glUniform4f) RES(glUniform1i) RES(glUniform2i)
    RES(glUniform3i) RES(glUniform4i) RES(glUniform3ui) RES(glUniform1fv) RES(glUniform2fv) RES(glUniform3fv)
    RES(glUniform4fv) RES(glUniform1iv) RES(glUniform1uiv) RES(glUniformMatrix2fv) RES(glUniformMatrix3fv)
    RES(glUniformMatrix4fv)
#undef RES
    if (!r_glGetIntegerv) r_glGetIntegerv = (PFN_glGetIntegerv)GetProcAddress(g_real, "glGetIntegerv");
}

// ----------------------------------------------------------------------------------------
// Shader identification index (shaderpack/index.txt)

typedef struct { char name[64]; int nsig; uint64_t sig[3]; } IndexEntry;
static IndexEntry *g_index;
static int g_indexCount;

static uint64_t line_hash(const char *s, size_t n)
{
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    return n ? fnv1a(s, n) : 0;
}

static void load_index(void)
{
    free(g_index);
    g_index = NULL;
    g_indexCount = 0;
    size_t len;
    char *txt = read_file("index.txt", &len);
    if (!txt) { logf_("index.txt missing"); return; }
    int cap = 128;
    g_index = (IndexEntry *)calloc(cap, sizeof(IndexEntry));
    char *line = txt;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        char *fields[4] = {0};
        int nf = 0;
        char *p = line;
        while (p && nf < 4) {
            fields[nf++] = p;
            char *tab = strchr(p, '\t');
            if (tab) { *tab = 0; p = tab + 1; } else p = NULL;
        }
        if (nf >= 2) {
            if (g_indexCount == cap) { cap *= 2; g_index = (IndexEntry *)realloc(g_index, cap * sizeof(IndexEntry)); }
            IndexEntry *e = &g_index[g_indexCount++];
            memset(e, 0, sizeof(*e));
            strncpy(e->name, fields[0], sizeof(e->name) - 1);
            for (int i = 1; i < nf; i++) e->sig[e->nsig++] = line_hash(fields[i], strlen(fields[i]));
        }
        if (!eol) break;
        line = eol + 1;
    }
    free(txt);
    logf_("index: %d entries", g_indexCount);
}

// Returns index entry number or -1.
static int identify(const char *src)
{
    if (!g_indexCount) return -1;
    // Small open-addressing set of the source's line hashes.
    size_t cap = 4096;
    for (const char *c = src; *c; c++) if (*c == '\n') cap++;
    size_t sz = 1;
    while (sz < cap * 2) sz <<= 1;
    uint64_t *set = (uint64_t *)calloc(sz, sizeof(uint64_t));
    const char *p = src;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        uint64_t h = line_hash(p, n);
        if (h) {
            size_t i = (size_t)h & (sz - 1);
            while (set[i] && set[i] != h) i = (i + 1) & (sz - 1);
            set[i] = h;
        }
        if (!e) break;
        p = e + 1;
    }
    int found = -1;
    for (int k = 0; k < g_indexCount && found < 0; k++) {
        IndexEntry *en = &g_index[k];
        int ok = en->nsig > 0;
        for (int s = 0; s < en->nsig && ok; s++) {
            uint64_t h = en->sig[s];
            size_t i = (size_t)h & (sz - 1);
            while (set[i] && set[i] != h) i = (i + 1) & (sz - 1);
            if (set[i] != h) ok = 0;
        }
        if (ok) found = k;
    }
    free(set);
    return found;
}

// ----------------------------------------------------------------------------------------
// Override source construction

static int is_comment_line(const char *line, const char *inc)
{
    for (const char *c = line; c < inc; c++) {
        if (c[0] == '/' && c[1] == '/') return 1;
    }
    return 0;
}

static void expand(Str *out, const char *text, int depth)
{
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p + 1) : strlen(p);
        const char *inc = NULL;
        for (const char *c = p; c < p + n; c++) {
            if (*c == '#' && !strncmp(c, "#include", 8)) { inc = c; break; }
        }
        if (inc && depth < 16 && !is_comment_line(p, inc)) {
            const char *q1 = strchr(inc, '"');
            const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (q1 && q2 && q2 < p + n) {
                char name[128], rel[160];
                size_t ln = (size_t)(q2 - q1 - 1);
                if (ln >= sizeof(name)) ln = sizeof(name) - 1;
                memcpy(name, q1 + 1, ln);
                name[ln] = 0;
                snprintf(rel, sizeof(rel), "include\\%s", name);
                char *body = read_file(rel, NULL);
                if (!body) { snprintf(rel, sizeof(rel), "original\\%s", name); body = read_file(rel, NULL); }
                if (body) {
                    str_cstr(out, "// BEGIN ");
                    str_cstr(out, name);
                    str_cstr(out, "\n");
                    expand(out, body, depth + 1);
                    str_cstr(out, "\n// END ");
                    str_cstr(out, name);
                    str_cstr(out, "\n");
                    free(body);
                } else {
                    logf_("include not found: %s", name);
                    str_cstr(out, "#error include not found\n");
                }
                p += n;
                continue;
            }
        }
        // Drop #version lines from the override; the game's prefix carries it.
        const char *t = p;
        while (t < p + n && (*t == ' ' || *t == '\t')) t++;
        if (!strncmp(t, "#version", 8)) str_cstr(out, "\n");
        else str_add(out, p, n);
        p += n;
    }
}

// Builds the replacement source for runtime source `orig` identified as entry `idx`.
// Returns NULL when there is no override file.
static char *build_override(const char *orig, int idx)
{
    if (idx < 0 || !g_enabled) return NULL;
    char rel[128];
    snprintf(rel, sizeof(rel), "programs\\%s.glsl", g_index[idx].name);
    char *main = read_file(rel, NULL);
    if (!main) return NULL;

    Str out = {0};
    const char *marker = strstr(orig, "END: CPU - DEFINES");
    if (marker) {
        const char *eol = strchr(marker, '\n');
        size_t n = eol ? (size_t)(eol - orig + 1) : strlen(orig);
        str_add(&out, orig, n);
    } else {
        const char *eol = strchr(orig, '\n');
        size_t n = eol ? (size_t)(eol - orig + 1) : strlen(orig);
        str_add(&out, orig, n); // just the #version line
    }
    str_cstr(&out, "#define HYTALE_SHADERPACK 1\n");
    expand(&out, main, 0);
    free(main);
    return out.p;
}

static const char *type_tag(GLint type)
{
    switch (type) {
    case GL_VERTEX_SHADER: return "vs";
    case GL_FRAGMENT_SHADER: return "fs";
    case GL_GEOMETRY_SHADER: return "gs";
    default: return "xx";
    }
}

// Compiles `src` into a fresh shader object. Returns 0 on failure (and logs the error).
static GLuint compile_new(GLenum type, const char *src, const char *name)
{
    GLuint sh = r_glCreateShader(type);
    const GLchar *s = src;
    r_glShaderSource(sh, 1, &s, NULL);
    r_glCompileShader(sh);
    GLint ok = 0;
    r_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint ll = 0;
        r_glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &ll);
        char *log = (char *)malloc(ll + 1);
        log[0] = 0;
        r_glGetShaderInfoLog(sh, ll, NULL, log);
        logf_("COMPILE ERROR %s (%s):\n%s", name, type_tag(type), log);
        char rel[160];
        snprintf(rel, sizeof(rel), "errors\\%s_%s.glsl", name, type_tag(type));
        write_file(rel, src, strlen(src));
        snprintf(rel, sizeof(rel), "errors\\%s_%s.log", name, type_tag(type));
        write_file(rel, log, strlen(log));
        free(log);
        r_glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// ----------------------------------------------------------------------------------------
// Shader / program bookkeeping

// orig: the game's source; curHash: hash of the source currently compiled for this stage.
typedef struct { GLenum type; char *orig; int idx; int overridden; uint64_t curHash; } ShaderRec;

typedef struct {
    char *name;
    GLint real;
    int fn;              // cached call kind (0 = none)
    GLsizei count;
    GLboolean transpose;
    size_t bytes;
    void *data;
} UniformRec;

typedef struct { char *name; GLuint binding; } BlockRec;

typedef struct {
    int used;
    int linked;
    int nsh;
    ShaderRec sh[4];
    int nuni, capuni;
    UniformRec *uni;
    int nblk;
    BlockRec blk[8];
    char pname[64];      // name of the fragment stage (or the first identified stage)
    int blkChecked;      // unbound uniform blocks already given the game's binding
    int extrasChecked;   // custom texture / forwarded uniform locations resolved
    int hasExtras;
    GLint ctexLoc[8];
    GLint fwdLoc[16];
    GLint locFrame, locTime, locWetness;
    int shareGen, shareSrc;  // cached: is this program the source of a texture share
} ProgRec;

// Uniform block bindings the game uses, by block name. Programs we rewrite may gain
// blocks (e.g. the scene UBO) that the game never binds for them.
typedef struct { char name[64]; GLuint binding; } KnownBlock;
static KnownBlock g_knownBlocks[16];
static int g_nKnownBlocks;


static ShaderRec *g_shaders;   // indexed by shader id
static size_t g_shaderCap;
static ProgRec *g_progs;       // indexed by program id
static size_t g_progCap;
static GLuint g_curProg;

static ShaderRec *shader_rec(GLuint id, int create)
{
    if (id >= g_shaderCap) {
        if (!create) return NULL;
        size_t n = g_shaderCap ? g_shaderCap : 256;
        while (n <= id) n *= 2;
        g_shaders = (ShaderRec *)realloc(g_shaders, n * sizeof(ShaderRec));
        memset(g_shaders + g_shaderCap, 0, (n - g_shaderCap) * sizeof(ShaderRec));
        g_shaderCap = n;
    }
    return &g_shaders[id];
}

static ProgRec *prog_rec(GLuint id, int create)
{
    if (id >= g_progCap) {
        if (!create) return NULL;
        size_t n = g_progCap ? g_progCap : 256;
        while (n <= id) n *= 2;
        g_progs = (ProgRec *)realloc(g_progs, n * sizeof(ProgRec));
        memset(g_progs + g_progCap, 0, (n - g_progCap) * sizeof(ProgRec));
        g_progCap = n;
    }
    ProgRec *p = &g_progs[id];
    if (!p->used && !create) return NULL;
    p->used = 1;
    return p;
}

static void prog_free(ProgRec *p)
{
    for (int i = 0; i < p->nsh; i++) free(p->sh[i].orig);
    for (int i = 0; i < p->nuni; i++) { free(p->uni[i].name); free(p->uni[i].data); }
    free(p->uni);
    for (int i = 0; i < p->nblk; i++) free(p->blk[i].name);
    memset(p, 0, sizeof(*p));
}

// Virtual uniform locations: (index + 1) << 10 | element offset.
#define VLOC_SHIFT 10
#define VLOC_MASK ((1 << VLOC_SHIFT) - 1)

static UniformRec *vloc_rec(ProgRec *p, GLint vloc, GLint *real)
{
    if (vloc < (1 << VLOC_SHIFT)) { *real = -1; return NULL; }
    int idx = (vloc >> VLOC_SHIFT) - 1;
    int off = vloc & VLOC_MASK;
    if (!p || idx >= p->nuni) { *real = -1; return NULL; }
    UniformRec *u = &p->uni[idx];
    *real = u->real < 0 ? -1 : u->real + off;
    return off == 0 ? u : NULL;
}

static void refresh_uniforms(GLuint prog, ProgRec *p)
{
    for (int i = 0; i < p->nuni; i++) p->uni[i].real = r_glGetUniformLocation(prog, p->uni[i].name);
}

// ----------------------------------------------------------------------------------------
// Hooks: shader compilation

static void APIENTRY hook_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    resolve_helpers();

    size_t total = 0;
    for (GLsizei i = 0; i < count; i++)
        total += (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(strings[i]);
    char *src = (char *)malloc(total + 1);
    size_t pos = 0;
    for (GLsizei i = 0; i < count; i++) {
        size_t n = (lengths && lengths[i] >= 0) ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(src + pos, strings[i], n);
        pos += n;
    }
    src[total] = 0;

    GLint type = 0;
    r_glGetShaderiv(shader, GL_SHADER_TYPE, &type);

    EnterCriticalSection(&g_lock);
    int idx = identify(src);
    ShaderRec *rec = shader_rec(shader, 1);
    free(rec->orig);
    rec->orig = src;
    rec->type = (GLenum)type;
    rec->idx = idx;
    rec->overridden = 0;

    if (g_dump) {
        uint64_t h = fnv1a(src, total);
        char rel[160];
        snprintf(rel, sizeof(rel), "dump\\%s_%s_%016llx.glsl", idx >= 0 ? g_index[idx].name : "unknown", type_tag(type), (unsigned long long)h);
        wchar_t path[MAX_PATH];
        pack_path(path, rel);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) write_file(rel, src, total);
    }

    char *ovr = build_override(src, idx);
    GLuint test = 0;
    if (ovr) {
        test = compile_new((GLenum)type, ovr, g_index[idx].name);
        if (test) r_glDeleteShader(test);
    }
    LeaveCriticalSection(&g_lock);

    if (ovr && test) {
        const GLchar *s = ovr;
        r_glShaderSource(shader, 1, &s, NULL);
        rec->overridden = 1;
        rec->curHash = fnv1a(ovr, strlen(ovr));
        logf_("override applied: %s (%s) shader=%u", g_index[idx].name, type_tag(type), shader);
    } else {
        rec->curHash = fnv1a(src, total);
        r_glShaderSource(shader, count, strings, lengths);
    }
    free(ovr);
}

#define GL_ACTIVE_ATTRIBUTES 0x8B89
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void (APIENTRY *PFN_glGetActiveAttrib)(GLuint, GLuint, GLsizei, GLsizei *, GLint *, GLenum *, GLchar *);
typedef GLint (APIENTRY *PFN_glGetAttribLocation)(GLuint, const GLchar *);

// After the game links a program that contains overridden stages: make sure the link worked
// and that every vertex attribute the original shaders expose is still active (the game
// treats a missing attribute as fatal). Otherwise relink with the game's sources.
// Returns 1 if the program was left as is.
static int validate_override_link(GLuint prog, ProgRec *p, GLint ok)
{
    int any = 0;
    for (int i = 0; i < p->nsh; i++) if (p->sh[i].overridden) any = 1;
    if (!any) return 1;

    static PFN_glCreateProgram createProg;
    static PFN_glGetActiveAttrib activeAttrib;
    static PFN_glGetAttribLocation attribLoc;
    if (!createProg) {
        createProg = (PFN_glCreateProgram)real_wglGetProcAddress("glCreateProgram");
        activeAttrib = (PFN_glGetActiveAttrib)real_wglGetProcAddress("glGetActiveAttrib");
        attribLoc = (PFN_glGetAttribLocation)real_wglGetProcAddress("glGetAttribLocation");
    }
    int bad = !ok;
    char missing[64] = "";
    if (!bad && createProg && activeAttrib && attribLoc) {
        GLuint tmp = createProg();
        GLuint sh[4] = {0};
        for (int i = 0; i < p->nsh; i++) {
            sh[i] = compile_new(p->sh[i].type, p->sh[i].orig, "original");
            if (sh[i]) r_glAttachShader(tmp, sh[i]);
        }
        r_glLinkProgram(tmp);
        GLint tmpOk = 0, n = 0;
        r_glGetProgramiv(tmp, GL_LINK_STATUS, &tmpOk);
        if (tmpOk) r_glGetProgramiv(tmp, GL_ACTIVE_ATTRIBUTES, &n);
        for (GLint a = 0; a < n && !bad; a++) {
            char name[64] = {0};
            GLint size; GLenum type;
            activeAttrib(tmp, (GLuint)a, sizeof(name) - 1, NULL, &size, &type, name);
            if (strncmp(name, "gl_", 3) && attribLoc(prog, name) < 0) { bad = 1; snprintf(missing, sizeof(missing), "%s", name); }
        }
        for (int i = 0; i < p->nsh; i++) if (sh[i]) { r_glDetachShader(tmp, sh[i]); r_glDeleteShader(sh[i]); }
        r_glDeleteProgram(tmp);
    }
    if (!bad) return 1;

    logf_("OVERRIDE REJECTED for %s (program %u): %s%s - using the game's shaders", p->pname, prog,
          ok ? "attribute optimised away: " : "link failed", missing);
    GLuint attached[8];
    GLsizei na = 0;
    r_glGetAttachedShaders(prog, 8, &na, attached);
    for (GLsizei i = 0; i < na; i++) r_glDetachShader(prog, attached[i]);
    GLuint fresh[4] = {0};
    for (int i = 0; i < p->nsh; i++) {
        fresh[i] = compile_new(p->sh[i].type, p->sh[i].orig, p->pname);
        if (fresh[i]) r_glAttachShader(prog, fresh[i]);
        p->sh[i].overridden = 0;
        p->sh[i].curHash = fnv1a(p->sh[i].orig, strlen(p->sh[i].orig));
    }
    r_glLinkProgram(prog);
    for (int i = 0; i < p->nsh; i++) if (fresh[i]) r_glDeleteShader(fresh[i]);
    return 0;
}

static void APIENTRY hook_glLinkProgram(GLuint prog)
{
    resolve_helpers();
    EnterCriticalSection(&g_lock);
    ProgRec *p = prog_rec(prog, 1);
    for (int i = 0; i < p->nsh; i++) free(p->sh[i].orig);
    p->nsh = 0;
    GLuint ids[4];
    GLsizei n = 0;
    r_glGetAttachedShaders(prog, 4, &n, ids);
    for (GLsizei i = 0; i < n && i < 4; i++) {
        ShaderRec *s = shader_rec(ids[i], 0);
        if (!s || !s->orig) continue;
        ShaderRec *d = &p->sh[p->nsh++];
        d->type = s->type;
        d->orig = _strdup(s->orig);
        d->idx = s->idx;
        d->overridden = s->overridden;
        d->curHash = s->curHash;
    }
    p->pname[0] = 0;
    for (int i = 0; i < p->nsh; i++) {
        if (p->sh[i].idx < 0) continue;
        if (!p->pname[0] || p->sh[i].type == GL_FRAGMENT_SHADER)
            snprintf(p->pname, sizeof(p->pname), "%s", g_index[p->sh[i].idx].name);
    }
    p->blkChecked = 0;
    p->extrasChecked = 0;
    LeaveCriticalSection(&g_lock);

    r_glLinkProgram(prog);

    GLint ok = 0;
    r_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    EnterCriticalSection(&g_lock);
    p = prog_rec(prog, 1);
    if (!validate_override_link(prog, p, ok)) r_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    p->linked = ok;
    refresh_uniforms(prog, p);
    if (!ok) {
        char buf[2048] = {0};
        r_glGetProgramInfoLog(prog, sizeof(buf) - 1, NULL, buf);
        logf_("LINK ERROR program %u (%s): %s", prog, p->pname, buf);
    }
    LeaveCriticalSection(&g_lock);
}

static void APIENTRY hook_glDeleteProgram(GLuint prog)
{
    EnterCriticalSection(&g_lock);
    ProgRec *p = prog_rec(prog, 0);
    if (p) prog_free(p);
    LeaveCriticalSection(&g_lock);
    r_glDeleteProgram(prog);
}

// The game deletes shader objects while they are still attached (before linking), so
// shader records are kept until the id is reused by a new glShaderSource.
static void APIENTRY hook_glDeleteShader(GLuint shader)
{
    r_glDeleteShader(shader);
}

static GLint APIENTRY hook_glGetUniformLocation(GLuint prog, const GLchar *name)
{
    EnterCriticalSection(&g_lock);
    ProgRec *p = prog_rec(prog, 1);
    int idx = -1;
    for (int i = 0; i < p->nuni; i++) if (!strcmp(p->uni[i].name, name)) { idx = i; break; }
    if (idx < 0) {
        if (p->nuni == p->capuni) {
            p->capuni = p->capuni ? p->capuni * 2 : 16;
            p->uni = (UniformRec *)realloc(p->uni, p->capuni * sizeof(UniformRec));
        }
        idx = p->nuni++;
        UniformRec *u = &p->uni[idx];
        memset(u, 0, sizeof(*u));
        u->name = _strdup(name);
        u->real = r_glGetUniformLocation(prog, name);
    }
    GLint v = (GLint)((idx + 1) << VLOC_SHIFT);
    LeaveCriticalSection(&g_lock);
    return v;
}

static void APIENTRY hook_glUniformBlockBinding(GLuint prog, GLuint index, GLuint binding)
{
    resolve_helpers();
    char name[128] = {0};
    r_glGetActiveUniformBlockName(prog, index, sizeof(name) - 1, NULL, name);
    EnterCriticalSection(&g_lock);
    ProgRec *p = prog_rec(prog, 1);
    int found = 0;
    for (int i = 0; i < p->nblk; i++) if (!strcmp(p->blk[i].name, name)) { p->blk[i].binding = binding; found = 1; }
    if (!found && p->nblk < 8 && name[0]) { p->blk[p->nblk].name = _strdup(name); p->blk[p->nblk].binding = binding; p->nblk++; }
    found = 0;
    for (int i = 0; i < g_nKnownBlocks; i++) if (!strcmp(g_knownBlocks[i].name, name)) found = 1;
    if (!found && g_nKnownBlocks < 16 && name[0]) {
        snprintf(g_knownBlocks[g_nKnownBlocks].name, 64, "%s", name);
        g_knownBlocks[g_nKnownBlocks].binding = binding;
        g_nKnownBlocks++;
        logf_("uniform block %s -> binding %u", name, binding);
    }
    LeaveCriticalSection(&g_lock);
    r_glUniformBlockBinding(prog, index, binding);
}

static void APIENTRY hook_glUseProgram(GLuint prog);

// ----------------------------------------------------------------------------------------
// Hooks: uniform setters (translate virtual locations, cache values for relinks)

enum { U_1F = 1, U_2F, U_3F, U_4F, U_1I, U_2I, U_3I, U_4I, U_3UI, U_1FV, U_2FV, U_3FV, U_4FV, U_1IV, U_1UIV, U_M2, U_M3, U_M4 };

static void cache_uniform(UniformRec *u, int fn, GLsizei count, GLboolean tr, const void *data, size_t bytes)
{
    if (!u) return;
    if (u->bytes != bytes) { free(u->data); u->data = malloc(bytes); u->bytes = bytes; }
    memcpy(u->data, data, bytes);
    u->fn = fn;
    u->count = count;
    u->transpose = tr;
}

#define UPRE GLint real; EnterCriticalSection(&g_lock); UniformRec *u = vloc_rec(prog_rec(g_curProg, 0), loc, &real);
#define UPOST LeaveCriticalSection(&g_lock);

static void APIENTRY hook_glUniform1f(GLint loc, GLfloat a) { UPRE GLfloat v[1] = {a}; cache_uniform(u, U_1F, 1, 0, v, sizeof v); UPOST r_glUniform1f(real, a); }
static void APIENTRY hook_glUniform2f(GLint loc, GLfloat a, GLfloat b) { UPRE GLfloat v[2] = {a, b}; cache_uniform(u, U_2F, 1, 0, v, sizeof v); UPOST r_glUniform2f(real, a, b); }
static void APIENTRY hook_glUniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c) { UPRE GLfloat v[3] = {a, b, c}; cache_uniform(u, U_3F, 1, 0, v, sizeof v); UPOST r_glUniform3f(real, a, b, c); }
static void APIENTRY hook_glUniform4f(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { UPRE GLfloat v[4] = {a, b, c, d}; cache_uniform(u, U_4F, 1, 0, v, sizeof v); UPOST r_glUniform4f(real, a, b, c, d); }
static void APIENTRY hook_glUniform1i(GLint loc, GLint a) { UPRE GLint v[1] = {a}; cache_uniform(u, U_1I, 1, 0, v, sizeof v); UPOST r_glUniform1i(real, a); }
static void APIENTRY hook_glUniform2i(GLint loc, GLint a, GLint b) { UPRE GLint v[2] = {a, b}; cache_uniform(u, U_2I, 1, 0, v, sizeof v); UPOST r_glUniform2i(real, a, b); }
static void APIENTRY hook_glUniform3i(GLint loc, GLint a, GLint b, GLint c) { UPRE GLint v[3] = {a, b, c}; cache_uniform(u, U_3I, 1, 0, v, sizeof v); UPOST r_glUniform3i(real, a, b, c); }
static void APIENTRY hook_glUniform4i(GLint loc, GLint a, GLint b, GLint c, GLint d) { UPRE GLint v[4] = {a, b, c, d}; cache_uniform(u, U_4I, 1, 0, v, sizeof v); UPOST r_glUniform4i(real, a, b, c, d); }
static void APIENTRY hook_glUniform3ui(GLint loc, GLuint a, GLuint b, GLuint c) { UPRE GLuint v[3] = {a, b, c}; cache_uniform(u, U_3UI, 1, 0, v, sizeof v); UPOST r_glUniform3ui(real, a, b, c); }
static void APIENTRY hook_glUniform1fv(GLint loc, GLsizei n, const GLfloat *v) { UPRE cache_uniform(u, U_1FV, n, 0, v, n * sizeof(GLfloat)); UPOST r_glUniform1fv(real, n, v); }
static void APIENTRY hook_glUniform2fv(GLint loc, GLsizei n, const GLfloat *v) { UPRE cache_uniform(u, U_2FV, n, 0, v, n * 2 * sizeof(GLfloat)); UPOST r_glUniform2fv(real, n, v); }
static void APIENTRY hook_glUniform3fv(GLint loc, GLsizei n, const GLfloat *v) { UPRE cache_uniform(u, U_3FV, n, 0, v, n * 3 * sizeof(GLfloat)); UPOST r_glUniform3fv(real, n, v); }
static void APIENTRY hook_glUniform4fv(GLint loc, GLsizei n, const GLfloat *v) { UPRE cache_uniform(u, U_4FV, n, 0, v, n * 4 * sizeof(GLfloat)); UPOST r_glUniform4fv(real, n, v); }
static void APIENTRY hook_glUniform1iv(GLint loc, GLsizei n, const GLint *v) { UPRE cache_uniform(u, U_1IV, n, 0, v, n * sizeof(GLint)); UPOST r_glUniform1iv(real, n, v); }
static void APIENTRY hook_glUniform1uiv(GLint loc, GLsizei n, const GLuint *v) { UPRE cache_uniform(u, U_1UIV, n, 0, v, n * sizeof(GLuint)); UPOST r_glUniform1uiv(real, n, v); }
static void APIENTRY hook_glUniformMatrix2fv(GLint loc, GLsizei n, GLboolean t, const GLfloat *v) { UPRE cache_uniform(u, U_M2, n, t, v, n * 4 * sizeof(GLfloat)); UPOST r_glUniformMatrix2fv(real, n, t, v); }
static void APIENTRY hook_glUniformMatrix3fv(GLint loc, GLsizei n, GLboolean t, const GLfloat *v) { UPRE cache_uniform(u, U_M3, n, t, v, n * 9 * sizeof(GLfloat)); UPOST r_glUniformMatrix3fv(real, n, t, v); }
static void APIENTRY hook_glUniformMatrix4fv(GLint loc, GLsizei n, GLboolean t, const GLfloat *v) { UPRE cache_uniform(u, U_M4, n, t, v, n * 16 * sizeof(GLfloat)); UPOST r_glUniformMatrix4fv(real, n, t, v); }

static void replay_uniform(UniformRec *u)
{
    if (!u->fn || u->real < 0) return;
    GLint l = u->real;
    const GLfloat *f = (const GLfloat *)u->data;
    const GLint *i = (const GLint *)u->data;
    const GLuint *ui = (const GLuint *)u->data;
    switch (u->fn) {
    case U_1F: r_glUniform1f(l, f[0]); break;
    case U_2F: r_glUniform2f(l, f[0], f[1]); break;
    case U_3F: r_glUniform3f(l, f[0], f[1], f[2]); break;
    case U_4F: r_glUniform4f(l, f[0], f[1], f[2], f[3]); break;
    case U_1I: r_glUniform1i(l, i[0]); break;
    case U_2I: r_glUniform2i(l, i[0], i[1]); break;
    case U_3I: r_glUniform3i(l, i[0], i[1], i[2]); break;
    case U_4I: r_glUniform4i(l, i[0], i[1], i[2], i[3]); break;
    case U_3UI: r_glUniform3ui(l, ui[0], ui[1], ui[2]); break;
    case U_1FV: r_glUniform1fv(l, u->count, f); break;
    case U_2FV: r_glUniform2fv(l, u->count, f); break;
    case U_3FV: r_glUniform3fv(l, u->count, f); break;
    case U_4FV: r_glUniform4fv(l, u->count, f); break;
    case U_1IV: r_glUniform1iv(l, u->count, i); break;
    case U_1UIV: r_glUniform1uiv(l, u->count, ui); break;
    case U_M2: r_glUniformMatrix2fv(l, u->count, u->transpose, f); break;
    case U_M3: r_glUniformMatrix3fv(l, u->count, u->transpose, f); break;
    case U_M4: r_glUniformMatrix4fv(l, u->count, u->transpose, f); break;
    }
}

// ----------------------------------------------------------------------------------------
// Live reload

// Returns 1 = unchanged, 2 = relinked, 0 = compile failure, -1 = link failure.
static int relink_program(GLuint prog, ProgRec *p, int useOverrides)
{
    char *src[4] = {0};
    int isOvr[4] = {0};
    int changed = 0;
    for (int i = 0; i < p->nsh; i++) {
        ShaderRec *s = &p->sh[i];
        char *ovr = useOverrides ? build_override(s->orig, s->idx) : NULL;
        isOvr[i] = ovr != NULL;
        src[i] = ovr ? ovr : _strdup(s->orig);
        if (fnv1a(src[i], strlen(src[i])) != s->curHash) changed = 1;
    }
    if (!changed) {
        for (int i = 0; i < p->nsh; i++) free(src[i]);
        return 1;
    }

    GLuint fresh[4] = {0};
    for (int i = 0; i < p->nsh; i++) {
        ShaderRec *s = &p->sh[i];
        const char *name = s->idx >= 0 ? g_index[s->idx].name : "unknown";
        fresh[i] = compile_new(s->type, src[i], name);
        if (!fresh[i] && isOvr[i]) {
            // Override broke: fall back to the game's source for this stage.
            free(src[i]);
            src[i] = _strdup(s->orig);
            isOvr[i] = 0;
            fresh[i] = compile_new(s->type, src[i], name);
        }
        if (!fresh[i]) {
            for (int k = 0; k < i; k++) if (fresh[k]) r_glDeleteShader(fresh[k]);
            for (int k = 0; k < p->nsh; k++) free(src[k]);
            return 0;
        }
    }

    // Whatever is attached now (the game's shaders or our previous ones) gets detached;
    // the new ones stay attached and are flagged for deletion with the program.
    GLuint attached[8];
    GLsizei na = 0;
    r_glGetAttachedShaders(prog, 8, &na, attached);
    for (GLsizei i = 0; i < na; i++) r_glDetachShader(prog, attached[i]);
    for (int i = 0; i < p->nsh; i++) r_glAttachShader(prog, fresh[i]);
    r_glLinkProgram(prog);
    GLint ok = 0;
    r_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    for (int i = 0; i < p->nsh; i++) {
        r_glDeleteShader(fresh[i]);
        p->sh[i].overridden = isOvr[i];
        p->sh[i].curHash = fnv1a(src[i], strlen(src[i]));
        free(src[i]);
    }
    if (!ok) {
        char buf[2048] = {0};
        r_glGetProgramInfoLog(prog, sizeof(buf) - 1, NULL, buf);
        const char *name = p->nsh && p->sh[p->nsh - 1].idx >= 0 ? g_index[p->sh[p->nsh - 1].idx].name : "unknown";
        logf_("RELINK ERROR program %u (%s): %s", prog, name, buf);
        return -1;
    }

    refresh_uniforms(prog, p);
    p->blkChecked = 0;
    p->extrasChecked = 0;
    for (int i = 0; i < p->nblk; i++) {
        GLuint bi = r_glGetUniformBlockIndex(prog, p->blk[i].name);
        if (bi != GL_INVALID_INDEX) r_glUniformBlockBinding(prog, bi, p->blk[i].binding);
    }
    r_glUseProgram(prog);
    for (int i = 0; i < p->nuni; i++) replay_uniform(&p->uni[i]);
    return 2;
}

// ----------------------------------------------------------------------------------------
// Texture sharing between passes (shaderpack/share.txt):
//   <SourceProgram> <sourceSampler> <DestProgram> <destSampler> <unit>
// The texture bound to the source program's sampler is re-bound for the destination
// program on a spare texture unit, e.g. the sun shadow map for volumetric light.

#define GL_TEXTURE0 0x84C0
#define MAX_UNITS 32
typedef struct { GLenum target; GLuint tex; GLuint sampler; } UnitState;
static UnitState g_units[MAX_UNITS];
static GLuint g_activeUnit;
static GLuint g_maxGameUnit;

typedef void (APIENTRY *PFN_glBindTexture)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glActiveTexture)(GLenum);
typedef void (APIENTRY *PFN_glBindSampler)(GLuint, GLuint);
static PFN_glBindTexture r_glBindTexture;
static PFN_glActiveTexture r_glActiveTexture;
static PFN_glBindSampler r_glBindSampler;

typedef struct {
    char src[64], srcSampler[64], dst[64], dstSampler[64];
    GLuint unit;
    int raw;             // bind with our own sampler: no depth comparison, nearest filtering
    int valid;
    GLenum target;
    GLuint tex, sampler;
} Share;
static Share g_shares[16];
static int g_nShares;
static int g_shareGen = 1;

#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
typedef void (APIENTRY *PFN_glGenSamplers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glSamplerParameteri)(GLuint, GLenum, GLint);
static GLuint g_rawSampler;

static GLuint get_raw_sampler(void)
{
    if (g_rawSampler) return g_rawSampler;
    PFN_glGenSamplers gen = (PFN_glGenSamplers)real_wglGetProcAddress("glGenSamplers");
    PFN_glSamplerParameteri par = (PFN_glSamplerParameteri)real_wglGetProcAddress("glSamplerParameteri");
    if (!gen || !par) return 0;
    gen(1, &g_rawSampler);
    par(g_rawSampler, GL_TEXTURE_COMPARE_MODE, 0);
    par(g_rawSampler, 0x2801 /*MIN_FILTER*/, GL_NEAREST);
    par(g_rawSampler, 0x2800 /*MAG_FILTER*/, GL_NEAREST);
    par(g_rawSampler, 0x2802 /*WRAP_S*/, GL_CLAMP_TO_EDGE);
    par(g_rawSampler, 0x2803 /*WRAP_T*/, GL_CLAMP_TO_EDGE);
    return g_rawSampler;
}

static void load_shares(void)
{
    g_nShares = 0;
    char *txt = read_file("share.txt", NULL);
    if (!txt) return;
    char *save = NULL;
    for (char *line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        if (line[0] == '#' || g_nShares >= 16) continue;
        Share s;
        memset(&s, 0, sizeof(s));
        unsigned unit = 0;
        char flag[16] = "";
        int n = sscanf(line, "%63s %63s %63s %63s %u %15s", s.src, s.srcSampler, s.dst, s.dstSampler, &unit, flag);
        if (n >= 5 && unit < MAX_UNITS) {
            s.unit = unit;
            s.raw = !strcmp(flag, "raw");
            g_shares[g_nShares++] = s;
        }
    }
    free(txt);
    g_shareGen++;
    logf_("shares: %d", g_nShares);
}

static void APIENTRY hook_glActiveTexture(GLenum t)
{
    g_activeUnit = t - GL_TEXTURE0;
    if (g_activeUnit < MAX_UNITS && g_activeUnit > g_maxGameUnit) {
        g_maxGameUnit = g_activeUnit;
        logf_("game uses texture unit %u", g_maxGameUnit);
    }
    r_glActiveTexture(t);
}

__declspec(dllexport) void APIENTRY glBindTexture(GLenum target, GLuint tex)
{
    if (g_activeUnit < MAX_UNITS) { g_units[g_activeUnit].target = target; g_units[g_activeUnit].tex = tex; }
    r_glBindTexture(target, tex);
}

static void APIENTRY hook_glBindSampler(GLuint unit, GLuint sampler)
{
    if (unit < MAX_UNITS) g_units[unit].sampler = sampler;
    r_glBindSampler(unit, sampler);
}

static void capture_shares(ProgRec *prev)
{
    for (int i = 0; i < g_nShares; i++) {
        Share *s = &g_shares[i];
        if (strcmp(prev->pname, s->src)) continue;
        for (int k = 0; k < prev->nuni; k++) {
            UniformRec *u = &prev->uni[k];
            if (strcmp(u->name, s->srcSampler) || u->fn != U_1I) continue;
            GLint unit = *(GLint *)u->data;
            if (unit >= 0 && unit < MAX_UNITS && g_units[unit].tex) {
                if (!s->valid || s->tex != g_units[unit].tex)
                    logf_("share %s.%s captured: tex %u target 0x%x (unit %d)", s->src, s->srcSampler, g_units[unit].tex, g_units[unit].target, unit);
                s->target = g_units[unit].target;
                s->tex = g_units[unit].tex;
                s->sampler = g_units[unit].sampler;
                s->valid = 1;
            }
        }
    }
}

static int is_share_src(ProgRec *p)
{
    if (p->shareGen != g_shareGen) {
        p->shareGen = g_shareGen;
        p->shareSrc = 0;
        for (int i = 0; i < g_nShares; i++) if (!strcmp(p->pname, g_shares[i].src)) p->shareSrc = 1;
    }
    return p->shareSrc;
}

// Shares are captured at draw time: by the next glUseProgram the game has often re-bound the
// unit for the following pass (the shadow map's unit holds the scene depth by then).
static void share_draw_capture(void)
{
    if (!g_nShares || !g_curProg) return;
    ProgRec *p = prog_rec(g_curProg, 0);
    if (!p || !p->linked || !is_share_src(p)) return;
    EnterCriticalSection(&g_lock);
    capture_shares(p);
    LeaveCriticalSection(&g_lock);
}

typedef void (APIENTRY *PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (APIENTRY *PFN_glDrawElements)(GLenum, GLsizei, GLenum, const void *);
typedef void (APIENTRY *PFN_glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFN_glDrawElementsInstanced)(GLenum, GLsizei, GLenum, const void *, GLsizei);
static PFN_glDrawArrays r_glDrawArrays;
static PFN_glDrawElements r_glDrawElements;
static PFN_glDrawArraysInstanced r_glDrawArraysInstanced;
static PFN_glDrawElementsInstanced r_glDrawElementsInstanced;

__declspec(dllexport) void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    share_draw_capture();
    r_glDrawArrays(mode, first, count);
}

__declspec(dllexport) void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    share_draw_capture();
    r_glDrawElements(mode, count, type, indices);
}

static void APIENTRY hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei inst)
{
    share_draw_capture();
    r_glDrawArraysInstanced(mode, first, count, inst);
}

static void APIENTRY hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei inst)
{
    share_draw_capture();
    r_glDrawElementsInstanced(mode, count, type, indices, inst);
}

static void apply_shares(GLuint prog, ProgRec *p)
{
    for (int i = 0; i < g_nShares; i++) {
        Share *s = &g_shares[i];
        if (!s->valid || strcmp(p->pname, s->dst)) continue;
        GLint loc = r_glGetUniformLocation(prog, s->dstSampler);
        if (loc < 0) continue;
        GLuint sampler = s->raw ? get_raw_sampler() : s->sampler;
        r_glActiveTexture(GL_TEXTURE0 + s->unit);
        r_glBindTexture(s->target, s->tex);
        if (r_glBindSampler) r_glBindSampler(s->unit, sampler);
        r_glActiveTexture(GL_TEXTURE0 + g_activeUnit);
        g_units[s->unit].target = s->target;
        g_units[s->unit].tex = s->tex;
        g_units[s->unit].sampler = sampler;
        r_glUniform1i(loc, (GLint)s->unit);
    }
}

#define GL_ACTIVE_UNIFORM_BLOCKS 0x8A36

// Gives uniform blocks the game never bound for this program the binding it uses elsewhere.
static void apply_known_blocks(GLuint prog, ProgRec *p)
{
    p->blkChecked = 1;
    GLint n = 0;
    r_glGetProgramiv(prog, GL_ACTIVE_UNIFORM_BLOCKS, &n);
    for (GLint b = 0; b < n; b++) {
        char name[128] = {0};
        r_glGetActiveUniformBlockName(prog, (GLuint)b, sizeof(name) - 1, NULL, name);
        int bound = 0;
        for (int i = 0; i < p->nblk; i++) if (!strcmp(p->blk[i].name, name)) bound = 1;
        if (bound) continue;
        for (int i = 0; i < g_nKnownBlocks; i++) {
            if (strcmp(g_knownBlocks[i].name, name)) continue;
            r_glUniformBlockBinding(prog, (GLuint)b, g_knownBlocks[i].binding);
            logf_("auto-bound %s for %s (program %u)", name, p->pname, prog);
        }
    }
}

static int pack_flag(const char *rel)
{
    wchar_t path[MAX_PATH];
    pack_path(path, rel);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static volatile LONG g_toggleOff;

// ----------------------------------------------------------------------------------------
// Custom textures (shaderpack/textures.txt):  <file.raw> <samplerName> <unit>
// .raw = "HYTX" + u32 width + u32 height + RGBA8 pixels (tools/png_to_raw.py).
// Any program with an active sampler of that name gets the texture on that unit.

#define GL_TEXTURE_2D         0x0DE1
#define GL_RGBA               0x1908
#define GL_RGBA8              0x8058
#define GL_UNSIGNED_BYTE      0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_LINEAR             0x2601
#define GL_REPEAT             0x2901
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_TEXTURE_BINDING_2D 0x8069

typedef void (APIENTRY *PFN_glGenTextures)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glDeleteTextures)(GLsizei, const GLuint *);
typedef void (APIENTRY *PFN_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (APIENTRY *PFN_glTexParameteri)(GLenum, GLenum, GLint);
typedef void (APIENTRY *PFN_glPixelStorei)(GLenum, GLint);
typedef void (APIENTRY *PFN_glGenerateMipmap)(GLenum);

typedef struct { char file[64]; char sampler[64]; GLuint unit; GLuint tex; int loaded; } CustomTex;
static CustomTex g_ctex[8];
static int g_nCtex;

static void load_textures_cfg(void)
{
    static PFN_glDeleteTextures del;
    if (!del) del = (PFN_glDeleteTextures)GetProcAddress(g_real, "glDeleteTextures");
    for (int i = 0; i < g_nCtex; i++) if (g_ctex[i].tex) del(1, &g_ctex[i].tex);
    g_nCtex = 0;
    char *txt = read_file("textures.txt", NULL);
    if (!txt) return;
    char *save = NULL;
    for (char *line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        if (line[0] == '#' || g_nCtex >= 8) continue;
        CustomTex t;
        memset(&t, 0, sizeof(t));
        unsigned unit = 0;
        if (sscanf(line, "%63s %63s %u", t.file, t.sampler, &unit) == 3 && unit < MAX_UNITS) {
            t.unit = unit;
            g_ctex[g_nCtex++] = t;
        }
    }
    free(txt);
}

static void ensure_textures(void)
{
    static PFN_glGenTextures gen;
    static PFN_glTexImage2D img;
    static PFN_glTexParameteri par;
    static PFN_glPixelStorei pst;
    if (!gen) {
        gen = (PFN_glGenTextures)GetProcAddress(g_real, "glGenTextures");
        img = (PFN_glTexImage2D)GetProcAddress(g_real, "glTexImage2D");
        par = (PFN_glTexParameteri)GetProcAddress(g_real, "glTexParameteri");
        pst = (PFN_glPixelStorei)GetProcAddress(g_real, "glPixelStorei");
    }
    for (int i = 0; i < g_nCtex; i++) {
        CustomTex *t = &g_ctex[i];
        if (t->loaded) continue;
        t->loaded = 1;
        char rel[96];
        snprintf(rel, sizeof(rel), "textures\\%s", t->file);
        size_t len = 0;
        unsigned char *raw = (unsigned char *)read_file(rel, &len);
        if (!raw || len < 12 || memcmp(raw, "HYTX", 4)) { logf_("texture %s missing/invalid", t->file); free(raw); continue; }
        uint32_t w, h;
        memcpy(&w, raw + 4, 4);
        memcpy(&h, raw + 8, 4);
        if (len < 12 + (size_t)w * h * 4) { logf_("texture %s truncated", t->file); free(raw); continue; }
        GLint prevTex = 0;
        r_glActiveTexture(GL_TEXTURE0 + t->unit);
        r_glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        gen(1, &t->tex);
        r_glBindTexture(GL_TEXTURE_2D, t->tex);
        pst(GL_UNPACK_ALIGNMENT, 1);
        img(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, raw + 12);
        pst(GL_UNPACK_ALIGNMENT, 4);
        // Mipmaps + anisotropic filtering: Hytale's TAA doesn't jitter, so unfiltered
        // high-frequency lookups (water normals) alias into grain at a distance.
        static PFN_glGenerateMipmap genMips;
        if (!genMips) genMips = (PFN_glGenerateMipmap)real_wglGetProcAddress("glGenerateMipmap");
        if (genMips) {
            genMips(GL_TEXTURE_2D);
            par(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703 /*LINEAR_MIPMAP_LINEAR*/);
            par(GL_TEXTURE_2D, 0x84FE /*TEXTURE_MAX_ANISOTROPY*/, 16);
        } else {
            par(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        par(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        par(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        par(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        r_glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        r_glActiveTexture(GL_TEXTURE0 + g_activeUnit);
        free(raw);
        logf_("texture %s loaded (%ux%u) -> %s unit %u", t->file, w, h, t->sampler, t->unit);
    }
}

// ----------------------------------------------------------------------------------------
// Uniform forwarding (shaderpack/forward.txt):  <SourceProgram> <srcUniform> <DestProgram|*> <dstUniform>
// The last value the game set on the source program is replayed into the destination
// program whenever it is used (e.g. SkyFS.uSunPosition -> DeferredFS.uHookSunPos).

typedef struct { char src[64], srcUni[64], dst[64], dstUni[64]; GLuint srcProg; } FwdRule;
static FwdRule g_fwd[16];
static int g_nFwd;

static void load_forward_cfg(void)
{
    g_nFwd = 0;
    char *txt = read_file("forward.txt", NULL);
    if (!txt) return;
    char *save = NULL;
    for (char *line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        if (line[0] == '#' || g_nFwd >= 16) continue;
        FwdRule r;
        memset(&r, 0, sizeof(r));
        if (sscanf(line, "%63s %63s %63s %63s", r.src, r.srcUni, r.dst, r.dstUni) == 4) g_fwd[g_nFwd++] = r;
    }
    free(txt);
}

static void replay_uniform_at(UniformRec *u, GLint loc)
{
    GLint saved = u->real;
    u->real = loc;
    replay_uniform(u);
    u->real = saved;
}

static LARGE_INTEGER g_t0, g_freq;
static int g_frame;
static int g_traceRemaining;

#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
typedef void (APIENTRY *PFN_glGetFramebufferAttachmentParameteriv)(GLenum, GLenum, GLenum, GLint *);
static PFN_glGetFramebufferAttachmentParameteriv r_glGetFramebufferAttachmentParameteriv;

static void trace_use(GLuint prog, ProgRec *p)
{
    GLint fbo = 0;
    r_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    char fmt[128] = "";
    if (fbo && r_glGetFramebufferAttachmentParameteriv) {
        size_t o = 0;
        for (int a = 0; a < 3; a++) {
            GLint type = 0, bits[4] = {0};
            r_glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + a, 0x8CD0 /*OBJECT_TYPE*/, &type);
            if (!type) break;
            for (int c = 0; c < 4; c++)
                r_glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + a, 0x8212 + c, &bits[c]);
            o += snprintf(fmt + o, sizeof(fmt) - o, " c%d:%d/%d/%d/%d", a, bits[0], bits[1], bits[2], bits[3]);
        }
    }
    logf_("TRACE use %u %s fbo=%d%s", prog, p ? p->pname : "?", fbo, fmt);
}

static void check_extras(GLuint prog, ProgRec *p)
{
    p->extrasChecked = 1;
    p->hasExtras = 0;
    for (int i = 0; i < g_nCtex; i++) {
        p->ctexLoc[i] = r_glGetUniformLocation(prog, g_ctex[i].sampler);
        if (p->ctexLoc[i] >= 0) { r_glUniform1i(p->ctexLoc[i], (GLint)g_ctex[i].unit); p->hasExtras = 1; }
    }
    for (int i = 0; i < g_nFwd; i++) {
        p->fwdLoc[i] = -1;
        if (strcmp(g_fwd[i].dst, "*") && strcmp(g_fwd[i].dst, p->pname)) continue;
        p->fwdLoc[i] = r_glGetUniformLocation(prog, g_fwd[i].dstUni);
        if (p->fwdLoc[i] >= 0) p->hasExtras = 1;
    }
    p->locFrame = r_glGetUniformLocation(prog, "uHookFrame");
    p->locTime = r_glGetUniformLocation(prog, "uHookTime");
    p->locWetness = r_glGetUniformLocation(prog, "uHookWetness");
    if (p->locFrame >= 0 || p->locTime >= 0 || p->locWetness >= 0) p->hasExtras = 1;
}

// Minecraft-style "wetness": follows the rain signal with Optifine's default half-lives
// (30 s getting wet, 10 s drying). The rain signal is the sky's top gradient saturation the
// shaders use too (AU_Common.glsl GetRainFactor); the rainbows need wetness.
static float g_wetness;
static LARGE_INTEGER g_wetLast;

static float smooth_step(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static void update_wetness(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = g_wetLast.QuadPart ? (double)(now.QuadPart - g_wetLast.QuadPart) / (double)g_freq.QuadPart : 0.0;
    g_wetLast = now;
    if (dt <= 0.0 || dt > 1.0) return;
    for (int i = 0; i < g_nFwd; i++) {
        if (strcmp(g_fwd[i].srcUni, "uTopGradientColor")) continue;
        ProgRec *src = prog_rec(g_fwd[i].srcProg, 0);
        if (!src) return;
        for (int k = 0; k < src->nuni; k++) {
            UniformRec *u = &src->uni[k];
            if (strcmp(u->name, "uTopGradientColor") || !u->data || u->bytes < 3 * sizeof(float)) continue;
            const float *c = (const float *)u->data;
            float mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            float mn = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
            if (mx < 0.08f) return;   // night sky: no colour to read the weather from
            float rain = smooth_step(0.40f, 0.20f, (mx - mn) / mx);
            double halfLife = rain > g_wetness ? 30.0 : 10.0;
            g_wetness += (float)((rain - g_wetness) * (1.0 - pow(0.5, dt / halfLife)));
            return;
        }
        return;
    }
}

static void apply_extras(ProgRec *p)
{
    for (int i = 0; i < g_nCtex; i++) {
        if (p->ctexLoc[i] < 0 || !g_ctex[i].tex) continue;
        r_glActiveTexture(GL_TEXTURE0 + g_ctex[i].unit);
        r_glBindTexture(GL_TEXTURE_2D, g_ctex[i].tex);
        if (r_glBindSampler) r_glBindSampler(g_ctex[i].unit, 0);
        g_units[g_ctex[i].unit].target = GL_TEXTURE_2D;
        g_units[g_ctex[i].unit].tex = g_ctex[i].tex;
        g_units[g_ctex[i].unit].sampler = 0;
        r_glActiveTexture(GL_TEXTURE0 + g_activeUnit);
    }
    for (int i = 0; i < g_nFwd; i++) {
        if (p->fwdLoc[i] < 0) continue;
        ProgRec *src = prog_rec(g_fwd[i].srcProg, 0);
        if (!src) continue;
        for (int k = 0; k < src->nuni; k++) {
            if (!strcmp(src->uni[k].name, g_fwd[i].srcUni)) { replay_uniform_at(&src->uni[k], p->fwdLoc[i]); break; }
        }
    }
    if (p->locFrame >= 0) r_glUniform1i(p->locFrame, g_frame);
    if (p->locWetness >= 0) r_glUniform1f(p->locWetness, g_wetness);
    if (p->locTime >= 0) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        r_glUniform1f(p->locTime, (float)((double)(now.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart));
    }
}

// ----------------------------------------------------------------------------------------
// Profiling: FPS every 10 s in hook.log; programs/_profile logs one frame's GPU time per pass
// (GL timestamps taken at every program switch).

#define GL_TIMESTAMP 0x8E28
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
typedef void (APIENTRY *PFN_glGenQueries)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glQueryCounter)(GLuint, GLenum);
typedef void (APIENTRY *PFN_glGetQueryObjectui64v)(GLuint, GLenum, uint64_t *);
typedef void (APIENTRY *PFN_glGetQueryObjectiv)(GLuint, GLenum, GLint *);

#define PROF_MAX 1024
static int g_profState;      // 0 idle, 1 armed, 2 recording, 3 collecting
static int g_profN, g_profWait;
static GLuint g_profQ[PROF_MAX];
static char g_profName[PROF_MAX][48];
static LARGE_INTEGER g_fpsT0;
static int g_fpsFrames;

static void profile_frame_marker(void)
{
    // FPS
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_fpsT0.QuadPart) g_fpsT0 = now;
    g_fpsFrames++;
    double secs = (double)(now.QuadPart - g_fpsT0.QuadPart) / (double)g_freq.QuadPart;
    if (secs >= 10.0) {
        logf_("fps %.1f (%.2f ms)", g_fpsFrames / secs, secs * 1000.0 / g_fpsFrames);
        g_fpsFrames = 0;
        g_fpsT0 = now;
    }

    if (g_profState == 1) {
        static PFN_glGenQueries gen;
        if (!gen) gen = (PFN_glGenQueries)real_wglGetProcAddress("glGenQueries");
        if (!g_profQ[0] && gen) gen(PROF_MAX, g_profQ);
        g_profState = 2;
        g_profN = 0;
    } else if (g_profState == 2) {
        g_profState = 3;
        g_profWait = 0;
    }
}

static void profile_use(const char *name)
{
    static PFN_glQueryCounter qc;
    if (!qc) qc = (PFN_glQueryCounter)real_wglGetProcAddress("glQueryCounter");
    if (g_profState == 2 && g_profN < PROF_MAX && qc) {
        qc(g_profQ[g_profN], GL_TIMESTAMP);
        snprintf(g_profName[g_profN], 48, "%s", name && name[0] ? name : "(other)");
        g_profN++;
    }
}

static void profile_collect(void)
{
    if (g_profState != 3 || ++g_profWait < 4) return;
    static PFN_glGetQueryObjectui64v get;
    if (!get) get = (PFN_glGetQueryObjectui64v)real_wglGetProcAddress("glGetQueryObjectui64v");
    if (!get || g_profN < 2) { g_profState = 0; return; }
    static uint64_t t[PROF_MAX];
    for (int i = 0; i < g_profN; i++) get(g_profQ[i], GL_QUERY_RESULT, &t[i]);
    // Aggregate durations per program name.
    char names[128][48];
    double ms[128];
    int counts[128];
    int nn = 0;
    double total = 0.0;
    for (int i = 0; i + 1 < g_profN; i++) {
        double d = (double)(t[i + 1] - t[i]) / 1.0e6;
        total += d;
        int k = 0;
        while (k < nn && strcmp(names[k], g_profName[i])) k++;
        if (k == nn && nn < 128) { snprintf(names[nn], 48, "%s", g_profName[i]); ms[nn] = 0; counts[nn] = 0; nn++; }
        if (k < 128) { ms[k] += d; counts[k]++; }
    }
    logf_("PROFILE frame GPU %.2f ms over %d passes:", total, g_profN - 1);
    for (int pass = 0; pass < nn; pass++) {
        int best = -1;
        for (int k = 0; k < nn; k++) if (counts[k] >= 0 && (best < 0 || ms[k] > ms[best])) best = k;
        if (best < 0) break;
        logf_("PROFILE %8.3f ms  %3dx  %s", ms[best], counts[best], names[best]);
        counts[best] = -1;
    }
    g_profState = 0;
}

static void do_reload(void)
{
    EnterCriticalSection(&g_lock);
    g_enabled = !g_toggleOff && !pack_flag("programs\\_disabled");
    load_index();
    load_shares();
    load_textures_cfg();
    load_forward_cfg();
    if (pack_flag("programs\\_profile")) {
        wchar_t path[MAX_PATH];
        pack_path(path, "programs\\_profile");
        DeleteFileW(path);
        if (g_profState == 0) g_profState = 1;
    }
    if (pack_flag("programs\\_trace")) {
        wchar_t path[MAX_PATH];
        pack_path(path, "programs\\_trace");
        DeleteFileW(path);
        g_traceRemaining = 800;
    }
    int relinked = 0, failed = 0;
    for (size_t id = 1; id < g_progCap; id++) {
        ProgRec *p = &g_progs[id];
        if (!p->used) continue;
        p->extrasChecked = 0;
        if (!p->linked || !p->nsh) continue;
        int r = relink_program((GLuint)id, p, 1);
        if (r == -1) {
            relink_program((GLuint)id, p, 0);
            failed++;
        } else if (r == 0) {
            failed++;
        } else if (r == 2) {
            relinked++;
        }
    }
    r_glUseProgram(g_curProg);
    logf_("reload done: relinked=%d failed=%d", relinked, failed);
    LeaveCriticalSection(&g_lock);
}

static void APIENTRY hook_glUseProgram(GLuint prog)
{
    if (g_reloadRequested && InterlockedExchange(&g_reloadRequested, 0)) {
        g_curProg = prog;
        do_reload();
    }
    EnterCriticalSection(&g_lock);
    g_curProg = prog;
    r_glUseProgram(prog);
    ProgRec *p = prog ? prog_rec(prog, 0) : NULL;
    if (g_profState == 2) profile_use(p ? p->pname : "");
    if (p && p->linked) {
        if (!p->blkChecked && g_nKnownBlocks) apply_known_blocks(prog, p);
        if (g_nShares) apply_shares(prog, p);
        for (int i = 0; i < g_nFwd; i++) if (!strcmp(g_fwd[i].src, p->pname)) g_fwd[i].srcProg = prog;
        if (!strcmp(p->pname, "PostEffectFS")) { g_frame++; update_wetness(); profile_frame_marker(); profile_collect(); }
        if (g_nCtex) ensure_textures();
        if (!p->extrasChecked) check_extras(prog, p);
        if (p->hasExtras) apply_extras(p);
    }
    if (g_traceRemaining > 0) { g_traceRemaining--; trace_use(prog, p); }
    LeaveCriticalSection(&g_lock);
}

static int is_game_foreground(void)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI watcher_thread(LPVOID arg)
{
    (void)arg;
    wchar_t dirs[2][MAX_PATH];
    pack_path(dirs[0], "programs");
    pack_path(dirs[1], "include");
    HANDLE h[2];
    for (int i = 0; i < 2; i++) {
        CreateDirectoryW(dirs[i], NULL);
        h[i] = FindFirstChangeNotificationW(dirs[i], FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    }
    int pauseDown = 0;
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, h, FALSE, 50);
        if (r == WAIT_TIMEOUT) {
            // Pause key toggles the shaderpack on/off (only while the game has focus).
            int down = (GetAsyncKeyState(VK_PAUSE) & 0x8000) != 0;
            if (down && !pauseDown && is_game_foreground()) {
                g_toggleOff = !g_toggleOff;
                logf_("shaderpack %s (Pause key)", g_toggleOff ? "OFF" : "ON");
                InterlockedExchange(&g_reloadRequested, 1);
            }
            pauseDown = down;
            continue;
        }
        if (r >= WAIT_OBJECT_0 + 2) { Sleep(1000); continue; }
        // Debounce bursts of writes from editors.
        Sleep(400);
        for (int i = 0; i < 2; i++) {
            FindNextChangeNotification(h[i]);
            while (WaitForSingleObject(h[i], 0) == WAIT_OBJECT_0) FindNextChangeNotification(h[i]);
        }
        InterlockedExchange(&g_reloadRequested, 1);
    }
    return 0;
}

// ----------------------------------------------------------------------------------------
// In-game options menu (Home key). Lists the #defines named in shaderpack/options.txt and
// edits their values in include/AU_Settings.glsl, which the file watcher then hot-reloads.
// Drawn as an overlay at SwapBuffers, after the game's own UI.

#define MENU_MAX 160
#define MENU_SETTINGS "include\\AU_Settings.glsl"
enum { MO_HEADER, MO_BOOL, MO_ENUM, MO_RANGE, MO_PACK };
typedef struct {
    int kind;
    char define[48];
    wchar_t label[64];
    double min, max, step, def, value;
    int nEnum;
    double enumVal[8];
    wchar_t enumLabel[8][24];
    int decimals, isFloat, present;
} MenuOpt;

static MenuOpt g_mo[MENU_MAX];
static int g_nMo, g_moSel = -1, g_moTop;
static volatile LONG g_menuOpen;
static int g_menuDirty = 1;
static double g_menuWriteAt;
static wchar_t g_menuStatus[96];
static CRITICAL_SECTION g_menuLock;
static HWND g_gameHwnd;
static WNDPROC g_prevWndProc;

static double now_sec(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)g_freq.QuadPart;
}

static char *menu_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void to_wide(wchar_t *out, int cap, const char *s)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) out[0] = 0;
    out[cap - 1] = 0;
}

static int decimals_of(const char *num)
{
    const char *d = strchr(num, '.');
    if (!d) return 0;
    int n = 0;
    d++;
    while (d[n] >= '0' && d[n] <= '9') n++;
    return n;
}

// Value token of the first "#define NAME value" line, or NULL. *tokEnd = end of the token.
static char *find_define_value(char *txt, const char *name, char **tokEnd)
{
    size_t nl = strlen(name);
    for (char *p = txt; (p = strstr(p, "#define ")) != NULL; p += 8) {
        if (p != txt && p[-1] != '\n') continue;
        char *q = p + 8;
        while (*q == ' ' || *q == '\t') q++;
        if (strncmp(q, name, nl) || (q[nl] != ' ' && q[nl] != '\t')) continue;
        q += nl;
        while (*q == ' ' || *q == '\t') q++;
        char *e = q;
        while (*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n') e++;
        if (tokEnd) *tokEnd = e;
        return q;
    }
    return NULL;
}

// options.txt:  "@ Section"  or  "DEFINE | Label | kind | default"
//   kind = bool | enum value:Label ... (underscores show as spaces) | range min max step
//   DEFINE "!PACK" is the shaderpack on/off switch (same as the Pause key).
static void menu_load(void)
{
    char *opt = read_file("options.txt", NULL);
    char *set = read_file(MENU_SETTINGS, NULL);
    g_nMo = 0;
    char *save = NULL;
    for (char *line = opt ? strtok_s(opt, "\n", &save) : NULL; line && g_nMo < MENU_MAX; line = strtok_s(NULL, "\n", &save)) {
        line = menu_trim(line);
        if (!*line || *line == '#') continue;
        MenuOpt *m = &g_mo[g_nMo];
        memset(m, 0, sizeof(*m));
        if (*line == '@') {
            m->kind = MO_HEADER;
            to_wide(m->label, 64, menu_trim(line + 1));
            g_nMo++;
            continue;
        }
        char *f[4] = {0};
        int nf = 0;
        char *s2 = NULL;
        for (char *t = strtok_s(line, "|", &s2); t && nf < 4; t = strtok_s(NULL, "|", &s2)) f[nf++] = menu_trim(t);
        if (nf < 3) continue;
        snprintf(m->define, sizeof(m->define), "%s", f[0]);
        to_wide(m->label, 64, f[1]);
        char *kind = f[2];
        if (!strcmp(m->define, "!PACK")) {
            m->kind = MO_PACK;
            m->present = 1;
            m->value = g_toggleOff ? 0.0 : 1.0;
            m->def = 1.0;
            g_nMo++;
            continue;
        }
        if (!strncmp(kind, "bool", 4)) {
            m->kind = MO_BOOL;
            m->max = 1.0;
            m->step = 1.0;
        } else if (!strncmp(kind, "enum", 4)) {
            m->kind = MO_ENUM;
            char *s3 = NULL;
            for (char *t = strtok_s(kind + 4, " ", &s3); t && m->nEnum < 8; t = strtok_s(NULL, " ", &s3)) {
                char *c = strchr(t, ':');
                if (!c) continue;
                *c = 0;
                m->enumVal[m->nEnum] = atof(t);
                to_wide(m->enumLabel[m->nEnum], 24, c + 1);
                for (wchar_t *w = m->enumLabel[m->nEnum]; *w; w++) if (*w == L'_') *w = L' ';
                m->nEnum++;
            }
            if (!m->nEnum) continue;
        } else if (!strncmp(kind, "range", 5)) {
            char a[32], b[32], c[32];
            if (sscanf(kind + 5, "%31s %31s %31s", a, b, c) != 3) continue;
            m->kind = MO_RANGE;
            m->min = atof(a);
            m->max = atof(b);
            m->step = atof(c);
            if (m->step <= 0.0) m->step = 1.0;
            m->decimals = decimals_of(c);
            m->isFloat = strchr(a, '.') || strchr(b, '.') || strchr(c, '.');
        } else {
            continue;
        }
        if (nf >= 4 && *f[3]) {
            m->def = atof(f[3]);
            if (strchr(f[3], '.')) m->isFloat = 1;
            if (decimals_of(f[3]) > m->decimals) m->decimals = decimals_of(f[3]);
        }
        char *e = NULL;
        char *v = set ? find_define_value(set, m->define, &e) : NULL;
        if (v) {
            char tok[32];
            size_t n = (size_t)(e - v);
            if (n > 31) n = 31;
            memcpy(tok, v, n);
            tok[n] = 0;
            m->value = atof(tok);
            m->present = 1;
            if (strchr(tok, '.')) m->isFloat = 1;
            if (decimals_of(tok) > m->decimals) m->decimals = decimals_of(tok);
        }
        if (nf < 4 || !*f[3]) m->def = m->value;
        g_nMo++;
    }
    free(opt);
    free(set);
    if (g_moSel < 0 || g_moSel >= g_nMo || g_mo[g_moSel].kind == MO_HEADER) {
        g_moSel = -1;
        for (int i = 0; i < g_nMo; i++) if (g_mo[i].kind != MO_HEADER) { g_moSel = i; break; }
    }
}

static void format_value(const MenuOpt *m, double v, char *out, size_t cap)
{
    if (m->kind == MO_RANGE && m->isFloat) snprintf(out, cap, "%.*f", m->decimals < 1 ? 1 : m->decimals, v);
    else snprintf(out, cap, "%d", (int)floor(v + 0.5));
}

static void display_value(const MenuOpt *m, wchar_t *out, int cap)
{
    if (!m->present) { _snwprintf(out, cap, L"n/a"); out[cap - 1] = 0; return; }
    if (m->kind == MO_BOOL || m->kind == MO_PACK) { _snwprintf(out, cap, L"%ls", m->value > 0.5 ? L"On" : L"Off"); out[cap - 1] = 0; return; }
    if (m->kind == MO_ENUM)
        for (int i = 0; i < m->nEnum; i++)
            if (fabs(m->enumVal[i] - m->value) < 1e-6) { _snwprintf(out, cap, L"%ls", m->enumLabel[i]); out[cap - 1] = 0; return; }
    char buf[32];
    format_value(m, m->value, buf, sizeof(buf));
    to_wide(out, cap, buf);
}

// Writes the menu's values into the settings file, keeping everything else as is.
static void menu_save(void)
{
    size_t n = 0;
    char *set = read_file(MENU_SETTINGS, &n);
    if (!set) return;
    int changed = 0;
    for (int i = 0; i < g_nMo; i++) {
        MenuOpt *m = &g_mo[i];
        if (m->kind == MO_HEADER || m->kind == MO_PACK || !m->present) continue;
        char *e = NULL;
        char *v = find_define_value(set, m->define, &e);
        if (!v) continue;
        // Same number already there (maybe written differently, "3.0" vs "3.00"): leave it.
        char tok[32];
        size_t tl = (size_t)(e - v) < 31 ? (size_t)(e - v) : 31;
        memcpy(tok, v, tl);
        tok[tl] = 0;
        if (fabs(atof(tok) - m->value) < 1e-9) continue;
        char buf[32];
        format_value(m, m->value, buf, sizeof(buf));
        size_t pre = (size_t)(v - set), oldLen = (size_t)(e - v), newLen = strlen(buf);
        char *ns = (char *)malloc(n - oldLen + newLen + 1);
        memcpy(ns, set, pre);
        memcpy(ns + pre, buf, newLen);
        memcpy(ns + pre + newLen, e, n - pre - oldLen + 1);
        n = n - oldLen + newLen;
        free(set);
        set = ns;
        changed++;
    }
    if (changed) {
        write_file(MENU_SETTINGS, set, n);
        logf_("menu: wrote %d setting(s)", changed);
    }
    free(set);
}

static void menu_move(int dir)
{
    for (int i = g_moSel + dir; i >= 0 && i < g_nMo; i += dir)
        if (g_mo[i].kind != MO_HEADER) { g_moSel = i; g_menuDirty = 1; return; }
}

static void menu_section(int dir)
{
    int i = g_moSel;
    if (dir > 0) {
        while (i < g_nMo && g_mo[i].kind != MO_HEADER) i++;
    } else {
        while (i >= 0 && g_mo[i].kind != MO_HEADER) i--;   // this section's header
        i--;
        while (i > 0 && g_mo[i].kind != MO_HEADER) i--;    // previous section's header
    }
    for (int k = i < 0 ? 0 : i; k < g_nMo; k++)
        if (g_mo[k].kind != MO_HEADER) { g_moSel = k; g_menuDirty = 1; return; }
}

static void menu_change(int dir, int reset)
{
    if (g_moSel < 0) return;
    MenuOpt *m = &g_mo[g_moSel];
    if (!m->present) return;
    double v = m->value;
    if (reset) {
        v = m->def;
    } else if (m->kind == MO_BOOL || m->kind == MO_PACK) {
        v = v > 0.5 ? 0.0 : 1.0;
    } else if (m->kind == MO_ENUM) {
        int k = 0;
        for (int i = 0; i < m->nEnum; i++) if (fabs(m->enumVal[i] - v) < 1e-6) k = i;
        k += dir;
        if (k < 0) k = 0;
        if (k >= m->nEnum) k = m->nEnum - 1;
        v = m->enumVal[k];
    } else {
        v = m->min + floor((v + dir * m->step - m->min) / m->step + 0.5) * m->step;
        if (v < m->min) v = m->min;
        if (v > m->max) v = m->max;
    }
    if (fabs(v - m->value) < 1e-9) return;
    m->value = v;
    if (m->kind == MO_PACK) {
        g_toggleOff = v < 0.5;
        logf_("shaderpack %s (menu)", g_toggleOff ? "OFF" : "ON");
        InterlockedExchange(&g_reloadRequested, 1);
    } else {
        g_menuWriteAt = now_sec() + 0.35;   // coalesce held keys into one reload
        _snwprintf(g_menuStatus, 96, L"Applying...");
    }
    g_menuDirty = 1;
}

static int menu_key_is_ours(WPARAM vk)
{
    return vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_BACK || vk == VK_PRIOR || vk == VK_NEXT;
}

// Keeps the menu keys away from the game while the menu is open (best effort: input the game
// reads some other way still gets through, which is why the menu polls the keys itself).
static LRESULT CALLBACK menu_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        if (w == VK_HOME || (g_menuOpen && menu_key_is_ours(w))) return 0;
    }
    if (msg == WM_CHAR && g_menuOpen && w == 8) return 0;
    return CallWindowProcW(g_prevWndProc, h, msg, w, l);
}

static DWORD WINAPI menu_input_thread(LPVOID arg)
{
    (void)arg;
    static const int keys[] = { VK_HOME, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_BACK, VK_PRIOR, VK_NEXT };
    enum { NK = sizeof(keys) / sizeof(keys[0]) };
    int down[NK] = {0};
    double next[NK] = {0};
    for (;;) {
        Sleep(15);
        double t = now_sec();
        EnterCriticalSection(&g_menuLock);
        if (g_menuWriteAt > 0.0 && t >= g_menuWriteAt) {
            g_menuWriteAt = 0.0;
            menu_save();
            _snwprintf(g_menuStatus, 96, L"Saved - shaders reloading");
            g_menuDirty = 1;
        }
        LeaveCriticalSection(&g_menuLock);
        int fg = is_game_foreground();
        for (int k = 0; k < NK; k++) {
            int d = fg && (GetAsyncKeyState(keys[k]) & 0x8000) != 0;
            int fire = 0, repeat = 0;
            if (d && !down[k]) { fire = 1; next[k] = t + 0.35; }
            else if (d && k >= 1 && k <= 4 && t >= next[k]) { fire = repeat = 1; next[k] = t + 0.06; }
            down[k] = d;
            if (!fire) continue;
            EnterCriticalSection(&g_menuLock);
            if (keys[k] == VK_HOME) {
                g_menuOpen = !g_menuOpen;
                if (g_menuOpen) { menu_load(); g_menuStatus[0] = 0; }
                g_menuDirty = 1;
            } else if (g_menuOpen) {
                int sliding = g_moSel >= 0 && g_mo[g_moSel].kind == MO_RANGE;
                switch (keys[k]) {
                case VK_UP: menu_move(-1); break;
                case VK_DOWN: menu_move(1); break;
                case VK_LEFT: if (!repeat || sliding) menu_change(-1, 0); break;
                case VK_RIGHT: if (!repeat || sliding) menu_change(1, 0); break;
                case VK_BACK: menu_change(0, 1); break;
                case VK_PRIOR: menu_section(-1); break;
                case VK_NEXT: menu_section(1); break;
                }
            }
            LeaveCriticalSection(&g_menuLock);
        }
    }
    return 0;
}

// ---- Panel (GDI into a top-down 32-bit DIB) ----

static HDC g_ovDC;
static HBITMAP g_ovBmp;
static unsigned char *g_ovBits;
static int g_ovW, g_ovH, g_ovScreenH, g_ovUpload;
static HFONT g_fText, g_fBold, g_fSmall;
static int g_fPx;

static HFONT menu_font(int px, int weight)
{
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void menu_fill(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    RECT r = { x0, y0, x1, y1 };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void menu_text(HDC dc, const wchar_t *s, int x0, int y0, int x1, int y1, HFONT f, COLORREF c, UINT align)
{
    RECT r = { x0, y0, x1, y1 };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void menu_render(int screenH)
{
    double s = screenH / 1080.0;
    if (s < 0.75) s = 0.75;
    int rowH = (int)(30 * s + 0.5), pad = (int)(18 * s + 0.5), W = (int)(600 * s + 0.5);
    int rows = (int)(screenH * 0.70 / rowH) - 4;
    if (rows < 6) rows = 6;
    if (rows > g_nMo) rows = g_nMo;
    if (g_moSel >= 0) {
        if (g_moSel - 1 < g_moTop) g_moTop = g_moSel - 1;          // keep the section header in view
        if (g_moSel >= g_moTop + rows) g_moTop = g_moSel - rows + 1;
    }
    if (g_moTop > g_nMo - rows) g_moTop = g_nMo - rows;
    if (g_moTop < 0) g_moTop = 0;
    int titleH = (int)(44 * s + 0.5), footH = rowH * 2 + pad;
    int H = titleH + rows * rowH + footH;

    if (!g_ovDC) g_ovDC = CreateCompatibleDC(NULL);
    if (W != g_ovW || H != g_ovH || !g_ovBmp) {
        if (g_ovBmp) DeleteObject(g_ovBmp);
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        g_ovBmp = CreateDIBSection(g_ovDC, &bi, DIB_RGB_COLORS, (void **)&g_ovBits, NULL, 0);
        if (!g_ovBmp) { g_ovBits = NULL; return; }
        SelectObject(g_ovDC, g_ovBmp);
        g_ovW = W;
        g_ovH = H;
    }
    int fpx = (int)(17 * s + 0.5);
    if (fpx != g_fPx) {
        if (g_fText) { DeleteObject(g_fText); DeleteObject(g_fBold); DeleteObject(g_fSmall); }
        g_fText = menu_font(fpx, FW_NORMAL);
        g_fBold = menu_font(fpx, FW_SEMIBOLD);
        g_fSmall = menu_font((int)(13.5 * s + 0.5), FW_SEMIBOLD);
        g_fPx = fpx;
    }

    HDC dc = g_ovDC;
    SetBkMode(dc, TRANSPARENT);
    menu_fill(dc, 0, 0, W, H, RGB(15, 19, 28));
    menu_fill(dc, 0, 0, W, titleH, RGB(22, 29, 44));
    menu_text(dc, L"AURORA SHADERS", pad, 0, W - pad, titleH, g_fBold, RGB(150, 190, 255), DT_LEFT);
    menu_text(dc, L"Options", pad, 0, W - pad, titleH, g_fText, RGB(150, 160, 180), DT_RIGHT);

    int y = titleH;
    for (int i = g_moTop; i < g_moTop + rows && i < g_nMo; i++, y += rowH) {
        MenuOpt *m = &g_mo[i];
        if (m->kind == MO_HEADER) {
            wchar_t up[64];
            int k = 0;
            for (; m->label[k] && k < 63; k++) up[k] = (wchar_t)towupper(m->label[k]);
            up[k] = 0;
            menu_text(dc, up, pad, y + rowH / 3, W - pad, y + rowH, g_fSmall, RGB(105, 132, 182), DT_LEFT);
            menu_fill(dc, pad, y + rowH - 1, W - pad, y + rowH, RGB(36, 46, 66));
            continue;
        }
        int sel = i == g_moSel;
        if (sel) {
            menu_fill(dc, pad / 2, y + 2, W - pad / 2, y + rowH - 2, RGB(37, 57, 94));
            menu_fill(dc, pad / 2, y + 2, pad / 2 + (int)(3 * s + 0.5), y + rowH - 2, RGB(120, 170, 255));
        }
        menu_text(dc, m->label, pad + (int)(8 * s), y, W * 6 / 10, y + rowH, sel ? g_fBold : g_fText,
                  m->present ? RGB(222, 228, 238) : RGB(105, 108, 118), DT_LEFT);
        wchar_t val[48], shown[64];
        display_value(m, val, 48);
        if (sel && m->present) _snwprintf(shown, 64, L"\x2039   %ls   \x203A", val);
        else _snwprintf(shown, 64, L"%ls", val);
        shown[63] = 0;
        int modified = m->present && fabs(m->value - m->def) > 1e-9;
        menu_text(dc, shown, W * 6 / 10, y, W - pad, y + rowH, sel ? g_fBold : g_fText,
                  modified ? RGB(255, 196, 112) : RGB(238, 242, 250), DT_RIGHT);
    }
    // Scroll hints.
    if (g_moTop > 0) menu_text(dc, L"\x25B2", 0, titleH, W - pad / 3, titleH + rowH / 2, g_fSmall, RGB(90, 110, 150), DT_RIGHT);
    if (g_moTop + rows < g_nMo) menu_text(dc, L"\x25BC", 0, y - rowH / 2, W - pad / 3, y, g_fSmall, RGB(90, 110, 150), DT_RIGHT);

    menu_fill(dc, pad, y + pad / 3, W - pad, y + pad / 3 + 1, RGB(36, 46, 66));
    y += pad / 2;
    menu_text(dc, L"\x2191\x2193  select      \x2190\x2192  change      PgUp/PgDn  section", pad, y, W - pad, y + rowH,
              g_fSmall, RGB(128, 138, 158), DT_LEFT);
    menu_text(dc, L"Backspace  default      Home  close", pad, y + rowH, W - pad, y + rowH * 2, g_fSmall, RGB(128, 138, 158), DT_LEFT);
    if (g_menuStatus[0]) menu_text(dc, g_menuStatus, pad, y + rowH, W - pad, y + rowH * 2, g_fSmall, RGB(140, 214, 160), DT_RIGHT);
    GdiFlush();

    // Alpha: translucent panel with rounded corners.
    int rad = (int)(12 * s + 0.5);
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            int cx = px < rad ? rad - px : (px >= W - rad ? px - (W - rad - 1) : 0);
            int cy = py < rad ? rad - py : (py >= H - rad ? py - (H - rad - 1) : 0);
            float a = 236.0f;
            if (cx && cy) {
                float d = sqrtf((float)(cx * cx + cy * cy));
                a = d >= rad ? 0.0f : (d > rad - 1 ? a * (rad - d) : a);
            }
            g_ovBits[(py * W + px) * 4 + 3] = (unsigned char)a;
        }
    }
}

// ---- Overlay draw (GL) ----

#define OV_GL_BGRA 0x80E1
#define OV_GL_TRIANGLE_STRIP 0x0005
#define OV_GL_BLEND 0x0BE2
#define OV_GL_DEPTH_TEST 0x0B71
#define OV_GL_CULL_FACE 0x0B44
#define OV_GL_SCISSOR_TEST 0x0C11
#define OV_GL_STENCIL_TEST 0x0B90
#define OV_GL_FRAMEBUFFER_SRGB 0x8DB9
#define OV_GL_RASTERIZER_DISCARD 0x8C89
#define OV_GL_SRC_ALPHA 0x0302
#define OV_GL_ONE_MINUS_SRC_ALPHA 0x0303
#define OV_GL_FUNC_ADD 0x8006
#define OV_GL_DRAW_FRAMEBUFFER 0x8CA9
#define OV_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define OV_GL_VERTEX_ARRAY_BINDING 0x85B5
#define OV_GL_ACTIVE_TEXTURE 0x84E0
#define OV_GL_SAMPLER_BINDING 0x8919
#define OV_GL_VIEWPORT 0x0BA2
#define OV_GL_BLEND_SRC_RGB 0x80C9
#define OV_GL_BLEND_DST_RGB 0x80C8
#define OV_GL_BLEND_SRC_ALPHA 0x80CB
#define OV_GL_BLEND_DST_ALPHA 0x80CA
#define OV_GL_BLEND_EQUATION_RGB 0x8009
#define OV_GL_BLEND_EQUATION_ALPHA 0x883D
#define OV_GL_COLOR_WRITEMASK 0x0C23
#define OV_GL_PIXEL_UNPACK_BUFFER 0x88EC
#define OV_GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define OV_GL_UNPACK_ROW_LENGTH 0x0CF2
#define OV_GL_UNPACK_SKIP_ROWS 0x0CF3
#define OV_GL_UNPACK_SKIP_PIXELS 0x0CF4
#define OV_UNIT 31

typedef void (APIENTRY *OV_GetIntegerv)(GLenum, GLint *);
typedef void (APIENTRY *OV_GetBooleanv)(GLenum, GLboolean *);
typedef GLboolean (APIENTRY *OV_IsEnabled)(GLenum);
typedef void (APIENTRY *OV_Cap)(GLenum);
typedef void (APIENTRY *OV_Viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *OV_GenTextures)(GLsizei, GLuint *);
typedef void (APIENTRY *OV_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (APIENTRY *OV_TexParameteri)(GLenum, GLenum, GLint);
typedef void (APIENTRY *OV_PixelStorei)(GLenum, GLint);
typedef void (APIENTRY *OV_ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef GLuint (APIENTRY *OV_CreateShader)(GLenum);
typedef void (APIENTRY *OV_ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (APIENTRY *OV_CompileShader)(GLuint);
typedef GLuint (APIENTRY *OV_CreateProgram)(void);
typedef void (APIENTRY *OV_AttachShader)(GLuint, GLuint);
typedef void (APIENTRY *OV_LinkProgram)(GLuint);
typedef void (APIENTRY *OV_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *OV_UseProgram)(GLuint);
typedef GLint (APIENTRY *OV_GetUniformLocation)(GLuint, const GLchar *);
typedef void (APIENTRY *OV_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *OV_Uniform1i)(GLint, GLint);
typedef void (APIENTRY *OV_GenVertexArrays)(GLsizei, GLuint *);
typedef void (APIENTRY *OV_BindVertexArray)(GLuint);
typedef void (APIENTRY *OV_BindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *OV_ActiveTexture)(GLenum);
typedef void (APIENTRY *OV_BindSampler)(GLuint, GLuint);
typedef void (APIENTRY *OV_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *OV_BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
typedef void (APIENTRY *OV_BlendEquationSeparate)(GLenum, GLenum);

static struct {
    int state;   // 0 = not tried, 1 = ready, -1 = failed
    GLuint prog, vao, tex;
    GLint locRect, locTex;
    OV_GetIntegerv GetIntegerv;
    OV_GetBooleanv GetBooleanv;
    OV_IsEnabled IsEnabled;
    OV_Cap Enable, Disable;
    OV_Viewport Viewport;
    OV_GenTextures GenTextures;
    OV_TexImage2D TexImage2D;
    OV_TexParameteri TexParameteri;
    OV_PixelStorei PixelStorei;
    OV_ColorMask ColorMask;
    OV_CreateShader CreateShader;
    OV_ShaderSource ShaderSource;
    OV_CompileShader CompileShader;
    OV_CreateProgram CreateProgram;
    OV_AttachShader AttachShader;
    OV_LinkProgram LinkProgram;
    OV_GetProgramiv GetProgramiv;
    OV_UseProgram UseProgram;
    OV_GetUniformLocation GetUniformLocation;
    OV_Uniform4f Uniform4f;
    OV_Uniform1i Uniform1i;
    OV_GenVertexArrays GenVertexArrays;
    OV_BindVertexArray BindVertexArray;
    OV_BindFramebuffer BindFramebuffer;
    OV_ActiveTexture ActiveTexture;
    OV_BindSampler BindSampler;
    OV_BindBuffer BindBuffer;
    OV_BlendFuncSeparate BlendFuncSeparate;
    OV_BlendEquationSeparate BlendEquationSeparate;
} ov;

static int ov_init(void)
{
    if (ov.state) return ov.state > 0;
    ov.state = -1;
#define OV_GL11(field, name) ov.field = (void *)GetProcAddress(g_real, name); if (!ov.field) { logf_("menu: missing %s", name); return 0; }
#define OV_EXT(field, name) ov.field = (void *)real_wglGetProcAddress(name); if (!ov.field) { logf_("menu: missing %s", name); return 0; }
    OV_GL11(GetIntegerv, "glGetIntegerv") OV_GL11(GetBooleanv, "glGetBooleanv") OV_GL11(IsEnabled, "glIsEnabled")
    OV_GL11(Enable, "glEnable") OV_GL11(Disable, "glDisable") OV_GL11(Viewport, "glViewport")
    OV_GL11(GenTextures, "glGenTextures") OV_GL11(TexImage2D, "glTexImage2D") OV_GL11(TexParameteri, "glTexParameteri")
    OV_GL11(PixelStorei, "glPixelStorei") OV_GL11(ColorMask, "glColorMask")
    OV_EXT(CreateShader, "glCreateShader") OV_EXT(ShaderSource, "glShaderSource") OV_EXT(CompileShader, "glCompileShader")
    OV_EXT(CreateProgram, "glCreateProgram") OV_EXT(AttachShader, "glAttachShader") OV_EXT(LinkProgram, "glLinkProgram")
    OV_EXT(GetProgramiv, "glGetProgramiv") OV_EXT(UseProgram, "glUseProgram") OV_EXT(GetUniformLocation, "glGetUniformLocation")
    OV_EXT(Uniform4f, "glUniform4f") OV_EXT(Uniform1i, "glUniform1i") OV_EXT(GenVertexArrays, "glGenVertexArrays")
    OV_EXT(BindVertexArray, "glBindVertexArray") OV_EXT(BindFramebuffer, "glBindFramebuffer")
    OV_EXT(ActiveTexture, "glActiveTexture") OV_EXT(BindSampler, "glBindSampler") OV_EXT(BindBuffer, "glBindBuffer")
    OV_EXT(BlendFuncSeparate, "glBlendFuncSeparate") OV_EXT(BlendEquationSeparate, "glBlendEquationSeparate")
#undef OV_GL11
#undef OV_EXT
    static const char *vs =
        "#version 410 core\n"
        "uniform vec4 uRect;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
        "    vUV = vec2(c.x, 1.0 - c.y);\n"
        "    gl_Position = vec4(mix(uRect.xy, uRect.zw, c), 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "#version 410 core\n"
        "uniform sampler2D uTex;\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "void main() { outColor = texture(uTex, vUV); }\n";
    GLuint v = ov.CreateShader(GL_VERTEX_SHADER), f = ov.CreateShader(GL_FRAGMENT_SHADER);
    ov.ShaderSource(v, 1, &vs, NULL);
    ov.ShaderSource(f, 1, &fs, NULL);
    ov.CompileShader(v);
    ov.CompileShader(f);
    ov.prog = ov.CreateProgram();
    ov.AttachShader(ov.prog, v);
    ov.AttachShader(ov.prog, f);
    ov.LinkProgram(ov.prog);
    GLint ok = 0;
    ov.GetProgramiv(ov.prog, GL_LINK_STATUS, &ok);
    if (!ok) { logf_("menu: overlay program failed to link"); return 0; }
    ov.locRect = ov.GetUniformLocation(ov.prog, "uRect");
    ov.locTex = ov.GetUniformLocation(ov.prog, "uTex");
    ov.GenVertexArrays(1, &ov.vao);
    ov.GenTextures(1, &ov.tex);
    ov.state = 1;
    logf_("menu: overlay ready");
    return 1;
}

static void menu_draw(int sw, int sh)
{
    GLint prog, vao, fbo, active, tex, sampler, vp[4], unpackBuf, rowLen, skipRows, skipPix, align;
    GLint bsRGB, bdRGB, bsA, bdA, beRGB, beA;
    GLboolean cm[4];
    ov.GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    ov.GetIntegerv(OV_GL_VERTEX_ARRAY_BINDING, &vao);
    ov.GetIntegerv(OV_GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    ov.GetIntegerv(OV_GL_ACTIVE_TEXTURE, &active);
    ov.GetIntegerv(OV_GL_VIEWPORT, vp);
    ov.GetIntegerv(OV_GL_BLEND_SRC_RGB, &bsRGB);
    ov.GetIntegerv(OV_GL_BLEND_DST_RGB, &bdRGB);
    ov.GetIntegerv(OV_GL_BLEND_SRC_ALPHA, &bsA);
    ov.GetIntegerv(OV_GL_BLEND_DST_ALPHA, &bdA);
    ov.GetIntegerv(OV_GL_BLEND_EQUATION_RGB, &beRGB);
    ov.GetIntegerv(OV_GL_BLEND_EQUATION_ALPHA, &beA);
    ov.GetBooleanv(OV_GL_COLOR_WRITEMASK, cm);
    static const GLenum caps[] = { OV_GL_BLEND, OV_GL_DEPTH_TEST, OV_GL_CULL_FACE, OV_GL_SCISSOR_TEST, OV_GL_STENCIL_TEST,
                                   OV_GL_FRAMEBUFFER_SRGB, OV_GL_RASTERIZER_DISCARD };
    GLboolean capOn[7];
    for (int i = 0; i < 7; i++) capOn[i] = ov.IsEnabled(caps[i]);

    ov.ActiveTexture(GL_TEXTURE0 + OV_UNIT);
    ov.GetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    ov.GetIntegerv(OV_GL_SAMPLER_BINDING, &sampler);
    r_glBindTexture(GL_TEXTURE_2D, ov.tex);
    ov.BindSampler(OV_UNIT, 0);
    if (g_ovUpload) {
        ov.GetIntegerv(OV_GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuf);
        ov.GetIntegerv(OV_GL_UNPACK_ROW_LENGTH, &rowLen);
        ov.GetIntegerv(OV_GL_UNPACK_SKIP_ROWS, &skipRows);
        ov.GetIntegerv(OV_GL_UNPACK_SKIP_PIXELS, &skipPix);
        ov.GetIntegerv(GL_UNPACK_ALIGNMENT, &align);
        ov.BindBuffer(OV_GL_PIXEL_UNPACK_BUFFER, 0);
        ov.PixelStorei(OV_GL_UNPACK_ROW_LENGTH, 0);
        ov.PixelStorei(OV_GL_UNPACK_SKIP_ROWS, 0);
        ov.PixelStorei(OV_GL_UNPACK_SKIP_PIXELS, 0);
        ov.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_ovW, g_ovH, 0, OV_GL_BGRA, GL_UNSIGNED_BYTE, g_ovBits);
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ov.BindBuffer(OV_GL_PIXEL_UNPACK_BUFFER, (GLuint)unpackBuf);
        ov.PixelStorei(OV_GL_UNPACK_ROW_LENGTH, rowLen);
        ov.PixelStorei(OV_GL_UNPACK_SKIP_ROWS, skipRows);
        ov.PixelStorei(OV_GL_UNPACK_SKIP_PIXELS, skipPix);
        ov.PixelStorei(GL_UNPACK_ALIGNMENT, align);
        g_ovUpload = 0;
    }

    ov.BindFramebuffer(OV_GL_DRAW_FRAMEBUFFER, 0);
    ov.Viewport(0, 0, sw, sh);
    for (int i = 1; i < 7; i++) ov.Disable(caps[i]);
    ov.Enable(OV_GL_BLEND);
    ov.BlendFuncSeparate(OV_GL_SRC_ALPHA, OV_GL_ONE_MINUS_SRC_ALPHA, 1, OV_GL_ONE_MINUS_SRC_ALPHA);
    ov.BlendEquationSeparate(OV_GL_FUNC_ADD, OV_GL_FUNC_ADD);
    ov.ColorMask(1, 1, 1, 1);
    ov.UseProgram(ov.prog);
    double s = sh / 1080.0;
    if (s < 0.75) s = 0.75;
    int px = (int)(40 * s + 0.5), py = (int)(110 * s + 0.5);
    float x0 = -1.0f + 2.0f * px / sw, x1 = -1.0f + 2.0f * (px + g_ovW) / sw;
    float y0 = 1.0f - 2.0f * (py + g_ovH) / sh, y1 = 1.0f - 2.0f * py / sh;
    ov.Uniform4f(ov.locRect, x0, y0, x1, y1);
    ov.Uniform1i(ov.locTex, OV_UNIT);
    ov.BindVertexArray(ov.vao);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);

    // Restore the game's state.
    ov.BindVertexArray((GLuint)vao);
    ov.UseProgram((GLuint)prog);
    r_glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    ov.BindSampler(OV_UNIT, (GLuint)sampler);
    ov.ActiveTexture((GLenum)active);
    ov.BindFramebuffer(OV_GL_DRAW_FRAMEBUFFER, (GLuint)fbo);
    ov.Viewport(vp[0], vp[1], vp[2], vp[3]);
    ov.BlendFuncSeparate((GLenum)bsRGB, (GLenum)bdRGB, (GLenum)bsA, (GLenum)bdA);
    ov.BlendEquationSeparate((GLenum)beRGB, (GLenum)beA);
    ov.ColorMask(cm[0], cm[1], cm[2], cm[3]);
    for (int i = 0; i < 7; i++) { if (capOn[i]) ov.Enable(caps[i]); else ov.Disable(caps[i]); }
}

static void menu_frame(HDC hdc)
{
    if (!g_gameHwnd) {
        HWND w = WindowFromDC(hdc);
        if (w) {
            g_gameHwnd = w;
            g_prevWndProc = (WNDPROC)GetWindowLongPtrW(w, GWLP_WNDPROC);
            SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)menu_wndproc);
            logf_("menu: game window %p, Home opens the options", (void *)w);
        }
    }
    if (!g_menuOpen || !g_gameHwnd) return;
    RECT rc;
    GetClientRect(g_gameHwnd, &rc);
    int sw = rc.right - rc.left, sh = rc.bottom - rc.top;
    if (sw <= 0 || sh <= 0 || !ov_init()) return;
    EnterCriticalSection(&g_menuLock);
    if (g_menuDirty || sh != g_ovScreenH) {
        menu_render(sh);
        g_ovScreenH = sh;
        g_ovUpload = 1;
        g_menuDirty = 0;
    }
    if (g_ovBits) menu_draw(sw, sh);
    LeaveCriticalSection(&g_menuLock);
}

typedef BOOL (WINAPI *PFN_wglSwapBuffers)(HDC);
static PFN_wglSwapBuffers r_wglSwapBuffers;

__declspec(dllexport) BOOL WINAPI wglSwapBuffers(HDC hdc)
{
    menu_frame(hdc);
    return r_wglSwapBuffers(hdc);
}

// ----------------------------------------------------------------------------------------

__declspec(dllexport) PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    PROC p = real_wglGetProcAddress(name);
    if (!p || !name) return p;
#define HOOK(fn) if (!strcmp(name, #fn)) { r_##fn = (PFN_##fn)p; return (PROC)hook_##fn; }
    HOOK(glShaderSource) HOOK(glLinkProgram) HOOK(glDeleteProgram) HOOK(glDeleteShader) HOOK(glUseProgram)
    HOOK(glGetUniformLocation) HOOK(glUniformBlockBinding)
    HOOK(glUniform1f) HOOK(glUniform2f) HOOK(glUniform3f) HOOK(glUniform4f) HOOK(glUniform1i) HOOK(glUniform2i)
    HOOK(glUniform3i) HOOK(glUniform4i) HOOK(glUniform3ui) HOOK(glUniform1fv) HOOK(glUniform2fv) HOOK(glUniform3fv)
    HOOK(glUniform4fv) HOOK(glUniform1iv) HOOK(glUniform1uiv) HOOK(glUniformMatrix2fv) HOOK(glUniformMatrix3fv)
    HOOK(glUniformMatrix4fv) HOOK(glActiveTexture) HOOK(glBindSampler)
    HOOK(glDrawArraysInstanced) HOOK(glDrawElementsInstanced)
#undef HOOK
    if (!strcmp(name, "glBindTexture")) return (PROC)glBindTexture;
    if (!strcmp(name, "glDrawArrays")) return (PROC)glDrawArrays;
    if (!strcmp(name, "glDrawElements")) return (PROC)glDrawElements;
    if (!strcmp(name, "glGetFramebufferAttachmentParameteriv"))
        r_glGetFramebufferAttachmentParameteriv = (PFN_glGetFramebufferAttachmentParameteriv)p;
    return p;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);

        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        wcscat(sys, L"\\opengl32.dll");
        g_real = LoadLibraryW(sys);
        if (!g_real) return FALSE;
        real_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(g_real, "wglGetProcAddress");
        r_glBindTexture = (PFN_glBindTexture)GetProcAddress(g_real, "glBindTexture");
        r_glDrawArrays = (PFN_glDrawArrays)GetProcAddress(g_real, "glDrawArrays");
        r_glDrawElements = (PFN_glDrawElements)GetProcAddress(g_real, "glDrawElements");
        r_wglSwapBuffers = (PFN_wglSwapBuffers)GetProcAddress(g_real, "wglSwapBuffers");
        int missing = stubs_resolve(g_real);

        GetModuleFileNameW(inst, g_packDir, MAX_PATH);
        wchar_t *slash = wcsrchr(g_packDir, L'\\');
        if (slash) *slash = 0;
        wcscat(g_packDir, L"\\shaderpack");

        wchar_t path[MAX_PATH];
        pack_path(path, "hook.log");
        g_log = _wfopen(path, L"w");
        pack_path(path, "dump");
        g_dump = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
        pack_path(path, "errors");
        CreateDirectoryW(path, NULL);
        logf_("hytale shader hook loaded, missing stubs=%d dump=%d", missing, g_dump);
        g_enabled = !pack_flag("programs\\_disabled");
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
        load_index();
        load_shares();
        load_textures_cfg();
        load_forward_cfg();
        CreateThread(NULL, 0, watcher_thread, NULL, 0, NULL);
        InitializeCriticalSection(&g_menuLock);
        CreateThread(NULL, 0, menu_input_thread, NULL, 0, NULL);
    }
    return TRUE;
}
