#ifndef CORE_LAYER_H
#define CORE_LAYER_H

#include <string>
#include <utility>

namespace Engine {

class Layer {
public:
    explicit Layer(std::string name = "Layer") : m_DebugName(std::move(name)) {}
    virtual ~Layer() = default;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float deltaTime) {}
    virtual void OnImGuiRender() {}

    const std::string& GetName() const { return m_DebugName; }

protected:
    std::string m_DebugName;
};

} // namespace Engine

#endif // CORE_LAYER_H
