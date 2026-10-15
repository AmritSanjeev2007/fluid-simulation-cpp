#include "camera.hpp"

#include "constants.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

Camera::Camera() : Camera(constants::camera::position, constants::camera::target) {}

Camera::Camera(const glm::vec3& position, const glm::vec3& target)
    : m_pivot(target),
      m_fovY(glm::radians(constants::camera::fovY)),
      m_nearZ(constants::camera::nearZ),
      m_farZ(constants::camera::farZ)
{
    const glm::vec3 offset = position - target;
    m_distance = std::clamp(glm::length(offset), constants::camera::minDistance, constants::camera::maxDistance);
    m_yaw      = std::atan2(offset.x, offset.z);
    rotate(0.0f, std::asin(std::clamp(offset.y / glm::length(offset), -1.0f, 1.0f)));
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(position(), m_pivot, constants::camera::up);
}

glm::mat4 Camera::projection(float aspect) const
{
    return glm::perspective(m_fovY, aspect, m_nearZ, m_farZ);
}

void Camera::rotate(float deltaYaw, float deltaPitch)
{
    // Stop just short of the poles, where lookAt's up vector would be parallel to forward.
    const float maxPitch = glm::radians(constants::camera::maxPitch);
    m_yaw   = std::remainder(m_yaw + deltaYaw, glm::two_pi<float>());
    m_pitch = std::clamp(m_pitch + deltaPitch, -maxPitch, maxPitch);
}

void Camera::translate(const glm::vec3& offset)
{
    m_pivot += offset;
}

void Camera::pan(float rightAmount, float upAmount)
{
    translate(right() * rightAmount + up() * upAmount);
}

void Camera::zoom(float factor)
{
    m_distance = std::clamp(m_distance * factor, constants::camera::minDistance, constants::camera::maxDistance);
}

glm::vec3 Camera::position() const
{
    const glm::vec3 offset = { std::cos(m_pitch) * std::sin(m_yaw),
                               std::sin(m_pitch),
                               std::cos(m_pitch) * std::cos(m_yaw) };
    return m_pivot + m_distance * offset;
}

glm::vec3 Camera::forward() const
{
    return glm::normalize(m_pivot - position());
}

glm::vec3 Camera::right() const
{
    return glm::normalize(glm::cross(forward(), constants::camera::up));
}

glm::vec3 Camera::up() const
{
    return glm::cross(right(), forward());
}
