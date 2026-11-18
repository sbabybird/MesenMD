#pragma once

#include <SDL_opengl.h>

// Standalone loader for the OpenGL 1.2/2.0+ entry points that the Windows
// opengl32.dll does not export. Resolved through SDL_GL_GetProcAddress so we
// can run the embedded RetroArch GLSL video filters without a third-party GL
// loader. GL 1.1 functions (glDrawArrays/glBindTexture/...) are still linked
// directly from opengl32 and are intentionally not listed here.

struct GlEntries
{
    PFNGLACTIVETEXTUREPROC            ActiveTexture;
    PFNGLBINDATTRIBLOCATIONPROC       BindAttribLocation;
    PFNGLBINDBUFFERPROC               BindBuffer;
    PFNGLBINDFRAMEBUFFERPROC          BindFramebuffer;
    PFNGLBINDVERTEXARRAYPROC          BindVertexArray;
    PFNGLBUFFERDATAPROC               BufferData;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC   CheckFramebufferStatus;
    PFNGLCOMPILESHADERPROC            CompileShader;
    PFNGLCREATEPROGRAMPROC            CreateProgram;
    PFNGLCREATESHADERPROC             CreateShader;
    PFNGLATTACHSHADERPROC            AttachShader;
    PFNGLDELETEBUFFERSPROC            DeleteBuffers;
    PFNGLDELETEFRAMEBUFFERSPROC       DeleteFramebuffers;
    PFNGLDELETEPROGRAMPROC            DeleteProgram;
    PFNGLDELETESHADERPROC             DeleteShader;
    PFNGLDELETEVERTEXARRAYSPROC       DeleteVertexArrays;
    PFNGLENABLEVERTEXATTRIBARRAYPROC  EnableVertexAttribArray;
    PFNGLFRAMEBUFFERTEXTURE2DPROC     FramebufferTexture2D;
    PFNGLGENBUFFERSPROC               GenBuffers;
    PFNGLGENFRAMEBUFFERSPROC          GenFramebuffers;
    PFNGLGENVERTEXARRAYSPROC          GenVertexArrays;
    PFNGLGETPROGRAMINFOLOGPROC        GetProgramInfoLog;
    PFNGLGETPROGRAMIVPROC             GetProgramiv;
    PFNGLGETSHADERINFOLOGPROC         GetShaderInfoLog;
    PFNGLGETSHADERIVPROC              GetShaderiv;
    PFNGLGETUNIFORMLOCATIONPROC       GetUniformLocation;
    PFNGLLINKPROGRAMPROC              LinkProgram;
    PFNGLSHADERSOURCEPROC             ShaderSource;
    PFNGLUNIFORM1FPROC                Uniform1f;
    PFNGLUNIFORM1IPROC                Uniform1i;
    PFNGLUNIFORM2FPROC                Uniform2f;
    PFNGLUNIFORMMATRIX4FVPROC         UniformMatrix4fv;
    PFNGLUSEPROGRAMPROC               UseProgram;
    PFNGLVERTEXATTRIBPOINTERPROC      VertexAttribPointer;
};

extern GlEntries gl;

// Resolves all entry points. Must be called after the OpenGL context is
// current (SDL_GL_GetProcAddress needs a live context).
void glLoaderInit();

#define glActiveTexture            gl.ActiveTexture
#define glBindAttribLocation       gl.BindAttribLocation
#define glBindBuffer               gl.BindBuffer
#define glBindFramebuffer          gl.BindFramebuffer
#define glBindVertexArray          gl.BindVertexArray
#define glBufferData               gl.BufferData
#define glCheckFramebufferStatus   gl.CheckFramebufferStatus
#define glCompileShader            gl.CompileShader
#define glCreateProgram            gl.CreateProgram
#define glCreateShader             gl.CreateShader
#define glAttachShader             gl.AttachShader
#define glDeleteBuffers            gl.DeleteBuffers
#define glDeleteFramebuffers       gl.DeleteFramebuffers
#define glDeleteProgram            gl.DeleteProgram
#define glDeleteShader             gl.DeleteShader
#define glDeleteVertexArrays       gl.DeleteVertexArrays
#define glEnableVertexAttribArray  gl.EnableVertexAttribArray
#define glFramebufferTexture2D     gl.FramebufferTexture2D
#define glGenBuffers               gl.GenBuffers
#define glGenFramebuffers          gl.GenFramebuffers
#define glGenVertexArrays          gl.GenVertexArrays
#define glGetProgramInfoLog        gl.GetProgramInfoLog
#define glGetProgramiv             gl.GetProgramiv
#define glGetShaderInfoLog         gl.GetShaderInfoLog
#define glGetShaderiv              gl.GetShaderiv
#define glGetUniformLocation       gl.GetUniformLocation
#define glLinkProgram              gl.LinkProgram
#define glShaderSource             gl.ShaderSource
#define glUniform1f                gl.Uniform1f
#define glUniform1i                gl.Uniform1i
#define glUniform2f                gl.Uniform2f
#define glUniformMatrix4fv         gl.UniformMatrix4fv
#define glUseProgram               gl.UseProgram
#define glVertexAttribPointer      gl.VertexAttribPointer