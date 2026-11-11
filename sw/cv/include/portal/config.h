#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace portal {

struct TrackerConfig {
    std::int64_t track_timeout_ns = 1'000'000'000;
    std::int64_t switch_cooldown_ns = 1'000'000'000;
    std::int64_t max_prediction_ns = 1'000'000'000;
    std::int64_t acquisition_ns = 500'000'000;
    std::size_t history_limit = 8;
    std::size_t position_average_samples = 3;
    double min_confidence = 0.6;
    double max_position_distance_m = 0.75;
    double max_signature_distance = 0.35;

    static constexpr long double position_match_weight = 0.45L;
    static constexpr long double signature_match_weight = 0.45L;
    static constexpr long double overlap_match_weight = 0.10L;
    static constexpr double unmatched_cost = 1.1;
};

struct ServiceConfig {
    TrackerConfig tracker;
    std::string backend = "cuda";
    std::string target = "fp32";
    std::string device = "auto";
    std::string host = "127.0.0.1";
    std::string model_path = "models/face_detection_yunet_2023mar.onnx";
    std::string schema_path = "api/openapi.json";
    int port = 8080;
    int width = 1280;
    int height = 720;
    double requested_fps = 30.0;
    double ipd_m = 0.063;
    double focal_length_px = 0.0;
    double horizontal_fov_deg = 60.0;
    float detector_score_threshold = 0.6F;
    float detector_nms_threshold = 0.3F;
    int detector_top_k = 5000;
    int jpeg_quality = 80;
    int camera_read_timeout_ms = 2000;
    int camera_retry_ms = 500;
    int camera_stale_ms = 1000;
    int worker_tick_ms = 50;
    int shutdown_poll_ms = 50;
    int camera_index_limit = 10;
    int fps_window_frames = 60;
    int http_threads = 4;
    bool request_mjpeg = true;
    bool synthetic = false;
};

}
