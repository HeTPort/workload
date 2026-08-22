#pragma once

#include "gpu_avs/backend.h"
#include "gles_context.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>

namespace gpu_avs {

class GlesBackend final : public IGpuBackend {
public:
    GlesBackend() = default;
    ~GlesBackend() override;

    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;

    SubmitStatus SubmitWorkload(uint64_t frame_index) override;
    bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) override;

    bool Readback(ReadbackBuffer& out, std::string& error) override;

    bool SupportsGpuTimestamp() const override;
    bool GetLastGpuTimeMs(double& out_ms) override;

    SubmitStatus LastStatus() const override { return last_status_; }

    void Destroy() override;

    const char* Name() const override { return "gles"; }

private:
    bool CreateFbo(std::string& error);
    bool CreateFullscreenQuad(std::string& error);
    bool CreateTextureResources(std::string& error);
    bool CreateProgram(std::string& error);

    GLuint CompileShader(GLenum type, const char* source, std::string& error);
    GLuint LinkProgram(GLuint vs, GLuint fs, std::string& error);

    const char* SelectFragmentShader() const;

    bool CheckGlError(const char* where, std::string& error);
    SubmitStatus GlErrorToSubmitStatus(GLenum err) const;

    bool InitTimerQuery();
    void DestroyTimerQuery();

private:
    WorkloadConfig cfg_;
    GlesContext context_;

    GLuint fbo_ = 0;
    GLuint color_tex_ = 0;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    GLuint workload_tex_ = 0;

    GLint u_iterations_ = -1;
    GLint u_resolution_ = -1;
    GLint u_texture0_ = -1;
    GLint u_texture_count_ = -1;

    bool timer_query_supported_ = false;
    bool timer_query_active_ = false;
    GLuint timer_query_ = 0;
    double last_gpu_time_ms_ = 0.0;

    SubmitStatus last_status_ = SubmitStatus::Ok;

    bool resources_created_ = false;
};

} // namespace gpu_avs
