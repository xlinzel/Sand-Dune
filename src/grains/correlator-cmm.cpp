#include <grains/correlator.h>
#include <algorithm>
#include <fftw3.h>
#include <cmath>
#include <numeric>

//////////////////////////////////////////////////////
// File-Local CMM Helpers
//////////////////////////////////////////////////////

namespace
{
constexpr int kPeakPatchRadius = 2;
constexpr int kAutocorrelationRadius = 4;
constexpr int kPeakMaskRadius = 5;
constexpr float kSubpixelClamp = 0.49f;
constexpr float kNewtonStepClamp = 0.25f;
constexpr float kNearBoundaryThreshold = 0.45f;
constexpr float kPeakLockThreshold = 0.47f;

// Per-evaluation output of the local CMM objective.
// This is the information the Gauss-Newton loop needs at one candidate
// sub-pixel position: residual value, gradient, and a Gauss-Newton Hessian.
struct CmmEvalResult
{
    float eps = 1e30f; // Eq. (9) residual.
    float g_u = 0.0f;  // d eps / d u'
    float g_v = 0.0f;  // d eps / d v'
    float H_uu = 0.0f; // Gauss-Newton Hessian term.
    float H_uv = 0.0f; // Cross term.
    float H_vv = 0.0f; // Gauss-Newton Hessian term.
};

// Best-known solution inside one integer correlation cell.
// FindPeak() first chooses the discrete peak cell, SolveCell() refines the
// sub-pixel offset inside that cell, and the final displacement is
// (int_u + du, int_v + dv).
struct CmmCellSolution
{
    float du = 0.0f;   // Local sub-pixel u' in this cell.
    float dv = 0.0f;   // Local sub-pixel v' in this cell.
    float eps = 1e30f; // Best local residual.
    int int_u = 0;     // Integer u cell index.
    int int_v = 0;     // Integer v cell index.
    int row = 0;       // Peak row in ccmap.
    int col = 0;       // Peak col in ccmap.
    bool valid = false;
};

// Cached 1D bicubic interpolation weights for one row/column of the 5x5
// patch. Evaluate() builds these once per candidate shift and reuses them
// across the 2D interpolation loops.
struct BicubicBasis1D
{
    int base = 0;     // Left integer support sample.
    float w[4] = {};  // Cubic weights.
    float dw[4] = {}; // Weight derivatives wrt shift.
};

// The CMM fit requires a full 5x5 patch around the correlation peak, so peaks
// too close to the map border fall back to integer precision.
bool IsPeakTooCloseToEdge(const Eigen::MatrixXf& ccmap, int row, int col)
{
    return (row == 0 || row == ccmap.rows() - 1 || col == 0 || col == ccmap.cols() - 1)
        || row == 1 || row == ccmap.rows() - 2 || col == 1 || col == ccmap.cols() - 2;
}

float CubicWeight(float x)
{
    x = std::abs(x);
    if(x <= 1.0f)
        return ((1.5f * x - 2.5f) * x * x) + 1.0f;
    if(x < 2.0f)
        return (((-0.5f * x + 2.5f) * x - 4.0f) * x) + 2.0f;
    return 0.0f;
}

float CubicDerivative(float x)
{
    float sign = (x < 0.0f) ? -1.0f : 1.0f;
    x = std::abs(x);

    if(x <= 1.0f)
        return sign * (4.5f * x * x - 5.0f * x);
    if(x < 2.0f)
        return sign * (-1.5f * x * x + 5.0f * x - 4.0f);
    return 0.0f;
}

float OffsetExtent(float du, float dv)
{
    return std::max(std::abs(du), std::abs(dv));
}

// Choose between two cell-local solutions after the main solve and optional
// handoff. Residual wins first; when two cells are nearly tied, prefer the one
// less pinned against the +/-0.5 local boundary.
bool BetterCell(const CmmCellSolution& candidate, const CmmCellSolution& current)
{
    constexpr float eps_tol = 5e-4f;

    if(!candidate.valid)
        return false;
    if(!current.valid)
        return true;
    if(candidate.eps + eps_tol < current.eps)
        return true;
    if(std::abs(candidate.eps - current.eps) > eps_tol)
        return false;

    float cand_extent = OffsetExtent(candidate.du, candidate.dv);
    float current_extent = OffsetExtent(current.du, current.dv);
    bool cand_locked = cand_extent > kPeakLockThreshold;
    bool current_locked = current_extent > kPeakLockThreshold;

    if(current_locked && !cand_locked)
        return true;

    return cand_extent + 1e-4f < current_extent;
}

// Standard peak-to-second-peak ratio used throughout the pipeline as a quality
// metric. This is independent of the CMM solve and is computed from the final
// chosen correlation peak location.
float ComputeSignalToNoise(const Eigen::MatrixXf& ccmap, int row, int col)
{
    Eigen::MatrixXf ccmap_flattened = ccmap.array() - ccmap.minCoeff();
    float primary_peak = ccmap_flattened(row, col); // Chosen peak.

    int r0 = std::max(0, row - kPeakMaskRadius);
    int c0 = std::max(0, col - kPeakMaskRadius);
    int r1 = std::min(static_cast<int>(ccmap.rows()), row + kPeakMaskRadius + 1);
    int c1 = std::min(static_cast<int>(ccmap.cols()), col + kPeakMaskRadius + 1);

    ccmap_flattened.block(r0, c0, r1 - r0, c1 - c0).setZero();

    float second_peak = ccmap_flattened.maxCoeff(); // Next-highest peak.
    if(!std::isfinite(second_peak) || second_peak <= 1e-12f)
        return 0.0f;

    return primary_peak / second_peak;
}

// File-local state bundle for one FindPeak() call.
// This keeps the CMM machinery together: the measured correlation map, the
// reference autocorrelation patch R, timing sinks, and the helper methods that
// evaluate and solve the paper's local residual.
struct PeakSearchContext
{
    const Eigen::MatrixXf& ccmap;
    int rows;
    int cols;
    int freq_cols;
    const fftwf_complex* ref_out_saved;
    fftwf_complex* product;
    float* ccmap_raw;
    fftwf_plan inv_plan;
    float R[2 * kAutocorrelationRadius + 1][2 * kAutocorrelationRadius + 1] = {}; // Local autocorrelation patch.

