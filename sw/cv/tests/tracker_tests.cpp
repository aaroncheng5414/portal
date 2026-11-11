#include "portal/tracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using portal::FaceObservation;
using portal::PersonTrack;
using portal::PersonTracker;
using portal::TrackerConfig;
using portal::TrackingState;
using portal::Vec2;
using portal::Vec3;

std::int64_t ns(double seconds) {
    return static_cast<std::int64_t>(std::llround(seconds * 1'000'000'000.0));
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(double actual, double expected, const std::string& message, double tolerance = 1e-8) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

void same_position(const Vec3& actual, const Vec3& expected, const std::string& message) {
    near(actual.x, expected.x, message + " x");
    near(actual.y, expected.y, message + " y");
    near(actual.z, expected.z, message + " z");
}

template <typename Operation>
void rejects(Operation operation, const std::string& message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

FaceObservation face(Vec3 position, int shape = 0, double size = 100.0) {
    FaceObservation observation;
    observation.box = {300.0 + position.x * 100.0, 180.0 - position.y * 100.0, size, size * 1.2};
    const std::array<Vec2, 5> points = shape == 0
        ? std::array<Vec2, 5>{{{0.28, 0.32}, {0.72, 0.32}, {0.50, 0.52}, {0.33, 0.75}, {0.67, 0.75}}}
        : std::array<Vec2, 5>{{{0.15, 0.25}, {0.85, 0.25}, {0.50, 0.70}, {0.40, 0.90}, {0.60, 0.90}}};
    for (std::size_t index = 0; index < points.size(); ++index) {
        observation.landmarks[index] = {
            observation.box.x + points[index].x * size,
            observation.box.y + points[index].y * size,
        };
    }
    observation.position_m = position;
    observation.confidence = 0.95;
    return observation;
}

const PersonTrack& track(const PersonTracker& tracker, std::uint64_t id) {
    const auto found = std::find_if(tracker.persons().begin(), tracker.persons().end(), [id](const PersonTrack& person) {
        return person.id == id;
    });
    require(found != tracker.persons().end(), "expected person ID " + std::to_string(id));
    return *found;
}

std::uint64_t id_at_x(const PersonTracker& tracker, double x) {
    const auto found = std::find_if(tracker.persons().begin(), tracker.persons().end(), [x](const PersonTrack& person) {
        return std::abs(person.observation.position_m.x - x) < 1e-8;
    });
    require(found != tracker.persons().end(), "expected a person at requested x");
    return found->id;
}

void same_tracks(const std::vector<PersonTrack>& actual, const std::vector<PersonTrack>& expected) {
    require(actual.size() == expected.size(), "person count changed unexpectedly");
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto& a = actual[index];
        const auto& b = expected[index];
        require(a.id == b.id && a.first_seen_ns == b.first_seen_ns && a.last_seen_ns == b.last_seen_ns
                && a.visible_since_ns == b.visible_since_ns && a.observed == b.observed, "track identity or observation times changed");
        same_position(a.observation.position_m, b.observation.position_m, "track position changed");
        same_position(a.filtered_position_m, b.filtered_position_m, "filtered position changed");
        same_position(a.velocity_mps, b.velocity_mps, "track velocity changed");
        near(a.observation.confidence, b.observation.confidence, "track confidence changed");
        near(a.observation.box.x, b.observation.box.x, "box x changed");
        near(a.observation.box.y, b.observation.box.y, "box y changed");
        near(a.observation.box.width, b.observation.box.width, "box width changed");
        near(a.observation.box.height, b.observation.box.height, "box height changed");
        for (std::size_t point = 0; point < a.observation.landmarks.size(); ++point) {
            near(a.observation.landmarks[point].x, b.observation.landmarks[point].x, "landmark x changed");
            near(a.observation.landmarks[point].y, b.observation.landmarks[point].y, "landmark y changed");
        }
        for (std::size_t feature = 0; feature < a.signature.size(); ++feature) {
            near(a.signature[feature], b.signature[feature], "signature changed");
        }
        require(a.history.size() == b.history.size(), "history size changed");
        for (std::size_t sample = 0; sample < a.history.size(); ++sample) {
            require(a.history[sample].timestamp_ns == b.history[sample].timestamp_ns, "history timestamp changed");
            same_position(a.history[sample].position_m, b.history[sample].position_m, "history position changed");
        }
    }
}

void independent_people_and_reordered_detections() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    PersonTracker tracker(config);
    tracker.update({face({-0.3, 0.0, 2.0}), face({0.3, 0.1, 2.0}, 1)}, ns(0.0));
    const auto left_id = id_at_x(tracker, -0.3);
    const auto right_id = id_at_x(tracker, 0.3);
    const auto active = tracker.active_person_id();
    require(active.has_value(), "a zero-dwell viewer should be selected");
    tracker.update({face({0.4, 0.1, 2.0}, 1), face({-0.2, 0.0, 2.0})}, ns(0.1));
    tracker.update({face({-0.1, 0.0, 2.0}), face({0.5, 0.1, 2.0}, 1)}, ns(0.2));
    require(tracker.persons().size() == 2, "reordering must not create people");
    require(tracker.active_person_id() == active, "updating other people must not replace the active viewer");
    const auto& left = track(tracker, left_id);
    const auto& right = track(tracker, right_id);
    same_position(left.observation.position_m, {-0.1, 0.0, 2.0}, "left person position");
    same_position(right.observation.position_m, {0.5, 0.1, 2.0}, "right person position");
    require(left.history.size() == 3 && right.history.size() == 3, "both people need independent history");
    near(left.history.front().position_m.x, -0.3, "left history start");
    near(right.history.front().position_m.x, 0.3, "right history start");
    near(left.velocity_mps.x, 1.0, "left velocity");
    near(right.velocity_mps.x, 1.0, "right velocity");
    require(left.last_seen_ns == ns(0.2) && right.last_seen_ns == ns(0.2), "every matched person must update");
}

void full_occlusion_preserves_motion_and_identity() {
    PersonTracker tracker;
    tracker.update({face({-0.4, 0.0, 2.0}), face({0.4, 0.0, 2.0}, 1)}, ns(0.0));
    const auto first = id_at_x(tracker, -0.4);
    const auto second = id_at_x(tracker, 0.4);
    tracker.update({face({-0.3, 0.0, 2.0}), face({0.3, 0.0, 2.0}, 1)}, ns(0.1));
    tracker.update({}, ns(0.2));
    require(!track(tracker, first).observed && !track(tracker, second).observed, "missing people must be marked unobserved");
    require(track(tracker, first).history.size() == 2, "occlusion must not append invented observations");
    tracker.update({face({0.1, 0.0, 2.0}, 1), face({-0.1, 0.0, 2.0})}, ns(0.3));
    require(tracker.persons().size() == 2, "brief full occlusion must not create replacement IDs");
    near(track(tracker, first).observation.position_m.x, -0.1, "first person reacquisition");
    near(track(tracker, second).observation.position_m.x, 0.1, "second person reacquisition");
    require(track(tracker, first).history.size() == 3 && track(tracker, second).history.size() == 3, "reacquisition must preserve real history");
}

void partial_occlusion_preserves_independent_updates() {
    PersonTracker tracker;
    tracker.update({face({-0.4, 0.0, 2.0}), face({0.4, 0.0, 2.0}, 1)}, ns(0.0));
    const auto first = id_at_x(tracker, -0.4);
    const auto second = id_at_x(tracker, 0.4);
    tracker.update({face({-0.3, 0.0, 2.0}), face({0.3, 0.0, 2.0}, 1)}, ns(0.1));
    tracker.update({face({-0.1, 0.0, 2.0})}, ns(0.3));
    require(track(tracker, first).history.size() == 3 && track(tracker, second).history.size() == 2, "only observed histories should grow");
    require(track(tracker, second).last_seen_ns == ns(0.1), "another person's observations must not refresh a missing person");
    tracker.update({face({-0.1, 0.0, 2.0}, 1), face({0.1, 0.0, 2.0})}, ns(0.5));
    require(tracker.persons().size() == 2, "brief partial occlusion must preserve both IDs");
    near(track(tracker, first).observation.position_m.x, 0.1, "moving first person after crossing");
    near(track(tracker, second).observation.position_m.x, -0.1, "returning second person after crossing");
}

void motion_disambiguates_crossing_people_after_occlusion() {
    PersonTracker tracker;
    tracker.update({face({-0.3, 0.0, 2.0}), face({0.3, 0.0, 2.0})}, ns(0.0));
    const auto moving_right = id_at_x(tracker, -0.3);
    const auto moving_left = id_at_x(tracker, 0.3);
    tracker.update({face({-0.1, 0.0, 2.0}), face({0.1, 0.0, 2.0})}, ns(0.1));
    tracker.update({}, ns(0.2));
    tracker.update({face({-0.2, 0.0, 2.0}), face({0.2, 0.0, 2.0})}, ns(0.25));
    require(tracker.persons().size() == 2, "crossing after brief loss must retain both tracks");
    near(track(tracker, moving_right).observation.position_m.x, 0.2, "right-moving person must follow its motion rather than its old location");
    near(track(tracker, moving_left).observation.position_m.x, -0.2, "left-moving person must follow its motion rather than its old location");
}

void landmark_signature_is_translation_and_scale_invariant() {
    const auto points = face({0.0, 0.0, 2.0}).landmarks;
    auto transformed = points;
    for (auto& point : transformed) {
        point.x = point.x * 3.4 - 270.0;
        point.y = point.y * 3.4 + 120.0;
    }
    const auto original = portal::landmark_signature(points);
    const auto adjusted = portal::landmark_signature(transformed);
    for (std::size_t index = 0; index < original.size(); ++index) {
        near(adjusted[index], original[index], "normalized landmark pair " + std::to_string(index));
    }
    const auto different = portal::landmark_signature(face({0.0, 0.0, 2.0}, 1).landmarks);
    double difference = 0.0;
    for (std::size_t index = 0; index < original.size(); ++index) {
        difference += std::abs(original[index] - different[index]);
    }
    require(difference > 0.1, "different facial geometry must retain a distinguishable signature");
}

void geometry_disambiguates_colocated_faces() {
    PersonTracker tracker;
    const auto narrow = face({0.0, 0.0, 2.0});
    const auto wide = face({0.0, 0.0, 2.0}, 1);
    tracker.update({narrow, wide}, ns(0.0));
    require(tracker.persons().size() == 2, "co-located observations still represent two people");
    const auto first_id = tracker.persons()[0].id;
    const auto second_id = tracker.persons()[1].id;
    const auto first_signature = tracker.persons()[0].signature;
    const auto second_signature = tracker.persons()[1].signature;
    tracker.update({wide, narrow}, ns(0.1));
    require(tracker.persons().size() == 2, "geometry matching must not create duplicate tracks");
    for (std::size_t index = 0; index < first_signature.size(); ++index) {
        near(track(tracker, first_id).signature[index], first_signature[index], "first co-located person's geometry");
        near(track(tracker, second_id).signature[index], second_signature[index], "second co-located person's geometry");
    }
    require(track(tracker, first_id).history.size() == 2 && track(tracker, second_id).history.size() == 2, "each co-located person must receive exactly one observation");
}

void association_is_one_to_one() {
    PersonTracker tracker;
    tracker.update({face({-0.2, 0.0, 2.0}), face({0.2, 0.0, 2.0})}, ns(0.0));
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.1));
    require(tracker.persons().size() == 2, "one observation must not create or collapse people");
    const auto observed = std::count_if(tracker.persons().begin(), tracker.persons().end(), [](const PersonTrack& person) { return person.observed; });
    require(observed == 1, "one observation cannot match multiple people");
    const auto sample_count = tracker.persons()[0].history.size() + tracker.persons()[1].history.size();
    require(sample_count == 3, "one observation must append exactly one history sample");

    PersonTracker splitting;
    splitting.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    const auto original_id = splitting.persons().front().id;
    splitting.update({face({-0.1, 0.0, 2.0}), face({0.1, 0.0, 2.0})}, ns(0.1));
    require(splitting.persons().size() == 2, "a second observation needs its own person");
    require(track(splitting, original_id).history.size() == 2, "one person cannot consume two same-frame observations");
    require(splitting.persons()[0].id != splitting.persons()[1].id, "person IDs must be unique");
}

