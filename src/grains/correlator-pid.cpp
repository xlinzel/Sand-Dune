#include <grains/correlator.h>
#include <algorithm>
#include <cmath>

namespace
{
/// @brief Bilinear interpolation of one in-bounds image sample.
///
/// This is the image-domain interpolation used by PID window warping before
/// correlation. It is separate from the later CMM bicubic interpolation, which
/// acts on the correlation/autocorrelation patch after FFT correlation exists.
float SampleBilinearInterior(const Eigen::MatrixXf& image, float row, float col)
{
    int r0 = static_cast<int>(row);
    int c0 = static_cast<int>(col);
    float tr = row - static_cast<float>(r0);
    float tc = col - static_cast<float>(c0);

    float v00 = image(r0, c0);
    float v01 = image(r0, c0 + 1);
    float v10 = image(r0 + 1, c0);
    float v11 = image(r0 + 1, c0 + 1);

    float top = v00 + tc * (v01 - v00);
    float bot = v10 + tc * (v11 - v10);
    return top + tr * (bot - top);
}

/// @brief Bilinear interpolation with zero padding outside the image bounds.
///
/// This is used by PID for edge windows whose warped sample locations may fall
/// partially outside the input image.
float SampleBilinearZero(const Eigen::MatrixXf& image, float row, float col)
{
    int r0 = static_cast<int>(std::floor(row));
    int c0 = static_cast<int>(std::floor(col));
    int r1 = r0 + 1;
    int c1 = c0 + 1;

    float tr = row - static_cast<float>(r0);
    float tc = col - static_cast<float>(c0);

    auto sample = [&](int r, int c) -> float
    {
        if(r < 0 || r >= image.rows() || c < 0 || c >= image.cols())
            return 0.0f;
        return image(r, c);
    };

    float v00 = sample(r0, c0);
    float v01 = sample(r0, c1);
    float v10 = sample(r1, c0);
    float v11 = sample(r1, c1);

    float top = v00 + tc * (v01 - v00);
    float bot = v10 + tc * (v11 - v10);
    return top + tr * (bot - top);
}

/// @brief Estimate the x-derivative of a correlator-grid field.
///
/// Centered finite differences are used in the interior and one-sided
/// differences at the boundaries.
float EstimateDerivativeX(const Eigen::MatrixXf& field, int row, int col, float spacing)
{
    if(field.cols() <= 1)
        return 0.0f;
    if(col == 0)
        return (field(row, 1) - field(row, 0)) / spacing;
    if(col == field.cols() - 1)
        return (field(row, col) - field(row, col - 1)) / spacing;
    return (field(row, col + 1) - field(row, col - 1)) / (2.0f * spacing);
}

/// @brief Estimate the y-derivative of a correlator-grid field.
///
/// Centered finite differences are used in the interior and one-sided
/// differences at the boundaries.
float EstimateDerivativeY(const Eigen::MatrixXf& field, int row, int col, float spacing)
{
    if(field.rows() <= 1)
        return 0.0f;
    if(row == 0)
        return (field(1, col) - field(0, col)) / spacing;
    if(row == field.rows() - 1)
        return (field(row, col) - field(row - 1, col)) / spacing;
    return (field(row + 1, col) - field(row - 1, col)) / (2.0f * spacing);
}

/// @brief Apply one 3x3 box-filter pass to a correlator-grid field.
///
/// PID uses this to regularize the displacement field and its derivatives before
/// building the local affine warp.
Eigen::MatrixXf BoxSmooth3x3(const Eigen::MatrixXf& field)
{
    Eigen::MatrixXf smoothed(field.rows(), field.cols());

    for(int row = 0; row < field.rows(); row++)
    {
        for(int col = 0; col < field.cols(); col++)
        {
            float sum = 0.0f;
            int count = 0;

            for(int dr = -1; dr <= 1; dr++)
            {
                for(int dc = -1; dc <= 1; dc++)
                {
                    int rr = row + dr;
                    int cc = col + dc;
                    if(rr < 0 || rr >= field.rows() || cc < 0 || cc >= field.cols())
                        continue;

                    sum += field(rr, cc);
                    count++;
                }
            }

            smoothed(row, col) = (count > 0) ? (sum / static_cast<float>(count)) : field(row, col);
        }
    }

    return smoothed;
}
}

