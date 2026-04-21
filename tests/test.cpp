#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <lib/doctest.h>
#include <sun/image.h>
#include <sun/mask.h>
#include <grains/correlator.h>
#include <grains/validation.h>
#include <grains/reconstruction.h>
#include <session.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <vector>
#include <random>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>

//NOTE: Many tests were written by AI, but then reviewed to ensure correctness

namespace
{
constexpr int kTestWindowSize = 32;
constexpr int kTestOverlap = 24;

std::string FormatFloat(double value, int precision = 6)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

void PrintSection(const std::string& title)
{
    std::cout << "\n=== " << title << " ===" << std::endl;
}

void PrintLine(const std::string& label, const std::string& value)
{
    std::cout << "  " << std::left << std::setw(24) << label << " : " << value << std::endl;
}

struct PlaneFitMetrics
{
    float rms = 0.0f;
    float normalized_std = 0.0f;
    float normalized_range = 0.0f;
    int rows = 0;
    int cols = 0;
};

CorrelatorParameters MakePidTestParameters()
{
    CorrelatorParameters parameters;
    parameters.window_size = kTestWindowSize;
    parameters.overlap = kTestOverlap;
    parameters.enable_pid = true;
    parameters.pid_iterations = 2;
    parameters.pid_relaxation = 1.0f;
    parameters.pid_smoothing_passes = 1;
    return parameters;
}

PlaneFitMetrics ComputePlaneFitMetrics(const Eigen::MatrixXf& field)
{
    // Fit a best-fit plane field(x,y) = a*x + b*y + c over the provided map and
    // report the RMS deviation from that plane. The normalized forms divide by
    // the map standard deviation and full value range to make the number easier
    // to compare across different displacement magnitudes.
    PlaneFitMetrics metrics;
    metrics.rows = field.rows();
    metrics.cols = field.cols();

    if(metrics.rows <= 0 || metrics.cols <= 0)
        return metrics;

    Eigen::MatrixXf A(metrics.rows * metrics.cols, 3);
    Eigen::VectorXf samples(metrics.rows * metrics.cols);

    int sample_idx = 0;
    for(int y = 0; y < metrics.rows; y++)
    {
        for(int x = 0; x < metrics.cols; x++)
        {
            A(sample_idx, 0) = static_cast<float>(x);
            A(sample_idx, 1) = static_cast<float>(y);
            A(sample_idx, 2) = 1.0f;
            samples(sample_idx) = field(y, x);
            sample_idx++;
        }
    }

    Eigen::Vector3f plane = A.colPivHouseholderQr().solve(samples);
    Eigen::VectorXf residual = samples - A * plane;
    metrics.rms = std::sqrt(residual.array().square().mean());

    float field_std = std::sqrt((samples.array() - samples.mean()).square().mean());
    float field_range = samples.maxCoeff() - samples.minCoeff();

    metrics.normalized_std = field_std > 1e-8f ? metrics.rms / field_std : 0.0f;
    metrics.normalized_range = field_range > 1e-8f ? metrics.rms / field_range : 0.0f;
    return metrics;
}

float ComputeFieldRmse(const VectorField& field, int window_size, int overlap,
                       const std::function<Eigen::Vector2f(float, float)>& expected_displacement,
                       float border_fraction = 0.10f)
{
    if(field.width == 0 || field.height == 0)
        return 0.0f;

    int row_start = std::max(0, static_cast<int>(std::floor(field.height * border_fraction)));
    int col_start = std::max(0, static_cast<int>(std::floor(field.width * border_fraction)));
    int row_end = std::min(field.height, field.height - row_start);
    int col_end = std::min(field.width, field.width - col_start);

    if(row_start >= row_end || col_start >= col_end)
    {
        row_start = 0;
        col_start = 0;
        row_end = field.height;
        col_end = field.width;
    }

    float half_extent = 0.5f * static_cast<float>(window_size - 1);
    int movement = window_size - overlap;
    double sum_sq = 0.0;
    int count = 0;

    for(int row = row_start; row < row_end; row++)
    {
        for(int col = col_start; col < col_end; col++)
        {
            float x = col * movement + half_extent;
            float y = row * movement + half_extent;
            Eigen::Vector2f expected = expected_displacement(x, y);
            float du = field.u(row, col) - expected.x();
            float dv = field.v(row, col) - expected.y();
            sum_sq += static_cast<double>(du * du + dv * dv);
            count++;
        }
    }

    return count > 0 ? static_cast<float>(std::sqrt(sum_sq / count)) : 0.0f;
}

Eigen::MatrixXf CenterCrop(const Eigen::MatrixXf& field, float border_fraction = 0.10f)
{
    // Remove 10% from each edge by default, leaving the central 80% of the map.
    // This is large enough to remain representative while avoiding edge-heavy
    // distortions and partially padded interrogation windows.
    int rows = field.rows();
    int cols = field.cols();
    if(rows <= 0 || cols <= 0)
        return Eigen::MatrixXf();

    int border_r = static_cast<int>(std::floor(rows * border_fraction));
    int border_c = static_cast<int>(std::floor(cols * border_fraction));
    border_r = std::clamp(border_r, 0, std::max(0, (rows - 1) / 2));
    border_c = std::clamp(border_c, 0, std::max(0, (cols - 1) / 2));

    int h = rows - 2 * border_r;
    int w = cols - 2 * border_c;
    return field.block(border_r, border_c, h, w);
}

std::string FormatUV(double u_value, double v_value, const std::string& suffix = "")
{
    return "u=" + FormatFloat(u_value) + suffix + ", v=" + FormatFloat(v_value) + suffix;
}

}

