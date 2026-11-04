#!/usr/bin/env python3
"""glgen.py — generates the OpenGL entry-point table shared by the emulator (C) and the WebGL2 bridge (TypeScript).

  python3 emu/tools/glgen.py /home/user/wine_spec emu/x64 src/lib/x64

Sources:
  * opengl32.spec (Wine) — every function the real opengl32.dll exports (GL 1.1 + wgl), with argument types;
  * GL_CORE below — GL 1.2 … 3.3 core + the extensions the bridge implements, reachable through wglGetProcAddress.
Signature letters: v void, i 32-bit (GLint/GLenum/GLuint/GLsizei/GLboolean/GLbitfield), q 64-bit on both bitnesses (GLint64/GLuint64),
z pointer-sized (GLintptr/GLsizeiptr: 4 bytes on x86, 8 on x64), f float, d double, p pointer (incl. GLsync). The same ids are used on both sides (gl_funcs.inc / gltable.ts), so the C side marshals arguments
generically and the TypeScript side implements each function against WebGL2.
"""
import sys, os, re

GL_CORE = """
glActiveTexture v:i
glAttachShader v:ii
glBeginQuery v:ii
glBeginTransformFeedback v:i
glBindAttribLocation v:iip
glBindBuffer v:ii
glBindBufferBase v:iii
glBindBufferRange v:iiizz
glBindFragDataLocation v:iip
glBindFramebuffer v:ii
glBindRenderbuffer v:ii
glBindSampler v:ii
glBindVertexArray v:i
glBlendColor v:ffff
glBlendEquation v:i
glBlendEquationSeparate v:ii
glBlendFuncSeparate v:iiii
glBlitFramebuffer v:iiiiiiiiii
glBufferData v:izpi
glBufferSubData v:izzp
glCheckFramebufferStatus i:i
glClearBufferfi v:iifi
glClearBufferfv v:iip
glClearBufferiv v:iip
glClearBufferuiv v:iip
glClearDepthf v:f
glClientWaitSync i:piq
glColorMaski v:iiiii
glCompileShader v:i
glCompressedTexImage2D v:iiiiiiip
glCompressedTexImage3D v:iiiiiiiip
glCompressedTexSubImage2D v:iiiiiiiip
glCompressedTexSubImage3D v:iiiiiiiiiip
glCopyBufferSubData v:iizzz
glCopyTexSubImage3D v:iiiiiiiii
glCreateProgram i:
glCreateShader i:i
glDeleteBuffers v:ip
glDeleteFramebuffers v:ip
glDeleteProgram v:i
glDeleteQueries v:ip
glDeleteRenderbuffers v:ip
glDeleteSamplers v:ip
glDeleteShader v:i
glDeleteSync v:p
glDeleteTransformFeedbacks v:ip
glDeleteVertexArrays v:ip
glDepthRangef v:ff
glDetachShader v:ii
glDisableVertexAttribArray v:i
glDisablei v:ii
glDrawArraysInstanced v:iiii
glDrawBuffers v:ip
glDrawElementsBaseVertex v:iiipi
glDrawElementsInstanced v:iiipi
glDrawElementsInstancedBaseVertex v:iiipii
glDrawRangeElements v:iiiiip
glDrawRangeElementsBaseVertex v:iiiiipi
glEnableVertexAttribArray v:i
glEnablei v:ii
glEndQuery v:i
glEndTransformFeedback v:
glFenceSync p:ii
glFlushMappedBufferRange v:izz
glFramebufferRenderbuffer v:iiii
glFramebufferTexture v:iiii
glFramebufferTexture1D v:iiiii
glFramebufferTexture2D v:iiiii
glFramebufferTexture3D v:iiiiii
glFramebufferTextureLayer v:iiiii
glGenBuffers v:ip
glGenFramebuffers v:ip
glGenQueries v:ip
glGenRenderbuffers v:ip
glGenSamplers v:ip
glGenTransformFeedbacks v:ip
glGenVertexArrays v:ip
glGenerateMipmap v:i
glGetActiveAttrib v:iiipppp
glGetActiveUniform v:iiipppp
glGetActiveUniformBlockName v:iiipp
glGetActiveUniformBlockiv v:iiip
glGetActiveUniformsiv v:ipip
glGetAttachedShaders v:iipp
glGetAttribLocation i:ip
glGetBooleani_v v:iip
glGetBufferParameteri64v v:iip
glGetBufferParameteriv v:iip
glGetBufferPointerv v:iip
glGetBufferSubData v:izzp
glGetFragDataLocation i:ip
glGetFramebufferAttachmentParameteriv v:iiip
glGetInteger64i_v v:iip
glGetInteger64v v:ip
glGetIntegeri_v v:iip
glGetMultisamplefv v:iip
glGetProgramInfoLog v:iipp
glGetProgramiv v:iip
glGetQueryObjectiv v:iip
glGetQueryObjectuiv v:iip
glGetQueryiv v:iip
glGetRenderbufferParameteriv v:iip
glGetSamplerParameterfv v:iip
glGetSamplerParameteriv v:iip
glGetShaderInfoLog v:iipp
glGetShaderPrecisionFormat v:iipp
glGetShaderSource v:iipp
glGetShaderiv v:iip
glGetStringi p:ii
glGetSynciv v:piipp
glGetTexParameterIiv v:iip
glGetTexParameterIuiv v:iip
glGetTransformFeedbackVarying v:iiipppp
glGetUniformBlockIndex i:ip
glGetUniformIndices v:iipp
glGetUniformLocation i:ip
glGetUniformfv v:iip
glGetUniformiv v:iip
glGetUniformuiv v:iip
glGetVertexAttribIiv v:iip
glGetVertexAttribIuiv v:iip
glGetVertexAttribPointerv v:iip
glGetVertexAttribdv v:iip
glGetVertexAttribfv v:iip
glGetVertexAttribiv v:iip
glIsBuffer i:i
glIsEnabledi i:ii
glIsFramebuffer i:i
glIsProgram i:i
glIsQuery i:i
glIsRenderbuffer i:i
glIsSampler i:i
glIsShader i:i
glIsSync i:p
glIsTransformFeedback i:i
glIsVertexArray i:i
glLinkProgram v:i
glMapBuffer p:ii
glMapBufferRange p:izzi
glMultiDrawArrays v:ippi
glMultiDrawElements v:ipipi
glPauseTransformFeedback v:
glPointParameterf v:if
glPointParameterfv v:ip
glPointParameteri v:ii
glPointParameteriv v:ip
glPrimitiveRestartIndex v:i
glProvokingVertex v:i
glQueryCounter v:ii
glReleaseShaderCompiler v:
glRenderbufferStorage v:iiii
glRenderbufferStorageMultisample v:iiiii
glResumeTransformFeedback v:
glSampleCoverage v:fi
glSampleMaski v:ii
glSamplerParameterIiv v:iip
glSamplerParameterIuiv v:iip
glSamplerParameterf v:iif
glSamplerParameterfv v:iip
glSamplerParameteri v:iii
glSamplerParameteriv v:iip
glShaderBinary v:ipipi
glShaderSource v:iipp
glStencilFuncSeparate v:iiii
glStencilMaskSeparate v:ii
glStencilOpSeparate v:iiii
glTexBuffer v:iii
glTexImage2DMultisample v:iiiiii
glTexImage3D v:iiiiiiiiip
glTexImage3DMultisample v:iiiiiii
glTexParameterIiv v:iip
glTexParameterIuiv v:iip
glTexSubImage3D v:iiiiiiiiiip
glTransformFeedbackVaryings v:iipi
glUniform1f v:if
glUniform1fv v:iip
glUniform1i v:ii
glUniform1iv v:iip
glUniform1ui v:ii
glUniform1uiv v:iip
glUniform2f v:iff
glUniform2fv v:iip
glUniform2i v:iii
glUniform2iv v:iip
glUniform2ui v:iii
glUniform2uiv v:iip
glUniform3f v:ifff
glUniform3fv v:iip
glUniform3i v:iiii
glUniform3iv v:iip
glUniform3ui v:iiii
glUniform3uiv v:iip
glUniform4f v:iffff
glUniform4fv v:iip
glUniform4i v:iiiii
glUniform4iv v:iip
glUniform4ui v:iiiii
glUniform4uiv v:iip
glUniformBlockBinding v:iii
glUniformMatrix2fv v:iiip
glUniformMatrix2x3fv v:iiip
glUniformMatrix2x4fv v:iiip
glUniformMatrix3fv v:iiip
glUniformMatrix3x2fv v:iiip
glUniformMatrix3x4fv v:iiip
glUniformMatrix4fv v:iiip
glUniformMatrix4x2fv v:iiip
glUniformMatrix4x3fv v:iiip
glUnmapBuffer i:i
glUseProgram v:i
glValidateProgram v:i
glVertexAttrib1d v:id
glVertexAttrib1dv v:ip
glVertexAttrib1f v:if
glVertexAttrib1fv v:ip
glVertexAttrib1s v:ii
glVertexAttrib1sv v:ip
glVertexAttrib2d v:idd
glVertexAttrib2dv v:ip
glVertexAttrib2f v:iff
glVertexAttrib2fv v:ip
glVertexAttrib2s v:iii
glVertexAttrib2sv v:ip
glVertexAttrib3d v:iddd
glVertexAttrib3dv v:ip
glVertexAttrib3f v:ifff
glVertexAttrib3fv v:ip
glVertexAttrib3s v:iiii
glVertexAttrib3sv v:ip
glVertexAttrib4Nbv v:ip
glVertexAttrib4Niv v:ip
glVertexAttrib4Nsv v:ip
glVertexAttrib4Nub v:iiiii
glVertexAttrib4Nubv v:ip
glVertexAttrib4Nuiv v:ip
glVertexAttrib4Nusv v:ip
glVertexAttrib4bv v:ip
glVertexAttrib4d v:idddd
glVertexAttrib4dv v:ip
glVertexAttrib4f v:iffff
glVertexAttrib4fv v:ip
glVertexAttrib4iv v:ip
glVertexAttrib4s v:iiiii
glVertexAttrib4sv v:ip
glVertexAttrib4ubv v:ip
glVertexAttrib4uiv v:ip
glVertexAttrib4usv v:ip
glVertexAttribDivisor v:ii
glVertexAttribI1i v:ii
glVertexAttribI1iv v:ip
glVertexAttribI1ui v:ii
glVertexAttribI1uiv v:ip
glVertexAttribI2i v:iii
glVertexAttribI2iv v:ip
glVertexAttribI2ui v:iii
glVertexAttribI2uiv v:ip
glVertexAttribI3i v:iiii
glVertexAttribI3iv v:ip
glVertexAttribI3ui v:iiii
glVertexAttribI3uiv v:ip
glVertexAttribI4bv v:ip
glVertexAttribI4i v:iiiii
glVertexAttribI4iv v:ip
glVertexAttribI4sv v:ip
glVertexAttribI4ubv v:ip
glVertexAttribI4ui v:iiiii
glVertexAttribI4uiv v:ip
glVertexAttribI4usv v:ip
glVertexAttribIPointer v:iiiip
glVertexAttribP1ui v:iiii
glVertexAttribP1uiv v:iiip
glVertexAttribP2ui v:iiii
glVertexAttribP2uiv v:iiip
glVertexAttribP3ui v:iiii
glVertexAttribP3uiv v:iiip
glVertexAttribP4ui v:iiii
glVertexAttribP4uiv v:iiip
glVertexAttribPointer v:iiiiip
glWaitSync v:piq
glBindTransformFeedback v:ii
glBindFragDataLocationIndexed v:iiip
glGetFragDataIndex i:ip
glClampColor v:ii
glBeginConditionalRender v:ii
glEndConditionalRender v:
glGetQueryObjecti64v v:iip
glGetQueryObjectui64v v:iip
glTexStorage2D v:iiiii
glTexStorage3D v:iiiiii
glInvalidateFramebuffer v:iip
glInvalidateSubFramebuffer v:iipiiii
glGetProgramBinary v:iippp
glProgramBinary v:iipi
glProgramParameteri v:iii
glGetInternalformativ v:iiiip
glReadnPixels v:iiiiiiip
glDebugMessageCallback v:pp
glDebugMessageControl v:iiiipi
glDebugMessageInsert v:iiiiip
glObjectLabel v:iiip
glGetObjectLabel v:iiipp
glPushDebugGroup v:iiip
glPopDebugGroup v:
glBlendBarrier v:
glMinSampleShading v:f
glBlendEquationi v:ii
glBlendEquationSeparatei v:iii
glBlendFunci v:iii
glBlendFuncSeparatei v:iiiii
glGetnUniformfv v:iiip
glGetnUniformiv v:iiip
glGetnUniformuiv v:iiip
glCopyImageSubData v:iiiiiiiiiiiiiii
glActiveTextureARB v:i
glClientActiveTextureARB v:i
glBindBufferARB v:ii
glDeleteBuffersARB v:ip
glGenBuffersARB v:ip
glBufferDataARB v:izpi
glBufferSubDataARB v:izzp
glMapBufferARB p:ii
glUnmapBufferARB i:i
glCreateProgramObjectARB i:
glCreateShaderObjectARB i:i
glShaderSourceARB v:iipp
glCompileShaderARB v:i
glAttachObjectARB v:ii
glLinkProgramARB v:i
glUseProgramObjectARB v:i
glDeleteObjectARB v:i
glGetObjectParameterivARB v:iip
glGetInfoLogARB v:iipp
glGetUniformLocationARB i:ip
glGetAttribLocationARB i:ip
glBindAttribLocationARB v:iip
glVertexAttribPointerARB v:iiiiip
glEnableVertexAttribArrayARB v:i
glDisableVertexAttribArrayARB v:i
glBindFramebufferEXT v:ii
glBindRenderbufferEXT v:ii
glGenFramebuffersEXT v:ip
glGenRenderbuffersEXT v:ip
glDeleteFramebuffersEXT v:ip
glDeleteRenderbuffersEXT v:ip
glFramebufferTexture2DEXT v:iiiii
glFramebufferRenderbufferEXT v:iiii
glRenderbufferStorageEXT v:iiii
glCheckFramebufferStatusEXT i:i
glGenerateMipmapEXT v:i
glIsFramebufferEXT i:i
glIsRenderbufferEXT i:i
glGetFramebufferAttachmentParameterivEXT v:iiip
glGetRenderbufferParameterivEXT v:iip
glBlitFramebufferEXT v:iiiiiiiiii
glRenderbufferStorageMultisampleEXT v:iiiii
glBindVertexArrayAPPLE v:i
glGenVertexArraysAPPLE v:ip
glDeleteVertexArraysAPPLE v:ip
glDrawArraysInstancedARB v:iiii
glDrawElementsInstancedARB v:iiipi
glVertexAttribDivisorARB v:ii
glCompressedTexImage2DARB v:iiiiiiip
glCompressedTexSubImage2DARB v:iiiiiiiip
glBlendEquationEXT v:i
glBlendFuncSeparateEXT v:iiii
glBlendEquationSeparateEXT v:ii
glMultiTexCoord2fARB v:iff
glDrawRangeElementsEXT v:iiiiip
glTexImage3DEXT v:iiiiiiiiip
glTexSubImage3DEXT v:iiiiiiiiiip
glPointParameterfARB v:if
glPointParameterfvARB v:ip
glFenceSyncARB p:ii
glDeleteSyncARB v:p
glClientWaitSyncARB i:piq
glWaitSyncARB v:piq
glGetSyncivARB v:piipp
glDebugMessageCallbackARB v:pp
glDebugMessageControlARB v:iiiipi
glGetStringiEXT p:ii
glMapBufferRangeEXT p:izzi
glFlushMappedBufferRangeEXT v:izz
glTexStorage2DEXT v:iiiii
glGenQueriesARB v:ip
glDeleteQueriesARB v:ip
glBeginQueryARB v:ii
glEndQueryARB v:i
glGetQueryObjectivARB v:iip
glGetQueryObjectuivARB v:iip
glGetQueryivARB v:iip
glIsQueryARB i:i
wglGetExtensionsStringARB p:i
wglGetExtensionsStringEXT p:
wglCreateContextAttribsARB i:iip
wglChoosePixelFormatARB i:ippipp
wglGetPixelFormatAttribivARB i:iiiipp
wglGetPixelFormatAttribfvARB i:iiiipp
wglSwapIntervalEXT i:i
wglGetSwapIntervalEXT i:
wglMakeContextCurrentARB i:iii
wglGetCurrentReadDCARB i:
"""

