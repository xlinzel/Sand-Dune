#include <grains/reconstruction.h>
#include <numbers>
#include <Eigen/Sparse>
#include <Eigen/IterativeLinearSolvers>
#include <vector>

Eigen::MatrixXf Reconstruction::ComputeFC(const VectorField& data) const
{
    Eigen::MatrixXf u_cent = data.u;
    Eigen::MatrixXf v_cent = data.v;

    //-----------------------------------------------

    // Mirror the displacement field into a 2x2 tiled domain before the FFT solve.
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> dx(data.height * 2, data.width * 2);
    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> dy(data.height * 2, data.width * 2);

    dx.block(0, 0, data.height, data.width) = u_cent;
    dy.block(0, 0, data.height, data.width) = v_cent;

    dx.block(data.height, 0, data.height, data.width) = u_cent.colwise().reverse().eval();
    dy.block(data.height, 0, data.height, data.width) = -v_cent.colwise().reverse().eval();

    dx.block(0, data.width, data.height, data.width) = -u_cent.rowwise().reverse().eval();
    dy.block(0, data.width, data.height, data.width) = v_cent.rowwise().reverse().eval();

    dx.block(data.height, data.width, data.height, data.width) = -u_cent.reverse().eval();
    dy.block(data.height, data.width, data.height, data.width) = -v_cent.reverse().eval();

    //FFT Setup and execution
    int rows = 2 * data.height;
    int cols = 2 * data.width;
    int freq_cols = floor(cols / 2) + 1;

    float* xin = (float*) fftwf_alloc_real(rows * cols);
    float* yin = (float*) fftwf_alloc_real(rows * cols);

    fftwf_complex* xout = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);
    fftwf_complex* yout = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);

    fftwf_plan xplan  = fftwf_plan_dft_r2c_2d(rows, cols, xin,  xout,  FFTW_MEASURE);
    fftwf_plan yplan  = fftwf_plan_dft_r2c_2d(rows, cols, yin,  yout,  FFTW_MEASURE);

    memcpy(xin, dx.data(), sizeof(float) * rows * cols);
    memcpy(yin, dy.data(), sizeof(float) * rows * cols);

    fftwf_execute(xplan);
    fftwf_execute(yplan);

    // Frankot-Chellappa frequency-domain integration.
    fftwf_complex* F_s = (fftwf_complex*) fftwf_alloc_complex(rows * freq_cols);

    //FFT Inverse Setup
    float* s = (float*) fftwf_malloc(sizeof(float) * rows * cols);

    fftwf_plan inv_plan = fftwf_plan_dft_c2r_2d(rows, cols, F_s, s, FFTW_MEASURE);

    for(int m = 0; m < rows; m++)
    {
        for(int n = 0; n < freq_cols; n++)
        {
            float fx = (float) n / cols;
            float fy = (m <= rows / 2) ? (float) m / rows : (float)(m - rows) / rows;

            float denominator = 2.0f * std::numbers::pi * (fx * fx + fy * fy) + eps;

            //Real part
            F_s[m * freq_cols + n][0] = (fx * xout[m * freq_cols + n][1] + fy * yout[m * freq_cols + n][1])
                                    / denominator;

            //Imaginary part
            F_s[m * freq_cols + n][1] = - (fx * xout[m * freq_cols + n][0] + fy * yout[m * freq_cols + n][0])
                                    / denominator;
        }
    }

    fftwf_execute(inv_plan);

    Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> fft_surf(data.height * 2, data.width * 2);
    memcpy(fft_surf.data(), s, sizeof(float) * rows * cols);

    fft_surf /= rows * cols;

    Eigen::MatrixXf surface(data.height, data.width);
    surface = fft_surf.block(0, 0, data.height, data.width);

    //---------------------------------------------------------------------------------------
    // Keep masked/empty locations at zero in the returned relative surface map.
    auto valid = (data.u.array() != 0.0f || data.v.array() != 0.0f);
    surface = valid.select(surface.array(), 0.0f);
    //---------------------------------------------------------------------------------------

    //FFT cleanup
    fftwf_free(xin);
    fftwf_free(yin);
    fftwf_free(xout);
    fftwf_free(yout);

    fftwf_destroy_plan(xplan);
    fftwf_destroy_plan(yplan);

    fftwf_free(F_s);
    fftwf_free(s);
    
    fftwf_destroy_plan(inv_plan);

    // Physical unit conversion is applied later in Session::ScaleFields().

    return surface;
}