/// @brief Convert the current displacement field into a first-order PID deformation model.
///
/// The resulting structure stores the window-center displacement `(u, v)` and
/// the first derivatives `(du/dx, du/dy, dv/dx, dv/dy)` used to define the
/// affine warp for the next PID correlation pass.
Correlator::DeformationField Correlator::EstimateDeformationField(const VectorField& field) const
{
    DeformationField deformation;
    deformation.u = field.u;
    deformation.v = field.v;
    deformation.du_dx = Eigen::MatrixXf::Zero(field.height, field.width);
    deformation.du_dy = Eigen::MatrixXf::Zero(field.height, field.width);
    deformation.dv_dx = Eigen::MatrixXf::Zero(field.height, field.width);
    deformation.dv_dy = Eigen::MatrixXf::Zero(field.height, field.width);

    if(field.width == 0 || field.height == 0)
        return deformation;

    for(int pass = 0; pass < std::max(0, pid_smoothing_passes); pass++)
    {
        deformation.u = BoxSmooth3x3(deformation.u);
        deformation.v = BoxSmooth3x3(deformation.v);
    }

    float spacing = static_cast<float>(window_size - overlap);

    for(int row = 0; row < field.height; row++)
    {
        for(int col = 0; col < field.width; col++)
        {
            /// @note These derivatives are evaluated on the correlator grid, not
            /// per image pixel. PID treats them as the local deformation Jacobian
            /// for the current interrogation window.
            deformation.du_dx(row, col) = EstimateDerivativeX(deformation.u, row, col, spacing);
            deformation.du_dy(row, col) = EstimateDerivativeY(deformation.u, row, col, spacing);
            deformation.dv_dx(row, col) = EstimateDerivativeX(deformation.v, row, col, spacing);
            deformation.dv_dy(row, col) = EstimateDerivativeY(deformation.v, row, col, spacing);
        }
    }

    for(int pass = 0; pass < std::max(0, pid_smoothing_passes); pass++)
    {
        deformation.du_dx = BoxSmooth3x3(deformation.du_dx);
        deformation.du_dy = BoxSmooth3x3(deformation.du_dy);
        deformation.dv_dx = BoxSmooth3x3(deformation.dv_dx);
        deformation.dv_dy = BoxSmooth3x3(deformation.dv_dy);
    }

    return deformation;
}

/// @brief Build one PID-warped flow window from the local affine deformation model.
///
/// For the window centered at `(win_row, win_col)`, the predictor supplies a
/// translation and first-order deformation Jacobian. The flow image is then
/// resampled at sub-pixel locations so the next correlation pass measures only
/// the residual displacement left after that local warp.
void Correlator::ExtractWarpedFlowWindow(const Eigen::MatrixXf& flow, int start_row, int start_col,
                                         int win_row, int win_col, const DeformationField& predictor,
                                         Eigen::MatrixXf& window) const
{
    window.setZero(window_size, window_size);

    if(win_row < 0 || win_row >= predictor.u.rows() || win_col < 0 || win_col >= predictor.u.cols())
        return;

    float u0 = predictor.u(win_row, win_col);
    float v0 = predictor.v(win_row, win_col);
    float du_dx = predictor.du_dx(win_row, win_col);
    float du_dy = predictor.du_dy(win_row, win_col);
    float dv_dx = predictor.dv_dx(win_row, win_col);
    float dv_dy = predictor.dv_dy(win_row, win_col);
    float half_extent = 0.5f * static_cast<float>(window_size - 1);
    float row_base = static_cast<float>(start_row) + v0
                   + dv_dx * (-half_extent)
                   + dv_dy * (-half_extent);
    float col_base = static_cast<float>(start_col) + u0
                   + du_dx * (-half_extent)
                   + du_dy * (-half_extent);
    float row_step_x = dv_dx;
    float col_step_x = 1.0f + du_dx;
    float row_step_y = 1.0f + dv_dy;
    float col_step_y = du_dy;
    float last = static_cast<float>(window_size - 1);

    /// @brief Because the local warp is affine, the four warped corners are
    /// sufficient to decide whether the entire window stays inside the image.
    auto corner_row = [&](float dx_extent, float dy_extent)
    {
        return row_base + dx_extent * row_step_x + dy_extent * row_step_y;
    };

    auto corner_col = [&](float dx_extent, float dy_extent)
    {
        return col_base + dx_extent * col_step_x + dy_extent * col_step_y;
    };

    float min_row = std::min({corner_row(0.0f, 0.0f), corner_row(last, 0.0f),
                              corner_row(0.0f, last), corner_row(last, last)});
    float max_row = std::max({corner_row(0.0f, 0.0f), corner_row(last, 0.0f),
                              corner_row(0.0f, last), corner_row(last, last)});
    float min_col = std::min({corner_col(0.0f, 0.0f), corner_col(last, 0.0f),
                              corner_col(0.0f, last), corner_col(last, last)});
    float max_col = std::max({corner_col(0.0f, 0.0f), corner_col(last, 0.0f),
                              corner_col(0.0f, last), corner_col(last, last)});

    bool interior =
        min_row >= 0.0f &&
        min_col >= 0.0f &&
        max_row <= static_cast<float>(flow.rows() - 2) &&
        max_col <= static_cast<float>(flow.cols() - 2);

    if(interior)
    {
        /// @brief Fast path for fully in-bounds warped windows.
        float current_row_base = row_base;
        float current_col_base = col_base;

        for(int row = 0; row < window_size; row++)
        {
            float warped_row = current_row_base;
            float warped_col = current_col_base;

            for(int col = 0; col < window_size; col++)
            {
                window(row, col) = SampleBilinearInterior(flow, warped_row, warped_col);
                warped_row += row_step_x;
                warped_col += col_step_x;
            }

            current_row_base += row_step_y;
            current_col_base += col_step_y;
        }
        return;
    }

    /// @brief Boundary-safe path with zero padding for out-of-bounds samples.
    float current_row_base = row_base;
    float current_col_base = col_base;
    for(int row = 0; row < window_size; row++)
    {
        float warped_row = current_row_base;
        float warped_col = current_col_base;

        for(int col = 0; col < window_size; col++)
        {
            window(row, col) = SampleBilinearZero(flow, warped_row, warped_col);
            warped_row += row_step_x;
            warped_col += col_step_x;
        }

        current_row_base += row_step_y;
        current_col_base += col_step_y;
    }
}
