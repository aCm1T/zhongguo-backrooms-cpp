#pragma once

#include "sdl_minimal.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLchar = char;
using GLfloat = float;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_BGRA = 0x80E1;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLboolean GL_FALSE = 0;

struct GLApi {
    void (*Viewport)(GLint,GLint,GLsizei,GLsizei){};
    void (*ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat){};
    void (*Clear)(GLbitfield){};
    const unsigned char* (*GetString)(GLenum){};
    void (*GenVertexArrays)(GLsizei,GLuint*){};
    void (*BindVertexArray)(GLuint){};
    void (*DeleteVertexArrays)(GLsizei,const GLuint*){};
    void (*GenTextures)(GLsizei,GLuint*){};
    void (*DeleteTextures)(GLsizei,const GLuint*){};
    void (*BindTexture)(GLenum,GLuint){};
    void (*ActiveTexture)(GLenum){};
    void (*TexParameteri)(GLenum,GLenum,GLint){};
    void (*TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*){};
    void (*PixelStorei)(GLenum,GLint){};
    GLuint (*CreateShader)(GLenum){};
    void (*ShaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*){};
    void (*CompileShader)(GLuint){};
    void (*GetShaderiv)(GLuint,GLenum,GLint*){};
    void (*GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*){};
    void (*DeleteShader)(GLuint){};
    GLuint (*CreateProgram)(){};
    void (*AttachShader)(GLuint,GLuint){};
    void (*LinkProgram)(GLuint){};
    void (*GetProgramiv)(GLuint,GLenum,GLint*){};
    void (*GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*){};
    void (*DeleteProgram)(GLuint){};
    void (*UseProgram)(GLuint){};
    GLint (*GetUniformLocation)(GLuint,const GLchar*){};
    void (*Uniform1f)(GLint,GLfloat){};
    void (*Uniform1i)(GLint,GLint){};
    void (*Uniform2f)(GLint,GLfloat,GLfloat){};
    void (*Uniform3f)(GLint,GLfloat,GLfloat,GLfloat){};
    void (*Uniform4f)(GLint,GLfloat,GLfloat,GLfloat,GLfloat){};
    void (*DrawArrays)(GLenum,GLint,GLsizei){};

    template<typename T> static T load(const char* name) {
        auto p = SDL_GL_GetProcAddress(name);
        if (!p) throw std::runtime_error(std::string("OpenGL function unavailable: ") + name);
        return reinterpret_cast<T>(p);
    }

    void initialize() {
#define LOAD(name) name = load<decltype(name)>("gl" #name)
        LOAD(Viewport); LOAD(ClearColor); LOAD(Clear); LOAD(GetString); LOAD(GenVertexArrays);
        LOAD(BindVertexArray); LOAD(DeleteVertexArrays); LOAD(GenTextures); LOAD(DeleteTextures);
        LOAD(BindTexture); LOAD(ActiveTexture); LOAD(TexParameteri); LOAD(TexImage2D);
        LOAD(PixelStorei); LOAD(CreateShader);
        LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv); LOAD(GetShaderInfoLog);
        LOAD(DeleteShader); LOAD(CreateProgram); LOAD(AttachShader); LOAD(LinkProgram);
        LOAD(GetProgramiv); LOAD(GetProgramInfoLog); LOAD(DeleteProgram); LOAD(UseProgram);
        LOAD(GetUniformLocation); LOAD(Uniform1f); LOAD(Uniform1i); LOAD(Uniform2f);
        LOAD(Uniform3f); LOAD(Uniform4f); LOAD(DrawArrays);
#undef LOAD
    }
};