Eigen::MatrixXf Reconstruction::Compute(const VectorField& data, const Eigen::MatrixXf& mask) const
{
    Eigen::MatrixXf surface = Eigen::MatrixXf::Zero(data.height, data.width);

    bool has_mask = (mask.rows() == data.height && mask.cols() == data.width);
    //Mask index getter to return validity of index wihtin data
    auto is_valid = [&](int row, int col)
    {
        return !has_mask || mask(row, col) != 0.0f;
    };

    //Form indexed map, so that the solver will onyl loop through known values
    Eigen::MatrixXi index = Eigen::MatrixXi::Constant(data.height, data.width, -1);
    int unknowns = 0;
    for(int row = 0; row < data.height; row++)
        for(int col = 0; col < data.width; col++)
            if(is_valid(row, col))
                index(row, col) = unknowns++;

    //If there are no unknown positons return surface
    if(unknowns == 0)
        return surface;

    using Triplet = Eigen::Triplet<float>;
    std::vector<Triplet> triplets;
    triplets.reserve(static_cast<size_t>(unknowns) * 6);
    Eigen::VectorXf rhs = Eigen::VectorXf::Zero(unknowns);

    auto add_edge = [&](int row_a, int col_a, int row_b, int col_b, float gradient)
    {
        int ia = index(row_a, col_a);
        int ib = index(row_b, col_b);
        if(ia < 0 || ib < 0)
            return;

        triplets.emplace_back(ia, ia, 1.0f);
        triplets.emplace_back(ib, ib, 1.0f);
        triplets.emplace_back(ia, ib, -1.0f);
        triplets.emplace_back(ib, ia, -1.0f);
        rhs(ia) -= gradient;
        rhs(ib) += gradient;
    };

    for(int row = 0; row < data.height; row++)
    {
        for(int col = 0; col + 1 < data.width; col++)
        {
            if(!is_valid(row, col) || !is_valid(row, col + 1))
                continue;
            float grad_x = 0.5f * (data.u(row, col) + data.u(row, col + 1));
            add_edge(row, col, row, col + 1, grad_x);
        }
    }

    for(int row = 0; row + 1 < data.height; row++)
    {
        for(int col = 0; col < data.width; col++)
        {
            if(!is_valid(row, col) || !is_valid(row + 1, col))
                continue;
            float grad_y = 0.5f * (data.v(row, col) + data.v(row + 1, col));
            add_edge(row, col, row + 1, col, grad_y);
        }
    }

    int gauge = -1;
    for(int row = 0; row < data.height && gauge < 0; row++)
        for(int col = 0; col < data.width && gauge < 0; col++)
            if(is_valid(row, col))
                gauge = index(row, col);

    if(gauge >= 0)
        triplets.emplace_back(gauge, gauge, 1.0f);

    Eigen::SparseMatrix<float> system(unknowns, unknowns);
    system.setFromTriplets(triplets.begin(), triplets.end());

    Eigen::ConjugateGradient<Eigen::SparseMatrix<float>, Eigen::Lower | Eigen::Upper> solver;
    solver.setMaxIterations(std::max(unknowns * 4, 200));
    solver.setTolerance(1e-5f);
    solver.compute(system);

    if(solver.info() != Eigen::Success)
        return surface;

    Eigen::VectorXf solution = solver.solve(rhs);
    if(solver.info() != Eigen::Success)
        return surface;

    for(int row = 0; row < data.height; row++)
        for(int col = 0; col < data.width; col++)
            if(is_valid(row, col))
                surface(row, col) = solution(index(row, col));

    return surface;
}