void per_person_ttl_runs_without_camera_frames() {
    PersonTracker tracker;
    tracker.update({face({-0.5, 0.0, 2.0}), face({0.5, 0.0, 2.0}, 1)}, ns(0.0));
    const auto first = id_at_x(tracker, -0.5);
    const auto second = id_at_x(tracker, 0.5);
    tracker.update({face({-0.4, 0.0, 2.0})}, ns(0.4));
    tracker.expire(ns(1.0));
    require(tracker.persons().size() == 1, "the older missing person must expire independently");
    require(tracker.persons().front().id == first && tracker.persons().front().id != second, "TTL must use each person's own last observation");
    require(track(tracker, first).history.size() == 2 && track(tracker, first).last_seen_ns == ns(0.4), "expire must not create a camera observation");
    tracker.expire(ns(1.4));
    require(tracker.persons().empty(), "expiry must work without new camera frames");
}

void returning_after_ttl_creates_fresh_identity() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    PersonTracker tracker(config);
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    const auto old_id = tracker.persons().front().id;
    tracker.update({face({0.1, 0.0, 2.0})}, ns(0.1));
    near(track(tracker, old_id).velocity_mps.x, 1.0, "old track should have motion history");
    tracker.expire(ns(1.1));
    require(tracker.persons().empty(), "old track should be removed at its TTL");
    tracker.update({face({0.15, 0.0, 2.0})}, ns(1.2));
    require(tracker.persons().size() == 1, "returning person should create a fresh track");
    const auto& fresh = tracker.persons().front();
    require(fresh.id != old_id, "expired IDs must not be reused");
    require(fresh.history.size() == 1 && fresh.first_seen_ns == ns(1.2), "returning person must not inherit expired history");
    same_position(fresh.velocity_mps, {0.0, 0.0, 0.0}, "returning person starts with zero inferred velocity");
    same_position(fresh.filtered_position_m, fresh.observation.position_m, "new ID must not inherit an expired person's average");
}

