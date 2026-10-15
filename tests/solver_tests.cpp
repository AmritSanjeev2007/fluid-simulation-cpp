// Headless checks of the fluid solver (see "Validation" in solver.md).
// Run: ./build/release/solver_tests [path/to/aerofoil.obj]

#include "meshes/obj_loader.hpp"
#include "meshes/raycast.hpp"
#include "solver/fluid_solver.hpp"
#include "solver/streamlines.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    int g_failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("  [%s] %s\n", ok ? "pass" : "FAIL", what.c_str());
        if (!ok)
            ++g_failures;
    }

    // Closed UV sphere centred on the origin.
    MeshData makeSphere(float radius, int stacks = 16, int slices = 32)
    {
        MeshData mesh;
        auto add = [&](glm::vec3 p) { mesh.vertices.push_back({ p * radius, p, {} }); };
        add({ 0, 1, 0 });
        for (int s = 1; s < stacks; ++s)
        {
            const float theta = glm::pi<float>() * s / stacks;
            for (int l = 0; l < slices; ++l)
            {
                const float phi = 2.0f * glm::pi<float>() * l / slices;
                add({ std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) });
            }
        }
        add({ 0, -1, 0 });
        const auto ring = [&](int s, int l) { return static_cast<std::uint32_t>(1 + (s - 1) * slices + (l % slices)); };
        const std::uint32_t south = static_cast<std::uint32_t>(mesh.vertices.size() - 1);
        for (int l = 0; l < slices; ++l)
        {
            mesh.indices.insert(mesh.indices.end(), { 0u, ring(1, l + 1), ring(1, l) });
            mesh.indices.insert(mesh.indices.end(), { south, ring(stacks - 1, l), ring(stacks - 1, l + 1) });
        }
        for (int s = 1; s < stacks - 1; ++s)
            for (int l = 0; l < slices; ++l)
            {
                mesh.indices.insert(mesh.indices.end(), { ring(s, l), ring(s, l + 1), ring(s + 1, l) });
                mesh.indices.insert(mesh.indices.end(), { ring(s, l + 1), ring(s + 1, l + 1), ring(s + 1, l) });
            }
        return mesh;
    }

    float maxSpeed(const MacGrid& g)
    {
        float m = 0.0f;
        for (const auto& c : g.velocity)
            for (float v : c.values())
                m = std::max(m, std::abs(v));
        return m;
    }

    void printDiagnostics(const FluidSolver& solver)
    {
        const SolverDiagnostics& d = solver.diagnostics();
        std::printf("  grid %dx%dx%d, h = %.4f m, %d substeps, CFL %.2f, pressure %d it (residual %.2e), "
                    "max div %.2e 1/s, %.1f ms\n",
                    d.gridSize.x, d.gridSize.y, d.gridSize.z, d.cellSize, d.substeps, d.cfl, d.pressureIterations,
                    d.pressureResidual, d.maxDivergence, d.stepMilliseconds);
    }

    void stillWater()
    {
        std::printf("Still water, gravity on, buoyancy off\n");
        FluidParameters  fluid;
        SolverParameters params;
        params.gridResolution = 32;
        FluidSolver solver;
        solver.configure(fluid, params, {});
        for (int s = 0; s < 10; ++s)
            solver.advance(fluid, params, params.timeStep);
        check(maxSpeed(solver.grid()) < 1e-6f, "velocity stays zero");

        std::vector<float> total;
        solver.displayField(DisplayField::TotalPressure, fluid, total);
        const MacGrid& g = solver.grid();
        const float    dp = total[g.pressure.index(0, 0, 0)] - total[g.pressure.index(0, g.cells().y - 1, 0)];
        const float    expected = fluid.density * 9.81f * (g.cells().y - 1) * g.domain.h;
        check(std::abs(dp - expected) < 1e-3f * expected, "total pressure is hydrostatic");
    }

    void uniformFlow()
    {
        std::printf("Uniform flow, no mesh\n");
        FluidParameters fluid;
        fluid.velocity = { 1.0f, 0.5f, 0.0f };
        SolverParameters params;
        params.gridResolution = 32;
        FluidSolver solver;
        solver.configure(fluid, params, {});
        for (int s = 0; s < 10; ++s)
            solver.advance(fluid, params, params.timeStep);
        float err = 0.0f;
        for (int a = 0; a < 3; ++a)
            for (float v : solver.grid().velocity[a].values())
                err = std::max(err, std::abs(v - fluid.velocity[a]));
        check(err < 1e-4f, "velocity stays at the free stream");
        float pmax = 0.0f;
        for (float p : solver.grid().pressure.values())
            pmax = std::max(pmax, std::abs(p));
        check(pmax < 1e-3f, "motion pressure stays zero");
    }

    void sphereFlow()
    {
        std::printf("Flow past a sphere (r = 0.5 m, U = 1 m/s along +x)\n");
        const MeshData sphere = makeSphere(0.5f);
        Obstacle       obstacle{ &sphere, 1, glm::mat4(1.0f), {} };

        FluidParameters fluid;
        fluid.velocity  = { 1.0f, 0.0f, 0.0f };
        fluid.viscosity = 0.1f;  // nu = 1e-4, Re = 10^4 at L = 1 m
        SolverParameters params;
        params.gridResolution = 64;
        FluidSolver solver;
        solver.configure(fluid, params, { &obstacle, 1 });
        const MacGrid& g = solver.grid();

        // SDF: centre inside, distance accurate near the surface.
        const float hcell = g.domain.h;
        check(g.phiAt({ 0, 0, 0 }) < 0.0f && g.phiAt({ 0, 0.9f, 0 }) > 0.0f, "SDF sign inside/outside");
        check(std::abs(g.phiAt({ 0.0f, 0.0f, 0.7f }) - 0.2f) < 0.5f * hcell, "SDF distance near the surface");

        for (int s = 0; s < 40; ++s)
            solver.advance(fluid, params, params.timeStep);
        printDiagnostics(solver);
        check(solver.diagnostics().pressureResidual <= params.pressureTolerance, "pressure solve converged");
        check(solver.diagnostics().maxDivergence < 1e-2f, "velocity is divergence-free");

        std::vector<float> cp;
        solver.displayField(DisplayField::PressureCoefficient, fluid, cp);
        const float front = g.sampleCell(g.pressure, { -0.5f - hcell, 0, 0 }) / (0.5f * fluid.density);
        const float side  = g.sampleCell(g.pressure, { 0, 0.5f + hcell, 0 }) / (0.5f * fluid.density);
        std::printf("  Cp at front stagnation point %.2f, at the side %.2f\n", front, side);
        check(front > 0.5f, "stagnation pressure in front of the sphere");
        check(side < 0.0f, "suction over the side of the sphere");
        const glm::vec3 ahead = g.velocityAt({ -0.5f - hcell, 0, 0 });
        check(std::abs(ahead.x) < 0.5f, "flow slows down at the stagnation point");

        Grid3<float> speed(g.cells());
        solver.displayField(DisplayField::VelocityMagnitude, fluid, speed.values());
        Streamlines lines;
        traceStreamlines(g, fluid.velocity, true, 8, speed, lines);
        bool startsUpstream = true, staysOutside = true, reachesOutflow = false;
        for (std::size_t l = 0; l < lines.first.size(); ++l)
        {
            const glm::vec4& start = lines.points[lines.first[l]];
            const glm::vec4& end   = lines.points[lines.first[l] + lines.count[l] - 1];
            startsUpstream = startsUpstream && start.x < g.domain.origin.x;
            reachesOutflow = reachesOutflow || end.x > g.domain.origin.x + g.domain.extent().x - g.domain.h;
            for (int p = 1; p < lines.count[l]; ++p)
                staysOutside = staysOutside && g.phiAt(glm::vec3(lines.points[lines.first[l] + p])) >= 0.0f;
        }
        std::printf("  %zu streamlines, %zu points\n", lines.first.size(), lines.points.size());
        check(lines.first.size() == 64, "one streamline per seed");
        check(startsUpstream, "streamlines start upstream of the domain");
        check(staysOutside && reachesOutflow, "streamlines go around the sphere to the outflow");
    }

    void heatedSphere()
    {
        std::printf("Heated sphere in still water, buoyancy on\n");
        const MeshData sphere = makeSphere(0.25f);
        Obstacle       obstacle{ &sphere, 1, glm::mat4(1.0f), {} };
        obstacle.thermal.temperature = 293.15f + 50.0f;

        FluidParameters fluid;
        fluid.buoyancy           = true;
        fluid.thermalExpansion   = 2e-3f;
        fluid.thermalDiffusivity = 1e-4f;  // exercises the implicit diffusion path
        SolverParameters params;
        params.gridResolution = 48;
        FluidSolver solver;
        solver.configure(fluid, params, { &obstacle, 1 });
        for (int s = 0; s < 30; ++s)
            solver.advance(fluid, params, params.timeStep);
        printDiagnostics(solver);
        const MacGrid& g     = solver.grid();
        const float    above = g.velocityAt({ 0, 0.4f, 0 }).y;
        const float    temp  = g.sampleCell(g.temperature, { 0, 0.3f, 0 }) - 293.15f;
        std::printf("  vertical velocity above the sphere %.4f m/s, temperature rise %.2f K\n", above, temp);
        check(above > 0.0f, "warm fluid rises");
        check(temp > 0.0f, "heat spreads from the surface");
    }

    void viscousFlow()
    {
        std::printf("Very viscous flow (implicit viscosity)\n");
        const MeshData sphere = makeSphere(0.3f);
        Obstacle       obstacle{ &sphere, 1, glm::mat4(1.0f), {} };
        FluidParameters fluid;
        fluid.velocity  = { 0.0f, 0.0f, 0.5f };
        fluid.density   = 1.0f;
        fluid.viscosity = 0.1f;  // nu = 0.1 m^2/s, far beyond the explicit limit
        SolverParameters params;
        params.gridResolution = 32;
        FluidSolver solver;
        solver.configure(fluid, params, { &obstacle, 1 });
        for (int s = 0; s < 20; ++s)
            solver.advance(fluid, params, params.timeStep);
        printDiagnostics(solver);
        const float m = maxSpeed(solver.grid());
        check(std::isfinite(m) && m < 2.0f, "stays stable");
    }

    void twoObstacles()
    {
        std::printf("Two spheres: one held at 70 C, one insulated, in still water\n");
        const MeshData sphere = makeSphere(0.2f);
        Obstacle heated{ &sphere, 1, glm::translate(glm::mat4(1.0f), { -0.5f, 0.0f, 0.0f }), {} };
        heated.thermal.temperature = 293.15f + 50.0f;
        Obstacle insulated{ &sphere, 2, glm::translate(glm::mat4(1.0f), { 0.5f, 0.0f, 0.0f }), {} };
        insulated.thermal.boundary = ThermalBoundary::Insulated;
        const Obstacle obstacles[] = { heated, insulated };

        FluidParameters fluid;
        fluid.thermalDiffusivity = 1e-4f;
        SolverParameters params;
        params.gridResolution = 48;
        FluidSolver solver;
        check(solver.configure(fluid, params, obstacles), "configures with two obstacles");
        check(!solver.configure(fluid, params, obstacles), "unchanged obstacles don't rebuild the domain");
        const MacGrid& g = solver.grid();
        check(g.phiAt({ -0.5f, 0, 0 }) < 0.0f && g.phiAt({ 0.5f, 0, 0 }) < 0.0f && g.phiAt({ 0, 0, 0 }) > 0.0f,
              "both spheres are solid, the gap between them is not");

        for (int s = 0; s < 10; ++s)
            solver.advance(fluid, params, params.timeStep);
        const float nearHeated    = g.sampleCell(g.temperature, { -0.5f, 0.25f, 0 }) - 293.15f;
        const float nearInsulated = g.sampleCell(g.temperature, { 0.5f, 0.25f, 0 }) - 293.15f;
        std::printf("  temperature rise next to the heated sphere %.2f K, the insulated one %.2f K\n",
                    nearHeated, nearInsulated);
        check(nearHeated > 1.0f, "the heated sphere warms the water next to it");
        check(std::abs(nearInsulated) < 0.5f, "the insulated sphere does not");

        Obstacle moved = insulated;
        moved.model    = glm::translate(glm::mat4(1.0f), { 0.6f, 0.0f, 0.0f });
        const Obstacle movedObstacles[] = { heated, moved };
        check(solver.configure(fluid, params, movedObstacles), "moving one obstacle rebuilds the domain");
    }

    void picking()
    {
        std::printf("Mouse picking\n");
        const MeshData  sphere = makeSphere(0.5f);
        const glm::mat4 model  = glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, -3.0f });

        const Ray  straight{ { 0, 0, 0 }, { 0, 0, -1 } };
        const auto hit = raycast(sphere, model, straight);
        check(hit && std::abs(*hit - 2.5f) < 0.02f, "ray hits the near side of the sphere");
        check(!raycast(sphere, model, Ray{ { 0, 0, 0 }, { 0, 0, 1 } }), "ray pointing away misses");
        check(!raycast(sphere, model, Ray{ { 1, 0, 0 }, { 0, 0, -1 } }), "ray beside the sphere misses");
        const glm::mat4 scaled = glm::scale(model, glm::vec3(3.0f));
        check(raycast(sphere, scaled, Ray{ { 1, 0, 0 }, { 0, 0, -1 } }).has_value(), "model scale is applied");

        // The centre of the screen looks straight down the camera's forward axis.
        const glm::mat4 viewProjection = glm::perspective(glm::radians(45.0f), 1.5f, 0.1f, 100.0f) *
                                         glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        const Ray centre = screenRay({ 0.0f, 0.0f }, glm::inverse(viewProjection));
        check(raycast(sphere, model, centre).has_value(), "screen centre picks the sphere ahead");
        const Ray corner = screenRay({ 0.9f, 0.9f }, glm::inverse(viewProjection));
        check(!raycast(sphere, model, corner), "screen corner misses it");

        check(isObjPath("meshes/wing.obj") && isObjPath("C:/Meshes/WING.OBJ"), ".obj files are accepted");
        check(!isObjPath("wing.stl") && !isObjPath("wing.obj.txt") && !isObjPath("obj"), "other files are not");
    }

    void aerofoil(const char* path)
    {
        std::printf("Aerofoil from %s (U = 1 m/s along +z), 64 cells\n", path);
        const MeshData mesh = loadObj(path);
        Obstacle       obstacle{ &mesh, 1, glm::mat4(1.0f), {} };
        FluidParameters fluid;
        fluid.velocity = { 0.0f, 0.0f, 1.0f };
        SolverParameters params;
        FluidSolver solver;
        solver.configure(fluid, params, { &obstacle, 1 });
        int solidCells = 0;
        for (auto s : solver.grid().solid.values())
            solidCells += s;
        std::printf("  %d solid cells\n", solidCells);
        check(solidCells > 0, "mesh is voxelised");
        for (int s = 0; s < 5; ++s)
        {
            solver.advance(fluid, params, params.timeStep);
            printDiagnostics(solver);
        }
        check(solver.diagnostics().maxDivergence < 1e-1f, "velocity is divergence-free");
    }
}

int main(int argc, char* argv[])
{
    stillWater();
    uniformFlow();
    sphereFlow();
    heatedSphere();
    viscousFlow();
    twoObstacles();
    picking();
    if (argc > 1)
        aerofoil(argv[1]);

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
