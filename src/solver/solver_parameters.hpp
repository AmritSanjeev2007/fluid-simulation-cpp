#pragma once

#include <glm/glm.hpp>

// Parameters the GUI edits and the solver reads. All values are SI (see solver.md).

struct FluidParameters
{
    float     viscosity = 0.001f;                   // dynamic viscosity, Pa s
    float     density   = 1000.0f;                  // kg/m^3
    glm::vec3 gravity   = { 0.0f, -9.81f, 0.0f };   // m/s^2
    glm::vec3 velocity  = { 0.0f, 0.0f, 0.0f };     // free-stream velocity, m/s

    float ambientTemperature = 293.15f;   // K, inflow and initial temperature
    float thermalDiffusivity = 1.43e-7f;  // m^2/s
    float thermalExpansion   = 2.07e-4f;  // 1/K, Boussinesq buoyancy strength
    bool  buoyancy           = false;

    float kinematicViscosity() const { return density > 0.0f ? viscosity / density : 0.0f; }
};

struct SolverParameters
{
    float timeStep          = 0.016f;  // s of simulated time per update
    int   gridResolution    = 64;      // cells along the longest domain axis
    int   solverIterations  = 100;     // maximum iterations of each linear solve
    float pressureTolerance = 1e-4f;   // relative residual at which a linear solve stops

    // Space around the mesh, as multiples of its characteristic size (median bounding-box side).
    float domainPadding = 1.0f;
    float wakeLength    = 2.0f;        // extra space downstream of the mesh
};

enum class ThermalBoundary
{
    FixedTemperature,  // the surface is held at a temperature
    Insulated,         // no heat flows through the surface
};

struct ObstacleThermal
{
    ThermalBoundary boundary    = ThermalBoundary::FixedTemperature;
    float           temperature = 293.15f;  // K, used when boundary is FixedTemperature
};