auto FormatTime = [](std::chrono::steady_clock::time_point start, 
                     std::chrono::steady_clock::time_point end) -> std::string
{
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto min = total_ms / 60000;
    auto sec = (total_ms % 60000) / 1000;
    auto ms  = total_ms % 1000;
    return std::to_string(min) + ":" + std::to_string(sec) + "." + std::to_string(ms);
};

TEST_CASE("Image Loading")
{
    const std::string image_path = std::string(PROJECT_DIR) + "/images/slides/ref.bmp";

    //Initializaiton and then load
    Image image;
    std::string output = image.Load(image_path.c_str());

    CHECK(output.empty());        //Empty string = success
    CHECK(image.GetLoaded());
    CHECK(image.GetWidth() > 0);
    CHECK(image.GetHeight() > 0);
    CHECK(!image.GetData().empty());

    //Loading constructor
    Image image1(image_path.c_str());
    CHECK(image1.GetLoaded());
    CHECK(image1.GetWidth() > 0);
    CHECK(image1.GetHeight() > 0);
    CHECK(!image1.GetData().empty());
}

TEST_CASE("Mask Generation")
{
    Eigen::MatrixXf data = Eigen::MatrixXf::Ones(100, 100);

    //Initializaiton and then load
    Mask mask;
    mask.GenBinCircleMask(100, 100, Eigen::Vector2f(40, 40), 30.0f);
    Eigen::MatrixXf result = mask.ApplyMask(data);

    CHECK(result(40, 40) == 1.0f);
    CHECK(result(50, 50) == 1.0f);
    CHECK(result(60, 50) == 1.0f);
    CHECK(result(70, 60) == 0.0f);
    CHECK(result(0, 0) == 0.0f);
}

