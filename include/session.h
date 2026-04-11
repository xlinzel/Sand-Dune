#pragma once

#include <future>
#include <chrono>
#include <fstream>

#include <sun/image.h>
#include <sun/mask.h>
#include <wind/vectorfield.h>
#include <grains/correlator.h>
#include <grains/validation.h>
#include <grains/reconstruction.h>
#include <wind/opticalparameters.h>
#include <wind/correlatorparameters.h>

/// @brief References:
/// - https://pmc.ncbi.nlm.nih.gov/articles/PMC8747424/
/// - https://link.springer.com/article/10.1007/s00348-015-1927-5
/// - https://link.springer.com/article/10.1007/s00348-005-0016-6
/// - https://link.springer.com/article/10.1007/s00348-010-0985-y

/// @brief Processing state of an individual pipeline stage.
enum StageState
{
    Idle,  ///< No data loaded; stage has never run.
    Ready, ///< Input data available; stage has not yet run.
    Busy,  ///< Stage is currently executing asynchronously.
    Done,  ///< Stage completed successfully; results are available.
    Dirty  ///< Results are stale because an upstream stage re-ran.
};

/// @brief Identifiers for the three main pipeline stages.
enum Stages
{
    STAGE_CORRELATION, ///< Cross-correlation stage.
    STAGE_VAL,   ///< Validation and outlier replacement stage.
    STAGE_RECON, ///< Surface reconstruction (or thickness map in calibration mode).
    STAGE_TOTAL  ///< Sentinel - total number of stages.
};

/// @brief Top-level controller for the BOS surface reconstruction pipeline.
///
/// Manages image loading, the three-stage processing pipeline (Correlation -> Validation -> Reconstruction),
/// refraction correction, result scaling, and async execution.  A single Session instance is
/// shared between the UI and the compute back-end.
///
/// ### Normal mode (@p n_calibration = false)
/// The pipeline produces refractive-index gradient maps (dn/dx, dn/dy) that are integrated
/// by Frankot-Chellappa reconstruction into a surface height (dn) map.
///
/// ### Calibration mode (@p n_calibration = true)
/// Refraction correction is skipped.  After validation the pipeline inverts the refraction
/// geometry to estimate the physical sample thickness from the measured pixel displacements,
/// enabling system calibration against a known reference sample.
class Session
{
public:
    Session();
    ~Session();

    // -------------------------------------------------------------------------
    // @name Image loading
    // -------------------------------------------------------------------------
    ///@{

    /// @brief Load the undisturbed reference background image.
    /// @param path Absolute or relative path to the image file.
    void LoadRef(const std::string& path);

    /// @brief Load one or more disturbed flow images for batch processing.
    /// @param paths Ordered list of image file paths.
    void LoadFlow(const std::vector<std::string>& paths);

    ///@}

    // -------------------------------------------------------------------------
    /// @name Synchronous pipeline execution
    // -------------------------------------------------------------------------
    ///@{

    /// @brief Run correlation on all loaded flow images (blocking).
    void RunCorrelation();

    /// @brief Run validation and outlier replacement on correlation results (blocking).
    void RunValidation();

    /// @brief Run surface reconstruction or thickness-map computation (blocking).
    ///
    /// In normal mode calls Reconstruction::Compute() followed by ScaleFields().
    /// In calibration mode (@p n_calibration = true) calls ComputeThicknessMap().
    void RunReconstruction();

    ///@}

    // -------------------------------------------------------------------------
    /// @name Asynchronous pipeline execution
    // -------------------------------------------------------------------------
    ///@{

    void RunCorrelationAsync();   ///< Launch RunCorrelation() on a background thread.
    void RunValidationAsync();    ///< Launch RunValidation() on a background thread.
    void RunReconstructionAsync();///< Launch RunReconstruction() on a background thread.

    /// @brief Run the complete pipeline (Correlation -> Validation -> Reconstruction) on a background thread.
    void RunAllAsync();

    /// @brief Returns true while any pipeline stage is executing asynchronously.
    bool IsRunning() const;

    ///@}

    // -------------------------------------------------------------------------
    /// @name Scaling
    // -------------------------------------------------------------------------
    ///@{

    /// @brief Convert raw pixel displacements to physical units (dn or mm) using the optical parameters.
    ///
    /// In normal mode scales correlation/validation fields and the reconstructed surface.
    /// In calibration mode (@p n_calibration = true) the correlation/validation fields are left in pixels
    /// and the surface is already in mm, so this call is a no-op for those outputs.
    void ScaleFields();

    ///@}

    // -------------------------------------------------------------------------
    /// @name Saving
    // -------------------------------------------------------------------------
    ///@{

    bool IsSaving() const; ///< Returns true while an async save is in progress.

    /// @brief Save all results asynchronously to the given base path.
    void SaveAsync(const std::string& base_path);

