#include <grains/piv.h>
#include <algorithm>
#include <cmath>

PIV::PIV(const int window_size, const int overlap, const int search_size)
    : window_size(window_size), overlap(overlap), search_size(search_size)
{}

VectorField PIV::Compute(const Eigen::MatrixXf& reference, const Eigen::MatrixXf& flow)
{
    Eigen::Vector2i ref_size(reference.rows(), reference.cols());
    Eigen::Vector2i flow_size(flow.rows(), flow.cols());

    if(ref_size(0) != flow_size(0) || ref_size(1) != flow_size(1))
    {
        return VectorField();
    }

    int margin = (search_size - window_size) / 2;
    int movement = window_size - overlap;

    VectorField vectorfield(floor(ref_size(0) / movement),floor(ref_size(1) / movement));

    for(int win_row = 0; win_row < floor(ref_size(0) / movement); win_row++)
    {
        for(int win_col = 0; win_col < floor(ref_size(1) / movement); win_col++)
        {
            Eigen::MatrixXf w_reference = Eigen::MatrixXf::Zero(window_size, window_size);

            w_reference.block(
                0, 0,
                std::min(window_size, ref_size(0) - win_row * movement),
                std::min(window_size, ref_size(1) - win_col * movement)) 
                
            = reference.block(
                win_row * movement, win_col * movement,
                std::min(window_size, ref_size(0) - win_row * movement),
                std::min(window_size, ref_size(1) - win_col * movement));

            Eigen::MatrixXf w_flow = Eigen::MatrixXf::Zero(search_size, search_size);

            w_flow.block(
                std::max(0, margin - win_row * movement),
                std::max(0, margin - win_col * movement),
                std::min(search_size - std::max(0, margin - win_row * movement), ref_size(0) - std::max(0, win_row * movement - margin)),
                std::min(search_size - std::max(0, margin - win_col * movement), ref_size(1) - std::max(0, win_col * movement - margin)))
            
            = flow.block(
                std::max(0, win_row * movement - margin),
                std::max(0, win_col * movement - margin),
                std::min(search_size - std::max(0, margin - win_row * movement), ref_size(0) - std::max(0, win_row * movement - margin)),
                std::min(search_size - std::max(0, margin - win_col * movement), ref_size(1) - std::max(0, win_col * movement - margin)));


            Eigen::MatrixXf ccmap = CrossCorrelation(w_reference, w_flow);

            PeakResult peak = FindPeak(ccmap);
            vectorfield.u(win_row, win_col) = peak.u;
            vectorfield.v(win_row, win_col) = peak.v;
            vectorfield.s2n(win_row, win_col) = peak.s2n;
        }
    }

    return vectorfield;
}

Eigen::MatrixXf PIV::CrossCorrelation(const Eigen::MatrixXf& w_reference, const Eigen::MatrixXf& w_flow)
{
    Eigen::Vector2i ref_size(w_reference.rows(), w_reference.cols());
    Eigen::Vector2i flow_size(w_flow.rows(), w_flow.cols());

    Eigen::MatrixXf ccmap(flow_size(0) + ref_size(0) - 1, flow_size(1) + ref_size(1) - 1); //Cross correlation map

    for(int row_offset = 0; row_offset < flow_size(0) + ref_size(0) - 1; row_offset++)
    {
        for(int col_offset = 0; col_offset < flow_size(1) + ref_size(1) - 1; col_offset++)
        {
            //TODO:: Swap to FFT implementation
            Eigen::MatrixXf ref_overlap = w_reference.block(
                std::clamp(ref_size(0) - row_offset - 1, 0, ref_size(0)), 
                std::clamp(ref_size(1) - col_offset - 1, 0, ref_size(1)),
                std::min({row_offset + 1, ref_size(0), flow_size(0), ref_size(0) + flow_size(0) - 1 - row_offset}),
                std::min({col_offset + 1, ref_size(1), flow_size(1), ref_size(1) + flow_size(1) - 1 - col_offset}));

            Eigen::MatrixXf flow_overlap = w_flow.block(
                std::clamp(row_offset - ref_size(0) + 1, 0, flow_size(0)),
                std::clamp(col_offset - ref_size(1) + 1, 0, flow_size(1)),
                std::min({row_offset + 1, ref_size(0), flow_size(0), ref_size(0) + flow_size(0) - 1 - row_offset}),
                std::min({col_offset + 1, ref_size(1), flow_size(1), ref_size(1) + flow_size(1) - 1 - col_offset}));

            float sum = (ref_overlap.array() * flow_overlap.array()).sum();

            ccmap(row_offset, col_offset) = sum;
        }
    }

    return ccmap;
}

PIV::PeakResult PIV::FindPeak(const Eigen::MatrixXf& ccmap)
{
    int row, col;

    float peak = ccmap.maxCoeff(&row, &col);

    if (row == 0 || row == ccmap.rows() - 1 || col == 0 || col == ccmap.cols() - 1)
    {
        return PeakResult{float(col - ccmap.cols()/2), float(row - ccmap.rows()/2), peak / ccmap.mean()};
    }
    else if (ccmap(row, col-1) <= 0 || ccmap(row, col+1) <= 0 || ccmap(row-1, col) <= 0 || ccmap(row+1, col) <= 0)
    {
        return PeakResult{float(col - ccmap.cols()/2), float(row - ccmap.rows()/2), peak / ccmap.mean()};
    }

    //Gaussian Interpolation
    float x_interp = col + (std::log(ccmap(row, col - 1)) - std::log(ccmap(row, col + 1)))
                            / (2 * std::log(ccmap(row, col - 1)) - 4 * std::log(ccmap(row, col)) + 2 * std::log(ccmap(row, col + 1)));

    float y_interp = row + (std::log(ccmap(row - 1, col)) - std::log(ccmap(row + 1, col)))
                            / (2 * std::log(ccmap(row - 1, col)) - 4 * std::log(ccmap(row, col)) + 2 * std::log(ccmap(row + 1, col)));

    float u = x_interp - ccmap.cols() / 2;
    float v = y_interp - ccmap.rows() / 2;

    float sig2noise = peak / ccmap.mean();

    return PeakResult{u, v, sig2noise};
}


int PIV::GetWindowSize() const
{
    return window_size;
}

int PIV::GetOverlap() const
{
    return overlap;
}

int PIV::GetSearchSize() const
{
    return search_size;
}

void PIV::SetWindowSize(const int size)
{
    window_size = size;
}

void PIV::SetOverlap(const int overlap)
{
    this->overlap = overlap;
}

void PIV::SetSearchSize(const int size)
{
    search_size = size;
}