TEST_CASE("Correlator Computation")
{
    SUBCASE("Parameters")
    {
        Correlator correlator(kTestWindowSize, kTestOverlap);
        CHECK(correlator.GetWindowSize() == kTestWindowSize);
        CHECK(correlator.GetOverlap() == kTestOverlap);
    }

    SUBCASE("Zero Displacement")
    {
        // Identical images should produce near-zero displacement
        Eigen::MatrixXf img = Eigen::MatrixXf::Random(200, 200);
        Correlator correlator(kTestWindowSize, kTestOverlap);
        VectorField result = correlator.Compute(img, img);

        auto zero_disp = [](float, float) { return Eigen::Vector2f(0.0f, 0.0f); };
        float rmse = ComputeFieldRmse(result, kTestWindowSize, kTestOverlap, zero_disp);
        float max_abs = std::max(result.u.cwiseAbs().maxCoeff(), result.v.cwiseAbs().maxCoeff());

        PrintSection("Zero Displacement");
        PrintLine("RMSE", FormatFloat(rmse) + " px");
        PrintLine("max |displacement|", FormatFloat(max_abs) + " px");

        CHECK(result.u.cwiseAbs().maxCoeff() < 0.15f);
        CHECK(result.v.cwiseAbs().maxCoeff() < 0.15f);
        CHECK(std::abs(result.u.mean()) < 0.05f);
        CHECK(std::abs(result.v.mean()) < 0.05f);
    }

    SUBCASE("Known Displacement")
    {
        // Shift flow image by 5 pixels horizontally
        Eigen::MatrixXf ref = Eigen::MatrixXf::Random(200, 200);
        Eigen::MatrixXf flow = Eigen::MatrixXf::Zero(200, 200);
        flow.block(0, 5, 200, 195) = ref.block(0, 0, 200, 195);

        Correlator correlator(kTestWindowSize, kTestOverlap);
        VectorField result = correlator.Compute(ref, flow);

        // Centre window should detect ~5px horizontal displacement
        int centre_row = result.u.rows() / 2;
        int centre_col = result.u.cols() / 2;
        CHECK(result.u.mean() == doctest::Approx(5.0f).epsilon(1.0f));
        CHECK(result.v.mean() == doctest::Approx(0.0f).epsilon(1.0f));

        /*std::cout << "===================Computing Test Mapping===================" << std::endl;
        std::cout << "U Vector Map: " << std::endl;
        std::cout << result.u << std::endl << std::endl;
        std::cout << "V Vector Map: " << std::endl;
        std::cout << result.v << std::endl << std::endl;
        std::cout << "Sig2noise Vector Map: " << std::endl;
        std::cout << result.s2n << std::endl << std::endl;*/
    }

    SUBCASE("Subpixel Displacement")
    {
        constexpr int image_size = 192;
        constexpr int spot_count = 120;
        constexpr float sigma = 1.1f;

        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> dist(10.0f, image_size - 10.0f);
        std::vector<Eigen::Vector2f> centers;
        centers.reserve(spot_count);
        for(int i = 0; i < spot_count; i++)
            centers.emplace_back(dist(rng), dist(rng));

        auto render = [&](float dx, float dy)
        {
            Eigen::MatrixXf img = Eigen::MatrixXf::Zero(image_size, image_size);
            const float radius = 4.0f * sigma;
            const float denom = 2.0f * sigma * sigma;

            for(const Eigen::Vector2f& center : centers)
            {
                float cx = center.x() + dx;
                float cy = center.y() + dy;

                int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
                int x1 = std::min(image_size - 1, static_cast<int>(std::ceil(cx + radius)));
                int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
                int y1 = std::min(image_size - 1, static_cast<int>(std::ceil(cy + radius)));

                for(int y = y0; y <= y1; y++)
                {
                    for(int x = x0; x <= x1; x++)
                    {
                        float dxp = x - cx;
                        float dyp = y - cy;
                        img(y, x) += std::exp(-(dxp * dxp + dyp * dyp) / denom);
                    }
                }
            }

            return img;
        };

        Eigen::MatrixXf ref = render(0.0f, 0.0f);

        const std::vector<Eigen::Vector2f> shifts = {
            { 0.25f, -0.30f},
            { 0.10f,  0.12f},
            {-0.35f,  0.28f},
            { 0.42f, -0.18f}
        };

        PrintSection("Subpixel Displacement");
        for(const Eigen::Vector2f& shift : shifts)
        {
            Eigen::MatrixXf flow = render(shift.x(), shift.y());

            Correlator correlator(kTestWindowSize, kTestOverlap);
            VectorField result = correlator.Compute(ref, flow);

            auto expected_disp = [&](float, float) { return Eigen::Vector2f(shift.x(), shift.y()); };
            float rmse = ComputeFieldRmse(result, kTestWindowSize, kTestOverlap, expected_disp);

            std::string label = "shift (" + FormatFloat(shift.x(), 2) + ", " + FormatFloat(shift.y(), 2) + ")";
            PrintLine(label, FormatFloat(rmse) + " px RMSE");

            CHECK(std::abs(result.u.mean() - shift.x()) < 0.08f);
            CHECK(std::abs(result.v.mean() - shift.y()) < 0.08f);
        }
    }

    SUBCASE("PID Affine Deformation")
    {
        // image_size=256, spot_count=500, sigma=1.8 give ~14 spots per 32px window
        // and better sub-pixel peak quality, so the RMSE reflects deformation
        // correction accuracy rather than per-window SNR noise.
        constexpr int image_size = 256;
        constexpr int spot_count = 500;
        constexpr float sigma = 1.8f;

        std::mt19937 rng(2468);
        std::uniform_real_distribution<float> dist(14.0f, image_size - 14.0f);
        std::vector<Eigen::Vector2f> centers;
        centers.reserve(spot_count);
        for(int i = 0; i < spot_count; i++)
            centers.emplace_back(dist(rng), dist(rng));

        Eigen::Vector2f image_center(0.5f * static_cast<float>(image_size - 1),
                                     0.5f * static_cast<float>(image_size - 1));

        constexpr float u0 = 1.15f;
        constexpr float v0 = -0.75f;
        constexpr float du_dx = 0.0105f;
        constexpr float du_dy = 0.0040f;
        constexpr float dv_dx = -0.0035f;
        constexpr float dv_dy = 0.0085f;

        auto displacement = [&](float x, float y) -> Eigen::Vector2f
        {
            float dx = x - image_center.x();
            float dy = y - image_center.y();
            return Eigen::Vector2f(
                u0 + du_dx * dx + du_dy * dy,
                v0 + dv_dx * dx + dv_dy * dy);
        };

        auto render_reference = [&]()
        {
            Eigen::MatrixXf img = Eigen::MatrixXf::Zero(image_size, image_size);
            const float radius = 4.0f * sigma;
            const float denom = 2.0f * sigma * sigma;

            for(const Eigen::Vector2f& center : centers)
            {
                float cx = center.x();
                float cy = center.y();

                int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
                int x1 = std::min(image_size - 1, static_cast<int>(std::ceil(cx + radius)));
                int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
                int y1 = std::min(image_size - 1, static_cast<int>(std::ceil(cy + radius)));

                for(int y = y0; y <= y1; y++)
                {
                    for(int x = x0; x <= x1; x++)
                    {
                        float dxp = x - cx;
                        float dyp = y - cy;
                        img(y, x) += std::exp(-(dxp * dxp + dyp * dyp) / denom);
                    }
                }
            }

            return img;
        };

        auto sample_bilinear = [&](const Eigen::MatrixXf& image, float row, float col) -> float
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
            float bottom = v10 + tc * (v11 - v10);
            return top + tr * (bottom - top);
        };

        auto warp_affine = [&](const Eigen::MatrixXf& source)
        {
            Eigen::MatrixXf warped = Eigen::MatrixXf::Zero(image_size, image_size);
            Eigen::Matrix2f A;
            A << 1.0f + du_dx, du_dy,
                 dv_dx, 1.0f + dv_dy;
            Eigen::Matrix2f invA = A.inverse();
            Eigen::Vector2f translation(u0, v0);

            for(int row = 0; row < image_size; row++)
            {
                for(int col = 0; col < image_size; col++)
                {
                    Eigen::Vector2f y(static_cast<float>(col), static_cast<float>(row));
                    Eigen::Vector2f x = image_center + invA * (y - image_center - translation);
                    warped(row, col) = sample_bilinear(source, x.y(), x.x());
                }
            }

            return warped;
        };

        Eigen::MatrixXf ref = render_reference();
        Eigen::MatrixXf flow = warp_affine(ref);

        Correlator baseline(kTestWindowSize, kTestOverlap);
        VectorField baseline_field = baseline.Compute(ref, flow);

        CorrelatorParameters pid_parameters;
        pid_parameters.window_size = kTestWindowSize;
        pid_parameters.overlap = kTestOverlap;
        pid_parameters.enable_pid = true;
        pid_parameters.pid_iterations = 2;
        pid_parameters.pid_relaxation = 1.0f;
        pid_parameters.pid_smoothing_passes = 1;

        Correlator pid2(pid_parameters);
        VectorField pid2_field = pid2.Compute(ref, flow);

        CorrelatorParameters pid3_parameters = pid_parameters;
        pid3_parameters.pid_iterations = 3;
        Correlator pid3(pid3_parameters);
        VectorField pid3_field = pid3.Compute(ref, flow);

        float baseline_rmse = ComputeFieldRmse(baseline_field, kTestWindowSize, kTestOverlap, displacement);
        float pid2_rmse = ComputeFieldRmse(pid2_field, kTestWindowSize, kTestOverlap, displacement);
        float pid3_rmse = ComputeFieldRmse(pid3_field, kTestWindowSize, kTestOverlap, displacement);

        PrintSection("PID Affine Deformation");
        PrintLine("baseline RMSE", FormatFloat(baseline_rmse) + " px");
        PrintLine("pid RMSE (2 iterations)", FormatFloat(pid2_rmse) + " px");
        PrintLine("pid RMSE (3 iterations)", FormatFloat(pid3_rmse) + " px");

        CHECK(pid2_rmse < baseline_rmse);
        CHECK(pid2_rmse <= baseline_rmse * 0.95f);
    }

}

