#pragma once

#include <vector>
#include <iostream>
#include <lib/stb_image.h>

class Image 
{
public:
    Image() = default;
    Image(const char* filename);
    ~Image();

    std::string Load(const char* filename);

    const std::vector<unsigned char>& GetData() const;

    bool GetLoaded() const;
    int GetWidth() const;
    int GetHeight() const;

private:
    bool loaded = false;
    int width, height, channels;
    std::vector<unsigned char> data;
};