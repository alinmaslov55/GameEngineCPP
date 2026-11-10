#include <gtest/gtest.h>
#include "Engine/Core/Time.h"
#include "Engine/Core/Input.h"
#include "Engine/Renderer/PerspectiveCamera.h"

#include <cmath>
#include <filesystem>

namespace {

class ScopedInputSession {
public:
    ScopedInputSession() { Engine::Input::Init(); }
    ~ScopedInputSession() { Engine::Input::Shutdown(); }
};

TEST(TimeTest, InitializationAndUpdate) {
    Engine::Time::Init();
    EXPECT_GE(Engine::Time::GetDeltaTime(), 0.0f);
    EXPECT_GE(Engine::Time::GetTotalTime(), 0.0f);

    Engine::Time::Update();
    EXPECT_GE(Engine::Time::GetDeltaTime(), 0.0f);
    EXPECT_GE(Engine::Time::GetTotalTime(), 0.0f);
}

TEST(InputTest, DefaultStateQueries) {
    ScopedInputSession input;
    Engine::Input::Update();

    // Query keyboard, mouse, and gamepad without crashing
    bool keyState = Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE);
    (void)keyState;

    bool mouseState = Engine::Input::IsMouseButtonPressed(1);
    (void)mouseState;

    glm::vec2 mousePos = Engine::Input::GetMousePosition();
    (void)mousePos;

    bool gamepadConnected = Engine::Input::IsGamepadConnected();
    (void)gamepadConnected;

    bool btnState = Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH);
    (void)btnState;

    float axis = Engine::Input::GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFTX);
    EXPECT_GE(axis, -1.0f);
    EXPECT_LE(axis, 1.0f);

    SUCCEED();
}

TEST(InputTest, EdgeTransitionsKeyMouseAndGamepad) {
    ScopedInputSession input;
    Engine::Input::SetGamepadConnectedForTesting(true);

    // Frame 0: Held = false
    EXPECT_FALSE(Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustReleased(SDL_SCANCODE_SPACE));

    EXPECT_FALSE(Engine::Input::IsMouseButtonPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT));

    EXPECT_FALSE(Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustReleased(SDL_GAMEPAD_BUTTON_SOUTH));

    // Frame 1: Held = false -> JustPressed = true
    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_SPACE, true);
    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, true);
    Engine::Input::SetGamepadButtonStateForTesting(SDL_GAMEPAD_BUTTON_SOUTH, true);

    EXPECT_TRUE(Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE));
    EXPECT_TRUE(Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustReleased(SDL_SCANCODE_SPACE));

    EXPECT_TRUE(Engine::Input::IsMouseButtonPressed(SDL_BUTTON_LEFT));
    EXPECT_TRUE(Engine::Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT));

    EXPECT_TRUE(Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_TRUE(Engine::Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustReleased(SDL_GAMEPAD_BUTTON_SOUTH));

    // Frame 2: Held = true (JustPressed clears, still pressed)
    Engine::Input::BeginFrame();

    EXPECT_TRUE(Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustReleased(SDL_SCANCODE_SPACE));

    EXPECT_TRUE(Engine::Input::IsMouseButtonPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT));

    EXPECT_TRUE(Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustReleased(SDL_GAMEPAD_BUTTON_SOUTH));

    // Frame 3: JustReleased = true
    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_SPACE, false);
    Engine::Input::SetMouseButtonStateForTesting(SDL_BUTTON_LEFT, false);
    Engine::Input::SetGamepadButtonStateForTesting(SDL_GAMEPAD_BUTTON_SOUTH, false);

    EXPECT_FALSE(Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE));
    EXPECT_TRUE(Engine::Input::IsKeyJustReleased(SDL_SCANCODE_SPACE));

    EXPECT_FALSE(Engine::Input::IsMouseButtonPressed(SDL_BUTTON_LEFT));
    EXPECT_FALSE(Engine::Input::IsMouseButtonJustPressed(SDL_BUTTON_LEFT));
    EXPECT_TRUE(Engine::Input::IsMouseButtonJustReleased(SDL_BUTTON_LEFT));

    EXPECT_FALSE(Engine::Input::IsGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_FALSE(Engine::Input::IsGamepadButtonJustPressed(SDL_GAMEPAD_BUTTON_SOUTH));
    EXPECT_TRUE(Engine::Input::IsGamepadButtonJustReleased(SDL_GAMEPAD_BUTTON_SOUTH));

    // Frame 4: Returned to idle
    Engine::Input::BeginFrame();
    EXPECT_FALSE(Engine::Input::IsKeyPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustPressed(SDL_SCANCODE_SPACE));
    EXPECT_FALSE(Engine::Input::IsKeyJustReleased(SDL_SCANCODE_SPACE));
}

