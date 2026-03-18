#include <sun/mask.h>

Mask::Mask(const int w, const int h, const Eigen::Vector2f center , const float radius)
{
    GenBinCircleMask(w, h, center, radius);
}

void Mask::GenBinCircleMask(const int w, const int h, const Eigen::Vector2f center , const float radius)
{
    width = w;
    height = h;

    mask = Eigen::MatrixXf::Zero(h, w); //Rows then columns hence the order

    for(int row = 0; row < h; row++)
    {
        for(int col = 0; col < w; col++)
        {
            float dx = float(col) - center(0);
            float dy = float(row) - center(1);

            mask(row, col) = (dx * dx + dy * dy) < radius * radius ? 1.0f : 0.0f;
        }
    }

    set = true;
}

void Mask::GenTukCircleMask(const int w, const int h, const Eigen::Vector2f center, const float radius, const float a)
{
    //Tukey masking
    //a = region of tapering for edge
    width = w;
    height = h;

    mask = Eigen::MatrixXf::Zero(h, w); //Rows then columns hence the order

    for(int row = 0; row < h; row++)
    {
        for(int col = 0; col < w; col++)
        {
            float dx = float(col) - center(0);
            float dy = float(row) - center(1);

            float r = std::sqrt(dx * dx + dy * dy);

            if(r <= (1 - a) * radius)
            {
                mask(row, col) = 1.0f;
            }
            else if( (1 - a) * radius < r && r <= radius)
            {
                mask(row, col) = 0.5 * (1 + cos(std::numbers::pi * (r - (1 - a) * radius) / (a * radius)));
            }
            else //(dx * dx + dy * dy) > radius
            {
                mask(row, col) = 0.0f;
            }
        }
    }

    set = true;
}

Eigen::MatrixXf Mask::ApplyMask(const Eigen::MatrixXf& data)
{
    if(mask.cols() != data.cols() || mask.rows() != data.rows())
    {
        return Eigen::MatrixXf(); //Return empty matrix on size mismatch
    }

    return data.array() * mask.array();
}

const Eigen::MatrixXf& Mask::GetMask() const
{
    return mask;
}

bool Mask::GetSet() const
{
    return set;
}

int Mask::GetWidth() const
{
    return width;
}

int Mask::GetHeight() const
{
    return height;
}

