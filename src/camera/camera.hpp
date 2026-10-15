#pragma once

#include <glm/glm.hpp>

// Orbit camera: looks at a pivot point from a distance, oriented by yaw and pitch.
// There is no roll, so the camera's up vector always stays in the plane containing world up.
class Camera
{
public:
    // Starts at constants::camera::position looking at constants::camera::target.
    Camera();
    Camera(const glm::vec3& position, const glm::vec3& target);

    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
    glm::mat4 viewProjection(float aspect) const { return projection(aspect) * view(); }

    // Rotates around the pivot. Angles are in radians; pitch is clamped short of straight up/down.
    void rotate(float deltaYaw, float deltaPitch);

    // Moves the camera and pivot together by a world-space offset.
    void translate(const glm::vec3& offset);

    // Moves the camera and pivot together in the plane perpendicular to forward().
    // Amounts are in world units along right() and up().
    void pan(float rightAmount, float upAmount);

    // Scales the distance to the pivot: factor < 1 moves closer, > 1 moves away.
    void zoom(float factor);

    glm::vec3 position() const;
    glm::vec3 forward() const;  // from the camera towards the pivot
    glm::vec3 right() const;
    glm::vec3 up() const;

    const glm::vec3& pivot() const { return m_pivot; }
    float            distance() const { return m_distance; }
    float            fovY() const { return m_fovY; }  // radians

private:
    glm::vec3 m_pivot{ 0.0f };
    float     m_distance = 1.0f;
    float     m_yaw      = 0.0f;  // radians, around world up; 0 looks down -Z
    float     m_pitch    = 0.0f;  // radians, positive looks down on the pivot
    float     m_fovY;
    float     m_nearZ;
    float     m_farZ;
};