void acquisition_requires_continuous_dwell() {
    PersonTracker tracker;
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    const auto candidate = tracker.persons().front().id;
    require(!tracker.active_person_id(), "first detection must not bypass dwell");
    const auto acquiring = tracker.get_position(ns(0.0));
    require(acquiring.state == TrackingState::Acquiring, "a new candidate should be acquiring");
    same_position(acquiring.position_m, {0.0, 0.0, 2.0}, "acquiring must expose measured position");
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.4));
    require(!tracker.active_person_id(), "selection must wait for the full acquisition dwell");
    tracker.update({}, ns(0.45));
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.5));
    require(tracker.persons().front().id == candidate, "brief absence can retain candidate identity");
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.999));
    require(!tracker.active_person_id(), "missing observations must restart continuous dwell");
    tracker.update({face({0.0, 0.0, 2.0})}, ns(1.0));
    require(tracker.active_person_id() == candidate, "continuous half-second dwell should acquire the viewer");
    require(tracker.get_position(ns(1.0)).state == TrackingState::Locked, "acquired viewer should be locked");
}

void cooldown_reserves_active_viewer_then_selects_eligible_person() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    PersonTracker tracker(config);
    tracker.update({face({-0.2, 0.0, 2.0})}, ns(0.0));
    const auto first = tracker.active_person_id();
    tracker.update({face({-0.2, 0.0, 2.0}), face({0.4, 0.0, 2.0}, 1)}, ns(0.1));
    const auto second = id_at_x(tracker, 0.4);
    tracker.update({face({0.4, 0.0, 2.0}, 1)}, ns(0.2));
    require(tracker.active_person_id() == first, "a brief absence must reserve the selected person");
    const auto missing = tracker.get_position(ns(0.2));
    require(missing.person_id == first && missing.state == TrackingState::Lost, "missing selected person must be reported lost");
    tracker.update({face({0.4, 0.0, 2.0}, 1)}, ns(1.099));
    require(tracker.active_person_id() == first, "another person must not replace the viewer before the one-second cooldown");
    tracker.update({face({0.4, 0.0, 2.0}, 1)}, ns(1.1));
    require(tracker.active_person_id() == second, "an eligible person must be selected after cooldown without a user action");
    require(tracker.get_position(ns(1.1)).state == TrackingState::Locked, "replacement viewer should be locked");
}

