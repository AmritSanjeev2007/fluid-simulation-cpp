#pragma once

#include <glm/glm.hpp>

// Colours are linear RGB in [0, 1] so they can go straight to OpenGL and ImGui.
namespace constants
{
    namespace window
    {
        inline constexpr const char* title  = "Fluid Simulation";
        inline constexpr int         width  = 1280;
        inline constexpr int         height = 720;
    }

    // Paths relative to the executable (assets/ is copied next to it on build).
    namespace assets
    {
        inline constexpr const char* windowIcon         = "assets/icons/triangle.svg";
        inline constexpr const char* meshVertexShader   = "assets/shaders/mesh.vert";
        inline constexpr const char* meshFragmentShader = "assets/shaders/mesh.frag";
        inline constexpr const char* screenVertexShader   = "assets/shaders/screen.vert";
        inline constexpr const char* screenFragmentShader = "assets/shaders/screen.frag";
        inline constexpr const char* fieldVertexShader    = "assets/shaders/field.vert";
        inline constexpr const char* fieldFragmentShader  = "assets/shaders/field.frag";
        inline constexpr const char* volumeVertexShader   = "assets/shaders/volume.vert";
        inline constexpr const char* volumeFragmentShader = "assets/shaders/volume.frag";
        inline constexpr const char* streamlineVertexShader   = "assets/shaders/streamline.vert";
        inline constexpr const char* streamlineFragmentShader = "assets/shaders/streamline.frag";
        inline constexpr const char* lineVertexShader     = "assets/shaders/line.vert";
        inline constexpr const char* lineFragmentShader   = "assets/shaders/line.frag";
    }

    namespace colors
    {
        // Native window title bar (applied on Windows 11+ only).
        inline constexpr glm::vec3 titleBar     = { 0.055f, 0.055f, 0.149f }; // navy, #0e0e26
        inline constexpr glm::vec3 titleBarText = { 1.000f, 1.000f, 1.000f };

        // Default clear colour of the 3D view.
        inline constexpr glm::vec3 background   = { 0.050f, 0.080f, 0.120f };

        // Surface colour of rendered meshes.
        inline constexpr glm::vec3 mesh         = { 1.000f, 1.000f, 1.000f };

        // Outline of the mesh under the mouse cursor.
        inline constexpr glm::vec3 hoverOutline = { 1.000f, 0.600f, 0.150f };

        // Wireframe of the simulation domain.
        inline constexpr glm::vec3 domainBox    = { 0.450f, 0.500f, 0.600f };

        // Destructive actions (e.g. "Delete mesh").
        inline constexpr glm::vec3 danger        = { 0.600f, 0.150f, 0.150f };
        inline constexpr glm::vec3 dangerHovered = { 0.750f, 0.200f, 0.200f };
        inline constexpr glm::vec3 dangerActive  = { 0.850f, 0.250f, 0.250f };

        // World axes in the axes gizmo.
        inline constexpr glm::vec3 axisX = { 0.900f, 0.250f, 0.330f };
        inline constexpr glm::vec3 axisY = { 0.540f, 0.800f, 0.000f };
        inline constexpr glm::vec3 axisZ = { 0.170f, 0.560f, 0.960f };
    }

    namespace lighting
    {
        // Direction the light travels (from the light towards the scene), world space.
        inline constexpr glm::vec3 direction = { -0.4f, -1.0f, -0.6f };
        inline constexpr glm::vec3 color     = { 1.0f, 1.0f, 1.0f };
        inline constexpr float     ambient   = 0.15f;
    }

    namespace camera
    {
        inline constexpr glm::vec3 position = { 3.0f, 2.5f, 4.0f };
        inline constexpr glm::vec3 target   = { 0.0f, 0.0f, 0.0f };
        inline constexpr glm::vec3 up       = { 0.0f, 1.0f, 0.0f };
        inline constexpr float     fovY     = 45.0f;   // degrees
        inline constexpr float     nearZ    = 0.1f;
        inline constexpr float     farZ     = 100.0f;

        // Orbit limits.
        inline constexpr float     minDistance = 0.05f;
        inline constexpr float     maxDistance = 0.5f * farZ;
        inline constexpr float     maxPitch    = 89.0f;   // degrees above/below the horizon
    }

    // World axes gizmo in the top-right corner of the 3D view. Sizes are in screen
    // coordinates and get multiplied by the window's DPI scale.
    namespace gizmo
    {
        inline constexpr float radius = 50.0f;   // half the side of its bounding box
        inline constexpr float margin = 12.0f;   // gap to the top-right corner
    }

    // Outline drawn around the mesh under the mouse cursor. Screen coordinates, multiplied by the DPI scale.
    namespace outline
    {
        inline constexpr float width = 2.0f;
    }

    // Blender-style viewport controls (see input/camera_controller.hpp).
    namespace controls
    {
        inline constexpr float orbitSpeed = 0.3f;    // degrees of rotation per pixel dragged
        inline constexpr float zoomStep   = 0.9f;    // distance multiplier per scroll notch towards the pivot
    }
}
