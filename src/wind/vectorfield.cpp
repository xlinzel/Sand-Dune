#include <wind/vectorfield.h>
#include <iostream>
#include <fstream>

VectorField::VectorField(const int rows, const int cols)
    : width(cols), height(rows)
{
    u = Eigen::MatrixXf::Zero(height, width);
    v = Eigen::MatrixXf::Zero(height, width);
    s2n = Eigen::MatrixXf::Zero(height, width);
}

void VectorField::SaveCSV(const std::string& path) const
{
    std::ofstream file;
    file.open(path);

    if(!file.is_open())
        return;

    file << "rows,cols\n";
    file << width << "," << height << "\n";

    file << "u,v,s2n\n";

    //Collumn major format
    for(int j = 0; j < width; j++)
    {
        for(int i = 0; i < height; i++)
        {
            file << u(i, j) << "," << v(i, j) << "," << s2n(i, j) << "\n";
        }
    }
}