void reacquiring_reserved_viewer_does_not_restart_dwell() {
    PersonTracker tracker;
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.5));
    const auto selected = tracker.active_person_id();
    require(selected.has_value(), "viewer should be selected before occlusion");
    tracker.update({}, ns(0.6));
    tracker.update({face({0.03, 0.0, 2.0})}, ns(0.7));
    require(tracker.active_person_id() == selected, "reserved viewer should keep identity after brief occlusion");
    require(tracker.get_position(ns(0.7)).state == TrackingState::Locked, "reacquiring reserved viewer should lock immediately");
}

void prediction_tapers_to_a_bounded_endpoint_without_refreshing_observations() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    config.track_timeout_ns = ns(3.0);
    PersonTracker tracker(config);
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    tracker.update({face({0.2, -0.1, 2.05})}, ns(0.2));
    const auto original = tracker.persons();
    const auto id = tracker.active_person_id();
    const auto midpoint = tracker.get_position(ns(0.7));
    require(midpoint.person_id == id && midpoint.predicted, "a delayed query should predict the selected person");
    same_position(midpoint.position_m, {0.475, -0.2375, 2.11875}, "half-horizon displacement starts from the filtered position");
    same_position(midpoint.velocity_mps, {0.5, -0.25, 0.125}, "velocity falls linearly halfway through prediction");
    require(midpoint.observation_timestamp_ns == ns(0.2) && midpoint.age_ns == ns(0.5), "prediction must retain the original observation time and age");
    const auto endpoint = tracker.get_position(ns(1.2));
    same_position(endpoint.position_m, {0.6, -0.3, 2.15}, "one-second prediction endpoint");
    same_position(endpoint.velocity_mps, {0.0, 0.0, 0.0}, "prediction velocity reaches zero at horizon");
    require(!endpoint.predicted && !endpoint.stale, "prediction horizon should stop motion without expiring the track");
    const auto held = tracker.get_position(ns(2.2));
    same_position(held.position_m, endpoint.position_m, "prediction must remain bounded beyond one second");
    require(held.observation_timestamp_ns == ns(0.2) && held.age_ns == ns(2.0), "repeated queries must not refresh observation age");
    same_tracks(tracker.persons(), original);
    require(tracker.active_person_id() == id, "queries must not change selection");
}