    void SaveCorrelationCSV(const std::string& base_path); ///< Write scaled correlation fields to CSV.
    void SaveValCSV(const std::string& base_path);     ///< Write validated fields to CSV.
    void SaveSurfaceCSV(const std::string& base_path); ///< Write the surface / thickness map to CSV.

    ///@}

    // -------------------------------------------------------------------------
    /// @name Data accessors
    // -------------------------------------------------------------------------
    ///@{

    const Image& GetRef()  const; ///< Currently loaded reference image.
    const Image& GetFlow() const; ///< Flow image at the active batch index.

    const std::string& GetRefPath()  const; ///< File path of the reference image.
    const std::string& GetFlowPath() const; ///< File path of the active flow image.

    void SetActiveIndex(int i); ///< Select which batch frame to display.
    int  GetActiveIndex() const;

    bool HasFlow() const;  ///< Returns true if at least one flow image is loaded.
    int  GetFlowCount() const; ///< Number of loaded flow images.
    const std::vector<std::string>& GetFlowPaths() const;

    const VectorField&    GetCorrelationField() const; ///< Scaled correlation field for the active frame.
    const VectorField&    GetValField()  const; ///< Validated field for the active frame.
    const Eigen::MatrixXf& GetSurface() const; ///< Reconstructed surface or thickness map for the active frame.

    /// @brief Query the current state of a pipeline stage.
    StageState GetStageState(Stages s) const;

    ///@}

    // -------------------------------------------------------------------------
    /// @name Mask parameters (set directly by the UI)
    // -------------------------------------------------------------------------
    ///@{
    int   posx = 0,    posy = 0; ///< Mask centre in field coordinates (col, row).
    int   radius = 1000;         ///< Mask radius in field units.
    float a = 0.1f;              ///< Tukey roll-off parameter [0, 1].
    bool  mask_apply = true;     ///< Apply the mask to reconstruction outputs when true.
    ///@}

    CorrelatorParameters correlatorparameters; ///< Window size and overlap for correlation.
    OpticalParameters opticalparameters;///< Camera geometry and sample properties.

    // -------------------------------------------------------------------------
    /// @name Progress and timing
    // -------------------------------------------------------------------------
    ///@{
    std::atomic<float> progress{0.0f};               ///< Pipeline progress in [0, 1], updated during async runs.
    std::chrono::steady_clock::time_point task_start; ///< Wall-clock time when the current async task began.
    ///@}

    // -------------------------------------------------------------------------
    /// @name Processing flags
    // -------------------------------------------------------------------------
    ///@{
    bool n_correction  = true;  ///< Apply refraction correction to correlation results (normal mode only).
    bool n_calibration = false; ///< Calibration mode: invert refraction to estimate sample thickness.
    bool b_ref         = true;  ///< Show refractive-index units (dn) when true; thickness (mm) when false.
    ///@}

private:
    std::string ref_path;
    std::vector<std::string> flow_paths;
    std::atomic<int> batch_index{0};
    Image ref;
    std::vector<Image> flows;
    Mask mask;

    // -------------------------------------------------------------------------
    /// @name Refraction correction
    // -------------------------------------------------------------------------
    ///@{

    /// @brief Pre-compute the per-pixel refraction correction matrices for a field of size h x w.
    ///
    /// Calculates the lateral ray displacement caused by refraction through a sample of known
    /// thickness (@p OpticalParameters::t) and refractive index (@p OpticalParameters::n).
    void ComputeRefractionCorrection(int h, int w);

    /// @brief Subtract the pre-computed correction matrices from all raw_correlation_field entries.
    void ApplyRefractionCorrection();

    /// @brief Invert the refraction geometry to estimate sample thickness from validated displacements.
    ///
    /// Used in calibration mode (@p n_calibration = true).  Results are stored in raw_surface
    /// and surface in millimetres.
    void ComputeThicknessMap();

    ///@}

    std::vector<VectorField> raw_correlation_field; ///< Per-frame correlation fields before refraction correction.
    std::vector<VectorField> correlation_field;     ///< Per-frame correlation fields after correction and scaling.
    Eigen::MatrixXf correction[2];          ///< Refraction correction matrices for u [0] and v [1].

    std::vector<VectorField> raw_val_field; ///< Per-frame validated fields before scaling.
    std::vector<VectorField> val_field;     ///< Per-frame validated fields after scaling.

    std::vector<Eigen::MatrixXf> raw_surface; ///< Per-frame surfaces before scaling (metres in calibration mode).
    std::vector<Eigen::MatrixXf> surface;     ///< Per-frame surfaces in final display units (dn or mm).

    int active_index = 0;

    StageState stagestates[STAGE_TOTAL];

    // Async
    std::atomic<bool> stop_requested{false};
    std::future<void> activetask;
    std::future<void> save_task;
};
