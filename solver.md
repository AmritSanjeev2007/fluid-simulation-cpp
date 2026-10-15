# Fluid Solver Design

This document describes how the fluid solver works. The solver lives in `src/solver/` and is driven by `SimulationInfo::update()` (`src/simulation_info.cpp`). [Implementation status](#implementation-status) lists what is built and what is still planned.

## Scope

| Decision | Choice |
|---|---|
| Physics | Incompressible, Newtonian, single-phase flow (constant density, constant viscosity) |
| Discretisation | Eulerian grid, staggered **MAC grid**, operator splitting with **pressure projection** |
| Obstacles | Any number of closed (watertight) meshes, converted to one **signed distance field (SDF)** on the grid |
| Extra scalar | **Temperature**: advected and diffused passive scalar, with optional **Boussinesq buoyancy** |
| Output | Colour-coded **pressure**, **temperature** and **velocity** |
| Units | SI throughout: metres, seconds, kilograms, kelvin, pascals. Mesh coordinates are metres (the GUI already labels mesh position in m). |

Out of scope: free surfaces, multiphase flow, compressibility, non-Newtonian fluids, moving or deforming obstacles (the mesh is static while the simulation runs; editing its transform rebuilds the SDF, see [Obstacles](#obstacles-signed-distance-field)).

## Parameters

### Existing parameters (already in the code)

These come from `FluidParameters` and `SolverParameters` (now in `src/solver/solver_parameters.hpp`) and are edited in `Gui::drawFluid` / `Gui::drawSolver` (`src/gui.cpp`).

| Parameter | Field | Default | GUI range | Role in the solver |
|---|---|---|---|---|
| Viscosity | `fluid.viscosity` | `0.001` | 0 to 0.1 Pa s (log slider) | **Dynamic viscosity** $\mu$ in Pa·s. The default is water at 20 °C. The solver uses the kinematic viscosity $\nu = \mu / \rho$. |
| Density | `fluid.density` | `1000` kg/m³ | 1 to 2000 | Constant density $\rho$. Scales pressure ($p$ is in Pa) and converts $\mu$ to $\nu$. |
| Gravity | `fluid.gravity` | `(0, -9.81, 0)` m/s² | ±50 per axis | Direction and strength of $\mathbf{g}$. Drives buoyancy and the hydrostatic part of the displayed pressure (see [Gravity and pressure](#gravity-and-pressure)). |
| Velocity | `fluid.velocity` | `(0, 0, 0)` m/s | unbounded | **Free-stream velocity** $\mathbf{U}_\infty$: the flow entering the domain, and the initial velocity everywhere. Zero means a closed box of still fluid. |
| Time step | `solver.timeStep` | `0.016` s | 0.001 to 0.05 | Simulated time advanced per `update()` call. Split into CFL-limited substeps (see [Time stepping](#time-stepping)). |
| Grid resolution | `solver.gridResolution` | `64` | 16 to 256 | Number of cells along the **longest** domain axis. The other axes use the same cell size $h$. |
| Iterations | `solver.solverIterations` | `100` (was 20) | 1 to 500 | Maximum iterations of the pressure solve (and the implicit diffusion solves). |

**Why viscosity is read as dynamic viscosity.** The default of 0.001 together with the density default of 1000 kg/m³ is exactly water ($\mu = 1.0\times10^{-3}$ Pa·s, $\nu = 1.0\times10^{-6}$ m²/s). Reading it as kinematic viscosity would make the default 1000 times more viscous than water. The GUI slider is labelled `Pa s` accordingly.

### Parameters added for the solver

Temperature, domain size and visualisation needed parameters that did not exist before the solver. Defaults are for water:

| Parameter | Suggested home | Default | Meaning |
|---|---|---|---|
| Ambient temperature $T_\infty$ | `FluidParameters` | 293.15 K (20 °C) | Temperature of the inflow and of the initial fluid; reference temperature for buoyancy. |
| Thermal diffusivity $\kappa$ | `FluidParameters` | $1.43\times10^{-7}$ m²/s | Diffusion coefficient of the temperature field. |
| Thermal expansion coefficient $\beta$ | `FluidParameters` | $2.07\times10^{-4}$ 1/K | Strength of Boussinesq buoyancy. |
| Buoyancy enabled | `FluidParameters` | off | Toggles the Boussinesq force term. |
| Mesh surface temperature $T_s$ | `MeshInfo::thermal` | 293.15 K | Fixed temperature of the obstacle surface (a heated or cooled body). |
| Mesh thermal boundary | `MeshInfo::thermal` | fixed temperature | Fixed temperature (Dirichlet) or insulated (zero flux). |
| Domain padding | `SolverParameters` | 1.0 | Space around the mesh, as a multiple of its characteristic size $L$ (the median side of its bounding box). |
| Wake length | `SolverParameters` | 2.0 | Extra space downstream of the mesh (in the direction of $\mathbf{U}_\infty$), same unit as padding. |
| Pressure tolerance | `SolverParameters` | $10^{-4}$ | Relative residual at which the pressure solve stops early. |
| Displayed field, colour range, slice axis/position | `ViewSettings` in `SimulationInfo` | velocity, auto, Z / centre | See [Visualisation](#visualisation). |

## Governing equations

Incompressible Navier–Stokes with the Boussinesq approximation, plus a temperature transport equation:

$$\frac{\partial \mathbf{u}}{\partial t} + (\mathbf{u}\cdot\nabla)\mathbf{u} = -\frac{1}{\rho}\nabla p + \nu\nabla^2\mathbf{u} - \beta\,(T - T_\infty)\,\mathbf{g}$$

$$\nabla\cdot\mathbf{u} = 0$$

$$\frac{\partial T}{\partial t} + (\mathbf{u}\cdot\nabla)T = \kappa\nabla^2 T$$

- $\mathbf{u}$ is velocity, $p$ is the **motion pressure** (pressure minus the hydrostatic part, see below), $\nu = \mu/\rho$.
- The buoyancy term is $-\beta(T - T_\infty)\mathbf{g}$: with gravity pointing down, fluid warmer than ambient is pushed up. It is zero when buoyancy is disabled.

### Gravity and pressure

With constant density, gravity on its own produces only a hydrostatic pressure gradient $\rho\mathbf{g}$ and no motion. Adding $\mathbf{g}$ to the velocity and letting the projection cancel it works, but wastes solver accuracy on a large, uninteresting gradient. So the solver works with the motion pressure $p$ and never adds $\mathbf{g}$ directly. Gravity enters only through:

1. the buoyancy term (when enabled), and
2. the displayed **total pressure** $p_\text{total} = p + \rho\,\mathbf{g}\cdot(\mathbf{x} - \mathbf{x}_\text{ref})$, with $\mathbf{x}_\text{ref}$ the centre of the domain. The visualisation can show either.

## Domain and grid

### Domain box

The simulation domain is an axis-aligned box in world space:

- **With a mesh:** the mesh's world-space bounding box (vertices transformed by `MeshTransform::modelMatrix()`), with characteristic size $L$ equal to its **median** side, expanded by `padding × L` on every side and by an additional `wakeLength × L` on the downstream side(s), meaning the faces whose outward normal $\mathbf{n}$ has $\mathbf{U}_\infty\cdot\mathbf{n} > 0$.
- **Without a mesh:** a fixed 2 m cube centred on the origin.

$L$ is the median side rather than the largest because for slender bodies the largest side is misleading: for the test aerofoil it is the 3.7 m span, while the flow scale is set by the 1.2 m chord. Padding by the span would spend most of the grid on empty space and leave the chord only a few cells long.

The domain is recomputed when the mesh, its transform, $\mathbf{U}_\infty$'s direction or the grid resolution changes, and every such change resets the fields.

### MAC grid

Cell size $h = (\text{longest domain side}) / \texttt{gridResolution}$. The cell counts are $n_x, n_y, n_z = \lceil \text{side} / h \rceil$, so the longest axis has exactly `gridResolution` cells.

Quantities are stored at staggered locations:

| Quantity | Location | Array size |
|---|---|---|
| $u$ (x-velocity) | centres of x-faces | $(n_x{+}1)\times n_y\times n_z$ |
| $v$ (y-velocity) | centres of y-faces | $n_x\times(n_y{+}1)\times n_z$ |
| $w$ (z-velocity) | centres of z-faces | $n_x\times n_y\times(n_z{+}1)$ |
| $p$, $T$, $\phi$ (SDF), cell type | cell centres | $n_x\times n_y\times n_z$ |

Storage is flat `std::vector<float>` arrays with x fastest (`index = i + nx * (j + ny * k)`), wrapped in a small `Grid3<T>` type with bounds-checked accessors in debug builds and trilinear sampling at world positions.

Each cell is tagged `Fluid` or `Solid` from the SDF. A face is solid if either neighbouring cell is solid.

Memory: at the maximum of 256 cells per axis the grid can reach 256×256×256 ≈ 16.8 M cells. With six float fields plus solver scratch (about 12 floats per cell) that is roughly 800 MB, so very high resolutions are only practical for elongated domains. 64 to 128 is the intended working range.

## Obstacles: signed distance field

The obstacles are the loaded meshes (`MeshData` from `obj_loader`). Each must be closed, which the README already requires. With several meshes, each gets its own SDF as below and the grid stores their minimum (the union of the solids); the domain box encloses all of them. Every solid cell remembers the first mesh containing it, so each mesh keeps its own thermal boundary (fixed temperature or insulated). Meshes should not overlap: where they do, the union is still solid but the overlap's thermal owner is whichever mesh came first.

### Building the SDF

1. Transform every vertex to world space with `modelMatrix()`. Working in world space keeps distances correct under non-uniform scale.
2. **Unsigned distance, narrow band:** for each triangle, visit the cells within its bounding box expanded by 3 cells and store the minimum exact point-to-triangle distance from each cell centre.
3. **Fill the rest:** propagate distances outward with fast sweeping (or set far cells to a large value; only the sign and the band near the surface matter to the solver).
4. **Sign:** for each grid row along x, cast a ray and count crossings with the mesh triangles. Cells after an odd number of crossings are inside ($\phi < 0$). Because the mesh is closed, parity is reliable; a ray that hits an edge or vertex exactly is retried with a tiny jitter.

$\phi$ is negative inside the solid, positive in the fluid, and its gradient (by central differences) is the outward surface normal.

The SDF is rebuilt whenever the mesh, its transform or the grid changes. While the simulation is running, a transform edit rebuilds the SDF, sets velocity to zero inside the new solid region and leaves the rest of the flow as it is.

### Boundary conditions on the mesh

- **Velocity:** no-slip, $\mathbf{u} = 0$ on the surface (the obstacle is static). Solid faces are set to 0 before the projection (no penetration), and the viscosity solve treats the surface as $\mathbf{u} = 0$ (no-slip). With $\nu \approx 0$ the result is effectively free-slip, which is the correct inviscid limit.
- **Pressure:** zero normal gradient ($\partial p/\partial n = 0$), which is what the projection gets by leaving solid neighbours out of the Poisson stencil.
- **Temperature:** $T = T_s$ (fixed temperature) or $\partial T/\partial n = 0$ (insulated), per the mesh's thermal setting.

The first version uses the binary cell mask above, which makes the surface look staircased at low resolution. A planned improvement is cut-cell face fractions computed from $\phi$ at face corners, used as weights in the pressure solve (the variational method of Batty, Bertails and Bridson, 2007), which gives smooth boundaries at the same resolution.

## Domain boundary conditions

Each of the six domain faces is classified by its outward normal $\mathbf{n}$:

| Face type | Condition | Velocity | Pressure | Temperature |
|---|---|---|---|---|
| **Inflow** ($\mathbf{U}_\infty\cdot\mathbf{n} < 0$) and **side** ($\mathbf{U}_\infty\cdot\mathbf{n} = 0$) | Far field | $\mathbf{u} = \mathbf{U}_\infty$ | $\partial p/\partial n = 0$ | $T = T_\infty$ |
| **Outflow** ($\mathbf{U}_\infty\cdot\mathbf{n} > 0$) | Open | $\partial\mathbf{u}/\partial n = 0$ | $p = 0$ | $\partial T/\partial n = 0$ |

A component counts as zero if it is below $10^{-3}|\mathbf{U}_\infty|$.

If $\mathbf{U}_\infty = 0$, all six faces are solid walls ($\mathbf{u}\cdot\mathbf{n} = 0$). The pressure system then has only Neumann conditions and is singular: the solver removes the mean from the right-hand side before solving and the mean from $p$ after solving. This is the setup for buoyancy-driven flows such as a heated body in still water.

## Time stepping

### Substeps

Each `update()` advances by `solver.timeStep`. Inside it, the solver takes $N$ equal substeps of $\Delta t = \texttt{timeStep}/N$, where

$$N = \max\left(1,\ \left\lceil \frac{u_\text{max}\,\texttt{timeStep}}{C_\text{max}\,h} \right\rceil\right),\qquad C_\text{max} = 2$$

with $u_\text{max}$ the largest face velocity (including $|\mathbf{U}_\infty|$). Semi-Lagrangian advection is stable for any CFL number, but accuracy near the obstacle drops sharply above about 2. $N$ is capped (for example at 16) so a blow-up cannot freeze the app. When the cap is hit, the GUI shows a warning.

Simulated time per rendered frame is therefore fixed at `timeStep`, independent of frame rate, which keeps runs reproducible.

### One substep

```
step(dt):
    1. advect velocity        u* = advect(u, u, dt)
    2. advect temperature     T* = advect(T, u, dt)
    3. add buoyancy           u* += dt * (-beta * (T* - T_inf) * g)      // if enabled
    4. diffuse velocity       solve (I - dt*nu*Laplacian) u** = u*       // skipped if nu*dt/h^2 is negligible
    5. diffuse temperature    solve (I - dt*kappa*Laplacian) T = T*
    6. apply boundaries       inflow/outflow faces, solid faces = 0, T on mesh surface
    7. project                solve Laplacian p = (rho/dt) div u**,  u = u** - (dt/rho) grad p
    8. extrapolate velocity   copy fluid velocities 2 cells into the solid band
    9. diagnostics            max divergence, CFL, residual, iterations used
```

### 1–2. Advection

Semi-Lagrangian: for each sample point $\mathbf{x}$ (a face centre for velocity components, a cell centre for $T$), trace back to $\mathbf{x}_\text{prev} = \mathbf{x} - \Delta t\,\mathbf{u}$ using **second-order Runge–Kutta** (midpoint), then sample the old field there with trilinear interpolation.

- If $\mathbf{x}_\text{prev}$ lands inside the solid ($\phi < 0$), push it back to the surface along the normal: $\mathbf{x}_\text{prev} \leftarrow \mathbf{x}_\text{prev} - \phi\,\nabla\phi$.
- If it leaves the domain through an inflow face, use the boundary value ($\mathbf{U}_\infty$ or $T_\infty$). If it leaves through an outflow face, clamp to the domain.
- Step 8's extrapolation ensures interpolation near the wall never reads stale values from inside the solid.

Plain semi-Lagrangian advection is very diffusive. A later upgrade is **MacCormack / BFECC** with min–max clamping, which keeps vortices and thermal plumes much sharper for little extra cost.

### 3. Buoyancy

With buoyancy enabled, add $-\beta(T - T_\infty)\,\mathbf{g}\,\Delta t$ to each face velocity, with $T$ averaged from the two cells sharing the face.

### 4–5. Diffusion

Explicit diffusion is only stable when $\nu\Delta t/h^2 \le 1/6$. With the GUI's maximum viscosity (0.1 Pa·s at 1000 kg/m³, $\nu = 10^{-4}$ m²/s) that holds for typical $h$, but low densities make $\nu$ very large: at $\rho = 1$ kg/m³ the same slider gives $\nu = 0.1$ m²/s, which is unstable for any useful step. So:

- If $\nu\Delta t/h^2 < 10^{-4}$, skip the viscosity step: numerical diffusion from advection is already larger. This is the case for water at typical domain sizes (for example $h = 1/64$ m and $\Delta t = 0.016$ s give $6.6\times10^{-5}$).
- Otherwise solve implicitly (backward Euler), $(I - \Delta t\,\nu\nabla^2)\,\mathbf{u}^{**} = \mathbf{u}^{*}$, one component at a time, with the same iterative solver as the pressure, warm-started from $\mathbf{u}^{*}$. This is unconditionally stable.

Temperature diffusion works the same way with $\kappa$.

### 7. Pressure projection

Make the velocity divergence-free. For each fluid cell, the discrete Poisson equation is

$$\sum_{\text{fluid nbrs } j}\frac{p_i - p_j}{h^2} + \sum_{\text{outflow nbrs}}\frac{p_i - 0}{h^2} = -\frac{\rho}{\Delta t}\,(\nabla\cdot\mathbf{u}^{**})_i$$

Solid neighbours and wall faces drop out of the sum (Neumann). Outflow faces add a ghost cell with $p = 0$ (Dirichlet). The divergence uses the face velocities directly: $(\nabla\cdot\mathbf{u})_i = (u_{i+1/2} - u_{i-1/2} + v_{j+1/2} - v_{j-1/2} + w_{k+1/2} - w_{k-1/2})/h$, with solid faces held at 0.

Then update every non-solid, non-inflow face: $u_{i+1/2} \leftarrow u_{i+1/2} - \frac{\Delta t}{\rho}\,\frac{p_{i+1} - p_i}{h}$.

**Linear solver.** The matrix is symmetric positive (semi-)definite, so the recommended solver is **preconditioned conjugate gradient** with a **MIC(0)** (modified incomplete Cholesky) preconditioner, the standard choice for this system:

- `solver.solverIterations` is the maximum number of CG iterations. The solve stops earlier when the relative residual drops below the pressure tolerance.
- The solve is warm-started from the previous substep's pressure.
- A simpler first implementation is red–black Gauss–Seidel with over-relaxation ($\omega \approx 1.8$). It is easier to write and to parallelise, but converges much more slowly.

**Iterations:** the default was raised from 20 to 100 and the slider to 500, with the tolerance ($10^{-4}$) usually stopping the solve much earlier. Thanks to the warm start, the aerofoil at 64 cells converges in about 10 to 35 PCG iterations per step. The Diagnostics panel shows the iterations used, the residual and the remaining divergence.

## Temperature

Temperature is a cell-centred scalar, initialised to $T_\infty$ everywhere. It is advected with the flow, diffused with $\kappa$, held at $T_s$ in the band of fluid cells next to the mesh (if the mesh has a fixed temperature), and enters the momentum equation only through buoyancy.

With buoyancy off it is a pure passive tracer, which is still useful for seeing where heat from the body goes. With $T_s = T_\infty$ and buoyancy off, the field stays uniform and costs one advection and one diffusion per substep. The solver can skip it entirely in that case.

## Visualisation

After each `update()` the solver uploads the fields the renderer needs to **3D textures** (`GL_R32F`): pressure, temperature and velocity magnitude (face velocities averaged to cell centres), plus the SDF. 3D textures are available in OpenGL 4.1, so this works on macOS as well.

### What can be shown

| Field | Colour map | Default range |
|---|---|---|
| **Velocity magnitude** $\lvert\mathbf{u}\rvert$ (m/s) | Sequential (viridis) | 0 to $\max\lvert\mathbf{u}\rvert$ |
| **Pressure** (Pa), motion or total | Diverging (blue–white–red) centred on 0 | Symmetric, $\pm\max\lvert p\rvert$ |
| **Pressure coefficient** $C_p = p / (\tfrac12\rho\lvert\mathbf{U}_\infty\rvert^2)$ | Diverging, centred on 0 | −1 to 1; only offered when $\mathbf{U}_\infty \ne 0$ |
| **Temperature** (K or °C) | Diverging centred on $T_\infty$ | Symmetric around $T_\infty$ |

Ranges are either **auto** (1st to 99th percentile, smoothed over about 10 frames so colours do not flicker) or **manual**. The GUI shows a colour bar with the field's unit next to the "View" section.

### Where it is drawn

1. **Slice plane:** an axis-aligned plane through the domain (axis and position chosen in the GUI), drawn with a shader that samples the 3D texture and maps it through the colour map. Cells inside the solid are drawn transparent, so the mesh shows through.
2. **Volume:** with the slice turned off, the whole domain is ray-marched in a full-screen pass (`volume.vert/.frag`), so the field's 3D distribution is visible at once. The colour ramp's opacity goes from fully transparent at the low end of the range to 0.5 at the high end. That opacity is what a ray collects crossing the domain's longest side through uniform fluid, so the look doesn't depend on grid resolution, and a **Volume density** slider scales it. Rays stop at solid cells, so the mesh is hidden behind fluid in front of it but not behind fluid behind it. The GUI colour bar shows the same fade.
3. **Streamlines:** lines traced downstream through the velocity field (midpoint steps of half a cell) and coloured by the selected field. They start from a uniform square grid of seeds on a plane perpendicular to $\mathbf{U}_\infty$, placed one cell outside the domain box upstream of its most upstream corner and spanning the domain's cross-section. Outside the domain the flow is the free stream, so each line runs straight from its seed to where it enters the box, then follows the computed velocity field. **Streamline resolution** (enabled only in this view) sets the number of seeds per side. Seeds whose straight path misses the box are skipped (only possible for oblique flow), and a line stops when it leaves the domain, reaches the obstacle or stagnates. Lines are retraced on the CPU after every step (`src/solver/streamlines.*`). With $\mathbf{U}_\infty = 0$ there is no seed plane, so nothing is drawn.
4. **Mesh surface colouring:** the mesh shader samples the chosen field at each fragment's world position offset by $1.5h$ along the surface normal (just outside the solid), so the obstacle itself shows surface pressure or temperature. This is the natural view for pressure and $C_p$ on an aerofoil.
5. **Velocity arrows** (later): instanced arrow glyphs on a coarse sub-grid of the slice plane, coloured by speed.

An **Opacity** slider fades whichever of the slice, volume or streamlines is shown (premultiplied alpha; below full opacity the slice and streamlines stop writing depth, so the mesh and domain box show through them).

The domain box is drawn as a thin wireframe so its extent is visible.

## Diagnostics

Shown in the GUI's Simulation panel:

- Grid size ($n_x\times n_y\times n_z$), cell size $h$, substeps $N$ per update, CFL number.
- Pressure solve: iterations used and final relative residual; max $\lvert\nabla\cdot\mathbf{u}\rvert\,h/\lvert\mathbf{U}_\infty\rvert$ after projection.
- Reynolds number $Re = \rho\lvert\mathbf{U}_\infty\rvert L/\mu$, with $L$ the mesh's largest size.
- Later: **drag and lift** on the mesh from the integrated surface pressure, $\mathbf{F} = -\oint p\,\mathbf{n}\,dA$, plus the viscous contribution.

**A note on Reynolds number.** Water at 1 m/s past a 1 m body gives $Re = 10^6$. A grid with 64 to 256 cells cannot resolve the boundary layer or the turbulence at that Reynolds number. The solver's numerical diffusion then dominates the physical viscosity, so the result is a plausible-looking flow with an effectively much lower Reynolds number (an implicit large-eddy simulation). Qualitative features (stagnation point, suction peak, wake, vortex shedding at moderate $Re$) are meaningful; exact drag coefficients at high $Re$ are not. Showing $Re$ alongside the grid size makes this visible to the user.

## Code structure

```
src/solver/                (no OpenGL calls; linked into both the app and the tests)
  solver_parameters.hpp    FluidParameters, SolverParameters, ObstacleThermal
  grid3.hpp                Grid3<T>: flat 3D array with index helpers and trilinear sampling
  mac_grid.*               Domain, boundary types, MacGrid: u, v, w, p, T, phi, solid mask
  sdf.*                    buildSdf(): narrow-band distance plus ray-parity sign
  streamlines.*            traceStreamlines(): seed plane upstream of the mesh, RK2 tracing
  linear_solver.*          StencilMatrix and MIC(0)-preconditioned CG
  fluid_solver.*           FluidSolver: domain setup, boundaries, advection, buoyancy,
                           diffusion, projection, diagnostics and display fields
src/render/
  field_renderer.*         3D texture upload, slice plane, domain wireframe
  colormap.hpp             CPU copies of the colour maps, for the GUI colour bar
assets/shaders/
  field.vert/.frag         slice plane colour mapping
  volume.vert/.frag        volume ray marching
  streamline.vert/.frag    streamline colouring
  line.vert/.frag          domain wireframe
tests/
  solver_tests.cpp         headless checks from the Validation table
```

Boundary handling and advection live in `fluid_solver.cpp` rather than separate files, because they share all of the solver's state.

Integration with the existing code:

- `SimulationInfo` owns a `FluidSolver`. `update()` first calls `configure()`, which rebuilds the domain if the mesh, its transform, the grid resolution, the padding or the direction of $\mathbf{U}_\infty$ changed (resetting the fields and the clock), then `advance()` when running. `reset()` reinitialises the fields to $\mathbf{U}_\infty$ (projected, so the start is potential flow around the body) and $T_\infty$.
- `SimulationInfo::setMesh` takes the CPU `MeshData` as well as the name, so the SDF can be built from it.
- After each step `SimulationInfo` copies the selected display field into a `FieldSnapshot`, which `FieldRenderer` uploads to 3D textures only when its version changes.
- The solver runs on the CPU, synchronously in the main loop. Grid loops use OpenMP when CMake finds it. The MIC(0) preconditioner is inherently sequential, so it is the main single-threaded cost. A GPU compute-shader version is possible later, but compute shaders need OpenGL 4.3, which macOS does not have, so the CPU solver remains the reference.

## Implementation status

Built:

1. `Grid3`, `MacGrid`, domain box from the mesh bounds; advection and projection.
2. Inflow, outflow, far-field and wall boundaries driven by $\mathbf{U}_\infty$.
3. SDF from the mesh, solid cells, no-penetration, velocity extrapolation into the solid.
4. Slice-plane, volume and streamline visualisation of every field with a colour bar and auto range.
5. Implicit viscosity (skipped automatically when negligible).
6. Temperature, mesh surface temperature (fixed or insulated), Boussinesq buoyancy.
7. $C_p$, total pressure and the diagnostics panel.

Still planned:

- Mesh surface colouring and velocity arrows.
- MacCormack/BFECC advection, cut-cell (variational) pressure, drag and lift.
- Running the solver on a worker thread so a slow step does not stall the UI.
- Loading meshes from the "Upload mesh..." button (the aerofoil is still hard-coded in `main.cpp`).

## Validation

`tests/solver_tests.cpp` automates the still-fluid, uniform-flow and divergence rows, a heated sphere, a very viscous flow, and flow past a sphere at 64 cells (front stagnation $C_p \approx 0.85$, side suction $C_p \approx -0.66$; the inviscid values are 1 and −1.25, and the gap is the coarse grid). The rest are still checked by eye in the app.

| Test | Expected result |
|---|---|
| Still fluid, $\mathbf{U}_\infty = 0$, gravity on, buoyancy off | Velocity stays exactly 0; total pressure is hydrostatic, $\rho g$ per metre of depth. |
| Uniform flow, no mesh | Velocity stays $\mathbf{U}_\infty$ everywhere; motion pressure stays 0. |
| Divergence after projection | Max $\lvert\nabla\cdot\mathbf{u}\rvert$ below the tolerance when the solver converges within the iteration cap. |
| Sphere at low $Re$ (≈ 1–20) | Symmetric, steady flow; attached recirculation bubble behind the sphere for $Re \gtrsim 20$. |
| Sphere at $Re \approx 300$ | Unsteady wake with periodic vortex shedding. |
| Heated sphere in still water, buoyancy on | A plume rises against $\mathbf{g}$; reversing gravity reverses it. |
| Aerofoil at an angle of attack | Stagnation point near the leading edge ($C_p \approx 1$), suction over the upper surface ($C_p < 0$). |
| Grid refinement 32 → 64 → 128 | Fields converge; the wake sharpens. |