void history_limit_applies_independently() {
    TrackerConfig config;
    config.history_limit = 3;
    PersonTracker tracker(config);
    for (int step = 0; step < 6; ++step) {
        tracker.update({face({-0.3 + step * 0.01, 0.0, 2.0}), face({0.3, step * 0.01, 2.0}, 1)}, ns(step * 0.1));
    }
    require(tracker.persons().size() == 2, "history pruning must not change people");
    for (const auto& person : tracker.persons()) {
        require(person.history.size() == 3, "each history must obey its own bound");
        require(person.history.front().timestamp_ns == ns(0.3) && person.history.back().timestamp_ns == ns(0.5), "history should retain the newest real observations");
    }
}

void expiry_holds_the_last_tapered_position() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    PersonTracker tracker(config);
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    tracker.update({face({0.2, 0.0, 2.0})}, ns(0.2));
    tracker.expire(ns(1.2));
    require(tracker.persons().empty(), "expired person must be removed from the live vector");
    const auto held = tracker.get_position(ns(2.0));
    same_position(held.position_m, {0.6, 0.0, 2.0}, "expiry should hold the filtered prediction endpoint without snapping back");
    same_position(held.velocity_mps, {0.0, 0.0, 0.0}, "expired position must have zero predicted velocity");
    require(held.state == TrackingState::Lost && held.stale, "held expired position must remain explicitly lost and stale");
    require(held.observation_timestamp_ns == ns(0.2), "expiry must preserve the actual last observation time");
}

