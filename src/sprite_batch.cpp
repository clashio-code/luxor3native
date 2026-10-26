// Batched textured-quad renderer for OpenGL ES 2.0.
#include "sprite_batch.h"
#include "luxor.h"

#include <GLES2/gl2.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace luxor {

namespace {

const char* kVertexShader =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

const char* kFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv) * v_color;\n"
    "}\n";

GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
        log_line("gl", "shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

SpriteBatch::SpriteBatch() = default;

SpriteBatch::~SpriteBatch() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (program_) glDeleteProgram(program_);
}

bool SpriteBatch::init() {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
        log_line("gl", "program link failed: %s", log);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    attr_pos_ = glGetAttribLocation(program_, "a_pos");
    attr_uv_ = glGetAttribLocation(program_, "a_uv");
    attr_color_ = glGetAttribLocation(program_, "a_color");
    uni_mvp_ = glGetUniformLocation(program_, "u_mvp");
    uni_tex_ = glGetUniformLocation(program_, "u_tex");

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return true;
}

uint32_t SpriteBatch::create_texture(int width, int height, const uint8_t* rgba) {
    if (!rgba || width <= 0 || height <= 0) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void SpriteBatch::destroy_texture(uint32_t tex) {
    if (tex) {
        GLuint t = tex;
        glDeleteTextures(1, &t);
    }
}

void SpriteBatch::set_viewport(int width, int height) {
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    // Projection stays in the original 800x600 logical coordinate system;
    // glViewport performs the proportional downscale to the physical display.
    // RG40XXV (640x480) shares the game's 4:3 aspect, so this is uniform.
    const float l = 0.0f, r = static_cast<float>(kLogicalWidth);
    const float b = static_cast<float>(kLogicalHeight), t = 0.0f;
    proj_[0]  = 2.0f / (r - l);
    proj_[1]  = 0.0f;  proj_[2]  = 0.0f; proj_[3]  = 0.0f;
    proj_[4]  = 0.0f;
    proj_[5]  = 2.0f / (t - b);
    proj_[6]  = 0.0f;  proj_[7]  = 0.0f;
    proj_[8]  = 0.0f;  proj_[9]  = 0.0f; proj_[10] = -1.0f; proj_[11] = 0.0f;
    proj_[12] = -(r + l) / (r - l);
    proj_[13] = -(t + b) / (t - b);
    proj_[14] = 0.0f;  proj_[15] = 1.0f;
}

void SpriteBatch::bind_texture(uint32_t tex) {
    if (has_current_ && current_tex_ == tex) return;
    flush();
    current_tex_ = tex;
    has_current_ = true;
    glBindTexture(GL_TEXTURE_2D, tex);
}

void SpriteBatch::draw(uint32_t tex, float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        float r, float g, float b, float a) {
    bind_texture(tex);
    const Vertex tl = { x,     y,     u0, v0, r, g, b, a };
    const Vertex tr = { x + w, y,     u1, v0, r, g, b, a };
    const Vertex bl = { x,     y + h, u0, v1, r, g, b, a };
    const Vertex br = { x + w, y + h, u1, v1, r, g, b, a };
    vertices_.push_back(tl); vertices_.push_back(bl); vertices_.push_back(tr);
    vertices_.push_back(tr); vertices_.push_back(bl); vertices_.push_back(br);
}

void SpriteBatch::draw_rotated(uint32_t tex, float cx, float cy, float w, float h,
                                float angle,
                                float u0, float v0, float u1, float v1,
                                float r, float g, float b, float a) {
    bind_texture(tex);
    float hw = w * 0.5f, hh = h * 0.5f;
    float c = std::cos(angle), s = std::sin(angle);
    // Local corners, counter-clockwise in a Y-down space.
    const float lx[4] = { -hw,  hw,  hw, -hw };
    const float ly[4] = { -hh, -hh,  hh,  hh };
    const float uu[4] = { u0,  u1,  u1,  u0 };
    const float vv[4] = { v0,  v0,  v1,  v1 };
    const size_t base = vertices_.size();
    for (int i = 0; i < 4; ++i) {
        Vertex vert;
        vert.x = cx + lx[i] * c - ly[i] * s;
        vert.y = cy + lx[i] * s + ly[i] * c;
        vert.u = uu[i]; vert.v = vv[i];
        vert.r = r; vert.g = g; vert.b = b; vert.a = a;
        vertices_.push_back(vert);
    }
    // Triangle fan: (0,1,2) and (0,2,3).
    vertices_.push_back(vertices_[base + 0]);
    vertices_.push_back(vertices_[base + 1]);
    vertices_.push_back(vertices_[base + 2]);
    vertices_.push_back(vertices_[base + 0]);
    vertices_.push_back(vertices_[base + 2]);
    vertices_.push_back(vertices_[base + 3]);
}

void SpriteBatch::flush() {
    if (vertices_.empty()) return;
    glUseProgram(program_);
    glUniformMatrix4fv(uni_mvp_, 1, GL_FALSE, proj_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_tex_);
    glUniform1i(uni_tex_, 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                 vertices_.data(), GL_STREAM_DRAW);

    glEnableVertexAttribArray(attr_pos_);
    glVertexAttribPointer(attr_pos_, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(attr_uv_);
    glVertexAttribPointer(attr_uv_, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(attr_color_);
    glVertexAttribPointer(attr_color_, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, r)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));

    glDisableVertexAttribArray(attr_pos_);
    glDisableVertexAttribArray(attr_uv_);
    glDisableVertexAttribArray(attr_color_);

    vertices_.clear();
    has_current_ = false;
}

}  // namespace luxor