TEST(InputTest, MouseScrollAccumulationAndReset) {
    ScopedInputSession input;

    Engine::Input::BeginFrame();
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaX(), 0.0f);
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaY(), 0.0f);

    Engine::Input::OnMouseWheel(0.25f, 1.0f);
    Engine::Input::OnMouseWheel(-0.50f, 1.5f);
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaX(), -0.25f);
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaY(), 2.5f);

    Engine::Input::BeginFrame();
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaX(), 0.0f);
    EXPECT_FLOAT_EQ(Engine::Input::GetMouseScrollDeltaY(), 0.0f);
}

TEST(InputTest, ActiveDeviceAutoSwitching) {
    ScopedInputSession input;
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::KeyboardMouse);

    // Small axis drift should not steal focus from KeyboardMouse
    Engine::Input::OnGamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 0.12f);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::KeyboardMouse);

    // Meaningful stick deflection switches to Gamepad
    Engine::Input::OnGamepadAxisEvent(SDL_GAMEPAD_AXIS_LEFTX, 0.60f);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::Gamepad);

    // Tiny mouse jitter should not fight Gamepad mode
    Engine::Input::OnMouseMotionEvent(0.5f, 0.5f);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::Gamepad);

    // Intentional mouse movement switches back to KeyboardMouse
    Engine::Input::OnMouseMotionEvent(4.0f, 1.0f);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::KeyboardMouse);

    // Gamepad button switches to Gamepad, wheel scroll switches back to KeyboardMouse
    Engine::Input::OnGamepadButtonEvent(true);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::Gamepad);

    Engine::Input::OnMouseWheel(0.0f, -1.0f);
    EXPECT_EQ(Engine::Input::GetActiveDeviceMode(), Engine::InputDeviceMode::KeyboardMouse);
}

