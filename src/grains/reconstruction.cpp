#include <grains/reconstruction.h>
#include <numbers>

Eigen::MatrixXf Reconstruction::Compute(const VectorField& data) const
{
    //Mean Subtraction (account for tilt) ------------------------------------------------------------
    //Could remove real gradient data from large refractive index migration in material
    auto nonzero_u = (data.u.array() != 0.0f);
    float mean_u = nonzero_u.select(data.u.array(), 0.0f).sum() / nonzero_u.count();

    auto nonzero_v = (data.v.array() != 0.0f);
    float mean_v = nonzero_v.select(data.v.array(), 0.0f).sum() / nonzero_v.count();

    Eigen::MatrixXf u_cent = nonzero_u.select(data.u.array() - mean_u, 0.0f);
    Eigen::MatrixXf v_cent = nonzero_v.select(data.v.array() - mean_v, 0.0f);

    //-----------------------------------------------

    //Preprocessing
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

    //Frankot Chellappa Method
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
    //Only apply integral to points that were non zero to begin with
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

    // No scaling needed — u/v were fully converted to dn/d(grid index) in Session

    return surface;
}