    void BuildAutocorrelationPatch()
    {
        // Paper step 3 / eqs. (5)-(6): build the local reference autocorrelation R.
        // The CMM prediction for a sub-pixel shift is obtained by sampling this
        // autocorrelation surface with bicubic interpolation.
        for(int i = 0; i < rows * freq_cols; i++)
        {
            product[i][0] = ref_out_saved[i][0] * ref_out_saved[i][0]
                          + ref_out_saved[i][1] * ref_out_saved[i][1];
            product[i][1] = 0.0f;
        }
        fftwf_execute(inv_plan);

        for(int p = -kAutocorrelationRadius; p <= kAutocorrelationRadius; p++)
        {
            for(int q = -kAutocorrelationRadius; q <= kAutocorrelationRadius; q++)
            {
                int ri = (p + rows) % rows;
                int ci = (q + cols) % cols;
                R[p + kAutocorrelationRadius][q + kAutocorrelationRadius] =
                    ccmap_raw[ri * cols + ci] / float(rows * cols);
            }
        }
    }

    bool ExtractNormalizedPeakPatch(int cell_row, int cell_col, float (&phi_norm)[25])
    {
        // Paper steps 1-2 and eq. (9): extract the measured 5x5 correlation
        // neighbourhood phi around the discrete peak cell, then normalize by
        // its mean so the residual compares the patch shape rather than scale.
        // This normalized phi patch is the measured target used by every later
        // helper in this context.
        float phi[25] = {}; // Measured 5x5 peak patch.
        for(int m = -kPeakPatchRadius; m <= kPeakPatchRadius; m++)
        {
            for(int n = -kPeakPatchRadius; n <= kPeakPatchRadius; n++)
            {
                phi[(m + kPeakPatchRadius) * 5 + (n + kPeakPatchRadius)] =
                    ccmap(cell_row + m, cell_col + n);
            }
        }

        const float phi_mean = std::accumulate(std::begin(phi), std::end(phi), 0.0f) / 25.0f; // Mean-normalize phi.
        if(std::abs(phi_mean) < 1e-8f)
            return false;

        for(int idx = 0; idx < 25; idx++)
            phi_norm[idx] = phi[idx] / phi_mean;

        return true;
    }

