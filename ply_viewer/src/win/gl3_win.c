#include "gl3_win.h"

#include <stdio.h>

PFNGLACTIVETEXTUREPROC ply_glActiveTexture = NULL;
PFNGLATTACHSHADERPROC ply_glAttachShader = NULL;
PFNGLBINDBUFFERPROC ply_glBindBuffer = NULL;
PFNGLBINDFRAMEBUFFERPROC ply_glBindFramebuffer = NULL;
PFNGLBINDVERTEXARRAYPROC ply_glBindVertexArray = NULL;
PFNGLBUFFERDATAPROC ply_glBufferData = NULL;
PFNGLBUFFERSUBDATAPROC ply_glBufferSubData = NULL;
PFNGLCOMPILESHADERPROC ply_glCompileShader = NULL;
PFNGLCREATEPROGRAMPROC ply_glCreateProgram = NULL;
PFNGLCREATESHADERPROC ply_glCreateShader = NULL;
PFNGLDELETEPROGRAMPROC ply_glDeleteProgram = NULL;
PFNGLDELETESHADERPROC ply_glDeleteShader = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC ply_glDrawArraysInstanced = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC ply_glEnableVertexAttribArray = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC ply_glFramebufferTexture2D = NULL;
PFNGLGENBUFFERSPROC ply_glGenBuffers = NULL;
PFNGLGENFRAMEBUFFERSPROC ply_glGenFramebuffers = NULL;
PFNGLGENVERTEXARRAYSPROC ply_glGenVertexArrays = NULL;
PFNGLGETPROGRAMINFOLOGPROC ply_glGetProgramInfoLog = NULL;
PFNGLGETPROGRAMIVPROC ply_glGetProgramiv = NULL;
PFNGLGETSHADERINFOLOGPROC ply_glGetShaderInfoLog = NULL;
PFNGLGETSHADERIVPROC ply_glGetShaderiv = NULL;
PFNGLGETUNIFORMLOCATIONPROC ply_glGetUniformLocation = NULL;
PFNGLLINKPROGRAMPROC ply_glLinkProgram = NULL;
PFNGLSHADERSOURCEPROC ply_glShaderSource = NULL;
PFNGLTEXBUFFERPROC ply_glTexBuffer = NULL;
PFNGLUNIFORM1FPROC ply_glUniform1f = NULL;
PFNGLUNIFORM1IPROC ply_glUniform1i = NULL;
PFNGLUNIFORM2FPROC ply_glUniform2f = NULL;
PFNGLUNIFORM3FPROC ply_glUniform3f = NULL;
PFNGLUNIFORMMATRIX4FVPROC ply_glUniformMatrix4fv = NULL;
PFNGLUSEPROGRAMPROC ply_glUseProgram = NULL;
PFNGLVERTEXATTRIBDIVISORPROC ply_glVertexAttribDivisor = NULL;
PFNGLVERTEXATTRIBIPOINTERPROC ply_glVertexAttribIPointer = NULL;
PFNGLVERTEXATTRIBPOINTERPROC ply_glVertexAttribPointer = NULL;
PFNGLDELETEBUFFERSPROC ply_glDeleteBuffers = NULL;
PFNGLDELETEVERTEXARRAYSPROC ply_glDeleteVertexArrays = NULL;
PFNGLDELETEFRAMEBUFFERSPROC ply_glDeleteFramebuffers = NULL;

static HMODULE glLib = NULL;

static void* gl_sym(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == NULL || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        if (!glLib) glLib = LoadLibraryA("opengl32.dll");
        p = glLib ? (void*)GetProcAddress(glLib, name) : NULL;
    }
    return p;
}