TEST_CASE("Correlation Repo Image Pair")
{
    Image ref_image;
    Image flow_image;

    CHECK(ref_image.Load((std::string(PROJECT_DIR) + "/images/if_0.1_ref.bmp").c_str()).empty());
    CHECK(flow_image.Load((std::string(PROJECT_DIR) + "/images/if_0.1_flow.bmp").c_str()).empty());

    Correlator baseline(kTestWindowSize, kTestOverlap);
    Correlator pid(MakePidTestParameters());
    VectorField result = baseline.Compute(ref_image.GetMat(), flow_image.GetMat());
    VectorField pid_result = pid.Compute(ref_image.GetMat(), flow_image.GetMat());

    Validation validation;
    VectorField validated = validation.PostProcess(result);
    VectorField pid_validated = validation.PostProcess(pid_result);

    float u_mean = result.u.mean();
    float v_mean = result.v.mean();
    float u_std = std::sqrt((result.u.array() - u_mean).square().mean());
    float v_std = std::sqrt((result.v.array() - v_mean).square().mean());
    float s2n_mean = result.s2n.mean();

    float pid_u_mean = pid_result.u.mean();
    float pid_v_mean = pid_result.v.mean();
    float pid_u_std = std::sqrt((pid_result.u.array() - pid_u_mean).square().mean());
    float pid_v_std = std::sqrt((pid_result.v.array() - pid_v_mean).square().mean());
    float pid_s2n_mean = pid_result.s2n.mean();

    PlaneFitMetrics raw_u_plane = ComputePlaneFitMetrics(result.u);
    PlaneFitMetrics raw_v_plane = ComputePlaneFitMetrics(result.v);
    PlaneFitMetrics validated_u_plane = ComputePlaneFitMetrics(validated.u);
    PlaneFitMetrics validated_v_plane = ComputePlaneFitMetrics(validated.v);
    PlaneFitMetrics pid_raw_u_plane = ComputePlaneFitMetrics(pid_result.u);
    PlaneFitMetrics pid_raw_v_plane = ComputePlaneFitMetrics(pid_result.v);
    PlaneFitMetrics pid_validated_u_plane = ComputePlaneFitMetrics(pid_validated.u);
    PlaneFitMetrics pid_validated_v_plane = ComputePlaneFitMetrics(pid_validated.v);

    PlaneFitMetrics raw_u_center_plane = ComputePlaneFitMetrics(CenterCrop(result.u));
    PlaneFitMetrics raw_v_center_plane = ComputePlaneFitMetrics(CenterCrop(result.v));
    PlaneFitMetrics validated_u_center_plane = ComputePlaneFitMetrics(CenterCrop(validated.u));
    PlaneFitMetrics validated_v_center_plane = ComputePlaneFitMetrics(CenterCrop(validated.v));
    PlaneFitMetrics pid_raw_u_center_plane = ComputePlaneFitMetrics(CenterCrop(pid_result.u));
    PlaneFitMetrics pid_raw_v_center_plane = ComputePlaneFitMetrics(CenterCrop(pid_result.v));
    PlaneFitMetrics pid_validated_u_center_plane = ComputePlaneFitMetrics(CenterCrop(pid_validated.u));
    PlaneFitMetrics pid_validated_v_center_plane = ComputePlaneFitMetrics(CenterCrop(pid_validated.v));

    PrintSection("Correlation Field Stats: if_0.1");
    PrintLine("baseline mean", FormatUV(u_mean, v_mean, " px"));
    PrintLine("pid mean", FormatUV(pid_u_mean, pid_v_mean, " px"));
    PrintLine("baseline stddev", FormatUV(u_std, v_std, " px"));
    PrintLine("pid stddev", FormatUV(pid_u_std, pid_v_std, " px"));
    PrintLine("baseline mean S2N", FormatFloat(s2n_mean));
    PrintLine("pid mean S2N", FormatFloat(pid_s2n_mean));
    PrintLine("baseline whole RMS", FormatUV(raw_u_plane.rms, raw_v_plane.rms, " px"));
    PrintLine("pid whole RMS", FormatUV(pid_raw_u_plane.rms, pid_raw_v_plane.rms, " px"));
    PrintLine("baseline whole RMS/std", FormatUV(raw_u_plane.normalized_std, raw_v_plane.normalized_std));
    PrintLine("pid whole RMS/std", FormatUV(pid_raw_u_plane.normalized_std, pid_raw_v_plane.normalized_std));
    PrintLine("baseline val whole", FormatUV(validated_u_plane.rms, validated_v_plane.rms, " px"));
    PrintLine("pid val whole", FormatUV(pid_validated_u_plane.rms, pid_validated_v_plane.rms, " px"));
    PrintLine("central crop", std::to_string(raw_u_center_plane.rows) + " x "
                               + std::to_string(raw_u_center_plane.cols)
                               + " (center 80% of map)");
    PrintLine("baseline ctr RMS", FormatUV(raw_u_center_plane.rms, raw_v_center_plane.rms, " px"));
    PrintLine("pid ctr RMS", FormatUV(pid_raw_u_center_plane.rms, pid_raw_v_center_plane.rms, " px"));
    PrintLine("baseline ctr RMS/std", FormatUV(raw_u_center_plane.normalized_std,
                                                raw_v_center_plane.normalized_std));
    PrintLine("pid ctr RMS/std", FormatUV(pid_raw_u_center_plane.normalized_std,
                                           pid_raw_v_center_plane.normalized_std));
    PrintLine("baseline val ctr", FormatUV(validated_u_center_plane.rms,
                                            validated_v_center_plane.rms, " px"));
    PrintLine("pid val ctr", FormatUV(pid_validated_u_center_plane.rms,
                                       pid_validated_v_center_plane.rms, " px"));

    CHECK(result.u.array().isFinite().all());
    CHECK(result.v.array().isFinite().all());
    CHECK(result.s2n.array().isFinite().all());
    CHECK(pid_result.u.array().isFinite().all());
    CHECK(pid_result.v.array().isFinite().all());
    CHECK(pid_result.s2n.array().isFinite().all());
    CHECK(result.u.rows() > 0);
    CHECK(result.u.cols() > 0);
    CHECK(validated_u_plane.rms < raw_u_plane.rms);
    CHECK(validated_v_plane.rms < raw_v_plane.rms);
    CHECK(pid_raw_u_center_plane.rms < raw_u_center_plane.rms);
    CHECK(pid_raw_v_center_plane.rms < raw_v_center_plane.rms);
}