void invalid_observations_are_rejected_transactionally() {
    PersonTracker tracker;
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    const auto original = tracker.persons();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto infinity = std::numeric_limits<double>::infinity();
    const std::vector<std::function<void(FaceObservation&)>> corruptions = {
        [nan](FaceObservation& observation) { observation.position_m.x = nan; },
        [infinity](FaceObservation& observation) { observation.landmarks[2].y = infinity; },
        [](FaceObservation& observation) { observation.box.width = -1.0; },
        [nan](FaceObservation& observation) { observation.confidence = nan; },
        [](FaceObservation& observation) { observation.landmarks.fill({1.0, 1.0}); },
    };
    for (const auto& corrupt : corruptions) {
        auto invalid = face({0.1, 0.0, 2.0});
        corrupt(invalid);
        rejects([&] { tracker.update({face({0.2, 0.0, 2.0}, 1), invalid}, ns(0.1)); }, "invalid frame must be rejected");
        same_tracks(tracker.persons(), original);
    }
    tracker.update({face({0.1, 0.0, 2.0})}, ns(0.1));
    require(tracker.persons().size() == 1 && tracker.persons().front().history.size() == 2, "failed updates must not consume timestamps or IDs");
}

void low_confidence_detections_do_not_refresh_tracks() {
    PersonTracker tracker;
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    auto weak = face({0.1, 0.0, 2.0});
    weak.confidence = 0.2;
    tracker.update({weak}, ns(0.1));
    require(tracker.persons().size() == 1 && !tracker.persons().front().observed, "below-threshold detection should not count as an observation");
    require(tracker.persons().front().history.size() == 1 && tracker.persons().front().last_seen_ns == ns(0.0), "weak detection must not refresh history or TTL");
}

void position_average_reduces_spikes_independently() {
    TrackerConfig config;
    config.acquisition_ns = 0;
    PersonTracker tracker(config);
    const std::array<double, 6> input = {0.0, 0.0, 0.15, 0.0, 0.0, 0.0};
    const std::array<double, 6> expected = {0.0, 0.0, 0.05, 0.05, 0.05, 0.0};
    tracker.update({face({0.0, 0.0, 2.0}), face({1.0, 1.0, 3.0}, 1)}, ns(0.0));
    const auto first = id_at_x(tracker, 0.0);
    const auto second = id_at_x(tracker, 1.0);
    same_position(track(tracker, first).filtered_position_m, {0.0, 0.0, 2.0}, "first sample should initialize immediately");
    for (std::size_t i = 1; i < input.size(); ++i) {
        tracker.update({face({1.0, 1.0, 3.0}, 1), face({input[i], -input[i], 2.0 + input[i]})}, ns(i * 0.1));
        require(tracker.persons().size() == 2, "smoothing must preserve both IDs");
        same_position(track(tracker, first).filtered_position_m, {expected[i], -expected[i], 2.0 + expected[i]}, "isolated spike should be averaged on all axes");
        same_position(track(tracker, first).observation.position_m, {input[i], -input[i], 2.0 + input[i]}, "raw position must remain available for matching");
        same_position(track(tracker, second).filtered_position_m, {1.0, 1.0, 3.0}, "another person's measurements must not enter the average");
        same_position(tracker.get_position(ns(i * 0.1)).position_m, track(tracker, first).filtered_position_m, "selected output should use the filtered position");
    }
    const auto before_loss = track(tracker, first);
    tracker.update({face({1.0, 1.0, 3.0}, 1)}, ns(0.6));
    tracker.expire(ns(0.8));
    same_position(track(tracker, first).filtered_position_m, before_loss.filtered_position_m, "missing observations must not change the average");
    require(track(tracker, first).history.size() == before_loss.history.size(), "missing frames must not append fake filter samples");
}

void position_average_does_not_change_matching() {
    TrackerConfig config;
    config.max_position_distance_m = 0.205;
    PersonTracker tracker(config);
    tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
    const auto id = tracker.persons().front().id;
    for (int i = 1; i < 10; ++i) {
        tracker.update({face({i * 0.2, 0.0, 2.0})}, ns(i * 0.1));
        require(tracker.persons().size() == 1 && tracker.persons().front().id == id, "filter lag must not reject a valid motion match");
    }
    near(track(tracker, id).filtered_position_m.x, 1.6, "filter should average the latest three measurements");
    near(track(tracker, id).observation.position_m.x, 1.8, "matching should retain the latest raw measurement");
}

