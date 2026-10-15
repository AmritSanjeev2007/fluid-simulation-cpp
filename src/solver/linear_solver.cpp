#include "linear_solver.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    double dot(const StencilMatrix& A, const Grid3<float>& a, const Grid3<float>& b)
    {
        const int n   = static_cast<int>(a.count());
        double          sum = 0.0;
#pragma omp parallel for reduction(+ : sum)
        for (int idx = 0; idx < n; ++idx)
            if (A.diag[idx] != 0.0f)
                sum += static_cast<double>(a[idx]) * b[idx];
        return sum;
    }

    float maxAbs(const StencilMatrix& A, const Grid3<float>& a)
    {
        float m = 0.0f;
        for (std::size_t idx = 0; idx < a.count(); ++idx)
            if (A.diag[idx] != 0.0f)
                m = std::max(m, std::abs(a[idx]));
        return m;
    }
}

void StencilMatrix::resize(glm::ivec3 size)
{
    diag.resize(size, 0.0f);
    plusI.resize(size, 0.0f);
    plusJ.resize(size, 0.0f);
    plusK.resize(size, 0.0f);
}

void StencilMatrix::clear()
{
    diag.fill(0.0f);
    plusI.fill(0.0f);
    plusJ.fill(0.0f);
    plusK.fill(0.0f);
}

void StencilMatrix::multiply(const Grid3<float>& x, Grid3<float>& y) const
{
    const int nx = diag.nx(), ny = diag.ny(), nz = diag.nz();
#pragma omp parallel for
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i)
            {
                if (diag(i, j, k) == 0.0f)
                {
                    y(i, j, k) = 0.0f;
                    continue;
                }
                float v = diag(i, j, k) * x(i, j, k);
                if (i + 1 < nx) v += plusI(i, j, k) * x(i + 1, j, k);
                if (i > 0)      v += plusI(i - 1, j, k) * x(i - 1, j, k);
                if (j + 1 < ny) v += plusJ(i, j, k) * x(i, j + 1, k);
                if (j > 0)      v += plusJ(i, j - 1, k) * x(i, j - 1, k);
                if (k + 1 < nz) v += plusK(i, j, k) * x(i, j, k + 1);
                if (k > 0)      v += plusK(i, j, k - 1) * x(i, j, k - 1);
                y(i, j, k) = v;
            }
}

void PcgSolver::buildPreconditioner(const StencilMatrix& A)
{
    constexpr float tau   = 0.97f;  // MIC blending
    constexpr float sigma = 0.25f;  // safety against tiny pivots

    const glm::ivec3 n = A.diag.size();
    m_precon.resize(n, 0.0f);
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                const float d = A.diag(i, j, k);
                if (d == 0.0f)
                    continue;

                float e = d;
                if (i > 0)
                {
                    const float a = A.plusI(i - 1, j, k) * m_precon(i - 1, j, k);
                    const float b = (A.plusJ(i - 1, j, k) + A.plusK(i - 1, j, k)) * m_precon(i - 1, j, k);
                    e -= a * a + tau * A.plusI(i - 1, j, k) * b * m_precon(i - 1, j, k);
                }
                if (j > 0)
                {
                    const float a = A.plusJ(i, j - 1, k) * m_precon(i, j - 1, k);
                    const float b = (A.plusI(i, j - 1, k) + A.plusK(i, j - 1, k)) * m_precon(i, j - 1, k);
                    e -= a * a + tau * A.plusJ(i, j - 1, k) * b * m_precon(i, j - 1, k);
                }
                if (k > 0)
                {
                    const float a = A.plusK(i, j, k - 1) * m_precon(i, j, k - 1);
                    const float b = (A.plusI(i, j, k - 1) + A.plusJ(i, j, k - 1)) * m_precon(i, j, k - 1);
                    e -= a * a + tau * A.plusK(i, j, k - 1) * b * m_precon(i, j, k - 1);
                }
                if (e < sigma * d)
                    e = d;
                m_precon(i, j, k) = 1.0f / std::sqrt(e);
            }
}

