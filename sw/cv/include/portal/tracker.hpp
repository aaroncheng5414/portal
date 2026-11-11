#pragma once

#include "portal/config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace portal {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct FaceObservation {
    Rect box;
    std::array<Vec2, 5> landmarks{};
    Vec3 position_m;
    double confidence = 0.0;
};

using LandmarkSignature = std::array<double, 10>;

LandmarkSignature landmark_signature(const std::array<Vec2, 5>& landmarks);

struct TrackSample {
    std::int64_t timestamp_ns = 0;
    Vec3 position_m;
};

struct PersonTrack {
    std::uint64_t id = 0;
    FaceObservation observation;
    LandmarkSignature signature{};
    Vec3 velocity_mps;
    std::vector<TrackSample> history;
    std::int64_t first_seen_ns = 0;
    std::int64_t last_seen_ns = 0;
    std::int64_t visible_since_ns = 0;
    bool observed = false;
};

enum class TrackingState { Acquiring, Locked, Lost };

struct PositionSnapshot {
    std::optional<std::uint64_t> person_id;
    Vec3 position_m;
    Vec3 velocity_mps;
    std::int64_t observation_timestamp_ns = 0;
    std::int64_t age_ns = 0;
    TrackingState state = TrackingState::Lost;
    bool predicted = false;
    bool stale = true;
};

class PersonTracker {
public:
    explicit PersonTracker(TrackerConfig config = {});

    void update(const std::vector<FaceObservation>& observations, std::int64_t capture_time_ns);
    void expire(std::int64_t now_ns);
    const std::vector<PersonTrack>& persons() const noexcept;
    std::optional<std::uint64_t> active_person_id() const noexcept;
    PositionSnapshot get_position(std::int64_t now_ns) const;

private:
    void expire_tracks(std::int64_t now_ns);
    void select_active(std::int64_t now_ns);

    TrackerConfig config_;
    std::vector<PersonTrack> persons_;
    std::optional<std::uint64_t> active_person_id_;
    std::optional<PositionSnapshot> last_selected_;
    std::uint64_t next_id_ = 1;
    std::optional<std::int64_t> last_capture_time_ns_;
    std::optional<std::int64_t> last_mutation_time_ns_;
};

}
