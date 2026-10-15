#pragma once

#include "constants.hpp"
#include "meshes/mesh.hpp"
#include "solver/fluid_solver.hpp"
#include "solver/solver_parameters.hpp"
#include "solver/streamlines.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct MeshTransform
{
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };  // Euler angles, degrees
    glm::vec3 scale    = { 1.0f, 1.0f, 1.0f };

    // Scale, then rotate (X, then Y, then Z), then translate.
    glm::mat4 modelMatrix() const;
};

struct MeshInfo
{
    int             id = 0;  // unique for the program's lifetime; 0 means no mesh
    std::string     name;
    MeshTransform   transform;
    ObstacleThermal thermal;
};

// A mesh in the scene: the settings the GUI edits plus the geometry the solver, picking and
// renderer use. The geometry never changes after the mesh is added.
struct SceneMesh
{
    MeshInfo info;
    MeshData data;
};

// How the field is drawn in the 3D view.
enum class FieldDisplay
{
    Off,
    Slice,        // a colour-mapped plane through the domain
    Volume,       // the whole domain, transparent where the field is low
    Streamlines,  // lines following the flow from a plane upstream of the mesh
};

// How the flow is drawn in the 3D view.
struct ViewSettings
{
    DisplayField field         = DisplayField::VelocityMagnitude;
    FieldDisplay display       = FieldDisplay::Slice;
    float        opacity       = 1.0f;  // overall opacity of the slice, volume and streamlines
    float        volumeDensity = 1.0f;  // scales the volume's opacity (see FieldRenderer::drawVolume)
    int          streamlineResolution = 16;  // seeds per side of the square seed grid
    int          sliceAxis     = 2;     // 0 = X, 1 = Y, 2 = Z (the slice is perpendicular to this axis)
    float        slicePosition = 0.5f;  // 0..1 across the domain
    bool         autoRange     = true;  // fit the colour range to the field every update
    glm::vec2    range{ 0.0f, 1.0f };   // colour range (min, max) in the field's unit
    bool         showDomain    = true;  // wireframe of the simulation box
};

// Cell-centred values of ViewSettings::field, ready for upload as a 3D texture.
struct FieldSnapshot
{
    std::vector<float>        values;  // nx * ny * nz, x fastest; 0 in solid cells
    std::vector<std::uint8_t> solid;   // 1 inside the obstacle
    Domain                    domain;
    int                       version = 0;  // increases whenever values or solid change
};

// Streamlines coloured by ViewSettings::field, ready for upload as a line buffer.
struct StreamlineSnapshot
{
    Streamlines lines;
    int         version = 0;  // increases whenever lines change
};

// Everything the simulation exposes to the outside world. The GUI reads and
// edits this object; it drives the FluidSolver and hands the renderer a field to draw.
class SimulationInfo
{
public:
    FluidParameters  fluid;
    SolverParameters solver;
    ViewSettings     view;
    glm::vec3        backgroundColor = constants::colors::background;

    // Rebuilds the solver's domain if the mesh or grid settings changed, then advances
    // the flow by solver.timeStep if running (or a single step was requested).
    void update();

    bool isPaused() const { return m_paused; }
    void togglePaused() { m_paused = !m_paused; }
    void requestStep() { m_stepRequested = true; }
    void reset();

    float elapsedTime() const { return m_elapsedTime; }
    int   stepCount() const { return m_stepCount; }

    // Meshes in the scene, in the order they were added. Every mesh is an obstacle to the flow.
    const std::vector<SceneMesh>& meshes() const { return m_meshes; }

    // Adds a mesh and selects it. Returns its id.
    int  addMesh(std::string name, MeshData data);
    void deleteMesh(int id);

    // The mesh whose properties the GUI shows, picked by clicking it. 0 / nullptr when none is.
    int       selectedMeshId() const { return m_selectedId; }
    MeshInfo* selectedMesh();
    void      selectMesh(int id) { m_selectedId = id; }

    const FluidSolver&   fluidSolver() const { return m_solver; }
    const FieldSnapshot&      field() const { return m_field; }
    const StreamlineSnapshot& streamlines() const { return m_streamlines; }

    // True when the displayed field is centred on a reference value (pressure, temperature).
    bool fieldIsDiverging() const;

private:
    bool  m_paused        = true;
    bool  m_stepRequested = false;
    float m_elapsedTime   = 0.0f;
    int   m_stepCount     = 0;

    void refreshField();
    void refreshStreamlines();

    std::vector<SceneMesh> m_meshes;
    int                    m_nextMeshId = 1;
    int                    m_selectedId = 0;

    FluidSolver   m_solver;
    FieldSnapshot m_field;
    Grid3<float>  m_displayGrid;  // m_field.values as a grid, for sampling along streamlines

    StreamlineSnapshot m_streamlines;
    int                m_streamlineFieldVersion = -1;  // m_field.version the lines were traced in
    int                m_streamlineResolution   = 0;
    DisplayField  m_fieldShown  = DisplayField::VelocityMagnitude;
    bool          m_fieldStale  = true;
    bool          m_rangeStale  = true;  // jump to the next auto range instead of easing into it
};
