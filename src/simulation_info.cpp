#include "simulation_info.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

glm::mat4 MeshTransform::modelMatrix() const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::scale(model, scale);
}

void SimulationInfo::update()
{
    // A mesh's data never changes after it is added, so its id doubles as the data's version.
    std::vector<Obstacle> obstacles;
    obstacles.reserve(m_meshes.size());
    for (const SceneMesh& mesh : m_meshes)
        obstacles.push_back(Obstacle{ &mesh.data, mesh.info.id, mesh.info.transform.modelMatrix(), mesh.info.thermal });

    // A new domain starts from fresh fields, so the clock starts over too.
    if (m_solver.configure(fluid, solver, obstacles))
    {
        m_elapsedTime = 0.0f;
        m_stepCount   = 0;
        m_fieldStale  = m_rangeStale = true;
    }

    if (!m_paused || m_stepRequested)
    {
        m_solver.advance(fluid, solver, solver.timeStep);
        m_elapsedTime += solver.timeStep;
        ++m_stepCount;
        m_stepRequested = false;
        m_fieldStale    = true;
    }

    if (view.field != m_fieldShown)
        m_fieldStale = m_rangeStale = true;
    if (m_fieldStale)
        refreshField();

    if (view.display == FieldDisplay::Streamlines &&
        (m_streamlineFieldVersion != m_field.version || m_streamlineResolution != view.streamlineResolution))
        refreshStreamlines();
}

void SimulationInfo::refreshStreamlines()
{
    traceStreamlines(m_solver.grid(), fluid.velocity, m_solver.hasObstacle(), view.streamlineResolution,
                     m_displayGrid, m_streamlines.lines);
    ++m_streamlines.version;
    m_streamlineFieldVersion = m_field.version;
    m_streamlineResolution   = view.streamlineResolution;
}

void SimulationInfo::reset()
{
    m_paused        = true;
    m_stepRequested = false;
    m_elapsedTime   = 0.0f;
    m_stepCount     = 0;
    if (m_solver.isConfigured())
        m_solver.reset(fluid, solver);
    m_fieldStale = m_rangeStale = true;
}

int SimulationInfo::addMesh(std::string name, MeshData data)
{
    const int id = m_nextMeshId++;
    m_meshes.push_back(SceneMesh{ MeshInfo{ id, std::move(name), MeshTransform{}, ObstacleThermal{} }, std::move(data) });
    m_selectedId = id;
    return id;
}

void SimulationInfo::deleteMesh(int id)
{
    std::erase_if(m_meshes, [id](const SceneMesh& mesh) { return mesh.info.id == id; });
    if (m_selectedId == id)
        m_selectedId = 0;
}

MeshInfo* SimulationInfo::selectedMesh()
{
    for (SceneMesh& mesh : m_meshes)
        if (mesh.info.id == m_selectedId)
            return &mesh.info;
    return nullptr;
}

bool SimulationInfo::fieldIsDiverging() const
{
    return view.field != DisplayField::VelocityMagnitude;
}

void SimulationInfo::refreshField()
{
    const MacGrid& grid = m_solver.grid();
    if (m_displayGrid.size() != grid.cells())
        m_displayGrid.resize(grid.cells());
    m_solver.displayField(view.field, fluid, m_displayGrid.values());
    m_field.values = m_displayGrid.values();
    m_field.solid  = grid.solid.values();
    m_field.domain = grid.domain;
    ++m_field.version;
    m_fieldShown = view.field;
    m_fieldStale = false;

    if (!view.autoRange)
        return;

    // Fit the 1st to 99th percentile of the fluid cells, so a few extreme cells next to
    // the obstacle don't wash out the rest of the picture.
    std::vector<float> fluidValues;
    fluidValues.reserve(m_field.values.size());
    for (std::size_t c = 0; c < m_field.values.size(); ++c)
        if (!m_field.solid[c])
            fluidValues.push_back(m_field.values[c]);
    if (fluidValues.empty())
        return;

    auto percentile = [&](float q)
    {
        const auto nth = fluidValues.begin() + static_cast<std::ptrdiff_t>(q * static_cast<float>(fluidValues.size() - 1));
        std::nth_element(fluidValues.begin(), nth, fluidValues.end());
        return *nth;
    };
    const float lo = percentile(0.01f);
    const float hi = percentile(0.99f);

    constexpr float minimumSpan = 1e-3f;
    glm::vec2 target;
    if (fieldIsDiverging())
    {
        const float centre = view.field == DisplayField::Temperature ? fluid.ambientTemperature - 273.15f : 0.0f;
        const float half   = std::max({ std::abs(lo - centre), std::abs(hi - centre), minimumSpan });
        target = { centre - half, centre + half };
    }
    else
    {
        target = { 0.0f, std::max(hi, minimumSpan) };
    }

    // Ease towards the new range so colours don't flicker from step to step.
    view.range  = m_rangeStale ? target : view.range + 0.3f * (target - view.range);
    m_rangeStale = false;
}