int ply_gl_load_win(void) {
    int missing = 0;
    ply_glActiveTexture = (PFNGLACTIVETEXTUREPROC)gl_sym("glActiveTexture"); if (!ply_glActiveTexture) missing++;
    ply_glAttachShader = (PFNGLATTACHSHADERPROC)gl_sym("glAttachShader"); if (!ply_glAttachShader) missing++;
    ply_glBindBuffer = (PFNGLBINDBUFFERPROC)gl_sym("glBindBuffer"); if (!ply_glBindBuffer) missing++;
    ply_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)gl_sym("glBindFramebuffer"); if (!ply_glBindFramebuffer) missing++;
    ply_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)gl_sym("glBindVertexArray"); if (!ply_glBindVertexArray) missing++;
    ply_glBufferData = (PFNGLBUFFERDATAPROC)gl_sym("glBufferData"); if (!ply_glBufferData) missing++;
    ply_glBufferSubData = (PFNGLBUFFERSUBDATAPROC)gl_sym("glBufferSubData"); if (!ply_glBufferSubData) missing++;
    ply_glCompileShader = (PFNGLCOMPILESHADERPROC)gl_sym("glCompileShader"); if (!ply_glCompileShader) missing++;
    ply_glCreateProgram = (PFNGLCREATEPROGRAMPROC)gl_sym("glCreateProgram"); if (!ply_glCreateProgram) missing++;
    ply_glCreateShader = (PFNGLCREATESHADERPROC)gl_sym("glCreateShader"); if (!ply_glCreateShader) missing++;
    ply_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)gl_sym("glDeleteProgram"); if (!ply_glDeleteProgram) missing++;
    ply_glDeleteShader = (PFNGLDELETESHADERPROC)gl_sym("glDeleteShader"); if (!ply_glDeleteShader) missing++;
    ply_glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)gl_sym("glDrawArraysInstanced"); if (!ply_glDrawArraysInstanced) missing++;
    ply_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)gl_sym("glEnableVertexAttribArray"); if (!ply_glEnableVertexAttribArray) missing++;
    ply_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)gl_sym("glFramebufferTexture2D"); if (!ply_glFramebufferTexture2D) missing++;
    ply_glGenBuffers = (PFNGLGENBUFFERSPROC)gl_sym("glGenBuffers"); if (!ply_glGenBuffers) missing++;
    ply_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)gl_sym("glGenFramebuffers"); if (!ply_glGenFramebuffers) missing++;
    ply_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)gl_sym("glGenVertexArrays"); if (!ply_glGenVertexArrays) missing++;
    ply_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)gl_sym("glGetProgramInfoLog"); if (!ply_glGetProgramInfoLog) missing++;
    ply_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)gl_sym("glGetProgramiv"); if (!ply_glGetProgramiv) missing++;
    ply_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)gl_sym("glGetShaderInfoLog"); if (!ply_glGetShaderInfoLog) missing++;
    ply_glGetShaderiv = (PFNGLGETSHADERIVPROC)gl_sym("glGetShaderiv"); if (!ply_glGetShaderiv) missing++;
    ply_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)gl_sym("glGetUniformLocation"); if (!ply_glGetUniformLocation) missing++;
    ply_glLinkProgram = (PFNGLLINKPROGRAMPROC)gl_sym("glLinkProgram"); if (!ply_glLinkProgram) missing++;
    ply_glShaderSource = (PFNGLSHADERSOURCEPROC)gl_sym("glShaderSource"); if (!ply_glShaderSource) missing++;
    ply_glTexBuffer = (PFNGLTEXBUFFERPROC)gl_sym("glTexBuffer"); if (!ply_glTexBuffer) missing++;
    ply_glUniform1f = (PFNGLUNIFORM1FPROC)gl_sym("glUniform1f"); if (!ply_glUniform1f) missing++;
    ply_glUniform1i = (PFNGLUNIFORM1IPROC)gl_sym("glUniform1i"); if (!ply_glUniform1i) missing++;
    ply_glUniform2f = (PFNGLUNIFORM2FPROC)gl_sym("glUniform2f"); if (!ply_glUniform2f) missing++;
    ply_glUniform3f = (PFNGLUNIFORM3FPROC)gl_sym("glUniform3f"); if (!ply_glUniform3f) missing++;
    ply_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)gl_sym("glUniformMatrix4fv"); if (!ply_glUniformMatrix4fv) missing++;
    ply_glUseProgram = (PFNGLUSEPROGRAMPROC)gl_sym("glUseProgram"); if (!ply_glUseProgram) missing++;
    ply_glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)gl_sym("glVertexAttribDivisor"); if (!ply_glVertexAttribDivisor) missing++;
    ply_glVertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)gl_sym("glVertexAttribIPointer"); if (!ply_glVertexAttribIPointer) missing++;
    ply_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)gl_sym("glVertexAttribPointer"); if (!ply_glVertexAttribPointer) missing++;
    ply_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)gl_sym("glDeleteBuffers"); if (!ply_glDeleteBuffers) missing++;
    ply_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)gl_sym("glDeleteVertexArrays"); if (!ply_glDeleteVertexArrays) missing++;
    ply_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)gl_sym("glDeleteFramebuffers"); if (!ply_glDeleteFramebuffers) missing++;
    return missing == 0;
}

