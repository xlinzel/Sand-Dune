#pragma once

#include <future>
#include <chrono>
#include <fstream>
#include <atomic>

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
    STAGE_RECON, ///< Surface reconstruction stage.
    STAGE_TOTAL  ///< Sentinel - total number of stages.
};

/// @brief Available surface-integration back ends.
enum ReconstructionSolver
{
    RECON_POISSON,           ///< Mask-aware least-squares Poisson solve on the correlation grid.
    RECON_FRANKOT_CHELLAPPA  ///< FFT-based Frankot-Chellappa integration.
};

/// @brief Top-level controller for the BOS surface reconstruction pipeline.
///
/// Manages image loading, the three-stage processing pipeline (Correlation -> Validation -> Reconstruction),
/// refraction correction, result scaling, and async execution.  A single Session instance is
/// shared between the UI and the compute back-end.
///
/// The pipeline produces corrected displacement-gradient maps that are integrated
/// by Frankot-Chellappa or a least squares Poisson reconstruction into either a refractive-index variation
/// map or a thickness-variation map, depending on @p b_ref.
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

    /// @brief Run surface reconstruction (blocking).
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

    /// @brief Convert raw pixel displacements to displayed gradient units and final surface units.
    ///
    /// Correlation/validation fields are scaled to dn/dx or mm/dx, while the
    /// reconstructed surface additionally absorbs the physical correlation-grid spacing.
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
    void SaveSurfaceCSV(const std::string& base_path); ///< Write the surface map to CSV.

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

    bool HasFlow() const;  ///< Returns true if every selected flow image loaded successfully.
    int  GetFlowCount() const; ///< Number of loaded flow images.
    const std::vector<std::string>& GetFlowPaths() const;

    const VectorField&    GetCorrelationField() const; ///< Scaled correlation field for the active frame.
    const VectorField&    GetRawCorrelationField() const; ///< Pixel-unit correlation field for the active frame after optional correction.
    const VectorField&    GetValField()  const; ///< Validated field for the active frame.
    const VectorField&    GetRawValField()  const; ///< Unscaled validated field for the active frame (pixels).
    const Eigen::MatrixXf& GetSurface() const; ///< Reconstructed surface for the active frame.

    /// @brief Query the current state of a pipeline stage.
    StageState GetStageState(Stages s) const;

    /// @brief Progress of one stage in [0, 1].
    float GetStageProgress(Stages s) const;

    /// @brief Remaining ETA in seconds for one stage, or -1 when unavailable.
    float GetStageEtaSeconds(Stages s) const;

    /// @brief Progress of the full pipeline in [0, 1] during RunAllAsync().
    float GetFullProgress() const;

    /// @brief Remaining ETA in seconds for the full pipeline, or -1 when unavailable.
    float GetFullEtaSeconds() const;

    /// @brief True while the combined RunAllAsync() pipeline is active.
    bool IsRunningFullPipeline() const;

    /// @brief Select the reconstruction back end used by the reconstruction stage.
    void SetReconstructionSolver(ReconstructionSolver solver);

    /// @brief Query the active reconstruction back end.
    ReconstructionSolver GetReconstructionSolver() const;

    ///@}

    // -------------------------------------------------------------------------
    /// @name Mask parameters (set directly by the UI)
    // -------------------------------------------------------------------------
    ///@{
    int   posx = 0,    posy = 0; ///< Mask centre in field coordinates (col, row).
    int   radius = 1000;         ///< Mask radius in field units.
    float a = 0.2f;              ///< Tukey roll-off parameter [0, 1].
    bool  mask_apply = true;     ///< Restrict reconstruction to the masked domain when true.
    ///@}

    CorrelatorParameters correlatorparameters; ///< Window size and overlap for correlation.
    OpticalParameters opticalparameters;///< Camera geometry and sample properties.

    // -------------------------------------------------------------------------
    /// @name Progress and timing
    // -------------------------------------------------------------------------
    ///@{
    std::atomic<float> progress{0.0f};               ///< Pipeline progress in [0, 1], updated during async runs.
    std::chrono::steady_clock::time_point task_start; ///< Wall-clock time when the current async task began.
    std::atomic<float> stage_progress[STAGE_TOTAL];   ///< Per-stage progress in [0, 1].
    std::chrono::steady_clock::time_point stage_start[STAGE_TOTAL]; ///< Per-stage start times.
    std::atomic<float> full_progress{0.0f};          ///< Full-pipeline progress in [0, 1] during RunAllAsync().
    std::chrono::steady_clock::time_point full_task_start; ///< Start time for the current full-pipeline run.
    std::atomic<bool> full_pipeline_running{false};  ///< True while RunAllAsync() is executing.
    ///@}

    // -------------------------------------------------------------------------
    /// @name Processing flags
    // -------------------------------------------------------------------------
    ///@{
    bool n_correction  = true;  ///< Apply refraction correction to correlation results.
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

    /// @brief Pre-compute the refraction-correction matrices for a correlator grid of size h x w.
    ///
    /// Calculates the lateral ray displacement caused by refraction through a sample of known
    /// thickness (@p OpticalParameters::t) and refractive index (@p OpticalParameters::n),
    /// then maps that displacement onto the correlation grid in pixel units.
    void ComputeRefractionCorrection(int h, int w);

    /// @brief Subtract the pre-computed correction matrices from all raw_correlation_field entries.
    void ApplyRefractionCorrection();

    /// @brief Build the active reconstruction mask in correlation-grid coordinates.
    Eigen::MatrixXf GetReconstructionMask(int width, int height);

    /// @brief Dispatch to the selected reconstruction back end.
    Eigen::MatrixXf ReconstructField(const Reconstruction& recon,
                                     const VectorField& field,
                                     const Eigen::MatrixXf& recon_mask) const;

    ///@}

    std::vector<VectorField> raw_correlation_field; ///< Per-frame correlation fields in pixel units after optional correction.
    std::vector<VectorField> correlation_field;     ///< Per-frame correlation fields in displayed gradient units.
    Eigen::MatrixXf correction[2];                  ///< Refraction correction matrices for u [0] and v [1], stored in pixel units.

    std::vector<VectorField> raw_val_field; ///< Per-frame validated fields in pixel units after optional correction.
    std::vector<VectorField> val_field;     ///< Per-frame validated fields in displayed gradient units.

    std::vector<Eigen::MatrixXf> raw_surface; ///< Per-frame reconstructed surfaces in grid-integrated raw units.
    std::vector<Eigen::MatrixXf> surface;     ///< Per-frame surfaces in final display units (dn or mm).

    int active_index = 0;

    std::atomic<StageState> stagestates[STAGE_TOTAL];
    std::atomic<ReconstructionSolver> reconstruction_solver{RECON_POISSON};

    // Async
    std::atomic<bool> stop_requested{false};
    std::future<void> activetask;
    std::future<void> save_task;
};