TYPEMAP = {"long": "i", "float": "f", "double": "d", "ptr": "p", "str": "p", "int64": "q", "wstr": "p"}

def parse_spec(path):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"@ stdcall (\w+)\(([^)]*)\)", line.strip())
        if not m: continue
        name, args = m.group(1), m.group(2).split()
        sig = "".join(TYPEMAP[a] for a in args)
        ret = "p" if name in ("wglGetProcAddress", "wglGetDefaultProcAddress", "wglGetCurrentContext", "wglGetCurrentDC", "glGetString", "wglCreateContext", "wglCreateLayerContext") else ("i" if name.startswith("wgl") or name in ("glGetError", "glIsEnabled", "glIsList", "glIsTexture", "glRenderMode", "glGenLists", "glAreTexturesResident") else "v")
        out.append((name, ret + ":" + sig))
    return out

def main():
    spec_dir, cdir, tsdir = sys.argv[1], sys.argv[2], sys.argv[3]
    fns = {}; exported = set()
    for n, s in parse_spec(os.path.join(spec_dir, "opengl32.spec")): fns[n] = s; exported.add(n)
    for line in GL_CORE.strip().splitlines():
        parts = line.split()
        if not parts: continue
        n, s = parts[0], parts[1]
        if n in fns and n.startswith("gl"): continue   # the 1.1 export wins
        fns[n] = s
    names = sorted(fns)
    # ---- C table (sorted by name for binary search); id = index ----
    with open(os.path.join(cdir, "gl_funcs.inc"), "w") as f:
        f.write("/* generated by tools/glgen.py — do not edit: OpenGL entry points (name, id, signature) shared with src/lib/x64/gltable.ts */\n")
        f.write("typedef struct { const char *name; const char *sig; } GlFn;\n")
        f.write("static const GlFn gl_fns[] = {\n")
        for i, n in enumerate(names): f.write('  { "%s", "%s" },%s\n' % (n, fns[n], "" if i % 1 else ""))
        f.write("};\n#define GL_FN_COUNT %d\n" % len(names))
    with open(os.path.join(tsdir, "gltable.ts"), "w") as f:
        f.write("// generated by emu/tools/glgen.py — do not edit: OpenGL entry points in id order (id = index), signature as in glgen.py\n")
        f.write("// third column: 1 = exported by opengl32.dll itself (GL 1.1 + wgl: always resolvable), 0 = only through wglGetProcAddress when the bridge implements it\n")
        f.write("export const GL_FUNCS: ReadonlyArray<readonly [string, string, number]> = [\n")
        for n in names: f.write("  ['%s', '%s', %d],\n" % (n, fns[n], 1 if n in exported else 0))
        f.write("];\n")
    print("%d GL entry points" % len(names))

if __name__ == "__main__": main()
