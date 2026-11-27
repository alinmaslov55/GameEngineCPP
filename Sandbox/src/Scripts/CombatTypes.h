#ifndef SANDBOX_COMBAT_TYPES_H
#define SANDBOX_COMBAT_TYPES_H

#include <glm/vec2.hpp>

namespace Engine {
class GameObject;
}

struct DamageEvent {
    float Amount = 0.0f;
    Engine::GameObject* Source = nullptr;
    glm::vec2 ImpulseXZ{ 0.0f };
    float HitStopSec = 0.0f;
    float StunDuration = 0.0f;
    bool IsFire = false;
    bool IsRadiant = false;
    bool BypassInvuln = false;
};

struct StatusApplication {
    float BurnDurationSec = 0.0f;
    float BurnDPS = 0.0f;
    float BlindDurationSec = 0.0f;
    float StunDurationSec = 0.0f;
};

#endif // SANDBOX_COMBAT_TYPES_H