    void BuildBasis(BicubicBasis1D (&basis)[5], float shift) const
    {
        // For one axis of the 5x5 patch, precompute the 4-sample bicubic
        // support and derivative weights used by Evaluate(). This avoids
        // rebuilding the same interpolation stencil for every sample.
        for(int i = 0; i < 5; i++)
        {
            float pos = static_cast<float>(i - kPeakPatchRadius) - shift; // Shifted patch coordinate.
            int base = static_cast<int>(std::floor(pos)); // Integer support anchor.
            basis[i].base = base;

            for(int k = 0; k < 4; k++)
            {
                float sample = static_cast<float>(base - 1 + k);
                float delta = pos - sample;
                basis[i].w[k] = CubicWeight(delta);
                basis[i].dw[k] = CubicDerivative(delta);
            }
        }
    }

    CmmEvalResult Evaluate(const float (&phi_norm)[25], float du, float dv) const
    {
        // Paper eqs. (5)-(6): evaluate the predicted correlation patch phi'
        // for the current sub-pixel shift (du, dv) by bicubically sampling R.
        // We also accumulate first derivatives so eq. (9) can be minimized with
        // a local Gauss-Newton solve instead of only a dense grid scan.
        BicubicBasis1D x_basis[5];
        BicubicBasis1D y_basis[5];
        BuildBasis(x_basis, du);
        BuildBasis(y_basis, dv);

        float pred[25] = {};      // Predicted phi'
        float pred_du[25] = {};   // d phi' / d u'
        float pred_dv[25] = {};   // d phi' / d v'
        float pred_mean = 0.0f;   // Mean of phi'
        float pred_du_mean = 0.0f;
        float pred_dv_mean = 0.0f;

        int idx = 0;
        for(int m = 0; m < 5; m++) // Patch row in phi / phi'
        {
            const BicubicBasis1D& yb = y_basis[m];
            for(int n = 0; n < 5; n++) // Patch col in phi / phi'
            {
                const BicubicBasis1D& xb = x_basis[n];
                float value = 0.0f; // phi' sample value.
                float dx = 0.0f;    // d phi' / d u'
                float dy = 0.0f;    // d phi' / d v'

                for(int ky = 0; ky < 4; ky++) // 4-sample cubic support in v
                {
                    int ry = yb.base - 1 + ky + kAutocorrelationRadius;
                    float wy = yb.w[ky];
                    float dwy = yb.dw[ky];

                    for(int kx = 0; kx < 4; kx++) // 4-sample cubic support in u
                    {
                        int rx = xb.base - 1 + kx + kAutocorrelationRadius;
                        float rv = R[ry][rx]; // One autocorrelation sample.
                        float wx = xb.w[kx];
                        float dwx = xb.dw[kx];

                        value += rv * wy * wx;
                        dx += rv * wy * dwx;
                        dy += rv * dwy * wx;
                    }
                }

                pred[idx] = value;
                pred_du[idx] = -dx;
                pred_dv[idx] = -dy;
                pred_mean += pred[idx];
                pred_du_mean += pred_du[idx];
                pred_dv_mean += pred_dv[idx];
                idx++;
            }
        }

        pred_mean /= 25.0f;
        pred_du_mean /= 25.0f;
        pred_dv_mean /= 25.0f;

        CmmEvalResult out;
        if(std::abs(pred_mean) < 1e-8f)
            return out;

        out.eps = 0.0f;
        for(int idx = 0; idx < 25; idx++)
        {
            float pred_norm = pred[idx] / pred_mean; // Mean-normalized phi'
            float pred_norm_du = (pred_du[idx] * pred_mean - pred[idx] * pred_du_mean)
                               / (pred_mean * pred_mean);
            float pred_norm_dv = (pred_dv[idx] * pred_mean - pred[idx] * pred_dv_mean)
                               / (pred_mean * pred_mean);

            float r = phi_norm[idx] - pred_norm; // Eq. (9) pointwise mismatch.

            out.eps += r * r;
            out.g_u += -2.0f * r * pred_norm_du;
            out.g_v += -2.0f * r * pred_norm_dv;
            out.H_uu += 2.0f * pred_norm_du * pred_norm_du;
            out.H_uv += 2.0f * pred_norm_du * pred_norm_dv;
            out.H_vv += 2.0f * pred_norm_dv * pred_norm_dv;
        }

        return out;
    }