TEST(InputTest, RadialDeadzoneMathAndContinuity) {
    constexpr float kDeadzone = 0.20f;

    // Inside deadzone (including diagonal where m = sqrt(0.12^2 + 0.12^2) ~ 0.1697 < 0.20)
    const glm::vec2 zeroCenter = Engine::Input::ApplyRadialDeadzone({ 0.0f, 0.0f }, kDeadzone);
    EXPECT_FLOAT_EQ(zeroCenter.x, 0.0f);
    EXPECT_FLOAT_EQ(zeroCenter.y, 0.0f);

    const glm::vec2 insideDiag = Engine::Input::ApplyRadialDeadzone({ 0.12f, 0.12f }, kDeadzone);
    EXPECT_FLOAT_EQ(insideDiag.x, 0.0f);
    EXPECT_FLOAT_EQ(insideDiag.y, 0.0f);

    // C0 continuity right across deadzone boundary
    const glm::vec2 justOutside = Engine::Input::ApplyRadialDeadzone({ 0.2008f, 0.0f }, kDeadzone);
    EXPECT_NEAR(justOutside.x, 0.001f, 1e-4f);
    EXPECT_FLOAT_EQ(justOutside.y, 0.0f);

    // 45-degree diagonal preservation (no cardinal snapping!)
    const glm::vec2 midDiag = Engine::Input::ApplyRadialDeadzone({ 0.5f, 0.5f }, kDeadzone);
    const float rawMag = std::sqrt(0.5f * 0.5f + 0.5f * 0.5f);
    const float expectedMag = (rawMag - kDeadzone) / (1.0f - kDeadzone);
    const float actualMag = std::sqrt(midDiag.x * midDiag.x + midDiag.y * midDiag.y);
    EXPECT_NEAR(midDiag.x, midDiag.y, 1e-5f);
    EXPECT_NEAR(actualMag, expectedMag, 1e-5f);

    // Saturated corner clamps cleanly to unit circle
    const glm::vec2 corner = Engine::Input::ApplyRadialDeadzone({ 1.0f, 1.0f }, kDeadzone);
    const float cornerMag = std::sqrt(corner.x * corner.x + corner.y * corner.y);
    EXPECT_NEAR(cornerMag, 1.0f, 1e-5f);
    EXPECT_NEAR(corner.x, corner.y, 1e-5f);

    // Edge boundary guard deadzone >= 1.0f
    const glm::vec2 blocked = Engine::Input::ApplyRadialDeadzone({ 1.0f, 0.0f }, 1.0f);
    EXPECT_FLOAT_EQ(blocked.x, 0.0f);
    EXPECT_FLOAT_EQ(blocked.y, 0.0f);

    // GetMoveVector diagonal WASD normalization + radial stick locomotion
    ScopedInputSession input;
    Engine::Input::SetGamepadConnectedForTesting(true);
    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_W, true);
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_D, true);

    const glm::vec2 wasdDiag = Engine::Input::GetMoveVector(kDeadzone);
    EXPECT_NEAR(wasdDiag.x, std::sqrt(0.5f), 1e-5f);
    EXPECT_NEAR(wasdDiag.y, std::sqrt(0.5f), 1e-5f);

    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_W, false);
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_D, false);
    // In SDL gamepad coordinates, Left Stick up is negative Y
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTX, 0.5f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTY, -0.5f);

    const glm::vec2 stickDiag = Engine::Input::GetMoveVector(kDeadzone);
    EXPECT_NEAR(stickDiag.x, midDiag.x, 1e-5f);
    EXPECT_NEAR(stickDiag.y, midDiag.y, 1e-5f);
}