TEST_CASE("Slide Gradient Diagnostics")
{
    struct GradientMetrics
    {
        float expected_corr = 0.0f;
        float orthogonal_corr = 0.0f;
        float residual_ratio = 0.0f;
        float neg_side_mean = 0.0f;
        float pos_side_mean = 0.0f;
    };

    struct GradientDiagnostic
    {
        GradientMetrics metrics;
        Eigen::MatrixXf crop;
        Eigen::MatrixXf residual;
    };

    struct PlateauMetrics
    {
        int locked_count = 0;
        int largest_component = 0;
        float locked_fraction = 0.0f;
    };

    auto correlation = [](const Eigen::VectorXf& a, const Eigen::VectorXf& b)
    {
        float a_mean = a.mean();
        float b_mean = b.mean();
        Eigen::ArrayXf ac = a.array() - a_mean;
        Eigen::ArrayXf bc = b.array() - b_mean;
        float denom = std::sqrt(ac.square().sum() * bc.square().sum());
        if(denom <= 1e-12f)
            return 0.0f;
        return (ac * bc).sum() / denom;
    };

    auto write_matrix_csv = [](const std::string& path, const Eigen::MatrixXf& matrix)
    {
        std::ofstream file(path);
        CHECK(file.is_open());
        if(!file.is_open())
            return;

        file << matrix.rows() << "," << matrix.cols() << "\n";
        for(int row = 0; row < matrix.rows(); row++)
        {
            for(int col = 0; col < matrix.cols(); col++)
            {
                if(col > 0)
                    file << ",";
                file << matrix(row, col);
            }
            file << "\n";
        }
    };

    auto field_metrics = [&](const Eigen::MatrixXf& field, bool dominant_x) -> GradientDiagnostic
    {
        int rows = static_cast<int>(field.rows());
        int cols = static_cast<int>(field.cols());
        int border_r = std::max(1, rows / 10);
        int border_c = std::max(1, cols / 10);
        int h = rows - 2 * border_r;
        int w = cols - 2 * border_c;

        Eigen::MatrixXf crop = field.block(border_r, border_c, h, w);
        Eigen::VectorXf values(h * w);
        Eigen::VectorXf x_coords(h * w);
        Eigen::VectorXf y_coords(h * w);

        int idx = 0;
        for(int y = 0; y < h; y++)
        {
            for(int x = 0; x < w; x++)
            {
                values(idx) = crop(y, x);
                x_coords(idx) = static_cast<float>(x);
                y_coords(idx) = static_cast<float>(y);
                idx++;
            }
        }

        Eigen::VectorXf profile;
        Eigen::MatrixXf modeled;
        if(dominant_x)
        {
            // For u, keep the measured mean profile along x by averaging each column.
            // Replicating that profile over rows removes only the expected 1D gradient
            // shape and leaves cross-axis structure, locking, striping, and other
            // artifacts in the residual.
            profile = crop.colwise().mean().transpose();
            modeled = profile.transpose().replicate(h, 1);
        }
        else
        {
            // For v, do the analogous construction using row means. This is not a
            // best-fit line or plane; it is an empirical dominant-axis profile.
            profile = crop.rowwise().mean();
            modeled = profile.replicate(1, w);
        }

        float field_std = std::sqrt((crop.array() - crop.mean()).square().mean());
        Eigen::MatrixXf residual = crop - modeled;
        float residual_std = std::sqrt(residual.array().square().mean());

        GradientDiagnostic diagnostic;
        diagnostic.metrics.expected_corr = std::abs(correlation(values, dominant_x ? x_coords : y_coords));
        diagnostic.metrics.orthogonal_corr = std::abs(correlation(values, dominant_x ? y_coords : x_coords));
        // residual_ratio = std(residual after removing dominant-axis mean profile)
        //                  / std(original cropped field)
        // Lower values mean less leftover structure after accounting for the
        // expected smooth gradient direction.
        diagnostic.metrics.residual_ratio = field_std > 1e-6f ? residual_std / field_std : 0.0f;

        if(dominant_x)
        {
            diagnostic.metrics.neg_side_mean = crop.leftCols(w / 2).mean();
            diagnostic.metrics.pos_side_mean = crop.rightCols(w - w / 2).mean();
        }
        else
        {
            diagnostic.metrics.neg_side_mean = crop.topRows(h / 2).mean();
            diagnostic.metrics.pos_side_mean = crop.bottomRows(h - h / 2).mean();
        }

        diagnostic.crop = crop;
        diagnostic.residual = residual;
        return diagnostic;
    };

    auto plateau_metrics = [](const Eigen::MatrixXf& field) -> PlateauMetrics
    {
        constexpr float tol = 1e-4f;

        auto is_locked = [&](float value)
        {
            float frac = value - std::round(value);
            float mag = std::abs(frac);
            return std::abs(mag - 0.49f) <= tol || std::abs(mag - 0.51f) <= tol;
        };

        PlateauMetrics metrics;
        int rows = field.rows();
        int cols = field.cols();
        if(rows <= 0 || cols <= 0)
            return metrics;

        std::vector<unsigned char> visited(rows * cols, 0);

        for(int row = 0; row < rows; row++)
        {
            for(int col = 0; col < cols; col++)
            {
                int index = row * cols + col;
                if(!is_locked(field(row, col)))
                    continue;

                metrics.locked_count++;
                if(visited[index])
                    continue;

                int component_size = 0;
                std::vector<int> queue{index};
                visited[index] = 1;

                while(!queue.empty())
                {
                    int current = queue.back();
                    queue.pop_back();
                    component_size++;

                    int current_row = current / cols;
                    int current_col = current % cols;

                    const int dr[4] = {1, -1, 0, 0};
                    const int dc[4] = {0, 0, 1, -1};

                    for(int dir = 0; dir < 4; dir++)
                    {
                        int next_row = current_row + dr[dir];
                        int next_col = current_col + dc[dir];

                        if(next_row < 0 || next_row >= rows || next_col < 0 || next_col >= cols)
                            continue;

                        int next_index = next_row * cols + next_col;
                        if(visited[next_index] || !is_locked(field(next_row, next_col)))
                            continue;

                        visited[next_index] = 1;
                        queue.push_back(next_index);
                    }
                }

                metrics.largest_component = std::max(metrics.largest_component, component_size);
            }
        }

        metrics.locked_fraction = static_cast<float>(metrics.locked_count) / static_cast<float>(rows * cols);
        return metrics;
    };

    struct PairInfo
    {
        const char* name;
        const char* ref_name;
        const char* flow_name;
    };

    const std::vector<PairInfo> pairs = {
        {"if_0.1", "if_0.1_ref.bmp", "if_0.1_flow.bmp"}
    };

    std::string output_dir = std::string(PROJECT_DIR) + "/csv/slide_gradient_diagnostics";
    std::filesystem::create_directories(output_dir);
    PrintSection("Slide Gradient Diagnostics");
    PrintLine("CSV output", output_dir);

    Validation validation;

    for(const PairInfo& pair : pairs)
    {
        Image ref_image;
        Image flow_image;
        CHECK(ref_image.Load((std::string(PROJECT_DIR) + "/images/" + pair.ref_name).c_str()).empty());
        CHECK(flow_image.Load((std::string(PROJECT_DIR) + "/images/" + pair.flow_name).c_str()).empty());

        Correlator correlator(kTestWindowSize, kTestOverlap);
        VectorField raw = correlator.Compute(ref_image.GetMat(), flow_image.GetMat());
        VectorField filtered = validation.PostProcess(raw);

        GradientDiagnostic raw_u = field_metrics(raw.u, true);
        GradientDiagnostic raw_v = field_metrics(raw.v, false);
        GradientDiagnostic filtered_u = field_metrics(filtered.u, true);
        GradientDiagnostic filtered_v = field_metrics(filtered.v, false);
        PlaneFitMetrics raw_u_plane = ComputePlaneFitMetrics(raw_u.crop);
        PlaneFitMetrics raw_v_plane = ComputePlaneFitMetrics(raw_v.crop);
        PlaneFitMetrics filtered_u_plane = ComputePlaneFitMetrics(filtered_u.crop);
        PlaneFitMetrics filtered_v_plane = ComputePlaneFitMetrics(filtered_v.crop);
        PlateauMetrics raw_u_plateau = plateau_metrics(raw_u.crop);
        PlateauMetrics raw_v_plateau = plateau_metrics(raw_v.crop);
        PlateauMetrics filtered_u_plateau = plateau_metrics(filtered_u.crop);
        PlateauMetrics filtered_v_plateau = plateau_metrics(filtered_v.crop);

        write_matrix_csv(output_dir + "/" + pair.name + "_raw_u_crop.csv", raw_u.crop);
        write_matrix_csv(output_dir + "/" + pair.name + "_raw_u_residual.csv", raw_u.residual);
        write_matrix_csv(output_dir + "/" + pair.name + "_raw_v_crop.csv", raw_v.crop);
        write_matrix_csv(output_dir + "/" + pair.name + "_raw_v_residual.csv", raw_v.residual);
        write_matrix_csv(output_dir + "/" + pair.name + "_val_u_crop.csv", filtered_u.crop);
        write_matrix_csv(output_dir + "/" + pair.name + "_val_u_residual.csv", filtered_u.residual);
        write_matrix_csv(output_dir + "/" + pair.name + "_val_v_crop.csv", filtered_v.crop);
        write_matrix_csv(output_dir + "/" + pair.name + "_val_v_residual.csv", filtered_v.residual);

        auto gradient_summary = [](const char* axis, const GradientDiagnostic& diag, bool include_halves)
        {
            std::ostringstream out;
            out << "main-axis corr=" << FormatFloat(diag.metrics.expected_corr, 4)
                << " (>0.75)"
                << " cross-axis corr=" << FormatFloat(diag.metrics.orthogonal_corr, 4)
                << " (<0.10)"
                << " residual ratio=" << FormatFloat(diag.metrics.residual_ratio, 4);
            if(include_halves)
                out << " halves=[" << FormatFloat(diag.metrics.neg_side_mean, 4)
                    << ", " << FormatFloat(diag.metrics.pos_side_mean, 4) << "]";
            return std::string(axis) + "  " + out.str();
        };

        auto plateau_summary = [](const PlateauMetrics& plateau)
        {
            std::ostringstream out;
            out << "count=" << plateau.locked_count
                << " frac=" << FormatFloat(plateau.locked_fraction, 4)
                << " max_cc=" << plateau.largest_component;
            return out.str();
        };

        PrintSection(std::string("Slide Pair: ") + pair.name);
        PrintLine("raw u", gradient_summary("u", raw_u, true));
        PrintLine("raw v", gradient_summary("v", raw_v, true));
        PrintLine("validated u", gradient_summary("u", filtered_u, false));
        PrintLine("validated v", gradient_summary("v", filtered_v, false));
        PrintLine("central plane RMS", FormatUV(raw_u_plane.rms, raw_v_plane.rms, " px"));
        PrintLine("central plane RMS/std", FormatUV(raw_u_plane.normalized_std,
                                                     raw_v_plane.normalized_std));
        PrintLine("validated plane RMS", FormatUV(filtered_u_plane.rms, filtered_v_plane.rms, " px"));
        PrintLine("validated RMS/std", FormatUV(filtered_u_plane.normalized_std,
                                                 filtered_v_plane.normalized_std));
        PrintLine("raw u half-pixel", plateau_summary(raw_u_plateau));
        PrintLine("raw v half-pixel", plateau_summary(raw_v_plateau));
        PrintLine("val u half-pixel", plateau_summary(filtered_u_plateau));
        PrintLine("val v half-pixel", plateau_summary(filtered_v_plateau));

        CHECK(raw.u.array().isFinite().all());
        CHECK(raw.v.array().isFinite().all());
        CHECK(filtered.u.array().isFinite().all());
        CHECK(filtered.v.array().isFinite().all());

        CHECK(raw_u.metrics.neg_side_mean * raw_u.metrics.pos_side_mean < 0.0f);
        CHECK(raw_v.metrics.neg_side_mean * raw_v.metrics.pos_side_mean < 0.0f);

        CHECK(raw_u.metrics.expected_corr > 0.75f);
        CHECK(raw_v.metrics.expected_corr > 0.75f);
        CHECK(raw_u.metrics.orthogonal_corr < 0.1f);
        CHECK(raw_v.metrics.orthogonal_corr < 0.1f);

        CHECK(filtered_u.metrics.expected_corr > filtered_u.metrics.orthogonal_corr);
        CHECK(filtered_v.metrics.expected_corr > filtered_v.metrics.orthogonal_corr);
        CHECK(filtered_u.metrics.residual_ratio < 0.35f);
        CHECK(filtered_v.metrics.residual_ratio < 0.45f);
        CHECK(filtered_u.metrics.residual_ratio <= raw_u.metrics.residual_ratio + 1e-4f);
        CHECK(filtered_v.metrics.residual_ratio <= raw_v.metrics.residual_ratio + 1e-4f);

        if(std::string(pair.name) == "if_0.1")
        {
            CHECK(raw_u_plateau.largest_component <= 64);
            CHECK(raw_v_plateau.largest_component <= 64);
            CHECK(filtered_u_plateau.largest_component <= 64);
            CHECK(filtered_v_plateau.largest_component <= 64);
            CHECK(raw_u_plateau.locked_fraction < 0.05f);
            CHECK(raw_v_plateau.locked_fraction < 0.05f);
            CHECK(filtered_u_plateau.locked_fraction < 0.05f);
            CHECK(filtered_v_plateau.locked_fraction < 0.05f);
        }
    }
}