    void SearchGrid(const float (&phi_norm)[25],
                    float u_min, float u_max, float v_min, float v_max, float step,
                    float& best_u, float& best_v, CmmEvalResult& best) const
    {
        // The paper minimizes eq. (9) by scanning the sub-pixel space. Here we
        // keep a small grid search as a refinement/fallback around the faster
        // Gauss-Newton estimate.
        u_min = std::clamp(u_min, -kSubpixelClamp, kSubpixelClamp);
        u_max = std::clamp(u_max, -kSubpixelClamp, kSubpixelClamp);
        v_min = std::clamp(v_min, -kSubpixelClamp, kSubpixelClamp);
        v_max = std::clamp(v_max, -kSubpixelClamp, kSubpixelClamp);

        if(u_min > u_max || v_min > v_max)
            return;

        int u_steps = static_cast<int>(std::floor((u_max - u_min) / step + 0.5f)); // Grid samples in u'
        int v_steps = static_cast<int>(std::floor((v_max - v_min) / step + 0.5f)); // Grid samples in v'

        for(int ui = 0; ui <= u_steps; ui++)
        {
            float du = std::min(kSubpixelClamp, u_min + ui * step);
            for(int vi = 0; vi <= v_steps; vi++)
            {
                float dv = std::min(kSubpixelClamp, v_min + vi * step);
                CmmEvalResult candidate = Evaluate(phi_norm, du, dv);
                if(candidate.eps < best.eps)
                {
                    best_u = du;
                    best_v = dv;
                    best = candidate;
                }
            }
        }
    }

