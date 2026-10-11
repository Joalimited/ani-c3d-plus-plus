// Minimal OpenGL 3.3 core loader on top of SDL_GL_GetProcAddress (works the same on Linux, Windows and macOS).
#pragma once
#include <SDL.h>
#ifdef ANI_GLES
// OpenGL ES 3.0 (Android / Fire TV, or a desktop test build against Mesa's GLES): every function used is core in ES 3.0
// and linked directly, so there is nothing to load.
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#else
#include <SDL_opengl.h>
#endif

#ifdef ANI_GLES
#define ANI_GL_FUNCS(X)
#else
#define ANI_GL_FUNCS(X)                                                              \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                         \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                         \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                       \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                           \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                 \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                         \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                       \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                         \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                           \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                         \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                               \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                             \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                             \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                 \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                             \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                               \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                   \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                   \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                             \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                             \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                       \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                           \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                   \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)                           \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)                           \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                     \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                       \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                   \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                             \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                   \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                   \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                 \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                           \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                 \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample)     \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                           \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                   \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                     \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)

#define ANI_GL_DECLARE(type, name) extern type name##_p;
ANI_GL_FUNCS(ANI_GL_DECLARE)
#undef ANI_GL_DECLARE

// route the names through our pointers (the 1.1 functions come straight from the GL library)
#define glCreateShader glCreateShader_p
#define glShaderSource glShaderSource_p
#define glCompileShader glCompileShader_p
#define glGetShaderiv glGetShaderiv_p
#define glGetShaderInfoLog glGetShaderInfoLog_p
#define glDeleteShader glDeleteShader_p
#define glCreateProgram glCreateProgram_p
#define glAttachShader glAttachShader_p
#define glLinkProgram glLinkProgram_p
#define glGetProgramiv glGetProgramiv_p
#define glGetProgramInfoLog glGetProgramInfoLog_p
#define glUseProgram glUseProgram_p
#define glGetUniformLocation glGetUniformLocation_p
#define glUniformMatrix4fv glUniformMatrix4fv_p
#define glUniform4fv glUniform4fv_p
#define glUniform1i glUniform1i_p
#define glGenVertexArrays glGenVertexArrays_p
#define glBindVertexArray glBindVertexArray_p
#define glGenBuffers glGenBuffers_p
#define glBindBuffer glBindBuffer_p
#define glBufferData glBufferData_p
#define glBufferSubData glBufferSubData_p
#define glVertexAttribPointer glVertexAttribPointer_p
#define glEnableVertexAttribArray glEnableVertexAttribArray_p
#define glVertexAttribDivisor glVertexAttribDivisor_p
#define glDrawArraysInstanced glDrawArraysInstanced_p
#define glGenerateMipmap glGenerateMipmap_p
#define glActiveTexture glActiveTexture_p
#define glGenFramebuffers glGenFramebuffers_p
#define glDeleteFramebuffers glDeleteFramebuffers_p
#define glBindFramebuffer glBindFramebuffer_p
#define glFramebufferRenderbuffer glFramebufferRenderbuffer_p
#define glGenRenderbuffers glGenRenderbuffers_p
#define glDeleteRenderbuffers glDeleteRenderbuffers_p
#define glBindRenderbuffer glBindRenderbuffer_p
#define glRenderbufferStorageMultisample glRenderbufferStorageMultisample_p
#define glRenderbufferStorage glRenderbufferStorage_p
#define glBlitFramebuffer glBlitFramebuffer_p
#define glCheckFramebufferStatus glCheckFramebufferStatus_p
#define glBlendFuncSeparate glBlendFuncSeparate_p

#endif  // !ANI_GLES

bool gl_load();  // false if a function is missing (needs an OpenGL 3.3 driver)
