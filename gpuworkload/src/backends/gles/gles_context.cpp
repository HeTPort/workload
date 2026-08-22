#include "gles_context.h"

#include <EGL/eglext.h>

#include <sstream>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif

namespace gpu_avs {

GlesContext::~GlesContext() {
    Destroy();
}

bool GlesContext::CreateContextWithVersion(
    EGLConfig config,
    int major,
    int minor,
    std::string& error
) {
    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, major,
        EGL_NONE
    };

    context_ = eglCreateContext(
        display_,
        config,
        EGL_NO_CONTEXT,
        ctx_attribs
    );

    if (context_ == EGL_NO_CONTEXT) {
        std::ostringstream os;
        os << "eglCreateContext GLES" << major
           << " failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    gles_major_ = major;
    gles_minor_ = minor;
    return true;
}

bool GlesContext::Init(std::string& error) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        error = "eglGetDisplay failed";
        return false;
    }

    EGLint egl_major = 0;
    EGLint egl_minor = 0;

    if (!eglInitialize(display_, &egl_major, &egl_minor)) {
        std::ostringstream os;
        os << "eglInitialize failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        std::ostringstream os;
        os << "eglBindAPI(EGL_OPENGL_ES_API) failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    EGLConfig config = nullptr;
    EGLint num_configs = 0;

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    if (!eglChooseConfig(
            display_,
            config_attribs,
            &config,
            1,
            &num_configs) ||
        num_configs <= 0) {
        std::ostringstream os;
        os << "eglChooseConfig failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };

    surface_ = eglCreatePbufferSurface(
        display_,
        config,
        pbuffer_attribs
    );

    if (surface_ == EGL_NO_SURFACE) {
        std::ostringstream os;
        os << "eglCreatePbufferSurface failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    std::string ctx_error;

    if (!CreateContextWithVersion(config, 3, 0, ctx_error)) {
        error = "GLES backend requires OpenGL ES 3.0 context: " + ctx_error;
        return false;
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        std::ostringstream os;
        os << "eglMakeCurrent failed, eglError=0x"
           << std::hex << eglGetError();
        error = os.str();
        return false;
    }

    return true;
}

void GlesContext::Destroy() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(
            display_,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        );
    }

    if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
        context_ = EGL_NO_CONTEXT;
    }

    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }

    if (display_ != EGL_NO_DISPLAY) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }

    gles_major_ = 0;
    gles_minor_ = 0;
}

} // namespace gpu_avs
