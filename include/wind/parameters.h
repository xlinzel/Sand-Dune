#pragma once


struct Parameters
{
    //RI Calculation parameters
    float t = 1.0f; // sample thickeness (mm)

    float P_px = 2.2f; //camera sensor pixel pitch (um)

    float Z_d = 400.0f; //background - sample (mm)
    float Z_a = 250.0f; //sample - lens (mm)
    float f = 50.0f; //lens focal length (mm)

    //Setup sensitivity and resolutoion calculations
    float d_a = 25.5f; //aperture diameter (mm)

    float d_bg = 0.1f; //background dot diameter (mm)
};