TEST_CASE("Validation and Post Processing")
{
    SUBCASE("Validation - Known Outlier Detection")
    {
        // Create uniform flow field U=5, V=0
        VectorField field;
        field.u = Eigen::MatrixXf::Constant(10, 10, 5.0f);
        field.v = Eigen::MatrixXf::Zero(10, 10);
        field.s2n = Eigen::MatrixXf::Constant(10, 10, 2.0f); // All above s2n threshold
        field.width = 10;
        field.height = 10;

        // Inject known outliers at specific positions
        field.u(5, 5) = 50.0f;  // Extreme outlier
        field.v(5, 5) = 50.0f;
        field.u(3, 3) = -30.0f; // Another outlier
        field.v(3, 3) = -30.0f;

        Validation validation;

        // Test Validate - check outlier positions are flagged
        auto mask = validation.Validate(field);
        CHECK(mask(5, 5) == false);
        CHECK(mask(3, 3) == false);
        CHECK(mask(5, 4) == true);  // Clean neighbour should not be flagged
        CHECK(mask(5, 6) == true);

        // Test PostProcess - check outliers are replaced with neighbourhood median (~5)
        VectorField processed = validation.PostProcess(field, mask);
        CHECK(processed.u(5, 5) == doctest::Approx(5.0f).epsilon(1.0f));
        CHECK(processed.v(5, 5) == doctest::Approx(0.0f).epsilon(1.0f));
        CHECK(processed.u(3, 3) == doctest::Approx(5.0f).epsilon(1.0f));
        CHECK(processed.v(3, 3) == doctest::Approx(0.0f).epsilon(1.0f));

        // Check clean vectors are unchanged
        CHECK(processed.u(5, 4) == doctest::Approx(5.0f).epsilon(0.001f));
        CHECK(processed.v(5, 4) == doctest::Approx(0.0f).epsilon(0.001f));

        // Test combined PostProcess
        VectorField full_processed = validation.PostProcess(field);
        CHECK(full_processed.u(5, 5) == doctest::Approx(5.0f).epsilon(1.0f));
        CHECK(full_processed.v(5, 5) == doctest::Approx(0.0f).epsilon(1.0f));
    }

    SUBCASE("Validation - S2N Threshold")
    {
        // Create uniform flow field with some low s2n vectors
        VectorField field;
        field.u = Eigen::MatrixXf::Constant(10, 10, 5.0f);
        field.v = Eigen::MatrixXf::Zero(10, 10);
        field.s2n = Eigen::MatrixXf::Constant(10, 10, 3.0f);
        field.width = 10;
        field.height = 10;

        // Inject low s2n at specific positions with wrong displacement
        field.s2n(5, 5) = 0.5f;
        field.u(5, 5) = 50.0f;
        field.v(5, 5) = 50.0f;

        Validation validation;
        auto mask = validation.Validate(field);

        // Low s2n vector should be flagged regardless of neighbourhood
        CHECK(mask(5, 5) == false);

        VectorField processed = validation.PostProcess(field, mask);
        CHECK(processed.u(5, 5) == doctest::Approx(5.0f).epsilon(1.0f));
        CHECK(processed.v(5, 5) == doctest::Approx(0.0f).epsilon(1.0f));
    }
}

