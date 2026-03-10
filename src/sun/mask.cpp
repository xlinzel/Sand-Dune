#include <sun/mask.h>

Mask::Mask(const int w, const int h, const Eigen::Vector2f center , const float radius)
{
    GenerateCircleMask(w, h, center, radius);
}

void Mask::GenerateCircleMask(const int w, const int h, const Eigen::Vector2f center , const float radius)
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

Eigen::MatrixXf Mask::ApplyMask(const Eigen::MatrixXf& data)
{
    if(mask.cols() != data.cols() || mask.rows() != data.rows())
    {
        return Eigen::MatrixXf(); //Return empty matrix on size mismatch
    }

    Eigen::MatrixXf return_data(mask.rows(), data.cols());

    for(int row = 0; row < mask.rows(); row++)
    {
        for(int col = 0; col < data.cols(); col++)
        {
            return_data(row, col) = mask(row, col) == 1.0f ? data(row, col) : 0.0f;
        }
    }

    return return_data;
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

