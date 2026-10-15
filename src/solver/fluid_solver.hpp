#pragma once

#include "linear_solver.hpp"
#include "mac_grid.hpp"
#include "meshes/mesh.hpp"
#include "solver_parameters.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// One obstacle as the solver sees it. meshVersion must change whenever the mesh data does.
struct Obstacle
{
    const MeshData* mesh        = nullptr;
    int             meshVersion = 0;
    glm::mat4       model{ 1.0f };
    ObstacleThermal thermal;
};

enum class DisplayField
{
    VelocityMagnitude,    // m/s
    Pressure,             // motion pressure, Pa
    TotalPressure,        // motion + hydrostatic pressure, Pa
    PressureCoefficient,  // Cp = p / (rho |U|^2 / 2)
    Temperature,          // degrees Celsius
};

struct SolverDiagnostics
{
    glm::ivec3 gridSize{ 0 };
    float      cellSize = 0.0f;           // m
    int        substeps = 0;              // per update
    bool       substepsCapped = false;    // true if the CFL limit wanted more substeps than allowed
    float      cfl = 0.0f;                // per substep
    int        pressureIterations = 0;    // last pressure solve
    float      pressureResidual = 0.0f;   // relative, last pressure solve
    float      maxDivergence = 0.0f;      // max |div u| after projection, 1/s
    float      reynolds = 0.0f;           // rho |U| L / mu
    float      stepMilliseconds = 0.0f;   // wall-clock time of the last update
};

// Incompressible Navier-Stokes on a MAC grid with pressure projection, static
// obstacles given by a signed distance field, and an advected-diffused temperature
// with optional Boussinesq buoyancy. The algorithm is described in solver.md.
class FluidSolver
{
public:
    // Rebuilds the domain, obstacle SDF and fields if anything that shapes them changed
    // (the meshes, their transforms, grid resolution, padding, or the direction of the free stream).
    // Returns true if it rebuilt; the fields are then reset. The domain encloses all obstacles,
    // and each keeps its own thermal boundary. Obstacles should not overlap.
    bool configure(const FluidParameters& fluid, const SolverParameters& params, std::span<const Obstacle> obstacles);

    // Fills the domain with the free stream (projected around the obstacle) at ambient temperature.
    void reset(const FluidParameters& fluid, const SolverParameters& params);

    // Advances the flow by `duration` seconds in CFL-limited substeps.
    void advance(const FluidParameters& fluid, const SolverParameters& params, float duration);

    const MacGrid&           grid() const { return m_grid; }
    const SolverDiagnostics& diagnostics() const { return m_diagnostics; }
    bool                     isConfigured() const { return m_configured; }
    bool                     hasObstacle() const { return m_hasObstacle; }

    // Characteristic length of the obstacles (median side of their bounding box), or of the domain without any.
    float characteristicLength() const { return m_length; }

    // Cell-centred values of a field for display (nx * ny * nz, x fastest). Solid cells are 0.
    void displayField(DisplayField field, const FluidParameters& fluid, std::vector<float>& out) const;

private:
    struct DomainKey
    {
        std::vector<std::pair<int, glm::mat4>> meshes;  // version and model of each obstacle
        int        resolution = 0;
        float      padding    = 0.0f;
        float      wake       = 0.0f;
        glm::ivec3 flow{ 0 };

        bool operator==(const DomainKey&) const = default;
    };

    void buildDomain(const SolverParameters& params, std::span<const Obstacle> obstacles, glm::ivec3 flow);

    // Thermal boundary of the obstacle a cell belongs to, or nullptr for fluid cells.
    const ObstacleThermal* thermalAt(int i, int j, int k) const;
    void buildPressureMatrix();

    void substep(const FluidParameters& fluid, const SolverParameters& params, float dt);
    glm::vec3 traceBack(const glm::vec3& position, float dt) const;
    bool      enteredThroughInflow(const glm::vec3& position) const;
    void advectVelocity(float dt);
    void advectTemperature(float dt);
    void addBuoyancy(const FluidParameters& fluid, float dt);
    void diffuseVelocity(const FluidParameters& fluid, const SolverParameters& params, float dt);
    void diffuseTemperature(const FluidParameters& fluid, const SolverParameters& params, float dt);
    void applyVelocityBoundaries();
    void applyTemperatureBoundaries();
    void project(const SolverParameters& params, float density, float dt);
    void extrapolateVelocity();
    float maxDivergence() const;

    MacGrid           m_grid;
    DomainKey         m_key;
    bool              m_configured  = false;
    bool              m_hasObstacle = false;
    float             m_length      = 1.0f;
    std::vector<ObstacleThermal> m_thermals;  // one per obstacle
    Grid3<std::uint8_t>          m_owner;     // 1-based index into m_thermals of each solid cell, 0 in fluid
    bool                         m_anyFixed     = false;
    bool                         m_anyInsulated = false;
    glm::vec3         m_freeStream{ 0.0f };
    float             m_ambient = 293.15f;
    SolverDiagnostics m_diagnostics;

    StencilMatrix m_pressureMatrix;
    bool          m_pressureSingular = true;  // no outflow face: pressure is defined up to a constant
    PcgSolver     m_pressureSolver;
    PcgSolver     m_diffusionSolver;
    StencilMatrix m_diffusionMatrix;
    Grid3<float>  m_rhs;

    std::array<Grid3<float>, 3> m_velocityScratch;
    Grid3<float>                m_scalarScratch;
};