TEST_CASE("Full Pipeline Test")
{
    Session session;

    // Parameters
    session.correlatorparameters.window_size = kTestWindowSize;
    session.correlatorparameters.overlap = kTestOverlap;

    session.opticalparameters.Z_d = 300.0f;
    session.opticalparameters.Z_a = 100.0f;
    session.opticalparameters.f = 30.0f;

    //-----------------------------------------------------------------------------
    // IMAGE LOADING
    //-----------------------------------------------------------------------------
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    session.LoadRef(std::string(PROJECT_DIR) + "/images/slides/ref.bmp");
    session.LoadFlow({std::string(PROJECT_DIR) + "/images/slides/flow.bmp"});

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    std::cout << "Image Loading Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_CORRELATION) == Ready);

    //-----------------------------------------------------------------------------
    // Correlation
    //-----------------------------------------------------------------------------

    begin = std::chrono::steady_clock::now();
    session.RunCorrelation();
    end = std::chrono::steady_clock::now();
    std::cout << "Correlation Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_CORRELATION) == Done);
    session.GetCorrelationField().SaveCSV(std::string(PROJECT_DIR) + "/csv/result.csv");

    //-----------------------------------------------------------------------------
    // VALIDATION
    //-----------------------------------------------------------------------------
    begin = std::chrono::steady_clock::now();
    session.RunValidation();
    end = std::chrono::steady_clock::now();
    std::cout << "Post Process Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_VAL) == Done);
    session.GetValField().SaveCSV(std::string(PROJECT_DIR) + "/csv/processed.csv");

    //-----------------------------------------------------------------------------
    // RECONSTRUCTION
    //-----------------------------------------------------------------------------
    begin = std::chrono::steady_clock::now();
    session.RunReconstruction();
    end = std::chrono::steady_clock::now();
    std::cout << "Reconstruction Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_RECON) == Done);

    const Eigen::MatrixXf& surface = session.GetSurface();

    std::ofstream file;
    file.open((std::string(PROJECT_DIR) + "/csv/surface.csv").c_str());

    if(!file.is_open())
        return;

    file << "rows,cols\n";
    file << surface.rows() << "," << surface.cols() << "\n";
    file << "val\n";

    for(int j = 0; j < surface.cols(); j++)
        for(int i = 0; i < surface.rows(); i++)
            file << surface(i, j) << "\n";
}
