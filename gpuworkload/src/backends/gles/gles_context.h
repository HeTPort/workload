#pragma once

#include <EGL/egl.h>

#include <string>

namespace gpu_avs {

class GlesContext {
public:
    GlesContext() = default;
    ~GlesContext();

    bool Init(std::string& error);
    void Destroy();

    EGLDisplay Display() const { return display_; }
    EGLContext Context() const { return context_; }
    EGLSurface Surface() const { return surface_; }

    int MajorVersion() const { return gles_major_; }
    int MinorVersion() const { return gles_minor_; }

private:
    bool CreateContextWithVersion(
        EGLConfig config,
        int major,
        int minor,
        std::string& error
    );

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;

    int gles_major_ = 0;
    int gles_minor_ = 0;
};

} // namespace gpu_avs
