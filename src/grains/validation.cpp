#include <grains/validation.h>

const VectorField Validation::PostProcess(const VectorField& data) const
{
    //Define postprocessed data class
    VectorField processed = data;

    //First pass validation: s2n threshold
    Eigen::Array<bool, Eigen::Dynamic, Eigen::Dynamic> valid;
    valid = (data.s2n.array() > s2n_threshold);

    //Normalized residuals method: https://link.springer.com/article/10.1007/s00348-005-0016-6

    std::array<float, 8> u_neighbourhood, v_neighbourhood;
    std::array<float, 8> u_neighbourhood_res, v_neighbourhood_res;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            //Copy the neighbourhood (surrounding pixels)
            if(i > 0 && i < data.height - 1 && j > 0 && j < data.width - 1)
            {
                u_neighbourhood[0] = data.u(i-1, j-1);
                u_neighbourhood[1] = data.u(i,   j-1);
                u_neighbourhood[2] = data.u(i+1, j-1);
                u_neighbourhood[3] = data.u(i-1, j  );
                u_neighbourhood[4] = data.u(i+1, j  );
                u_neighbourhood[5] = data.u(i-1, j+1);
                u_neighbourhood[6] = data.u(i,   j+1);
                u_neighbourhood[7] = data.u(i+1, j+1);

                v_neighbourhood[0] = data.v(i-1, j-1);
                v_neighbourhood[1] = data.v(i,   j-1);
                v_neighbourhood[2] = data.v(i+1, j-1);
                v_neighbourhood[3] = data.v(i-1, j  );
                v_neighbourhood[4] = data.v(i+1, j  );
                v_neighbourhood[5] = data.v(i-1, j+1);
                v_neighbourhood[6] = data.v(i,   j+1);
                v_neighbourhood[7] = data.v(i+1, j+1);
                n = 8;
            }
            else //Slow method for edge cases
            {
                n = 0;
                for(int di = -1; di < 2; di++)
                {
                    for(int dj = -1; dj < 2; dj++)
                    {
                        if(di == 0 && dj == 0) continue;
                        int ni = i + di, nj = j + dj;
                        if(ni < 0 || ni >= data.height || nj < 0 || nj >= data.width) continue;
                        u_neighbourhood[n] = data.u(ni, nj);
                        v_neighbourhood[n] = data.v(ni, nj);
                        n++;
                    }
                }
            }

            std::sort(u_neighbourhood.begin(), u_neighbourhood.begin() + n);
            std::sort(v_neighbourhood.begin(), v_neighbourhood.begin() + n);

            float u_med, v_med;

            if(n % 2 != 0)
            {
                u_med = u_neighbourhood[n / 2];
                v_med = v_neighbourhood[n / 2];
            }
            else
            {
                u_med = (u_neighbourhood[(n - 1) / 2] + u_neighbourhood[n / 2]) / 2.0;
                v_med = (v_neighbourhood[(n - 1) / 2] + v_neighbourhood[n / 2]) / 2.0;
            }

            //If the outlier is already flagged, reassign
            if(!valid(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
                continue;
            }

            //Residuals calculations
            for(int k = 0; k < n; k++)
            {
                u_neighbourhood_res[k] = std::abs(u_neighbourhood[k] - u_med);
                v_neighbourhood_res[k] = std::abs(v_neighbourhood[k] - v_med);
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

            if(std::sqrt(u_nrm * u_nrm + v_nrm * v_nrm) > nrm_threshold)
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
            }

        }
    }

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
    std::array<float, 8> u_neighbourhood_res, v_neighbourhood_res;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            //Copy the neighbourhood (surrounding pixels)
            if(i > 0 && i < data.height - 1 && j > 0 && j < data.width - 1)
            {
                u_neighbourhood[0] = data.u(i-1, j-1);
                u_neighbourhood[1] = data.u(i,   j-1);
                u_neighbourhood[2] = data.u(i+1, j-1);
                u_neighbourhood[3] = data.u(i-1, j  );
                u_neighbourhood[4] = data.u(i+1, j  );
                u_neighbourhood[5] = data.u(i-1, j+1);
                u_neighbourhood[6] = data.u(i,   j+1);
                u_neighbourhood[7] = data.u(i+1, j+1);

                v_neighbourhood[0] = data.v(i-1, j-1);
                v_neighbourhood[1] = data.v(i,   j-1);
                v_neighbourhood[2] = data.v(i+1, j-1);
                v_neighbourhood[3] = data.v(i-1, j  );
                v_neighbourhood[4] = data.v(i+1, j  );
                v_neighbourhood[5] = data.v(i-1, j+1);
                v_neighbourhood[6] = data.v(i,   j+1);
                v_neighbourhood[7] = data.v(i+1, j+1);
                n = 8;
            }
            else //Slow method for edge cases
            {
                n = 0;
                for(int di = -1; di < 2; di++)
                {
                    for(int dj = -1; dj < 2; dj++)
                    {
                        if(di == 0 && dj == 0) continue;
                        int ni = i + di, nj = j + dj;
                        if(ni < 0 || ni >= data.height || nj < 0 || nj >= data.width) continue;
                        u_neighbourhood[n] = data.u(ni, nj);
                        v_neighbourhood[n] = data.v(ni, nj);
                        n++;
                    }
                }
            }

            std::sort(u_neighbourhood.begin(), u_neighbourhood.begin() + n);
            std::sort(v_neighbourhood.begin(), v_neighbourhood.begin() + n);

            float u_med, v_med;

            if(n % 2 != 0)
            {
                u_med = u_neighbourhood[n / 2];
                v_med = v_neighbourhood[n / 2];
            }
            else
            {
                u_med = (u_neighbourhood[(n - 1) / 2] + u_neighbourhood[n / 2]) / 2.0;
                v_med = (v_neighbourhood[(n - 1) / 2] + v_neighbourhood[n / 2]) / 2.0;
            }

            //If the outlier is already flagged, reassign
            if(!valid(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
                continue;
            }

            //Residuals calculations
            for(int k = 0; k < n; k++)
            {
                u_neighbourhood_res[k] = std::abs(u_neighbourhood[k] - u_med);
                v_neighbourhood_res[k] = std::abs(v_neighbourhood[k] - v_med);
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

            if(std::sqrt(u_nrm * u_nrm + v_nrm * v_nrm) > nrm_threshold)
            {
                valid(i, j) = 0.0;
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
    std::array<float, 8> u_neighbourhood_res, v_neighbourhood_res;
    int n;

    //Secondary pass: neighbourhood median residual thrshold
    for(int i = 0; i < data.height; i++)
    {
        for(int j = 0; j < data.width; j++)
        {
            //Copy the neighbourhood (surrounding pixels)
            if(i > 0 && i < data.height - 1 && j > 0 && j < data.width - 1)
            {
                u_neighbourhood[0] = data.u(i-1, j-1);
                u_neighbourhood[1] = data.u(i,   j-1);
                u_neighbourhood[2] = data.u(i+1, j-1);
                u_neighbourhood[3] = data.u(i-1, j  );
                u_neighbourhood[4] = data.u(i+1, j  );
                u_neighbourhood[5] = data.u(i-1, j+1);
                u_neighbourhood[6] = data.u(i,   j+1);
                u_neighbourhood[7] = data.u(i+1, j+1);

                v_neighbourhood[0] = data.v(i-1, j-1);
                v_neighbourhood[1] = data.v(i,   j-1);
                v_neighbourhood[2] = data.v(i+1, j-1);
                v_neighbourhood[3] = data.v(i-1, j  );
                v_neighbourhood[4] = data.v(i+1, j  );
                v_neighbourhood[5] = data.v(i-1, j+1);
                v_neighbourhood[6] = data.v(i,   j+1);
                v_neighbourhood[7] = data.v(i+1, j+1);
                n = 8;
            }
            else //Slow method for edge cases
            {
                n = 0;
                for(int di = -1; di < 2; di++)
                {
                    for(int dj = -1; dj < 2; dj++)
                    {
                        if(di == 0 && dj == 0) continue;
                        int ni = i + di, nj = j + dj;
                        if(ni < 0 || ni >= data.height || nj < 0 || nj >= data.width) continue;
                        u_neighbourhood[n] = data.u(ni, nj);
                        v_neighbourhood[n] = data.v(ni, nj);
                        n++;
                    }
                }
            }

            std::sort(u_neighbourhood.begin(), u_neighbourhood.begin() + n);
            std::sort(v_neighbourhood.begin(), v_neighbourhood.begin() + n);

            float u_med, v_med;

            if(n % 2 != 0)
            {
                u_med = u_neighbourhood[n / 2];
                v_med = v_neighbourhood[n / 2];
            }
            else
            {
                u_med = (u_neighbourhood[(n - 1) / 2] + u_neighbourhood[n / 2]) / 2.0;
                v_med = (v_neighbourhood[(n - 1) / 2] + v_neighbourhood[n / 2]) / 2.0;
            }

            //If the outlier is already flagged, reassign
            if(!mask(i, j))
            {
                processed.u(i, j) = u_med;
                processed.v(i, j) = v_med;
            }
        }
    }

    return processed;
}