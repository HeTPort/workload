#include "gles_backend.h"

#include "gpu_avs/utils.h"

#include <EGL/egl.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#ifndef GL_CONTEXT_LOST
#define GL_CONTEXT_LOST 0x0507
#endif

#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif

#ifndef GL_QUERY_RESULT_EXT
#define GL_QUERY_RESULT_EXT 0x8866
#endif

#ifndef GL_QUERY_RESULT_AVAILABLE_EXT
#define GL_QUERY_RESULT_AVAILABLE_EXT 0x8867
#endif

#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

namespace gpu_avs {

using PFNGLGENQUERIESEXTPROC_LOCAL =
    void (*)(GLsizei n, GLuint* ids);
using PFNGLDELETEQUERIESEXTPROC_LOCAL =
    void (*)(GLsizei n, const GLuint* ids);
using PFNGLBEGINQUERYEXTPROC_LOCAL =
    void (*)(GLenum target, GLuint id);
using PFNGLENDQUERYEXTPROC_LOCAL =
    void (*)(GLenum target);
using PFNGLGETQUERYOBJECTUIVEXTPROC_LOCAL =
    void (*)(GLuint id, GLenum pname, GLuint* params);
using PFNGLGETQUERYOBJECTUI64VEXTPROC_LOCAL =
    void (*)(GLuint id, GLenum pname, GLuint64* params);

static PFNGLGENQUERIESEXTPROC_LOCAL glGenQueriesEXT_ptr = nullptr;
static PFNGLDELETEQUERIESEXTPROC_LOCAL glDeleteQueriesEXT_ptr = nullptr;
static PFNGLBEGINQUERYEXTPROC_LOCAL glBeginQueryEXT_ptr = nullptr;
static PFNGLENDQUERYEXTPROC_LOCAL glEndQueryEXT_ptr = nullptr;
static PFNGLGETQUERYOBJECTUIVEXTPROC_LOCAL glGetQueryObjectuivEXT_ptr = nullptr;
static PFNGLGETQUERYOBJECTUI64VEXTPROC_LOCAL glGetQueryObjectui64vEXT_ptr = nullptr;

static bool HasGlExtension(const char* name) {
    if (!name || !name[0]) {
        return false;
    }

    const char* ext_str =
        reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

    if (ext_str) {
        std::string extensions(ext_str);
        std::string target(name);

        size_t pos = extensions.find(target);
        while (pos != std::string::npos) {
            bool left_ok =
                pos == 0 ||
                extensions[pos - 1] == ' ';

            size_t end = pos + target.size();
            bool right_ok =
                end == extensions.size() ||
                extensions[end] == ' ';

            if (left_ok && right_ok) {
                return true;
            }

            pos = extensions.find(target, pos + 1);
        }
    }

#ifdef GL_NUM_EXTENSIONS
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);

    for (GLint i = 0; i < count; ++i) {
        const char* ext =
            reinterpret_cast<const char*>(
                glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i))
            );

        if (ext && std::string(ext) == name) {
            return true;
        }
    }
#endif

    return false;
}

static bool ParseTextureSize(
    const std::string& s,
    uint32_t& w,
    uint32_t& h
) {
    size_t pos = s.find('x');
    if (pos == std::string::npos) {
        pos = s.find('X');
    }

    if (pos == std::string::npos) {
        return false;
    }

    try {
        w = static_cast<uint32_t>(std::stoul(s.substr(0, pos)));
        h = static_cast<uint32_t>(std::stoul(s.substr(pos + 1)));
    } catch (...) {
        return false;
    }

    return w > 0 && h > 0;
}

static const char* kFullscreenVert = R"(#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kAluFrag = R"(#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = vUv * 2.0 - 1.0;
    vec4 x = vec4(p, 0.37, 1.0);

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;

        x = x * vec4(1.00013, 0.99991, 1.00007, 0.99989)
              + vec4(0.00031, 0.00017, 0.00023, 0.00029);

        x = fract(x * 1.61803398875 + x.yzwx);
    }

    oColor = vec4(x.rgb, 1.0);
}
)";

