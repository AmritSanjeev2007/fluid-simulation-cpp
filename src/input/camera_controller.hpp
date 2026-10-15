#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;
class Camera;

// Drives a Camera from the mouse, Blender style:
//   middle drag          orbit around the pivot
//   Shift + middle drag  pan perpendicular to the camera's forward vector
//   scroll wheel         zoom towards / away from the pivot
//
// Installs a GLFW scroll callback (chaining to any callback already installed, e.g.
// ImGui's) and uses the window's user pointer, both of which are restored on destruction.
class CameraController
{
public:
    CameraController(GLFWwindow* window, Camera& camera);
    ~CameraController();

    CameraController(const CameraController&)            = delete;
    CameraController& operator=(const CameraController&) = delete;

    // Call once per frame, after ImGui's NewFrame. While the UI wants the mouse (cursor over
    // a panel) new drags and scrolling are ignored; a drag already in progress continues.
    void update(bool uiWantsMouse);

private:
    enum class Drag { None, Orbit, Pan };

    static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    GLFWwindow* m_window;
    Camera&     m_camera;

    void* m_previousUserPointer;
    void (*m_previousScrollCallback)(GLFWwindow*, double, double);

    Drag      m_drag          = Drag::None;
    bool      m_middleWasDown = false;
    glm::vec2 m_lastCursor{ 0.0f };
    float     m_pendingScroll = 0.0f;
};
