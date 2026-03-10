#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <lib/doctest.h>
#include <sun/image.h>
#include <sun/mask.h>
#include <iostream>
#include <vector>

TEST_CASE("Image Loading")
{
    //Initializaiton and then load
    Image image;
    std::string output = image.Load("../images/image.bmp");

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
    mask.GenerateCircleMask(100, 100, Eigen::Vector2f(40, 40), 30.0f);
    Eigen::MatrixXf result = mask.ApplyMask(data);

    CHECK(result(40, 40) == 1.0f);
    CHECK(result(50, 50) == 1.0f);
    CHECK(result(60, 50) == 1.0f);
    CHECK(result(70, 60) == 0.0f);
    CHECK(result(0, 0) == 0.0f);
}