static const char* kSfuFrag = R"(#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = vUv * 6.2831853;
    float x = p.x + 0.17;
    float y = p.y + 0.31;

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;
        x = sin(x) + cos(y) + 0.001;
        y = cos(y) - sin(x) + 0.002;
    }

    oColor = vec4(fract(abs(x)), fract(abs(y)), fract(abs(x + y)), 1.0);
}
)";

static const char* kTextureFrag = R"(#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform sampler2D uTexture0;
uniform int uIterations;
uniform int uTextureCount;
uniform vec2 uResolution;

void main() {
    vec2 uv = vUv;
    vec4 c = vec4(0.0);

    int count = max(1, uTextureCount);

    for (int i = 0; i < 512; ++i) {
        if (i >= uIterations) break;

        float fi = float(i);
        vec2 off = vec2(
            sin(fi * 0.17) * 0.013,
            cos(fi * 0.11) * 0.017
        );

        for (int t = 0; t < 16; ++t) {
            if (t >= count) break;
            c += texture(uTexture0, fract(uv + off + float(t) * 0.007));
        }

        uv = fract(uv * 1.013 + off);
    }

    c /= float(max(1, uIterations * count));
    oColor = vec4(c.rgb, 1.0);
}
)";

static const char* kFillFrag = R"(#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = gl_FragCoord.xy / uResolution.xy;
    oColor = vec4(p.x, p.y, fract(p.x + p.y), 1.0);
}
)";

static const char* kMixedFrag = R"(#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform sampler2D uTexture0;
uniform int uIterations;
uniform int uTextureCount;
uniform vec2 uResolution;

void main() {
    vec2 uv = vUv;
    vec4 x = vec4(uv, 0.37, 1.0);
    vec4 tex = vec4(0.0);

    int texCount = max(1, uTextureCount);

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;

        x = x * vec4(1.00011, 0.99997, 1.00005, 0.99993)
              + vec4(0.00019, 0.00023, 0.00029, 0.00031);

        float fi = float(i);
        vec2 off = vec2(sin(fi * 0.13), cos(fi * 0.19)) * 0.01;

        for (int t = 0; t < 8; ++t) {
            if (t >= texCount) break;
            tex += texture(uTexture0, fract(uv + off + float(t) * 0.011));
        }

        x = fract(x + tex * 0.017 + x.yzwx * 0.013);
        uv = fract(uv * 1.007 + off);
    }

    vec3 rgb = fract(x.rgb + tex.rgb / float(max(1, uIterations * texCount)));
    oColor = vec4(rgb, 1.0);
}
)";

GlesBackend::~GlesBackend() {
    Destroy();
}

