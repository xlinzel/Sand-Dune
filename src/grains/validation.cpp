#include <grains/validation.h>


/// @brief Given data and i,j, compute the u and v neighbourhood and n.
int Neighbourhood(int i, int j, const VectorField& data, std::array<float, 8>& u_n, std::array<float, 8>& v_n)
{
    int n = 0;

    if(i > 0 && i < data.height - 1 && j > 0 && j < data.width - 1)
    {
        u_n[0] = data.u(i-1, j-1);
        u_n[1] = data.u(i,   j-1);
        u_n[2] = data.u(i+1, j-1);
        u_n[3] = data.u(i-1, j  );
        u_n[4] = data.u(i+1, j  );
        u_n[5] = data.u(i-1, j+1);
        u_n[6] = data.u(i,   j+1);
        u_n[7] = data.u(i+1, j+1);

        v_n[0] = data.v(i-1, j-1);
        v_n[1] = data.v(i,   j-1);
        v_n[2] = data.v(i+1, j-1);
        v_n[3] = data.v(i-1, j  );
        v_n[4] = data.v(i+1, j  );
        v_n[5] = data.v(i-1, j+1);
        v_n[6] = data.v(i,   j+1);
        v_n[7] = data.v(i+1, j+1);
        n = 8;
    }
    else //Slow method for edge cases
    {
        for(int di = -1; di < 2; di++)
        {
            for(int dj = -1; dj < 2; dj++)
            {
                if(di == 0 && dj == 0) continue;
                int ni = i + di, nj = j + dj;
                if(ni < 0 || ni >= data.height || nj < 0 || nj >= data.width) continue;
                u_n[n] = data.u(ni, nj);
                v_n[n] = data.v(ni, nj);
                n++;
            }
        }
    }

    std::sort(u_n.begin(), u_n.begin() + n);
    std::sort(v_n.begin(), v_n.begin() + n);

    return n;
}

/// @brief Given the u and v neighbourhoods compute the median values for each. Median variables passed by reference.
void NMedian(std::array<float, 8>& u_n, std::array<float, 8>& v_n, int n, float& u_med, float& v_med)
{
    if(n % 2 != 0)
    {
        u_med = u_n[n / 2];
        v_med = v_n[n / 2];
    }
    else
    {
        u_med = (u_n[(n - 1) / 2] + u_n[n / 2]) / 2.0;
        v_med = (v_n[(n - 1) / 2] + v_n[n / 2]) / 2.0;
    }
}

/// @brief Given all the data and median values, compute if via UNO if the given point isan outlier. Return bool flag.
bool Validation::OutlierComp(int i, int j, const VectorField& data, std::array<float, 8>& u_n, std::array<float, 8>& v_n,
                    int n,  float u_med, float v_med) const
{
    std::array<float, 8> u_neighbourhood_res, v_neighbourhood_res;

    //Residuals calculations
    for(int k = 0; k < n; k++)
    {
        u_neighbourhood_res[k] = std::abs(u_n[k] - u_med);
        v_neighbourhood_res[k] = std::abs(v_n[k] - v_med);
    }

    std::sort(u_neighbourhood_res.begin(), u_neighbourhood_res.begin() + n);
    std::sort(v_neighbourhood_res.begin(), v_neighbourhood_res.begin() + n);
    
    float u_res_med, v_res_med;

    if(n % 2 != 0)
    {
        u_res_med = u_neighbourhood_res[n / 2];
        v_res_med = v_neighbourhood_res[n / 2];
    }
    else
    {
        u_res_med = (u_neighbourhood_res[(n - 1) / 2] + u_neighbourhood_res[n / 2]) / 2.0;
        v_res_med = (v_neighbourhood_res[(n - 1) / 2] + v_neighbourhood_res[n / 2]) / 2.0;
    }

    float u_nrm, v_nrm;

    u_nrm = std::abs((data.u(i, j) - u_med) / (u_res_med + eps));
    v_nrm = std::abs((data.v(i, j) - v_med) / (v_res_med + eps));

    return std::sqrt(u_nrm * u_nrm + v_nrm * v_nrm) > nrm_threshold;
}


const VectorField Validation::PostProcess(const VectorField& data) const
{
    //Define postprocessed data class
    VectorField processed = data;

    //First pass validation: s2n threshold
    Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> valid;
    valid = (data.s2n.array() > s2n_threshold);

    //Normalized residuals method: https://link.springer.com/article/10.1007/s00348-005-0016-6

    std::array<float, 8> u_neighbourhood, v_neighbourhood;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            n = Neighbourhood(i, j, data, u_neighbourhood, v_neighbourhood);

            float u_med, v_med;
            NMedian(u_neighbourhood, v_neighbourhood, n, u_med, v_med);

            //If the outlier is already flagged, reassign
            if(!valid(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
                continue;
            }

            if(OutlierComp(i, j, data, u_neighbourhood, v_neighbourhood, n, u_med, v_med))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
            }
        }
    }

    processed.CalcMag();

    return processed;
}

const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> Validation::Validate(const VectorField& data) const
{
    //Define postprocessed data class
    VectorField processed = data;

    //First pass validation: s2n threshold
    Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> valid;
    valid = (data.s2n.array() > s2n_threshold);

    //Normalized residuals method: https://link.springer.com/article/10.1007/s00348-005-0016-6

    std::array<float, 8> u_neighbourhood, v_neighbourhood;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            n = Neighbourhood(i, j, data, u_neighbourhood, v_neighbourhood);

            float u_med, v_med;
            NMedian(u_neighbourhood, v_neighbourhood, n, u_med, v_med);

            //If the outlier is already flagged, reassign
            if(!valid(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
                continue;
            }

            if(OutlierComp(i, j, data, u_neighbourhood, v_neighbourhood, n, u_med, v_med))
            {
                valid(i, j) = false;
            }

        }
    }

    return valid;
}

const VectorField Validation::PostProcess(const VectorField& data, const Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic>& mask) const
{
    //Define postprocessed data class
    VectorField processed = data;

    //Normalized residuals method: https://link.springer.com/article/10.1007/s00348-005-0016-6

    std::array<float, 8> u_neighbourhood, v_neighbourhood;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            n = Neighbourhood(i, j, data, u_neighbourhood, v_neighbourhood);

            float u_med, v_med;
            NMedian(u_neighbourhood, v_neighbourhood, n, u_med, v_med);

            //If the outlier is already flagged, reassign
            if(!mask(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
                continue;
            }

            if(OutlierComp(i, j, data, u_neighbourhood, v_neighbourhood, n, u_med, v_med))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
            }
        }
    }

    return processed;
}
