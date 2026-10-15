static GLuint g_dlssInputVAO;
static GLenum g_dlssInputMode;
static GLint g_dlssInputFirst;
static GLsizei g_dlssInputCount;

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
    BindVAO bindVAO = (BindVAO)dlss_getproc("glBindVertexArray");
    SetViewport setViewport = (SetViewport)dlss_getproc("glViewport");
    GetIndexed getIndexed = (GetIndexed)dlss_getproc("glGetIntegeri_v");
    GLint resolve = r_glGetUniformLocation(g_dlssProg, "uHookDlssResolve");
    if (!bindVAO || !setViewport || !g_dlssInputVAO || resolve < 0 || g_dlssInputCount <= 0) return 0;
    GLint previousProgram, previousVAO, previousActive, previousTexture, previousSampler = 0, previousViewport[4];
    r_glGetIntegerv(0x8b8d, &previousProgram);
    r_glGetIntegerv(0x85b5, &previousVAO);
    r_glGetIntegerv(0x84e0, &previousActive);
    r_glGetIntegerv(GL_VIEWPORT, previousViewport);
    r_glActiveTexture(GL_TEXTURE0 + 20);
    r_glGetIntegerv(0x8069, &previousTexture);
    if (getIndexed && r_glBindSampler) { getIndexed(0x8919, 20, &previousSampler); r_glBindSampler(20, 0); }
    GLenum capabilities[] = {0x0be2, 0x0b71, 0x0b44, 0x0b90, 0x8db9};
    GLboolean enabled[5];
    for (int index = 0; index < 5; index++) {
        enabled[index] = dl.IsEnabled(capabilities[index]);
        if (enabled[index]) dl.Disable(capabilities[index]);
    }
    r_glUseProgram(g_dlssProg);
    r_glBindTexture(GL_TEXTURE_2D, texture);
    r_glUniform1i(r_glGetUniformLocation(g_dlssProg, "uHookDlssOutput"), 20);
    r_glUniform1f(r_glGetUniformLocation(g_dlssProg, "uHookDlssSharpness"), (float)g_setDlssSharpness);
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
    for (int index = 0; index < 5; index++) if (enabled[index]) dl.Enable(capabilities[index]);
    return 1;
}