void PcgSolver::applyPreconditioner(const StencilMatrix& A, const Grid3<float>& r, Grid3<float>& z)
{
    const glm::ivec3 n = A.diag.size();

    // Solve L q = r (q stored in z).
    for (int k = 0; k < n.z; ++k)
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i)
            {
                if (A.diag(i, j, k) == 0.0f)
                {
                    z(i, j, k) = 0.0f;
                    continue;
                }
                float t = r(i, j, k);
                if (i > 0) t -= A.plusI(i - 1, j, k) * m_precon(i - 1, j, k) * z(i - 1, j, k);
                if (j > 0) t -= A.plusJ(i, j - 1, k) * m_precon(i, j - 1, k) * z(i, j - 1, k);
                if (k > 0) t -= A.plusK(i, j, k - 1) * m_precon(i, j, k - 1) * z(i, j, k - 1);
                z(i, j, k) = t * m_precon(i, j, k);
            }

    // Solve L^T z = q in place.
    for (int k = n.z - 1; k >= 0; --k)
        for (int j = n.y - 1; j >= 0; --j)
            for (int i = n.x - 1; i >= 0; --i)
            {
                if (A.diag(i, j, k) == 0.0f)
                    continue;
                const float p = m_precon(i, j, k);
                float t = z(i, j, k);
                if (i + 1 < n.x) t -= A.plusI(i, j, k) * p * z(i + 1, j, k);
                if (j + 1 < n.y) t -= A.plusJ(i, j, k) * p * z(i, j + 1, k);
                if (k + 1 < n.z) t -= A.plusK(i, j, k) * p * z(i, j, k + 1);
                z(i, j, k) = t * p;
            }
}

SolveResult PcgSolver::solve(const StencilMatrix& A, const Grid3<float>& b, Grid3<float>& x,
                             int maxIterations, float tolerance)
{
    const glm::ivec3 n     = A.diag.size();
    const int        count = static_cast<int>(A.diag.count());
    m_r.resize(n);
    m_z.resize(n);
    m_s.resize(n);
    m_q.resize(n);

    const float bNorm = maxAbs(A, b);
    if (bNorm == 0.0f)
    {
        for (int idx = 0; idx < count; ++idx)
            if (A.diag[idx] != 0.0f)
                x[idx] = 0.0f;
        return {};
    }

    // r = b - A x
    A.multiply(x, m_q);
#pragma omp parallel for
    for (int idx = 0; idx < count; ++idx)
        m_r[idx] = A.diag[idx] != 0.0f ? b[idx] - m_q[idx] : 0.0f;

    SolveResult result;
    result.residual = maxAbs(A, m_r) / bNorm;
    if (result.residual <= tolerance)
        return result;

    buildPreconditioner(A);
    applyPreconditioner(A, m_r, m_z);
    m_s = m_z;
    double rho = dot(A, m_r, m_z);

    while (result.iterations < maxIterations)
    {
        ++result.iterations;
        A.multiply(m_s, m_q);
        const double sq = dot(A, m_s, m_q);
        if (sq <= 0.0)
            break;
        const float alpha = static_cast<float>(rho / sq);
#pragma omp parallel for
        for (int idx = 0; idx < count; ++idx)
        {
            x[idx] += alpha * m_s[idx];
            m_r[idx] -= alpha * m_q[idx];
        }

        result.residual = maxAbs(A, m_r) / bNorm;
        if (result.residual <= tolerance)
            break;

        applyPreconditioner(A, m_r, m_z);
        const double rhoNew = dot(A, m_r, m_z);
        const float  beta   = static_cast<float>(rhoNew / rho);
        rho = rhoNew;
#pragma omp parallel for
        for (int idx = 0; idx < count; ++idx)
            m_s[idx] = m_z[idx] + beta * m_s[idx];
    }
    return result;
}