void position_average_window_is_configurable() {
    for (const auto window : {std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        TrackerConfig config;
        config.position_average_samples = window;
        config.history_limit = 2;
        PersonTracker tracker(config);
        tracker.update({face({0.0, 0.0, 2.0})}, ns(0.0));
        tracker.update({face({0.1, 0.0, 2.0})}, ns(0.1));
        tracker.update({face({0.2, 0.0, 2.0})}, ns(0.2));
        near(tracker.persons().front().filtered_position_m.x, window == 1 ? 0.2 : 0.15, "window should use only retained real samples, with one disabling smoothing");
    }
    TrackerConfig invalid;
    invalid.position_average_samples = 0;
    rejects([&] { PersonTracker tracker(invalid); }, "zero-sample average must be rejected");
}

void out_of_order_times_do_not_corrupt_tracks() {
    PersonTracker tracker;
    tracker.update({face({0.0, 0.0, 2.0})}, ns(1.0));
    const auto original = tracker.persons();
    for (const auto time : {ns(0.9), ns(1.0), std::int64_t{-1}}) {
        rejects([&] { tracker.update({face({0.1, 0.0, 2.0})}, time); }, "non-increasing or negative frame time should be rejected");
        same_tracks(tracker.persons(), original);
    }
    tracker.expire(ns(1.2));
    const auto expired_state = tracker.persons();
    rejects([&] { tracker.update({face({0.1, 0.0, 2.0})}, ns(1.1)); }, "frame cannot precede the expiry clock");
    rejects([&] { tracker.expire(ns(1.19)); }, "expiry clock cannot move backward");
    same_tracks(tracker.persons(), expired_state);
    tracker.update({face({0.1, 0.0, 2.0})}, ns(1.3));
    require(tracker.persons().front().history.size() == 2, "valid updates must still work after rejected timestamps");
}

}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"independent people and reordered detections", independent_people_and_reordered_detections},
        {"full occlusion preserves motion and identity", full_occlusion_preserves_motion_and_identity},
        {"partial occlusion preserves independent updates", partial_occlusion_preserves_independent_updates},
        {"motion disambiguates crossing people after occlusion", motion_disambiguates_crossing_people_after_occlusion},
        {"landmark signature ignores translation and scale", landmark_signature_is_translation_and_scale_invariant},
        {"geometry disambiguates co-located faces", geometry_disambiguates_colocated_faces},
        {"association is one-to-one", association_is_one_to_one},
        {"per-person TTL runs without camera frames", per_person_ttl_runs_without_camera_frames},
        {"returning after TTL creates fresh identity", returning_after_ttl_creates_fresh_identity},
        {"acquisition requires continuous dwell", acquisition_requires_continuous_dwell},
        {"cooldown reserves active viewer then selects eligible person", cooldown_reserves_active_viewer_then_selects_eligible_person},
        {"reserved viewer reacquires without another dwell", reacquiring_reserved_viewer_does_not_restart_dwell},
        {"prediction tapers and does not refresh observations", prediction_tapers_to_a_bounded_endpoint_without_refreshing_observations},
        {"history limit applies independently", history_limit_applies_independently},
        {"expiry holds the last tapered position", expiry_holds_the_last_tapered_position},
        {"invalid observations are rejected transactionally", invalid_observations_are_rejected_transactionally},
        {"low confidence does not refresh tracks", low_confidence_detections_do_not_refresh_tracks},
        {"position average reduces spikes independently", position_average_reduces_spikes_independently},
        {"position average does not change matching", position_average_does_not_change_matching},
        {"position average window is configurable", position_average_window_is_configurable},
        {"out-of-order times do not corrupt tracks", out_of_order_times_do_not_corrupt_tracks},
    };
    std::size_t failed = 0;
    for (const auto& entry : tests) {
        try {
            entry.second();
            std::cout << "PASS " << entry.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << entry.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
