#include "fluid_solver.hpp"

#include "sdf.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>

namespace
{
    constexpr int   maxSubsteps         = 16;
    constexpr float maxCfl              = 2.0f;    // cells travelled per substep
    constexpr float negligibleDiffusion = 1e-4f;   // skip diffusion below this nu * dt / h^2
    constexpr int   extrapolationLayers = 2;

    const glm::ivec3 axisStep[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

    // Sign of each free-stream component, ignoring components too small to matter.
    glm::ivec3 flowDirection(const glm::vec3& velocity)
    {
        const float speed = glm::length(velocity);
        glm::ivec3 flow(0);
        if (speed < 1e-6f)
            return flow;
        for (int a = 0; a < 3; ++a)
            if (std::abs(velocity[a]) > 1e-3f * speed)
                flow[a] = velocity[a] > 0.0f ? 1 : -1;
        return flow;
    }

    // Spreads values into invalid cells from their valid 6-neighbours, one layer per pass.
    void extrapolate(Grid3<float>& field, Grid3<std::uint8_t>& valid, int layers)
    {
        const glm::ivec3 n = field.size();
        Grid3<std::uint8_t> next;
        for (int layer = 0; layer < layers; ++layer)
        {
            next = valid;
#pragma omp parallel for
            for (int k = 0; k < n.z; ++k)
                for (int j = 0; j < n.y; ++j)
                    for (int i = 0; i < n.x; ++i)
                    {
                        if (valid(i, j, k))
                            continue;
                        float sum   = 0.0f;
                        int   count = 0;
                        for (int a = 0; a < 3; ++a)
                            for (int s = -1; s <= 1; s += 2)
                            {
                                const glm::ivec3 nb = glm::ivec3(i, j, k) + s * axisStep[a];
                                if (valid.inRange(nb.x, nb.y, nb.z) && valid(nb.x, nb.y, nb.z))
                                {
                                    sum += field(nb.x, nb.y, nb.z);
                                    ++count;
                                }
                            }
                        if (count > 0)
                        {
                            field(i, j, k) = sum / static_cast<float>(count);
                            next(i, j, k)  = 1;
                        }
                    }
            valid.swap(next);
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool FluidSolver::configure(const FluidParameters& fluid, const SolverParameters& params,
                            std::span<const Obstacle> obstacles)
{
    // Meshes without triangles can't block anything, so they don't count as obstacles.
    std::vector<Obstacle> used;
    for (const Obstacle& o : obstacles)
        if (o.mesh && !o.mesh->indices.empty() && used.size() < 255)  // owners are stored in a byte
            used.push_back(o);

    m_thermals.clear();
    m_anyFixed = m_anyInsulated = false;
    for (const Obstacle& o : used)
    {
        m_thermals.push_back(o.thermal);
        (o.thermal.boundary == ThermalBoundary::FixedTemperature ? m_anyFixed : m_anyInsulated) = true;
    }

    DomainKey key;
    for (const Obstacle& o : used)
        key.meshes.emplace_back(o.meshVersion, o.model);
    key.resolution  = params.gridResolution;
    key.padding     = params.domainPadding;
    key.wake        = params.wakeLength;
    key.flow        = flowDirection(fluid.velocity);
    if (m_configured && key == m_key)
        return false;

    m_key         = std::move(key);
    m_configured  = true;
    m_hasObstacle = !used.empty();
    buildDomain(params, used, m_key.flow);
    buildPressureMatrix();
    reset(fluid, params);
    return true;
}

const ObstacleThermal* FluidSolver::thermalAt(int i, int j, int k) const
{
    if (!m_owner.inRange(i, j, k))
        return nullptr;
    const int owner = m_owner(i, j, k);
    return owner > 0 ? &m_thermals[owner - 1] : nullptr;
}

void FluidSolver::buildDomain(const SolverParameters& params, std::span<const Obstacle> obstacles, glm::ivec3 flow)
{
    // Without a mesh the domain is a 2 m cube around the origin.
    glm::vec3 lo(-1.0f), hi(1.0f);
    m_length = 2.0f;
    if (!obstacles.empty())
    {
        lo = glm::vec3(std::numeric_limits<float>::max());
        hi = glm::vec3(std::numeric_limits<float>::lowest());
        for (const Obstacle& o : obstacles)
        {
            glm::vec3 meshLo, meshHi;
            meshBounds(*o.mesh, o.model, meshLo, meshHi);
            lo = glm::min(lo, meshLo);
            hi = glm::max(hi, meshHi);
        }
        const glm::vec3 size = hi - lo;
        float sides[3] = { size.x, size.y, size.z };
        std::sort(sides, sides + 3);
        // The median side: for a wing it is the chord, not the span or the thickness.
        m_length = std::max({ sides[1], 1e-3f * sides[2], 1e-4f });

        const float padding = std::max(params.domainPadding, 0.0f) * m_length;
        const float wake    = std::max(params.wakeLength, 0.0f) * m_length;
        lo -= padding;
        hi += padding;
        for (int a = 0; a < 3; ++a)
        {
            if (flow[a] > 0)
                hi[a] += wake;
            else if (flow[a] < 0)
                lo[a] -= wake;
        }
    }

    const glm::vec3 extent  = hi - lo;
    const float     longest = std::max({ extent.x, extent.y, extent.z });

    Domain domain;
    domain.h      = longest / static_cast<float>(std::max(params.gridResolution, 4));
    domain.size   = glm::max(glm::ivec3(glm::ceil(extent / domain.h - 1e-3f)), glm::ivec3(4));
    domain.origin = 0.5f * (lo + hi) - 0.5f * glm::vec3(domain.size) * domain.h;
    m_grid.allocate(domain);

    const bool still = flow == glm::ivec3(0);
    for (int a = 0; a < 3; ++a)
    {
        BoundaryType& low  = m_grid.boundaries[2 * a];
        BoundaryType& high = m_grid.boundaries[2 * a + 1];
        if (still)
            low = high = BoundaryType::Wall;
        else if (flow[a] > 0)
            low = BoundaryType::Inflow, high = BoundaryType::Outflow;
        else if (flow[a] < 0)
            low = BoundaryType::Outflow, high = BoundaryType::Inflow;
        else
            low = high = BoundaryType::Inflow;  // far field parallel to the flow
    }

    m_owner.resize(domain.size, 0);
    if (!obstacles.empty())
    {
        // The union of the obstacles is the minimum of their distances. Each solid cell
        // belongs to the first obstacle containing it, for its thermal boundary.
        m_grid.phi.fill(std::numeric_limits<float>::max());
        Grid3<float> meshPhi;
        for (std::size_t o = 0; o < obstacles.size(); ++o)
        {
            buildSdf(*obstacles[o].mesh, obstacles[o].model, domain, meshPhi);
            for (std::size_t n = 0; n < meshPhi.count(); ++n)
            {
                m_grid.phi[n] = std::min(m_grid.phi[n], meshPhi[n]);
                if (meshPhi[n] < 0.0f && m_owner[n] == 0)
                    m_owner[n] = static_cast<std::uint8_t>(o + 1);
            }
        }
        for (std::size_t n = 0; n < m_grid.phi.count(); ++n)
            m_grid.solid[n] = m_grid.phi[n] < 0.0f ? 1 : 0;
    }
    else
    {
        m_grid.phi.fill(std::numeric_limits<float>::max());
        m_grid.solid.fill(0);
    }

    m_diagnostics          = {};
    m_diagnostics.gridSize = domain.size;
    m_diagnostics.cellSize = domain.h;
}

void FluidSolver::buildPressureMatrix()
{
    const glm::ivec3 n = m_grid.cells();
    StencilMatrix&   A = m_pressureMatrix;
    A.resize(n);
    m_pressureSingular = true;

    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                if (m_grid.cellSolid(i, j, k))
                    continue;
                float diag = 0.0f;
                for (int a = 0; a < 3; ++a)
                    for (int side = 0; side < 2; ++side)
                    {
                        const glm::ivec3 nb = glm::ivec3(i, j, k) + (side ? 1 : -1) * axisStep[a];
                        if (!m_grid.solid.inRange(nb.x, nb.y, nb.z))
                        {
                            // Outflow faces hold p = 0 in a ghost cell; walls and inflow are Neumann.
                            if (m_grid.boundaries[2 * a + side] == BoundaryType::Outflow)
                            {
                                diag += 1.0f;
                                m_pressureSingular = false;
                            }
                            continue;
                        }
                        if (m_grid.cellSolid(nb.x, nb.y, nb.z))
                            continue;
                        diag += 1.0f;
                        if (side == 1)
                        {
                            Grid3<float>& plus = a == 0 ? A.plusI : a == 1 ? A.plusJ : A.plusK;
                            plus(i, j, k) = -1.0f;
                        }
                    }
                A.diag(i, j, k) = diag;
            }
}

void FluidSolver::reset(const FluidParameters& fluid, const SolverParameters& params)
{
    m_freeStream = fluid.velocity;
    m_ambient    = fluid.ambientTemperature;
    for (int a = 0; a < 3; ++a)
        m_grid.velocity[a].fill(m_freeStream[a]);
    m_grid.pressure.fill(0.0f);
    m_grid.temperature.fill(m_ambient);

    // Projecting the uniform stream gives potential flow around the obstacle, a
    // divergence-free start. The time step only scales the pressure, which is discarded.
    applyVelocityBoundaries();
    project(params, std::max(fluid.density, 1e-6f), 1.0f);
    m_grid.pressure.fill(0.0f);
    extrapolateVelocity();
    applyTemperatureBoundaries();
}

// ---------------------------------------------------------------------------
// Time stepping
// ---------------------------------------------------------------------------

void FluidSolver::advance(const FluidParameters& fluid, const SolverParameters& params, float duration)
{
    const auto start = std::chrono::steady_clock::now();
    m_freeStream = fluid.velocity;
    m_ambient    = fluid.ambientTemperature;

    float maxSpeed = std::max({ std::abs(m_freeStream.x), std::abs(m_freeStream.y), std::abs(m_freeStream.z) });
    for (const Grid3<float>& component : m_grid.velocity)
        for (float value : component.values())
            maxSpeed = std::max(maxSpeed, std::abs(value));

    const float h       = m_grid.domain.h;
    const int   wanted  = static_cast<int>(std::ceil(maxSpeed * duration / (maxCfl * h)));
    const int   substeps = std::clamp(wanted, 1, maxSubsteps);
    const float dt       = duration / static_cast<float>(substeps);

    for (int s = 0; s < substeps; ++s)
        substep(fluid, params, dt);

    const float speed = glm::length(m_freeStream);
    m_diagnostics.substeps       = substeps;
    m_diagnostics.substepsCapped = wanted > maxSubsteps;
    m_diagnostics.cfl            = maxSpeed * dt / h;
    m_diagnostics.maxDivergence  = maxDivergence();
    m_diagnostics.reynolds       = fluid.viscosity > 0.0f ? fluid.density * speed * m_length / fluid.viscosity
                                                          : std::numeric_limits<float>::infinity();
    m_diagnostics.stepMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void FluidSolver::substep(const FluidParameters& fluid, const SolverParameters& params, float dt)
{
    advectVelocity(dt);
    advectTemperature(dt);
    addBuoyancy(fluid, dt);
    applyVelocityBoundaries();
    diffuseVelocity(fluid, params, dt);
    diffuseTemperature(fluid, params, dt);
    applyTemperatureBoundaries();
    project(params, std::max(fluid.density, 1e-6f), dt);
    extrapolateVelocity();
}

// ---------------------------------------------------------------------------
// Advection (semi-Lagrangian, RK2 backtrace)
// ---------------------------------------------------------------------------

glm::vec3 FluidSolver::traceBack(const glm::vec3& position, float dt) const
{
    const glm::vec3 mid  = position - 0.5f * dt * m_grid.velocityAt(position);
    glm::vec3       prev = position - dt * m_grid.velocityAt(mid);

    // Departure points inside the obstacle are pushed back out to its surface.
    if (m_hasObstacle)
    {
        const float d = m_grid.phiAt(prev);
        if (d < 0.0f)
        {
            const glm::vec3 g   = m_grid.phiGradient(prev);
            const float     len = glm::length(g);
            if (len > 1e-6f)
                prev -= d * g / len;
        }
    }
    return prev;
}

bool FluidSolver::enteredThroughInflow(const glm::vec3& p) const
{
    const glm::vec3 lo = m_grid.domain.origin;
    const glm::vec3 hi = lo + m_grid.domain.extent();
    for (int a = 0; a < 3; ++a)
    {
        if (p[a] < lo[a] && m_grid.boundaries[2 * a] == BoundaryType::Inflow)
            return true;
        if (p[a] > hi[a] && m_grid.boundaries[2 * a + 1] == BoundaryType::Inflow)
            return true;
    }
    return false;
}

void FluidSolver::advectVelocity(float dt)
{
    for (int a = 0; a < 3; ++a)
    {
        const glm::ivec3 n   = m_grid.faces(a);
        Grid3<float>&    dst = m_velocityScratch[a];
        if (dst.size() != n)
            dst.resize(n);
#pragma omp parallel for
        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                {
                    const glm::vec3 prev = traceBack(m_grid.facePosition(a, i, j, k), dt);
                    dst(i, j, k) = enteredThroughInflow(prev) ? m_freeStream[a]
                                                              : m_grid.sampleFace(a, m_grid.velocity[a], prev);
                }
    }
    // Swap only after all three components are advected: the backtrace reads all of them.
    for (int a = 0; a < 3; ++a)
        m_grid.velocity[a].swap(m_velocityScratch[a]);
}

void FluidSolver::advectTemperature(float dt)
{
    const glm::ivec3 n = m_grid.cells();
    if (m_scalarScratch.size() != n)
        m_scalarScratch.resize(n);
#pragma omp parallel for
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                const glm::vec3 prev = traceBack(m_grid.domain.cellCenter(i, j, k), dt);
                m_scalarScratch(i, j, k) = enteredThroughInflow(prev) ? m_ambient
                                                                      : m_grid.sampleCell(m_grid.temperature, prev);
            }
    m_grid.temperature.swap(m_scalarScratch);
}

// ---------------------------------------------------------------------------
// Forces and diffusion
// ---------------------------------------------------------------------------

void FluidSolver::addBuoyancy(const FluidParameters& fluid, float dt)
{
    if (!fluid.buoyancy || fluid.thermalExpansion == 0.0f)
        return;

    for (int a = 0; a < 3; ++a)
    {
        const float g = fluid.gravity[a];
        if (g == 0.0f)
            continue;
        Grid3<float>&    f = m_grid.velocity[a];
        const glm::ivec3 n = f.size();
#pragma omp parallel for
        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                {
                    const glm::ivec3 c(i, j, k);
                    if (c[a] == 0 || c[a] == n[a] - 1 || m_grid.faceSolid(a, i, j, k))
                        continue;
                    const glm::ivec3 below = c - axisStep[a];
                    const float T = 0.5f * (m_grid.temperature(i, j, k) + m_grid.temperature(below.x, below.y, below.z));
                    f(i, j, k) -= dt * fluid.thermalExpansion * (T - m_ambient) * g;
                }
    }
}

void FluidSolver::diffuseVelocity(const FluidParameters& fluid, const SolverParameters& params, float dt)
{
    const float h     = m_grid.domain.h;
    const float alpha = fluid.kinematicViscosity() * dt / (h * h);
    if (alpha < negligibleDiffusion)
        return;

    // Backward Euler, (I - dt nu Laplacian) u_new = u, one component at a time. Unknowns are
    // the interior, non-solid faces; the obstacle is no-slip (u = 0), inflow faces hold the
    // free stream, walls slip and outflow faces have zero normal gradient.
    for (int a = 0; a < 3; ++a)
    {
        Grid3<float>&    f = m_grid.velocity[a];
        const glm::ivec3 n = f.size();
        StencilMatrix&   A = m_diffusionMatrix;
        A.resize(n);
        m_rhs.resize(n);

        auto isBoundaryFace = [&](const glm::ivec3& c) { return c[a] == 0 || c[a] == n[a] - 1; };

        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                {
                    const glm::ivec3 c(i, j, k);
                    if (isBoundaryFace(c) || m_grid.faceSolid(a, i, j, k))
                        continue;
                    float diag = 1.0f;
                    float rhs  = f(i, j, k);
                    for (int d = 0; d < 3; ++d)
                        for (int side = 0; side < 2; ++side)
                        {
                            const glm::ivec3 nb = c + (side ? 1 : -1) * axisStep[d];
                            if (!f.inRange(nb.x, nb.y, nb.z))
                            {
                                if (m_grid.boundaries[2 * d + side] == BoundaryType::Inflow)
                                {
                                    diag += alpha;
                                    rhs += alpha * m_freeStream[a];
                                }
                                continue;
                            }
                            if (m_grid.faceSolid(a, nb.x, nb.y, nb.z))
                            {
                                diag += alpha;  // no-slip: neighbour value 0
                                continue;
                            }
                            if (isBoundaryFace(nb))
                            {
                                if (m_grid.boundaries[2 * a + (nb[a] == 0 ? 0 : 1)] != BoundaryType::Outflow)
                                {
                                    diag += alpha;
                                    rhs += alpha * f(nb.x, nb.y, nb.z);
                                }
                                continue;
                            }
                            diag += alpha;
                            if (side == 1)
                            {
                                Grid3<float>& plus = d == 0 ? A.plusI : d == 1 ? A.plusJ : A.plusK;
                                plus(i, j, k) = -alpha;
                            }
                        }
                    A.diag(i, j, k) = diag;
                    m_rhs(i, j, k)  = rhs;
                }

        m_diffusionSolver.solve(A, m_rhs, f, params.solverIterations, params.pressureTolerance);
    }
    applyVelocityBoundaries();
}

void FluidSolver::diffuseTemperature(const FluidParameters& fluid, const SolverParameters& params, float dt)
{
    const float h     = m_grid.domain.h;
    const float alpha = fluid.thermalDiffusivity * dt / (h * h);
    if (alpha < negligibleDiffusion)
        return;

    const glm::ivec3 n     = m_grid.cells();
    StencilMatrix&   A     = m_diffusionMatrix;
    A.resize(n);
    m_rhs.resize(n);

    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                if (m_grid.cellSolid(i, j, k))
                    continue;
                float diag = 1.0f;
                float rhs  = m_grid.temperature(i, j, k);
                for (int d = 0; d < 3; ++d)
                    for (int side = 0; side < 2; ++side)
                    {
                        const glm::ivec3 nb = glm::ivec3(i, j, k) + (side ? 1 : -1) * axisStep[d];
                        if (!m_grid.solid.inRange(nb.x, nb.y, nb.z))
                        {
                            if (m_grid.boundaries[2 * d + side] == BoundaryType::Inflow)
                            {
                                diag += alpha;
                                rhs += alpha * m_ambient;
                            }
                            continue;
                        }
                        if (const ObstacleThermal* thermal = thermalAt(nb.x, nb.y, nb.z))
                        {
                            if (thermal->boundary == ThermalBoundary::FixedTemperature)
                            {
                                diag += alpha;
                                rhs += alpha * thermal->temperature;
                            }
                            continue;
                        }
                        diag += alpha;
                        if (side == 1)
                        {
                            Grid3<float>& plus = d == 0 ? A.plusI : d == 1 ? A.plusJ : A.plusK;
                            plus(i, j, k) = -alpha;
                        }
                    }
                A.diag(i, j, k) = diag;
                m_rhs(i, j, k)  = rhs;
            }

    m_diffusionSolver.solve(A, m_rhs, m_grid.temperature, params.solverIterations, params.pressureTolerance);
}

// ---------------------------------------------------------------------------
// Boundary conditions
// ---------------------------------------------------------------------------

void FluidSolver::applyVelocityBoundaries()
{
    for (int a = 0; a < 3; ++a)
    {
        Grid3<float>&    f = m_grid.velocity[a];
        const glm::ivec3 n = f.size();
#pragma omp parallel for
        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                {
                    const glm::ivec3 c(i, j, k);
                    if (m_hasObstacle && m_grid.faceSolid(a, i, j, k))
                    {
                        f(i, j, k) = 0.0f;  // static obstacle: no flow through it
                        continue;
                    }
                    if (c[a] != 0 && c[a] != n[a] - 1)
                        continue;

                    const int side = c[a] == 0 ? 0 : 1;
                    switch (m_grid.boundaries[2 * a + side])
                    {
                    case BoundaryType::Wall:
                        f(i, j, k) = 0.0f;
                        break;
                    case BoundaryType::Inflow:
                        f(i, j, k) = m_freeStream[a];
                        break;
                    case BoundaryType::Outflow:
                    {
                        const glm::ivec3 inner = c + (side ? -1 : 1) * axisStep[a];
                        f(i, j, k) = f(inner.x, inner.y, inner.z);
                        break;
                    }
                    }
                }
    }
}

void FluidSolver::applyTemperatureBoundaries()
{
    if (!m_hasObstacle)
        return;

    const glm::ivec3 n = m_grid.cells();
    if (m_anyInsulated)
    {
        // Insulated: no gradient across the surface, so the solid takes the neighbouring fluid's values.
        // This fills every obstacle; fixed-temperature ones are overwritten below.
        Grid3<std::uint8_t> valid(n);
        for (std::size_t c = 0; c < valid.count(); ++c)
            valid[c] = m_grid.solid[c] ? 0 : 1;
        extrapolate(m_grid.temperature, valid, extrapolationLayers);
    }

    if (m_anyFixed)
    {
        // Hold fixed-temperature obstacles and the layer of fluid touching them at their surface temperature.
#pragma omp parallel for
        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                {
                    const ObstacleThermal* thermal = thermalAt(i, j, k);
                    for (int a = 0; a < 3 && !thermal; ++a)
                        for (int s = -1; s <= 1 && !thermal; s += 2)
                        {
                            const glm::ivec3 nb = glm::ivec3(i, j, k) + s * axisStep[a];
                            thermal = thermalAt(nb.x, nb.y, nb.z);
                        }
                    if (thermal && thermal->boundary == ThermalBoundary::FixedTemperature)
                        m_grid.temperature(i, j, k) = thermal->temperature;
                }
    }
}

// ---------------------------------------------------------------------------
// Pressure projection
// ---------------------------------------------------------------------------

void FluidSolver::project(const SolverParameters& params, float density, float dt)
{
    const glm::ivec3 n     = m_grid.cells();
    const float      h     = m_grid.domain.h;
    const auto&      u     = m_grid.velocity[0];
    const auto&      v     = m_grid.velocity[1];
    const auto&      w     = m_grid.velocity[2];
    const StencilMatrix& A = m_pressureMatrix;

    // Right-hand side: -(rho h / dt) * (net outflow of each cell, in m/s).
    m_rhs.resize(n);
    const float scale  = density * h / dt;
    double      sum    = 0.0;
    long long   active = 0;
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                if (A.diag(i, j, k) == 0.0f)
                {
                    m_rhs(i, j, k) = 0.0f;
                    continue;
                }
                const float flux = u(i + 1, j, k) - u(i, j, k) + v(i, j + 1, k) - v(i, j, k) + w(i, j, k + 1) - w(i, j, k);
                m_rhs(i, j, k) = -scale * flux;
                sum += m_rhs(i, j, k);
                ++active;
            }

    // A closed box only has a solution if the net flux is zero, so remove its mean.
    if (m_pressureSingular && active > 0)
    {
        const float mean = static_cast<float>(sum / static_cast<double>(active));
        for (std::size_t c = 0; c < m_rhs.count(); ++c)
            if (A.diag[c] != 0.0f)
                m_rhs[c] -= mean;
    }

    const SolveResult result =
        m_pressureSolver.solve(A, m_rhs, m_grid.pressure, params.solverIterations, params.pressureTolerance);
    m_diagnostics.pressureIterations = result.iterations;
    m_diagnostics.pressureResidual   = result.residual;

    if (m_pressureSingular && active > 0)
    {
        double total = 0.0;
        for (std::size_t c = 0; c < m_grid.pressure.count(); ++c)
            if (A.diag[c] != 0.0f)
                total += m_grid.pressure[c];
        const float mean = static_cast<float>(total / static_cast<double>(active));
        for (std::size_t c = 0; c < m_grid.pressure.count(); ++c)
            if (A.diag[c] != 0.0f)
                m_grid.pressure[c] -= mean;
    }

    // Subtract the pressure gradient from every face the fluid can move through.
    const float coef = dt / (density * h);
    for (int a = 0; a < 3; ++a)
    {
        Grid3<float>&    f  = m_grid.velocity[a];
        const glm::ivec3 nf = f.size();
#pragma omp parallel for
        for (int k = 0; k < nf.z; ++k)
            for (int j = 0; j < nf.y; ++j)
                for (int i = 0; i < nf.x; ++i)
                {
                    if (m_grid.faceSolid(a, i, j, k))
                        continue;
                    const glm::ivec3 c(i, j, k);
                    const glm::ivec3 below = c - axisStep[a];
                    if (c[a] == 0)
                    {
                        if (m_grid.boundaries[2 * a] == BoundaryType::Outflow)
                            f(i, j, k) -= coef * m_grid.pressure(i, j, k);
                    }
                    else if (c[a] == nf[a] - 1)
                    {
                        if (m_grid.boundaries[2 * a + 1] == BoundaryType::Outflow)
                            f(i, j, k) += coef * m_grid.pressure(below.x, below.y, below.z);
                    }
                    else
                    {
                        f(i, j, k) -= coef * (m_grid.pressure(i, j, k) - m_grid.pressure(below.x, below.y, below.z));
                    }
                }
    }
}

void FluidSolver::extrapolateVelocity()
{
    if (!m_hasObstacle)
        return;
    for (int a = 0; a < 3; ++a)
    {
        Grid3<float>&       f = m_grid.velocity[a];
        Grid3<std::uint8_t> valid(f.size());
        const glm::ivec3    n = f.size();
        for (int k = 0; k < n.z; ++k)
            for (int j = 0; j < n.y; ++j)
                for (int i = 0; i < n.x; ++i)
                    valid(i, j, k) = m_grid.faceSolid(a, i, j, k) ? 0 : 1;
        extrapolate(f, valid, extrapolationLayers);
    }
}

float FluidSolver::maxDivergence() const
{
    const glm::ivec3 n = m_grid.cells();
    const auto&      u = m_grid.velocity[0];
    const auto&      v = m_grid.velocity[1];
    const auto&      w = m_grid.velocity[2];
    float            m = 0.0f;
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                if (m_pressureMatrix.diag(i, j, k) == 0.0f)
                    continue;
                // Solid faces hold extrapolated values between steps; the projection saw them as 0.
                auto face = [&](int a, int fi, int fj, int fk, const Grid3<float>& g)
                { return m_grid.faceSolid(a, fi, fj, fk) ? 0.0f : g(fi, fj, fk); };
                const float flux = face(0, i + 1, j, k, u) - face(0, i, j, k, u) + face(1, i, j + 1, k, v) -
                                   face(1, i, j, k, v) + face(2, i, j, k + 1, w) - face(2, i, j, k, w);
                m = std::max(m, std::abs(flux));
            }
    return m / m_grid.domain.h;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void FluidSolver::displayField(DisplayField field, const FluidParameters& fluid, std::vector<float>& out) const
{
    const glm::ivec3 n = m_grid.cells();
    out.resize(static_cast<std::size_t>(n.x) * n.y * n.z);

    const float     dynamicPressure = 0.5f * fluid.density * glm::dot(fluid.velocity, fluid.velocity);
    const glm::vec3 centre          = m_grid.domain.center();
    const auto&     u = m_grid.velocity[0];
    const auto&     v = m_grid.velocity[1];
    const auto&     w = m_grid.velocity[2];

#pragma omp parallel for
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                const std::size_t idx = m_grid.pressure.index(i, j, k);
                if (m_grid.solid[idx])
                {
                    out[idx] = 0.0f;
                    continue;
                }
                float value = 0.0f;
                switch (field)
                {
                case DisplayField::VelocityMagnitude:
                    value = glm::length(glm::vec3(0.5f * (u(i, j, k) + u(i + 1, j, k)),
                                                  0.5f * (v(i, j, k) + v(i, j + 1, k)),
                                                  0.5f * (w(i, j, k) + w(i, j, k + 1))));
                    break;
                case DisplayField::Pressure:
                    value = m_grid.pressure[idx];
                    break;
                case DisplayField::TotalPressure:
                    value = m_grid.pressure[idx] +
                            fluid.density * glm::dot(fluid.gravity, m_grid.domain.cellCenter(i, j, k) - centre);
                    break;
                case DisplayField::PressureCoefficient:
                    value = dynamicPressure > 0.0f ? m_grid.pressure[idx] / dynamicPressure : 0.0f;
                    break;
                case DisplayField::Temperature:
                    value = m_grid.temperature[idx] - 273.15f;
                    break;
                }
                out[idx] = value;
            }
}
