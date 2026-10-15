static GLuint g_dlssSharpenProgram, g_dlssSharpenVAO;
static int g_dlssSharpenTried;

static int dlss_sharpen(GLuint texture, const GLint *viewport, float strength)
{
    typedef void (APIENTRY *BindVAO)(GLuint);
    typedef void (APIENTRY *GenVAO)(GLsizei, GLuint *);
    typedef void (APIENTRY *SetViewport)(GLint, GLint, GLsizei, GLsizei);
    typedef GLuint (APIENTRY *CreateProgram)(void);
    static BindVAO bindVAO;
    static GenVAO genVAO;
    static SetViewport setViewport;
    if (strength <= 0.0f) return 0;
    if (!g_dlssSharpenTried) {
        g_dlssSharpenTried = 1;
        bindVAO = (BindVAO)dlss_getproc("glBindVertexArray");
        genVAO = (GenVAO)dlss_getproc("glGenVertexArrays");
        setViewport = (SetViewport)dlss_getproc("glViewport");
        if (!bindVAO || !genVAO || !setViewport) return 0;
        const char *sources[2] = {
            "#version 410 core\nout vec2 uv;void main(){vec2 pos=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=pos;gl_Position=vec4(pos*2.0-1.0,0,1);}",
            "#version 410 core\nin vec2 uv;out vec4 color;uniform sampler2D scene;uniform vec2 pixel;uniform float strength;"
            "void main(){vec3 center=texture(scene,uv).rgb;vec3 north=texture(scene,uv+vec2(0,pixel.y)).rgb;"
            "vec3 south=texture(scene,uv-vec2(0,pixel.y)).rgb;vec3 east=texture(scene,uv+vec2(pixel.x,0)).rgb;"
            "vec3 west=texture(scene,uv-vec2(pixel.x,0)).rgb;vec3 low=min(center,min(min(north,south),min(east,west)));"
            "vec3 high=max(center,max(max(north,south),max(east,west)));vec3 detail=center-(north+south+east+west)*0.25;"
            "vec3 result=clamp(center+detail*strength*2.0,low,high);color=vec4(result,1);}"
        };
        GLuint shaders[2] = {0};
        CreateProgram createProgram = (CreateProgram)dlss_getproc("glCreateProgram");
        if (!createProgram) return 0;
        g_dlssSharpenProgram = createProgram();
        for (int index = 0; index < 2; index++) {
            shaders[index] = r_glCreateShader(index ? 0x8b30 : 0x8b31);
            r_glShaderSource(shaders[index], 1, &sources[index], NULL);
            r_glCompileShader(shaders[index]);
            r_glAttachShader(g_dlssSharpenProgram, shaders[index]);
        }
        r_glLinkProgram(g_dlssSharpenProgram);
        GLint linked = 0;
        r_glGetProgramiv(g_dlssSharpenProgram, 0x8b82, &linked);
        for (int index = 0; index < 2; index++) r_glDeleteShader(shaders[index]);
        if (!linked) {
            logf_("dlss: sharpening shader failed to link; retaining unsharpened presentation");
            r_glDeleteProgram(g_dlssSharpenProgram);
            g_dlssSharpenProgram = 0;
            return 0;
        }
        genVAO(1, &g_dlssSharpenVAO);
    }
    if (!g_dlssSharpenProgram) return 0;
    GLint previousProgram, previousVAO, previousActive, previousTexture, previousViewport[4];
    r_glGetIntegerv(0x8b8d, &previousProgram);
    r_glGetIntegerv(0x85b5, &previousVAO);
    r_glGetIntegerv(0x84e0, &previousActive);
    r_glGetIntegerv(GL_VIEWPORT, previousViewport);
    r_glActiveTexture(GL_TEXTURE0);
    r_glGetIntegerv(0x8069, &previousTexture);
    typedef void (APIENTRY *GetIndexed)(GLenum, GLuint, GLint *);
    GetIndexed getIndexed = (GetIndexed)dlss_getproc("glGetIntegeri_v");
    GLint previousSampler = 0;
    if (getIndexed && r_glBindSampler) { getIndexed(0x8919, 0, &previousSampler); r_glBindSampler(0, 0); }
    GLenum capabilities[] = {0x0be2, 0x0b71, 0x0b44, 0x0b90, 0x8db9};
    GLboolean enabled[5];
    for (int index = 0; index < 5; index++) {
        enabled[index] = dl.IsEnabled(capabilities[index]);
        if (enabled[index]) dl.Disable(capabilities[index]);
    }
    r_glUseProgram(g_dlssSharpenProgram);
    r_glBindTexture(GL_TEXTURE_2D, texture);
    r_glUniform1i(r_glGetUniformLocation(g_dlssSharpenProgram, "scene"), 0);
    r_glUniform2f(r_glGetUniformLocation(g_dlssSharpenProgram, "pixel"), 1.0f / g_dlssOW, 1.0f / g_dlssOH);
    r_glUniform1f(r_glGetUniformLocation(g_dlssSharpenProgram, "strength"), strength);
    bindVAO(g_dlssSharpenVAO);
    setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    r_glDrawArrays(0x0004, 0, 3);
    setViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    bindVAO((GLuint)previousVAO);
    r_glUseProgram((GLuint)previousProgram);
    r_glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    if (getIndexed && r_glBindSampler) r_glBindSampler(0, (GLuint)previousSampler);
    r_glActiveTexture((GLenum)previousActive);
    for (int index = 0; index < 5; index++) if (enabled[index]) dl.Enable(capabilities[index]);
    return 1;
}
