#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <lib/doctest.h>
#include <sun/image.h>
#include <sun/mask.h>
#include <grains/piv.h>
#include <grains/validation.h>
#include <grains/reconstruction.h>
#include <session.h>
#include <iostream>
#include <chrono>
#include <vector>

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
    //Initializaiton and then load
    Image image;
    std::string output = image.Load((std::string(PROJECT_DIR) + "/images/image.bmp").c_str());

    CHECK(output.empty());        //Empty string = success
    CHECK(image.GetLoaded());
    CHECK(image.GetWidth() > 0);
    CHECK(image.GetHeight() > 0);
    CHECK(!image.GetData().empty());

    //Loading constructor
    Image image1("../images/image.bmp");
    CHECK(image.GetLoaded());
    CHECK(image.GetWidth() > 0);
    CHECK(image.GetHeight() > 0);
    CHECK(!image.GetData().empty());
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

TEST_CASE("PIV Computation")
{
    SUBCASE("Parameters")
    {
        PIV piv(64, 48, 80);
        CHECK(piv.GetWindowSize() == 64);
        CHECK(piv.GetOverlap() == 48);
        CHECK(piv.GetSearchSize() == 80);

        piv.SetWindowSize(32);
        CHECK(piv.GetWindowSize() == 32);
    }

    SUBCASE("Zero Displacement")
    {
        // Identical images should produce near-zero displacement
        Eigen::MatrixXf img = Eigen::MatrixXf::Random(200, 200);
        PIV piv(64, 48, 80);
        VectorField result = piv.Compute(img, img);

        std::cout << "U vector maxcoeff: " << result.u.cwiseAbs().maxCoeff() << std::endl;
        std::cout << "V vector maxcoeff: " << result.v.cwiseAbs().maxCoeff() << std::endl;

        CHECK(result.u.cwiseAbs().maxCoeff() < 1.0f);
        CHECK(result.v.cwiseAbs().maxCoeff() < 1.0f);
    }

    SUBCASE("Known Displacement")
    {
        // Shift flow image by 5 pixels horizontally
        Eigen::MatrixXf ref = Eigen::MatrixXf::Random(200, 200);
        Eigen::MatrixXf flow = Eigen::MatrixXf::Zero(200, 200);
        flow.block(0, 5, 200, 195) = ref.block(0, 0, 200, 195);

        PIV piv(64, 48, 80);
        VectorField result = piv.Compute(ref, flow);

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
    session.pivparameters.window_size = 64;
    session.pivparameters.overlap = 54;
    session.pivparameters.search_size = 72;

    session.opticalparameters.Z_d = 300.0f;
    session.opticalparameters.Z_a = 100.0f;
    session.opticalparameters.f = 30.0f;

    //-----------------------------------------------------------------------------
    // IMAGE LOADING
    //-----------------------------------------------------------------------------
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    session.LoadRef(std::string(PROJECT_DIR) + "/images/ref.bmp");
    session.LoadFlow(std::string(PROJECT_DIR) + "/images/flow.bmp");

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    std::cout << "Image Loading Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_PIV) == Ready);

    //-----------------------------------------------------------------------------
    // PIV
    //-----------------------------------------------------------------------------

    begin = std::chrono::steady_clock::now();
    session.RunPIV();
    end = std::chrono::steady_clock::now();
    std::cout << "PIV Elapsed Time: " << FormatTime(begin, end) << "\n";

    CHECK(session.GetStageState(STAGE_PIV) == Done);
    session.GetPIVField().SaveCSV(std::string(PROJECT_DIR) + "/csv/result.csv");

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