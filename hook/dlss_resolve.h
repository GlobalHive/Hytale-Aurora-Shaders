static GLuint g_dlssInputVAO;
static GLenum g_dlssInputMode;
static GLint g_dlssInputFirst;
static GLsizei g_dlssInputCount;
static GLint g_dlssInputViewport[4];
static int g_dlssDiagnosticFrames;

static void dlss_set_viewport(GLuint program, const GLint *viewport)
{
    GLfloat bounds[] = {(GLfloat)viewport[0], (GLfloat)viewport[1], (GLfloat)viewport[2], (GLfloat)viewport[3]};
    r_glUniform4fv(r_glGetUniformLocation(program, "uHookDlssViewport"), 1, bounds);
}

typedef struct { GLint texture, level, layered, layer, access, format; int valid; } DLSSMaskState;
static DLSSMaskState dlss_mask_begin(void)
{
    typedef void (APIENTRY *GetIndexed)(GLenum, GLuint, GLint *);
    GetIndexed getIndexed = (GetIndexed)dlss_getproc("glGetIntegeri_v");
    DLSSMaskState state = {0};
    if (!getIndexed) return state;
    getIndexed(0x8f3a, 0, &state.texture);
    getIndexed(0x8f3b, 0, &state.level);
    getIndexed(0x8f3c, 0, &state.layered);
    getIndexed(0x8f3d, 0, &state.layer);
    getIndexed(0x8f3e, 0, &state.access);
    getIndexed(0x906e, 0, &state.format);
    state.valid = 1;
    r_glBindImageTexture(0, aud_texture(AUD_TEX_RESPONSIVE), 0, 0, 0, GL_WRITE_ONLY, 0x8229);
    return state;
}
static void dlss_mask_end(DLSSMaskState state)
{
    if (state.valid) r_glBindImageTexture(0, (GLuint)state.texture, state.level, (GLboolean)state.layered,
        state.layer, (GLenum)(state.access ? state.access : GL_READ_WRITE), (GLenum)(state.format ? state.format : GL_RGBA8));
}