bool GlesBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    last_status_ = SubmitStatus::Ok;

    if (cfg_.mode != "offscreen") {
        error = "GLES backend supports only mode=offscreen";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.rt_format != "RGBA8") {
        error = "GLES backend supports only rt_format=RGBA8";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.width == 0 || cfg_.height == 0) {
        error = "invalid resolution";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (!context_.Init(error)) {
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    InitTimerQuery();
    return true;
}

bool GlesBackend::CreateResources(std::string& error) {
    last_status_ = SubmitStatus::Ok;

    if (!CreateFbo(error)) {
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    if (!CreateFullscreenQuad(error)) {
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    if (!CreateTextureResources(error)) {
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    if (!CreateProgram(error)) {
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    resources_created_ = true;
    return true;
}

bool GlesBackend::CreateFbo(std::string& error) {
    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(cfg_.width),
        static_cast<GLsizei>(cfg_.height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    if (!CheckGlError("glTexImage2D color target", error)) {
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        color_tex_,
        0
    );

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::ostringstream os;
        os << "FBO incomplete, status=0x" << std::hex << status;
        error = os.str();
        return false;
    }

    return CheckGlError("CreateFbo", error);
}

bool GlesBackend::CreateFullscreenQuad(std::string& error) {
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(0)
    );

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );

    return CheckGlError("CreateFullscreenQuad", error);
}

static uint32_t HashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

bool GlesBackend::CreateTextureResources(std::string& error) {
    uint32_t tex_w = 512;
    uint32_t tex_h = 512;

    ParseTextureSize(cfg_.texture_size, tex_w, tex_h);

    tex_w = std::min(tex_w, 8192u);
    tex_h = std::min(tex_h, 8192u);

    std::vector<uint8_t> data(
        static_cast<size_t>(tex_w) *
        static_cast<size_t>(tex_h) *
        4
    );

    for (uint32_t y = 0; y < tex_h; ++y) {
        for (uint32_t x = 0; x < tex_w; ++x) {
            size_t idx =
                (static_cast<size_t>(y) * tex_w + x) * 4;

            uint32_t v = HashU32(
                x * 73856093U ^
                y * 19349663U ^
                cfg_.iterations * 83492791U ^
                cfg_.texture_count * 2654435761U
            );

            data[idx + 0] = static_cast<uint8_t>((v >> 0) & 0xFF);
            data[idx + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            data[idx + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
            data[idx + 3] = 255;
        }
    }

    glGenTextures(1, &workload_tex_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, workload_tex_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(tex_w),
        static_cast<GLsizei>(tex_h),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        data.data()
    );

    return CheckGlError("CreateTextureResources", error);
}

GLuint GlesBackend::CompileShader(
    GLenum type,
    const char* source,
    std::string& error
) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);

        std::string log;
        log.resize(static_cast<size_t>(len));

        if (len > 0) {
            glGetShaderInfoLog(shader, len, nullptr, log.data());
        }

        std::ostringstream os;
        os << "shader compile failed: " << log;
        error = os.str();

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint GlesBackend::LinkProgram(
    GLuint vs,
    GLuint fs,
    std::string& error
) {
    GLuint program = glCreateProgram();

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);

        std::string log;
        log.resize(static_cast<size_t>(len));

        if (len > 0) {
            glGetProgramInfoLog(program, len, nullptr, log.data());
        }

        std::ostringstream os;
        os << "program link failed: " << log;
        error = os.str();

        glDeleteProgram(program);
        return 0;
    }

    return program;
}

const char* GlesBackend::SelectFragmentShader() const {
    if (cfg_.shader == "alu") {
        return kAluFrag;
    }

    if (cfg_.shader == "sfu") {
        return kSfuFrag;
    }

    if (cfg_.shader == "texture") {
        return kTextureFrag;
    }

    if (cfg_.shader == "fill") {
        return kFillFrag;
    }

    return kMixedFrag;
}

bool GlesBackend::CreateProgram(std::string& error) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kFullscreenVert, error);
    if (!vs) {
        return false;
    }

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, SelectFragmentShader(), error);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    program_ = LinkProgram(vs, fs, error);

    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!program_) {
        return false;
    }

    glUseProgram(program_);

    u_iterations_ = glGetUniformLocation(program_, "uIterations");
    u_resolution_ = glGetUniformLocation(program_, "uResolution");
    u_texture0_ = glGetUniformLocation(program_, "uTexture0");
    u_texture_count_ = glGetUniformLocation(program_, "uTextureCount");

    if (u_texture0_ >= 0) {
        glUniform1i(u_texture0_, 0);
    }

    return CheckGlError("CreateProgram", error);
}

