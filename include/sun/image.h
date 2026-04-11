#pragma once

#include <vector>
#include <iostream>
#include <lib/stb_image.h>
#include <Eigen/Dense>

/// @brief Loads a raster image from disk and exposes it as raw bytes or an Eigen matrix.
///
/// Internally stores an 8-bit grayscale (single-channel) pixel buffer.
/// Pixel values are normalised to [0, 1] when accessed via GetMat().
class Image
{
public:
    Image() = default;

    /// @brief Load an image immediately on construction.
    /// @param filename Path to the image file (PNG, JPEG, BMP, etc.).
    Image(const char* filename);

    ~Image();

    /// @brief Load (or reload) an image from disk.
    /// @param filename Path to the image file.
    /// @return An empty string on success, or an error message on failure.
    std::string Load(const char* filename);

    /// @brief Raw 8-bit pixel data in row-major order.
    const std::vector<unsigned char>& GetData() const;

    /// @brief Pixel data as a float matrix normalised to [0, 1], shape (height x width).
    Eigen::MatrixXf GetMat() const;

    bool GetLoaded() const; ///< Returns true if an image has been successfully loaded.
    int  GetWidth()  const; ///< Image width in pixels.
    int  GetHeight() const; ///< Image height in pixels.

private:
    bool loaded = false;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<unsigned char> data;
};