static int dlss_resolve(GLuint texture, const GLint *viewport)
{
    typedef void (APIENTRY *BindVAO)(GLuint);
    typedef void (APIENTRY *SetViewport)(GLint, GLint, GLsizei, GLsizei);
    typedef void (APIENTRY *GetIndexed)(GLenum, GLuint, GLint *);
    typedef void (APIENTRY *GetBoolean)(GLenum, GLboolean *);
    typedef void (APIENTRY *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    BindVAO bindVAO = (BindVAO)dlss_getproc("glBindVertexArray");
    SetViewport setViewport = (SetViewport)dlss_getproc("glViewport");
    GetIndexed getIndexed = (GetIndexed)dlss_getproc("glGetIntegeri_v");
    GetBoolean getBoolean = (GetBoolean)dlss_getproc("glGetBooleanv");
    ColorMask colorMask = (ColorMask)dlss_getproc("glColorMask");
    GLint resolve = r_glGetUniformLocation(g_dlssProg, "uHookDlssResolve");
    if (!bindVAO || !setViewport || !getBoolean || !colorMask || !g_dlssInputVAO || resolve < 0 || g_dlssInputCount <= 0) return 0;
    GLint previousProgram, previousVAO, previousActive, previousTexture, previousSampler = 0, previousViewport[4];
    r_glGetIntegerv(0x8b8d, &previousProgram);
    r_glGetIntegerv(0x85b5, &previousVAO);
    r_glGetIntegerv(0x84e0, &previousActive);
    r_glGetIntegerv(GL_VIEWPORT, previousViewport);
    r_glActiveTexture(GL_TEXTURE0 + 20);
    r_glGetIntegerv(0x8069, &previousTexture);
    if (getIndexed && r_glBindSampler) { getIndexed(0x8919, 20, &previousSampler); r_glBindSampler(20, 0); }
    GLboolean previousMask[4];
    getBoolean(0x0c23, previousMask);
    colorMask(1, 1, 1, 1);
    GLenum capabilities[] = {0x0be2, 0x0b71, 0x0b44, 0x0b90, 0x8db9, 0x8c89};
    GLboolean enabled[6];
    for (int index = 0; index < 6; index++) {
        enabled[index] = dl.IsEnabled(capabilities[index]);
        if (enabled[index]) dl.Disable(capabilities[index]);
    }
    r_glUseProgram(g_dlssProg);
    r_glBindTexture(GL_TEXTURE_2D, texture);
    r_glUniform1i(r_glGetUniformLocation(g_dlssProg, "uHookDlssOutput"), 20);
    r_glUniform1f(r_glGetUniformLocation(g_dlssProg, "uHookDlssSharpness"), (float)g_setDlssSharpness);
    dlss_set_viewport(g_dlssProg, viewport);
    r_glUniform1i(resolve, 1);
    bindVAO(g_dlssInputVAO);
    setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    r_glDrawArrays(g_dlssInputMode, g_dlssInputFirst, g_dlssInputCount);
    r_glUniform1i(resolve, 0);
    setViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    bindVAO((GLuint)previousVAO);
    r_glUseProgram((GLuint)previousProgram);
    r_glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    if (getIndexed && r_glBindSampler) r_glBindSampler(20, (GLuint)previousSampler);
    r_glActiveTexture((GLenum)previousActive);
    colorMask(previousMask[0], previousMask[1], previousMask[2], previousMask[3]);
    for (int index = 0; index < 6; index++) if (enabled[index]) dl.Enable(capabilities[index]);
    return 1;
}

static void dlss_diagnostic_errors(const char *stage)
{
    PFN_glGetError getError = (PFN_glGetError)dlss_getproc("glGetError");
    if (!getError) return;
    GLenum error = 0;
    int count = 0;
    while (count++ < 16 && (error = getError()) != 0) logf_("dlss diagnostic: %s GL error 0x%x", stage, error);
}

static void dlss_diagnose(const GLint *viewport)
{
    typedef void (APIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    typedef void (APIENTRY *PixelStore)(GLenum, GLint);
    typedef GLenum (APIENTRY *FramebufferStatus)(GLenum);
    typedef void (APIENTRY *DeleteFramebuffers)(GLsizei, const GLuint *);
    typedef void (APIENTRY *GetUniform)(GLuint, GLint, GLfloat *);
    ReadPixels readPixels = (ReadPixels)dlss_getproc("glReadPixels");
    PixelStore pixelStore = (PixelStore)dlss_getproc("glPixelStorei");
    FramebufferStatus framebufferStatus = (FramebufferStatus)dlss_getproc("glCheckFramebufferStatus");
    DeleteFramebuffers deleteFramebuffers = (DeleteFramebuffers)dlss_getproc("glDeleteFramebuffers");
    PFN_glBindBuffer bindBuffer = (PFN_glBindBuffer)dlss_getproc("glBindBuffer");
    GetUniform getUniform = (GetUniform)dlss_getproc("glGetUniformfv");
    if (!readPixels || !pixelStore || !framebufferStatus || !deleteFramebuffers || !bindBuffer || !getUniform) return;
    GLfloat pixelSize[2] = {0};
    GLint pixelSizeLocation = r_glGetUniformLocation(g_dlssProg, "uPixelSize");
    if (pixelSizeLocation >= 0) getUniform(g_dlssProg, pixelSizeLocation, pixelSize);
    logf_("dlss diagnostic: capture program=%u VAO=%u mode=0x%x first=%d count=%d viewport=%d,%d %dx%d pixelSize=%.9g,%.9g; output=%d,%d %dx%d RR=%d reset=%d",
        g_dlssProg, g_dlssInputVAO, g_dlssInputMode, g_dlssInputFirst, g_dlssInputCount,
        g_dlssInputViewport[0], g_dlssInputViewport[1], g_dlssInputViewport[2], g_dlssInputViewport[3], pixelSize[0], pixelSize[1],
        viewport[0], viewport[1], viewport[2], viewport[3], g_dlssRR, g_dlssReset);
    GLint previousFramebuffer = 0, previousBuffer = 0;
    r_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &previousFramebuffer);
    r_glGetIntegerv(0x88ed, &previousBuffer);
    bindBuffer(0x88eb, 0);
    const GLenum packNames[] = {0x0d05, 0x0d02, 0x0d04, 0x0d03, 0x806c, 0x806b, 0x0d00, 0x0d01};
    GLint packValues[8];
    for (int index = 0; index < 8; index++) {
        r_glGetIntegerv(packNames[index], &packValues[index]);
        pixelStore(packNames[index], index == 0 ? 1 : 0);
    }
    GLuint framebuffer = 0; dl.GenFb(1, &framebuffer); dl.BindFb(GL_READ_FRAMEBUFFER_, framebuffer);
    const int images[] = {AUD_TEX_COLOR, AUD_TEX_DEPTH, AUD_TEX_MOTION, AUD_TEX_OUTPUT};
    const char *names[] = {"input HDR", "depth", "motion", "output HDR"};
    for (int image = 0; image < 4; image++) {
        dl.FbTex(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D, aud_texture(images[image]), 0);
        GLenum status = framebufferStatus(GL_READ_FRAMEBUFFER_);
        if (status != 0x8cd5) { logf_("dlss diagnostic: %s framebuffer incomplete 0x%x", names[image], status); continue; }
        int width = image == 3 ? g_dlssOW : g_dlssW, height = image == 3 ? g_dlssOH : g_dlssH;
        for (int sample = 0; sample < 5; sample++) {
            int horizontal = sample == 0 ? 2 : (sample - 1) % 2 ? 3 : 1;
            int vertical = sample == 0 ? 2 : sample <= 2 ? 1 : 3;
            GLfloat value[4] = {0};
            readPixels(width * horizontal / 4, height * vertical / 4, 1, 1, 0x1908, 0x1406, value);
            logf_("dlss diagnostic: %s sample%d=(%.6g %.6g %.6g %.6g)", names[image], sample, value[0], value[1], value[2], value[3]);
        }
    }
    dl.BindFb(GL_READ_FRAMEBUFFER_, (GLuint)previousFramebuffer); deleteFramebuffers(1, &framebuffer);
    for (int index = 0; index < 8; index++) pixelStore(packNames[index], packValues[index]);
    bindBuffer(0x88eb, (GLuint)previousBuffer);
    dlss_diagnostic_errors("pixel readback");
}
