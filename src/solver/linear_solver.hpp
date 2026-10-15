#pragma once

#include "grid3.hpp"

// Symmetric 7-point stencil matrix over a 3D grid of unknowns. Row (i,j,k) has
// diag(i,j,k) on the diagonal and couples to (i+1,j,k) with plusI(i,j,k), and so on;
// the -1 neighbours are read from the neighbour's plus entries. A cell with
// diag == 0 is not an unknown (its value is left untouched by the solver).
struct StencilMatrix
{
    Grid3<float> diag;
    Grid3<float> plusI;
    Grid3<float> plusJ;
    Grid3<float> plusK;

    void resize(glm::ivec3 size);
    void clear();

    // y = A x over the active cells.
    void multiply(const Grid3<float>& x, Grid3<float>& y) const;
};

struct SolveResult
{
    int   iterations = 0;
    float residual   = 0.0f;  // max-norm residual relative to the right-hand side
};

// Conjugate gradient with a modified incomplete Cholesky, MIC(0), preconditioner
// (Bridson, "Fluid Simulation for Computer Graphics", ch. 5). Works for the
// pressure Poisson system and the implicit diffusion systems alike.
class PcgSolver
{
public:
    // Solves A x = b, starting from the current x. Stops after maxIterations or
    // once the residual drops below tolerance * max|b|.
    SolveResult solve(const StencilMatrix& A, const Grid3<float>& b, Grid3<float>& x,
                      int maxIterations, float tolerance);

private:
    void buildPreconditioner(const StencilMatrix& A);
    void applyPreconditioner(const StencilMatrix& A, const Grid3<float>& r, Grid3<float>& z);

    Grid3<float> m_precon;
    Grid3<float> m_r, m_z, m_s, m_q;
};