bool GlesBackend::InitTimerQuery() {
    if (!HasGlExtension("GL_EXT_disjoint_timer_query")) {
        timer_query_supported_ = false;
        return false;
    }

    glGenQueriesEXT_ptr =
        reinterpret_cast<PFNGLGENQUERIESEXTPROC_LOCAL>(
            eglGetProcAddress("glGenQueriesEXT"));

    glDeleteQueriesEXT_ptr =
        reinterpret_cast<PFNGLDELETEQUERIESEXTPROC_LOCAL>(
            eglGetProcAddress("glDeleteQueriesEXT"));

    glBeginQueryEXT_ptr =
        reinterpret_cast<PFNGLBEGINQUERYEXTPROC_LOCAL>(
            eglGetProcAddress("glBeginQueryEXT"));

    glEndQueryEXT_ptr =
        reinterpret_cast<PFNGLENDQUERYEXTPROC_LOCAL>(
            eglGetProcAddress("glEndQueryEXT"));

    glGetQueryObjectuivEXT_ptr =
        reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC_LOCAL>(
            eglGetProcAddress("glGetQueryObjectuivEXT"));

    glGetQueryObjectui64vEXT_ptr =
        reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC_LOCAL>(
            eglGetProcAddress("glGetQueryObjectui64vEXT"));

    if (!glGenQueriesEXT_ptr ||
        !glDeleteQueriesEXT_ptr ||
        !glBeginQueryEXT_ptr ||
        !glEndQueryEXT_ptr ||
        !glGetQueryObjectuivEXT_ptr ||
        !glGetQueryObjectui64vEXT_ptr) {
        timer_query_supported_ = false;
        return false;
    }

    glGenQueriesEXT_ptr(1, &timer_query_);
    timer_query_supported_ = timer_query_ != 0;

    return timer_query_supported_;
}

void GlesBackend::DestroyTimerQuery() {
    if (timer_query_supported_ &&
        timer_query_ != 0 &&
        glDeleteQueriesEXT_ptr) {
        glDeleteQueriesEXT_ptr(1, &timer_query_);
    }

    timer_query_ = 0;
    timer_query_supported_ = false;
    timer_query_active_ = false;
}

bool GlesBackend::SupportsGpuTimestamp() const {
    return timer_query_supported_;
}

bool GlesBackend::GetLastGpuTimeMs(double& out_ms) {
    if (!timer_query_supported_) {
        return false;
    }

    out_ms = last_gpu_time_ms_;
    return true;
}

