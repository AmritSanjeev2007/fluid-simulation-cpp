#include "camera_controller.hpp"

#include "camera/camera.hpp"
#include "constants.hpp"

#include <GLFW/glfw3.h>

#include <cmath>

CameraController::CameraController(GLFWwindow* window, Camera& camera)
    : m_window(window),
      m_camera(camera),
      m_previousUserPointer(glfwGetWindowUserPointer(window))
{
    glfwSetWindowUserPointer(window, this);
    m_previousScrollCallback = glfwSetScrollCallback(window, scrollCallback);

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    m_lastCursor = { static_cast<float>(x), static_cast<float>(y) };
}

CameraController::~CameraController()
{
    glfwSetScrollCallback(m_window, m_previousScrollCallback);
    glfwSetWindowUserPointer(m_window, m_previousUserPointer);
}

void CameraController::scrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    auto* self = static_cast<CameraController*>(glfwGetWindowUserPointer(window));
    self->m_pendingScroll += static_cast<float>(yOffset);
    if (self->m_previousScrollCallback)
        self->m_previousScrollCallback(window, xOffset, yOffset);
}

void CameraController::update(bool uiWantsMouse)
{
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(m_window, &x, &y);
    const glm::vec2 cursor = { static_cast<float>(x), static_cast<float>(y) };
    const glm::vec2 delta  = cursor - m_lastCursor;  // pixels, +y is down
    m_lastCursor = cursor;

    // Like Blender, the drag mode is chosen when the button goes down and kept until release.
    const bool middleDown    = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const bool middlePressed = middleDown && !m_middleWasDown;
    m_middleWasDown = middleDown;
    if (!middleDown)
    {
        m_drag = Drag::None;
    }
    else if (middlePressed && !uiWantsMouse)
    {
        const bool shift = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                           glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        m_drag = shift ? Drag::Pan : Drag::Orbit;
    }

    switch (m_drag)
    {
    case Drag::Orbit:
    {
        // Dragging right turns the scene right (camera moves left); dragging down tips its top towards you.
        const float radiansPerPixel = glm::radians(constants::controls::orbitSpeed);
        m_camera.rotate(-delta.x * radiansPerPixel, delta.y * radiansPerPixel);
        break;
    }
    case Drag::Pan:
    {
        // Scale so the point at the pivot's depth stays under the cursor.
        int windowHeight = 1;
        glfwGetWindowSize(m_window, nullptr, &windowHeight);
        const float viewHeight     = 2.0f * m_camera.distance() * std::tan(0.5f * m_camera.fovY());
        const float worldPerPixel  = viewHeight / static_cast<float>(windowHeight > 0 ? windowHeight : 1);
        m_camera.pan(-delta.x * worldPerPixel, delta.y * worldPerPixel);
        break;
    }
    case Drag::None:
        break;
    }

    if (m_pendingScroll != 0.0f && !uiWantsMouse)
        m_camera.zoom(std::pow(constants::controls::zoomStep, m_pendingScroll));
    m_pendingScroll = 0.0f;
}
