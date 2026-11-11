#pragma once

#include <cstddef>
#include <cstdint>

namespace portal {

struct TrackerConfig {
    std::int64_t track_timeout_ns = 1'000'000'000;
    std::int64_t switch_cooldown_ns = 1'000'000'000;
    std::int64_t max_prediction_ns = 1'000'000'000;
    std::int64_t acquisition_ns = 500'000'000;
    std::size_t history_limit = 8;
    double min_confidence = 0.6;
    double max_position_distance_m = 0.75;
    double max_signature_distance = 0.35;

    static constexpr long double position_match_weight = 0.45L;
    static constexpr long double signature_match_weight = 0.45L;
    static constexpr long double overlap_match_weight = 0.10L;
    static constexpr double unmatched_cost = 1.1;
};

}