SubmitStatus GlesBackend::SubmitWorkload(uint64_t frame_index) {
    (void)frame_index;

    last_status_ = SubmitStatus::Ok;

    if (!resources_created_) {
        last_status_ = SubmitStatus::AllocationFail;
        return last_status_;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(
        0,
        0,
        static_cast<GLsizei>(cfg_.width),
        static_cast<GLsizei>(cfg_.height)
    );

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glUseProgram(program_);
    glBindVertexArray(vao_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, workload_tex_);

    if (u_iterations_ >= 0) {
        glUniform1i(u_iterations_, static_cast<GLint>(cfg_.iterations));
    }

    if (u_resolution_ >= 0) {
        glUniform2f(
            u_resolution_,
            static_cast<GLfloat>(cfg_.width),
            static_cast<GLfloat>(cfg_.height)
        );
    }

    if (u_texture_count_ >= 0) {
        glUniform1i(
            u_texture_count_,
            static_cast<GLint>(cfg_.texture_count)
        );
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (timer_query_supported_ && timer_query_ != 0) {
        glBeginQueryEXT_ptr(GL_TIME_ELAPSED_EXT, timer_query_);
        timer_query_active_ = true;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (timer_query_supported_ && timer_query_active_) {
        glEndQueryEXT_ptr(GL_TIME_ELAPSED_EXT);
        timer_query_active_ = false;
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        last_status_ = GlErrorToSubmitStatus(err);
        return last_status_;
    }

    return SubmitStatus::Ok;
}

bool GlesBackend::WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) {
    last_status_ = SubmitStatus::Ok;

    const double start_s = NowSeconds();
    const double timeout_s =
        static_cast<double>(timeout_ns) / 1000000000.0;

    glFlush();

    if (!timer_query_supported_) {
        glFinish();

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            last_status_ = GlErrorToSubmitStatus(err);

            std::ostringstream os;
            os << "glFinish failed, glError=0x" << std::hex << err;
            error = os.str();
            return false;
        }

        return true;
    }

    GLuint available = 0;

    while (true) {
        glGetQueryObjectuivEXT_ptr(
            timer_query_,
            GL_QUERY_RESULT_AVAILABLE_EXT,
            &available
        );

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            last_status_ = GlErrorToSubmitStatus(err);

            std::ostringstream os;
            os << "glGetQueryObjectuivEXT failed, glError=0x"
               << std::hex << err;
            error = os.str();
            return false;
        }

        if (available) {
            break;
        }

        if (NowSeconds() - start_s > timeout_s) {
            last_status_ = SubmitStatus::GpuTimeout;
            error = "GLES timer query wait timeout";
            return false;
        }

        SleepMs(1);
    }

    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        last_status_ = GlErrorToSubmitStatus(err);

        std::ostringstream os;
        os << "glGetIntegerv(GL_GPU_DISJOINT_EXT) failed, glError=0x"
           << std::hex << err;
        error = os.str();
        return false;
    }

    if (disjoint) {
        last_gpu_time_ms_ = 0.0;
        return true;
    }

    GLuint64 elapsed_ns = 0;
    glGetQueryObjectui64vEXT_ptr(
        timer_query_,
        GL_QUERY_RESULT_EXT,
        &elapsed_ns
    );

    err = glGetError();
    if (err != GL_NO_ERROR) {
        last_status_ = GlErrorToSubmitStatus(err);

        std::ostringstream os;
        os << "glGetQueryObjectui64vEXT failed, glError=0x"
           << std::hex << err;
        error = os.str();
        return false;
    }

    last_gpu_time_ms_ =
        static_cast<double>(elapsed_ns) / 1000000.0;

    return true;
}

bool GlesBackend::Readback(ReadbackBuffer& out, std::string& error) {
    last_status_ = SubmitStatus::Ok;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    out.width = cfg_.width;
    out.height = cfg_.height;
    out.format = cfg_.rt_format;
    out.data.resize(
        static_cast<size_t>(cfg_.width) *
        static_cast<size_t>(cfg_.height) *
        4
    );

    glReadPixels(
        0,
        0,
        static_cast<GLsizei>(cfg_.width),
        static_cast<GLsizei>(cfg_.height),
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        out.data.data()
    );

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        last_status_ = GlErrorToSubmitStatus(err);

        std::ostringstream os;
        os << "glReadPixels failed, glError=0x"
           << std::hex << err;
        error = os.str();
        return false;
    }

    return true;
}

bool GlesBackend::CheckGlError(const char* where, std::string& error) {
    GLenum err = glGetError();

    if (err == GL_NO_ERROR) {
        return true;
    }

    std::ostringstream os;
    os << where << " failed, glError=0x"
       << std::hex << err;

    error = os.str();
    return false;
}

SubmitStatus GlesBackend::GlErrorToSubmitStatus(GLenum err) const {
    if (err == GL_CONTEXT_LOST) {
        return SubmitStatus::DeviceLost;
    }

    if (err == GL_OUT_OF_MEMORY) {
        return SubmitStatus::AllocationFail;
    }

    return SubmitStatus::ApiError;
}

void GlesBackend::Destroy() {
    DestroyTimerQuery();

    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    if (workload_tex_ != 0) {
        glDeleteTextures(1, &workload_tex_);
        workload_tex_ = 0;
    }

    if (color_tex_ != 0) {
        glDeleteTextures(1, &color_tex_);
        color_tex_ = 0;
    }

    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }

    context_.Destroy();

    resources_created_ = false;
    last_status_ = SubmitStatus::Ok;
}

} // namespace gpu_avs
