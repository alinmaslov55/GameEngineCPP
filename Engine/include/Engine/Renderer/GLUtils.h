#ifndef RENDERER_GLUTILS_H
#define RENDERER_GLUTILS_H

#include "Engine/Utils/Logger.h"

#include <glad/glad.h>
#include <string_view>

namespace Engine {

/// @brief RAII guard that temporarily sets GL_UNPACK_ALIGNMENT and restores the prior value on exit.
class ScopedUnpackAlignment {
public:
    explicit ScopedUnpackAlignment(GLint alignment = 1) noexcept
        : m_TargetAlignment(alignment) {
        if (glad_glGetIntegerv != nullptr && glad_glPixelStorei != nullptr) {
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &m_PreviousAlignment);
            if (m_PreviousAlignment != m_TargetAlignment) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, m_TargetAlignment);
                m_Modified = true;
            }
        }
    }

    ~ScopedUnpackAlignment() noexcept {
        if (m_Modified && glad_glPixelStorei != nullptr) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, m_PreviousAlignment);
        }
    }

    ScopedUnpackAlignment(const ScopedUnpackAlignment&) = delete;
    ScopedUnpackAlignment& operator=(const ScopedUnpackAlignment&) = delete;

private:
    GLint m_PreviousAlignment = 4;
    GLint m_TargetAlignment = 1;
    bool m_Modified = false;
};

/// @brief Evaluates glCheckFramebufferStatus(GL_FRAMEBUFFER) once and logs on incomplete status.
inline bool CheckFramebufferComplete(std::string_view label, int index = -1) {
    if (glad_glCheckFramebufferStatus == nullptr) {
        return false;
    }
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (index >= 0) {
            ENGINE_CORE_ERROR("{} {} FBO incomplete! Status: {:#x}", label, index, status);
        } else {
            ENGINE_CORE_ERROR("{} FBO incomplete! Status: {:#x}", label, status);
        }
        return false;
    }
    return true;
}

} // namespace Engine

#endif // RENDERER_GLUTILS_H
