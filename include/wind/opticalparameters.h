#pragma once


struct OpticalParameters
{
    //RI Calculation parameters
    float t = 1.0f; // sample thickeness (mm)
    float n = 1.45f; //refractive index (for thickness measumrents)

    float P_px = 2.315f; //camera sensor pixel pitch (um)

    float Z_d = 218.0f; //background - sample (mm)
    float Z_a = 58.0f; //sample - lens (mm)
    float f = 25.0f; //lens focal length (mm)

    //Setup sensitivity and resolutoion calculations
    float d_a = 10.5f; //aperture diameter (mm)

    float d_bg = 0.1f; //background dot diameter (mm)
};