#include <sun/image.h>

Image::Image(const char* filename)
{
    Load(filename);
}

Image::~Image() {}

std::string Image::Load(const char* filename)
{
    unsigned char* raw = stbi_load(filename, &width, &height, &channels, 1); //stb_iamge handles grayscale automatically if passed 1 for requested channels

    if (!raw) //Error check for invalid image data return
    {
        loaded = false;
        return std::string("Failed to return image data: ") + filename;
    }
    
    
    data = std::vector<unsigned char>(raw, raw + width * height);
    stbi_image_free(raw);
    loaded = true;

    return std::string("");
}

const std::vector<unsigned char>& Image::GetData() const
{
    return data;
}

bool Image::GetLoaded() const
{
    return loaded;
}

int Image::GetWidth() const
{
    return width;
}

int Image::GetHeight() const
{
    return height;
}