    CmmCellSolution SolveCell(int cell_row, int cell_col, bool use_hint, float hint_u, float hint_v)
    {
        // Paper step 4 within one integer peak cell: solve only the local
        // sub-pixel offsets (u', v') while the discrete peak location stays
        // fixed. The final displacement is added back in FindPeak().
        // This helper is the main bridge between the paper math and the final
        // implementation: seed, evaluate, Gauss-Newton refine, and optionally
        // run small fallback searches before returning one cell-local answer.
        CmmCellSolution solution;
        if(cell_row <= 1 || cell_row >= ccmap.rows() - 2 || cell_col <= 1 || cell_col >= ccmap.cols() - 2)
            return solution;

        solution.row = cell_row;
        solution.col = cell_col;
        solution.int_u = cell_col - ccmap.cols() / 2;
        solution.int_v = cell_row - ccmap.rows() / 2;

        float gauss_u = 0.0f; // Gaussian seed in u'
        float gauss_v = 0.0f; // Gaussian seed in v'

        if(ccmap(cell_row, cell_col - 1) > 0 && ccmap(cell_row, cell_col) > 0 && ccmap(cell_row, cell_col + 1) > 0)
        {
            gauss_u = std::clamp(
                cell_col + (std::log(ccmap(cell_row, cell_col - 1)) - std::log(ccmap(cell_row, cell_col + 1)))
                    / (2 * std::log(ccmap(cell_row, cell_col - 1)) - 4 * std::log(ccmap(cell_row, cell_col))
                    + 2 * std::log(ccmap(cell_row, cell_col + 1)))
                - static_cast<float>(cell_col),
                -kSubpixelClamp, kSubpixelClamp);
        }

        if(ccmap(cell_row - 1, cell_col) > 0 && ccmap(cell_row, cell_col) > 0 && ccmap(cell_row + 1, cell_col) > 0)
        {
            gauss_v = std::clamp(
                cell_row + (std::log(ccmap(cell_row - 1, cell_col)) - std::log(ccmap(cell_row + 1, cell_col)))
                    / (2 * std::log(ccmap(cell_row - 1, cell_col)) - 4 * std::log(ccmap(cell_row, cell_col))
                    + 2 * std::log(ccmap(cell_row + 1, cell_col)))
                - static_cast<float>(cell_row),
                -kSubpixelClamp, kSubpixelClamp);
        }

        if(!std::isfinite(gauss_u)) gauss_u = 0.0f;
        if(!std::isfinite(gauss_v)) gauss_v = 0.0f;

        float phi_norm[25] = {};
        if(!ExtractNormalizedPeakPatch(cell_row, cell_col, phi_norm))
            return solution;

        float best_u = std::clamp(gauss_u, -kSubpixelClamp, kSubpixelClamp); // Current best u'
        float best_v = std::clamp(gauss_v, -kSubpixelClamp, kSubpixelClamp); // Current best v'
        CmmEvalResult best = Evaluate(phi_norm, best_u, best_v);

        if(use_hint)
        {
            float hinted_u = std::clamp(hint_u, -kSubpixelClamp, kSubpixelClamp);
            float hinted_v = std::clamp(hint_v, -kSubpixelClamp, kSubpixelClamp);
            CmmEvalResult hinted = Evaluate(phi_norm, hinted_u, hinted_v);
            if(hinted.eps < best.eps)
            {
                best_u = hinted_u;
                best_v = hinted_v;
                best = hinted;
            }
        }

        CmmEvalResult centered = Evaluate(phi_norm, 0.0f, 0.0f);
        if(centered.eps < best.eps)
        {
            best_u = 0.0f;
            best_v = 0.0f;
            best = centered;
        }

        bool improved = false;
        for(int iter = 0; iter < 8; iter++)
        {
            // Minimize the eq. (9) residual locally with a damped Gauss-Newton
            // step, then clamp the update to keep the search inside this cell.
            float det = best.H_uu * best.H_vv - best.H_uv * best.H_uv; // 2x2 Hessian determinant.
            if(std::abs(det) < 1e-10f)
                break;

            float delta_u = (-best.g_u * best.H_vv + best.g_v * best.H_uv) / det; // Newton update in u'
            float delta_v = (-best.g_v * best.H_uu + best.g_u * best.H_uv) / det; // Newton update in v'

            delta_u = std::clamp(delta_u, -kNewtonStepClamp, kNewtonStepClamp);
            delta_v = std::clamp(delta_v, -kNewtonStepClamp, kNewtonStepClamp);

            bool accepted = false;
            for(float alpha : {1.0f, 0.5f, 0.25f, 0.1f, 0.05f}) // Backtracking line search.
            {
                float cand_u = std::clamp(best_u + alpha * delta_u, -kSubpixelClamp, kSubpixelClamp);
                float cand_v = std::clamp(best_v + alpha * delta_v, -kSubpixelClamp, kSubpixelClamp);
                CmmEvalResult candidate = Evaluate(phi_norm, cand_u, cand_v);
                if(candidate.eps < best.eps)
                {
                    best_u = cand_u;
                    best_v = cand_v;
                    best = candidate;
                    improved = true;
                    accepted = true;
                    break;
                }
            }

            if(!accepted)
                break;
            if(std::abs(delta_u) < 1e-4f && std::abs(delta_v) < 1e-4f)
                break;
        }

        SearchGrid(phi_norm, best_u - 0.01f, best_u + 0.01f, best_v - 0.01f, best_v + 0.01f, 0.005f,
                   best_u, best_v, best);

        bool invalid_solution = !std::isfinite(best.eps) || best.eps >= 1e29f;
        bool near_boundary = OffsetExtent(best_u, best_v) > kNearBoundaryThreshold;
        if(!improved && (invalid_solution || near_boundary))
        {
            SearchGrid(phi_norm, -kSubpixelClamp, kSubpixelClamp, -kSubpixelClamp, kSubpixelClamp, 0.05f,
                       best_u, best_v, best);
            SearchGrid(phi_norm, best_u - 0.05f, best_u + 0.05f, best_v - 0.05f, best_v + 0.05f, 0.01f,
                       best_u, best_v, best);
        }

        solution.du = best_u;
        solution.dv = best_v;
        solution.eps = best.eps;
        solution.valid = std::isfinite(best.eps) && best.eps < 1e29f;
        return solution;
    }
};
}