TEST(InputTest, ActionBindingsMultiInputAndTriggerEdges) {
    ScopedInputSession input;
    Engine::Input::SetGamepadConnectedForTesting(true);

    Engine::ActionBinding heavyAttack;
    heavyAttack.PrimaryKey = SDL_SCANCODE_K;
    heavyAttack.SecondaryKey = SDL_SCANCODE_LCTRL;
    heavyAttack.MouseButton = SDL_BUTTON_RIGHT;
    heavyAttack.PadButton = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    heavyAttack.PadTriggerAxis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    Engine::Input::BindAction("HeavyAttack", heavyAttack);

    // Overlapping key hold transfer: press K -> press LCTRL -> release K -> release LCTRL
    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_K, true);
    EXPECT_TRUE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_TRUE(Engine::Input::IsActionJustPressed("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_LCTRL, true);
    EXPECT_TRUE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_FALSE(Engine::Input::IsActionJustPressed("HeavyAttack"));
    EXPECT_FALSE(Engine::Input::IsActionJustReleased("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_K, false);
    EXPECT_TRUE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_FALSE(Engine::Input::IsActionJustReleased("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetKeyStateForTesting(SDL_SCANCODE_LCTRL, false);
    EXPECT_FALSE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_TRUE(Engine::Input::IsActionJustReleased("HeavyAttack"));

    // Analog trigger threshold (> 0.5f)
    Engine::Input::BeginFrame();
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.40f);
    EXPECT_FALSE(Engine::Input::IsActionPressed("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.55f);
    EXPECT_TRUE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_TRUE(Engine::Input::IsActionJustPressed("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.90f);
    EXPECT_TRUE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_FALSE(Engine::Input::IsActionJustPressed("HeavyAttack"));

    Engine::Input::BeginFrame();
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.45f);
    EXPECT_FALSE(Engine::Input::IsActionPressed("HeavyAttack"));
    EXPECT_TRUE(Engine::Input::IsActionJustReleased("HeavyAttack"));
}

TEST(InputTest, ActionBindingJsonRoundTrip) {
    ScopedInputSession input;
    Engine::Input::ClearActions();

    Engine::ActionBinding dodge;
    dodge.PrimaryKey = SDL_SCANCODE_LSHIFT;
    dodge.SecondaryKey = SDL_SCANCODE_SPACE;
    dodge.MouseButton = SDL_BUTTON_X1;
    dodge.PadButton = SDL_GAMEPAD_BUTTON_EAST;
    dodge.PadTriggerAxis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;

    Engine::ActionBinding heavy;
    heavy.PrimaryKey = SDL_SCANCODE_F;
    heavy.MouseButton = SDL_BUTTON_RIGHT;
    heavy.PadTriggerAxis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;

    Engine::Input::BindAction("Dodge", dodge);
    Engine::Input::BindAction("Heavy", heavy);

    const std::string jsonPayload = Engine::Input::SerializeBindingsToString();
    EXPECT_FALSE(jsonPayload.empty());

    // Malformed JSON must fail cleanly without erasing valid bindings
    EXPECT_FALSE(Engine::Input::DeserializeBindingsFromString("{ not valid json"));
    ASSERT_NE(Engine::Input::GetActionBinding("Dodge"), nullptr);

    // Full string round-trip
    Engine::Input::ClearActions();
    EXPECT_EQ(Engine::Input::GetActionBinding("Dodge"), nullptr);
    ASSERT_TRUE(Engine::Input::DeserializeBindingsFromString(jsonPayload));

    const Engine::ActionBinding* loadedDodge = Engine::Input::GetActionBinding("Dodge");
    const Engine::ActionBinding* loadedHeavy = Engine::Input::GetActionBinding("Heavy");
    ASSERT_NE(loadedDodge, nullptr);
    ASSERT_NE(loadedHeavy, nullptr);
    EXPECT_EQ(*loadedDodge, dodge);
    EXPECT_EQ(*loadedHeavy, heavy);

    // File round-trip in isolated OS temp directory
    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() / "inp01_bindings_roundtrip_test.json";
    std::filesystem::remove(tempPath);

    ASSERT_TRUE(Engine::Input::SaveBindingsToFile(tempPath.string()));
    Engine::Input::ClearActions();
    ASSERT_TRUE(Engine::Input::LoadBindingsFromFile(tempPath.string()));

    ASSERT_NE(Engine::Input::GetActionBinding("Dodge"), nullptr);
    EXPECT_EQ(*Engine::Input::GetActionBinding("Dodge"), dodge);

    std::filesystem::remove(tempPath);
}

TEST(InputTest, MouseGroundHitXZViewportCenter16x9And9x16) {
    ScopedInputSession input;

    // 0. Uninitialized zero-sized viewport must return false safely
    glm::vec2 hitXZ{ 0.0f };
    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({ 6.5f, 0.0f, 7.0f });
    cam.SetDistance(15.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);

    Engine::Input::SetViewportRect({ 0.0f, 0.0f }, { 0.0f, 0.0f });
    EXPECT_FALSE(Engine::Input::GetMouseGroundHitXZ(cam, 0.0f, hitXZ));

    // 1. 16:9 Landscape Standalone viewport center (1920/2, 1080/2) -> FocalPoint.xz within 1e-4m
    cam.SetAspectRatio(1920.0f / 1080.0f);
    Engine::Input::SetViewportRect({ 0.0f, 0.0f }, { 1920.0f, 1080.0f });
    Engine::Input::SetMousePositionForTesting({ 960.0f, 540.0f });
    ASSERT_TRUE(Engine::Input::GetMouseGroundHitXZ(cam, 0.0f, hitXZ));
    EXPECT_NEAR(hitXZ.x, cam.GetFocalPoint().x, 1e-4f);
    EXPECT_NEAR(hitXZ.y, cam.GetFocalPoint().z, 1e-4f);

    // Offset right of center lands East (+X), top half lands North (-Z)
    Engine::Input::SetMousePositionForTesting({ 1440.0f, 270.0f });
    ASSERT_TRUE(Engine::Input::GetMouseGroundHitXZ(cam, 0.0f, hitXZ));
    EXPECT_GT(hitXZ.x, cam.GetFocalPoint().x);
    EXPECT_LT(hitXZ.y, cam.GetFocalPoint().z);

    // 2. 9:16 Portrait Docked Sub-Viewport center (topLeft + (W/2, H/2)) -> FocalPoint.xz within 1e-4m
    cam.SetAspectRatio(1080.0f / 1920.0f);
    const glm::vec2 dockedOrigin{ 284.0f, 48.0f };
    const glm::vec2 dockedSize{ 540.0f, 960.0f };
    Engine::Input::SetViewportRect(dockedOrigin, dockedSize);
    Engine::Input::SetMousePositionForTesting(dockedOrigin + dockedSize * 0.5f);
    ASSERT_TRUE(Engine::Input::GetMouseGroundHitXZ(cam, 0.0f, hitXZ));
    EXPECT_NEAR(hitXZ.x, cam.GetFocalPoint().x, 1e-4f);
    EXPECT_NEAR(hitXZ.y, cam.GetFocalPoint().z, 1e-4f);

    // ScreenToViewportNDC maps top-left to (-1, +1) and bottom-right to (+1, -1)
    glm::vec2 ndc{ 0.0f };
    ASSERT_TRUE(Engine::Input::ScreenToViewportNDC(dockedOrigin, ndc));
    EXPECT_NEAR(ndc.x, -1.0f, 1e-5f);
    EXPECT_NEAR(ndc.y,  1.0f, 1e-5f);

    ASSERT_TRUE(Engine::Input::ScreenToViewportNDC(dockedOrigin + dockedSize, ndc));
    EXPECT_NEAR(ndc.x,  1.0f, 1e-5f);
    EXPECT_NEAR(ndc.y, -1.0f, 1e-5f);
}

TEST(InputTest, ResolveAimDirectionXZKeyboardMouseAndGamepadParity) {
    ScopedInputSession input;
    Engine::Input::SetGamepadConnectedForTesting(true);

    Engine::PerspectiveCamera cam(32.0f, 16.0f / 9.0f, 0.1f, 250.0f);
    cam.SetFocalPoint({ 10.0f, 0.0f, 10.0f });
    cam.SetDistance(15.0f);
    cam.SetPitchDegrees(40.0f);
    cam.SetYawDegrees(0.0f);

    Engine::Input::SetViewportRect({ 0.0f, 0.0f }, { 1920.0f, 1080.0f });
    const glm::vec2 charXZ{ 10.0f, 10.0f };
    const glm::vec2 fallback{ 0.0f, 1.0f };

    // 1. KeyboardMouse: mouse placed East of viewport center -> aim points (+X, 0)
    Engine::Input::SetActiveDeviceModeForTesting(Engine::InputDeviceMode::KeyboardMouse);
    Engine::Input::SetMousePositionForTesting({ 1440.0f, 540.0f });
    glm::vec2 aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(glm::length(aimXZ), 1.0f, 1e-4f);
    EXPECT_GT(aimXZ.x, 0.99f);
    EXPECT_NEAR(aimXZ.y, 0.0f, 0.05f);

    // Cursor directly over character feet at center -> cleanly falls back to normalized fallback
    Engine::Input::SetMousePositionForTesting({ 960.0f, 540.0f });
    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(aimXZ.x, fallback.x, 1e-5f);
    EXPECT_NEAR(aimXZ.y, fallback.y, 1e-5f);

    // 2. Gamepad: Right Stick dominates Left Stick when pushed outside deadzone
    Engine::Input::BeginFrame();
    Engine::Input::SetActiveDeviceModeForTesting(Engine::InputDeviceMode::Gamepad);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTX, -1.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTY, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTX, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTY, -0.8f); // Stick UP -> camera ForwardXZ (-Z)

    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(aimXZ.x, 0.0f, 1e-4f);
    EXPECT_NEAR(aimXZ.y, -1.0f, 1e-4f);

    // Right Stick inside deadzone -> falls back to Left Stick (-X West)
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTX, 0.05f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTY, -0.05f);
    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(aimXZ.x, -1.0f, 1e-4f);
    EXPECT_NEAR(aimXZ.y, 0.0f, 1e-4f);

    // Orbiting camera Yaw to 90 deg projects Right Stick UP (+ForwardXZ) along +X world East
    cam.SetYawDegrees(90.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTX, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTY, -0.9f);
    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(aimXZ.x, 1.0f, 1e-4f);
    EXPECT_NEAR(aimXZ.y, 0.0f, 1e-4f);

    // Both sticks neutral -> falls back to normalized fallback
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTX, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_LEFTY, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTX, 0.0f);
    Engine::Input::SetGamepadAxisStateForTesting(SDL_GAMEPAD_AXIS_RIGHTY, 0.0f);
    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, fallback);
    EXPECT_NEAR(aimXZ.x, fallback.x, 1e-5f);
    EXPECT_NEAR(aimXZ.y, fallback.y, 1e-5f);

    // Disconnected gamepad in Gamepad mode falls back cleanly
    Engine::Input::SetGamepadConnectedForTesting(false);
    aimXZ = Engine::Input::ResolveAimDirectionXZ(cam, charXZ, { 2.0f, 0.0f });
    EXPECT_NEAR(aimXZ.x, 1.0f, 1e-5f);
    EXPECT_NEAR(aimXZ.y, 0.0f, 1e-5f);
}

TEST(InputTest, GamepadRumbleClampingAndDisconnectedNoOp) {
    ScopedInputSession input;

    float low = -1.0f;
    float high = -1.0f;
    uint32_t dur = 999u;
    uint32_t calls = 999u;

    // Disconnected gamepad: safe no-op
    Engine::Input::TriggerGamepadRumble(0.75f, 0.50f, 150u);
    Engine::Input::GetLastRumbleForTesting(low, high, dur, calls);
    EXPECT_EQ(calls, 0u);

    // Connected gamepad: values clamp cleanly to [0.0f, 1.0f]
    Engine::Input::SetGamepadConnectedForTesting(true);
    Engine::Input::TriggerGamepadRumble(-0.4f, 1.8f, 220u);
    Engine::Input::GetLastRumbleForTesting(low, high, dur, calls);
    EXPECT_EQ(calls, 1u);
    EXPECT_FLOAT_EQ(low, 0.0f);
    EXPECT_FLOAT_EQ(high, 1.0f);
    EXPECT_EQ(dur, 220u);

    // Zero stop call propagates cleanly
    Engine::Input::TriggerGamepadRumble(0.0f, 0.0f, 0u);
    Engine::Input::GetLastRumbleForTesting(low, high, dur, calls);
    EXPECT_EQ(calls, 2u);
    EXPECT_FLOAT_EQ(low, 0.0f);
    EXPECT_FLOAT_EQ(high, 0.0f);
    EXPECT_EQ(dur, 0u);
}

} // namespace

