// Live reload keeps the game's shader objects. The game flags its shaders for deletion right
// after attaching them and later links the same objects into new programs (when a world loads).
// A reload that swapped in fresh objects freed the game's, the driver handed their names to other
// passes' shaders, and the world loaded black after a settings change (issue #12).
static void reload_test(void)
{
    InitializeCriticalSection(&g_lock);
    resolve_helpers();
    GLboolean (APIENTRY *isShader)(GLuint) = (void *)real_wglGetProcAddress("glIsShader");
    GLuint (APIENTRY *createProgram)(void) = (void *)real_wglGetProcAddress("glCreateProgram");
    assert(isShader && createProgram);
    const char *sources[2] = {
        "#version 410 core\nvoid main(){vec2 c=vec2(gl_VertexID&1,gl_VertexID>>1);gl_Position=vec4(c*2.-1.,0,1);}\n",
        "#version 410 core\nuniform vec4 testColour;out vec4 outColor;void main(){outColor=testColour;}\n"};
    // The game: compile, attach, flag for deletion, link.
    GLuint shaders[2] = {r_glCreateShader(GL_VERTEX_SHADER), r_glCreateShader(GL_FRAGMENT_SHADER)};
    GLuint first = createProgram();
    for (int stage = 0; stage < 2; stage++) {
        hook_glShaderSource(shaders[stage], 1, &sources[stage], NULL);
        r_glCompileShader(shaders[stage]); r_glAttachShader(first, shaders[stage]); r_glDeleteShader(shaders[stage]);
    }
    hook_glLinkProgram(first);
    ProgRec *program = prog_rec(first, 0);
    assert(program && program->linked && program->nsh == 2);
    // A settings change: the pack's source for both stages differs from what is compiled.
    for (int stage = 0; stage < 2; stage++) { program->sh[stage].curHash ^= 1; shader_rec(shaders[stage], 0)->curHash ^= 1; }
    assert(relink_program(first, program, 1) == 2);
    GLuint other = compile_new(GL_FRAGMENT_SHADER, sources[1], "reload test");
    assert(other && isShader(shaders[0]) && isShader(shaders[1]) && other != shaders[0] && other != shaders[1]);
    for (int stage = 0; stage < 2; stage++) assert(shader_rec(shaders[stage], 0)->curHash == program->sh[stage].curHash);
    // A world loads: the game links its objects into a new program, which must draw.
    GLuint second = createProgram();
    for (int stage = 0; stage < 2; stage++) r_glAttachShader(second, shaders[stage]);
    hook_glLinkProgram(second);
    GLint linked = 0; r_glGetProgramiv(second, GL_LINK_STATUS, &linked); assert(linked);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    r_glUseProgram(second);
    const float colour[4] = {.25f, .5f, .75f, 1};
    r_glUniform4fv(r_glGetUniformLocation(second, "testColour"), 1, colour);
    ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 4, 4);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    float pixel[4]; readPixels(1, 1, 1, 1, GL_RGBA, GL_FLOAT, pixel);
    for (int channel = 0; channel < 4; channel++) assert(fabsf(pixel[channel] - colour[channel]) < 1e-4f);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); r_glDeleteShader(other);
    hook_glDeleteProgram(second); hook_glDeleteProgram(first);
    bindFramebuffer(0x8d40, 0); assert(!getError());
    DeleteCriticalSection(&g_lock);
    puts("PASS: live reload recompiles the game's own shader objects; a program linked from them afterwards draws");
}