//////////////////////////////////////////////////////
// Sub-Pixel Peak Search
//////////////////////////////////////////////////////

Correlator::PeakResult Correlator::FindPeak(const Eigen::MatrixXf& ccmap)
{
    // Top-level peak workflow:
    // 1. find the discrete correlation peak
    // 2. build the reference autocorrelation patch R for CMM
    // 3. solve the local sub-pixel offset inside the peak cell
    // 4. if needed, test neighbouring cells near the +/-0.5 boundary
    // 5. return displacement and S2N using the final chosen peak location
    int row, col;
    ccmap.maxCoeff(&row, &col);

    if(IsPeakTooCloseToEdge(ccmap, row, col))
    {
        return PeakResult{
            float(col - ccmap.cols() / 2),
            float(row - ccmap.rows() / 2),
            ComputeSignalToNoise(ccmap, row, col)
        };
    }

    PeakSearchContext context{
        ccmap,
        rows,
        cols,
        freq_cols,
        ref_out_saved,
        product,
        ccmap_raw,
        inv_plan
    };
    context.BuildAutocorrelationPatch();

    // Paper steps 2 and 5: start from the discrete peak cell, solve the local
    // sub-pixel offset there, and then combine integer + sub-pixel parts.
    CmmCellSolution best_cell = context.SolveCell(row, col, false, 0.0f, 0.0f);
    if(!best_cell.valid)
    {
        best_cell.valid = true;
        best_cell.row = row;
        best_cell.col = col;
        best_cell.int_u = col - ccmap.cols() / 2;
        best_cell.int_v = row - ccmap.rows() / 2;
    }

    for(int handoff_iter = 0; handoff_iter < 2 && best_cell.valid; handoff_iter++)
    {
        // When the optimum lands close to the edge of the current cell, also
        // evaluate the neighbouring integer peak cell and keep the lower-residual
        // representation. This reduces boundary locking near +/-0.5 pixels.
        // This is still per-window and local: no neighbouring vectors are used,
        // only alternative representations of the same peak inside nearby cells.
        int u_offsets[2] = {0, 0}; // Candidate neighboring u cells.
        int v_offsets[2] = {0, 0}; // Candidate neighboring v cells.
        int u_count = 1;
        int v_count = 1;

        if(best_cell.du > kPeakLockThreshold) u_offsets[u_count++] = 1;
        else if(best_cell.du < -kPeakLockThreshold) u_offsets[u_count++] = -1;

        if(best_cell.dv > kPeakLockThreshold) v_offsets[v_count++] = 1;
        else if(best_cell.dv < -kPeakLockThreshold) v_offsets[v_count++] = -1;

        bool moved = false;
        CmmCellSolution handoff_best = best_cell;

        for(int vi = 0; vi < v_count; vi++)
        {
            for(int ui = 0; ui < u_count; ui++)
            {
                int du_offset = u_offsets[ui];
                int dv_offset = v_offsets[vi];
                if(du_offset == 0 && dv_offset == 0)
                    continue;

                int candidate_row = best_cell.row + dv_offset;
                int candidate_col = best_cell.col + du_offset;

                CmmCellSolution candidate = context.SolveCell(
                    candidate_row,
                    candidate_col,
                    true,
                    best_cell.du - static_cast<float>(du_offset),
                    best_cell.dv - static_cast<float>(dv_offset));
                if(BetterCell(candidate, handoff_best))
                {
                    handoff_best = candidate;
                    moved = true;
                }
            }
        }

        if(!moved)
            break;

        best_cell = handoff_best;
    }

    row = best_cell.row;
    col = best_cell.col;
    float u = best_cell.int_u + best_cell.du;
    float v = best_cell.int_v + best_cell.dv;

    return PeakResult{u, v, ComputeSignalToNoise(ccmap, row, col)};
}
