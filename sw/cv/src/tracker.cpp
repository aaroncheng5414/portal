#include "portal/tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace portal {
namespace {

bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double bounded(long double value) {
    const auto limit = static_cast<long double>(std::numeric_limits<double>::max());
    return static_cast<double>(std::clamp(value, -limit, limit));
}

Vec3 scaled(Vec3 value, long double scale) {
    return {bounded(value.x * scale), bounded(value.y * scale), bounded(value.z * scale)};
}

Vec3 displaced(Vec3 position, Vec3 velocity, long double seconds) {
    return {
        bounded(static_cast<long double>(position.x) + velocity.x * seconds),
        bounded(static_cast<long double>(position.y) + velocity.y * seconds),
        bounded(static_cast<long double>(position.z) + velocity.z * seconds),
    };
}

long double distance(Vec3 a, Vec3 b) {
    return std::hypot(static_cast<long double>(a.x) - b.x,
                      static_cast<long double>(a.y) - b.y,
                      static_cast<long double>(a.z) - b.z);
}

double signature_distance(const LandmarkSignature& a, const LandmarkSignature& b) {
    long double squared = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto difference = static_cast<long double>(a[i]) - b[i];
        squared += difference * difference;
    }
    return bounded(std::sqrt(squared / a.size()));
}

long double overlap(Rect a, Rect b) {
    const auto left = std::max(static_cast<long double>(a.x), static_cast<long double>(b.x));
    const auto top = std::max(static_cast<long double>(a.y), static_cast<long double>(b.y));
    const auto right = std::min(static_cast<long double>(a.x) + a.width,
                                static_cast<long double>(b.x) + b.width);
    const auto bottom = std::min(static_cast<long double>(a.y) + a.height,
                                 static_cast<long double>(b.y) + b.height);
    const auto intersection = std::max(0.0L, right - left) * std::max(0.0L, bottom - top);
    const auto total = static_cast<long double>(a.width) * a.height +
                       static_cast<long double>(b.width) * b.height - intersection;
    return total > 0.0L ? intersection / total : 0.0L;
}

void validate(const TrackerConfig& config) {
    if (config.track_timeout_ns <= 0 || config.switch_cooldown_ns < 0 ||
        config.max_prediction_ns < 0 || config.acquisition_ns < 0 || config.history_limit == 0 ||
        !std::isfinite(config.min_confidence) || config.min_confidence < 0.0 || config.min_confidence > 1.0 ||
        !std::isfinite(config.max_position_distance_m) || config.max_position_distance_m <= 0.0 ||
        !std::isfinite(config.max_signature_distance) || config.max_signature_distance <= 0.0) {
        throw std::invalid_argument("Invalid tracker configuration.");
    }
}

void validate(const FaceObservation& observation) {
    const auto& box = observation.box;
    if (!finite(observation.position_m) || !std::isfinite(observation.confidence) ||
        observation.confidence < 0.0 || observation.confidence > 1.0 ||
        !std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
        !std::isfinite(box.height) || box.width <= 0.0 || box.height <= 0.0) {
        throw std::invalid_argument("Invalid face observation.");
    }
}

PositionSnapshot raw_snapshot(const PersonTrack& person, TrackingState state) {
    PositionSnapshot result;
    result.person_id = person.id;
    result.position_m = person.observation.position_m;
    result.velocity_mps = person.velocity_mps;
    result.observation_timestamp_ns = person.last_seen_ns;
    result.state = state;
    result.stale = false;
    return result;
}

PositionSnapshot predict(PositionSnapshot result, std::int64_t now_ns, const TrackerConfig& config) {
    result.age_ns = now_ns - result.observation_timestamp_ns;
    const bool expired = result.age_ns >= config.track_timeout_ns;
    const bool horizon_reached = config.max_prediction_ns == 0
        ? result.age_ns > 0 : result.age_ns >= config.max_prediction_ns;
    if (!result.stale) {
        const auto elapsed_ns = std::min({result.age_ns, config.max_prediction_ns, config.track_timeout_ns});
        const auto elapsed = static_cast<long double>(elapsed_ns) / 1'000'000'000.0L;
        const auto horizon = static_cast<long double>(config.max_prediction_ns) / 1'000'000'000.0L;
        if (horizon > 0.0L) {
            result.position_m = displaced(result.position_m, result.velocity_mps,
                                           elapsed - elapsed * elapsed / (2.0L * horizon));
            result.velocity_mps = scaled(result.velocity_mps, 1.0L - elapsed / horizon);
        }
        result.predicted = result.age_ns > 0 && !horizon_reached && !expired;
    } else {
        result.predicted = false;
    }
    result.stale = result.stale || expired;
    if (result.stale || horizon_reached || config.max_prediction_ns == 0) result.velocity_mps = {};
    if (expired) result.state = TrackingState::Lost;
    return result;
}

Vec3 velocity(const std::vector<TrackSample>& history) {
    if (history.size() < 2) return {};
    const auto& origin = history.front();
    long double sum_t = 0.0L;
    long double sum_tt = 0.0L;
    std::array<long double, 3> sum_p{};
    std::array<long double, 3> sum_tp{};
    for (const auto& sample : history) {
        const auto time = static_cast<long double>(sample.timestamp_ns - origin.timestamp_ns) / 1'000'000'000.0L;
        const std::array<long double, 3> point = {
            static_cast<long double>(sample.position_m.x) - origin.position_m.x,
            static_cast<long double>(sample.position_m.y) - origin.position_m.y,
            static_cast<long double>(sample.position_m.z) - origin.position_m.z,
        };
        sum_t += time;
        sum_tt += time * time;
        for (std::size_t axis = 0; axis < point.size(); ++axis) {
            sum_p[axis] += point[axis];
            sum_tp[axis] += time * point[axis];
        }
    }
    const auto count = static_cast<long double>(history.size());
    const auto denominator = count * sum_tt - sum_t * sum_t;
    if (denominator <= 0.0L) return {};
    return {
        bounded((count * sum_tp[0] - sum_t * sum_p[0]) / denominator),
        bounded((count * sum_tp[1] - sum_t * sum_p[1]) / denominator),
        bounded((count * sum_tp[2] - sum_t * sum_p[2]) / denominator),
    };
}

struct Candidate {
    FaceObservation observation;
    LandmarkSignature signature;
};

bool candidate_less(const Candidate& a, const Candidate& b) {
    const auto& ap = a.observation.position_m;
    const auto& bp = b.observation.position_m;
    const auto& ab = a.observation.box;
    const auto& bb = b.observation.box;
    const auto a_key = std::tie(ap.x, ap.y, ap.z, ab.x, ab.y, ab.width, ab.height, a.observation.confidence);
    const auto b_key = std::tie(bp.x, bp.y, bp.z, bb.x, bb.y, bb.width, bb.height, b.observation.confidence);
    if (a_key != b_key) return a_key < b_key;
    if (a.signature != b.signature) return a.signature < b.signature;
    for (std::size_t i = 0; i < a.observation.landmarks.size(); ++i) {
        const auto& al = a.observation.landmarks[i];
        const auto& bl = b.observation.landmarks[i];
        if (std::tie(al.x, al.y) != std::tie(bl.x, bl.y)) return std::tie(al.x, al.y) < std::tie(bl.x, bl.y);
    }
    return false;
}

std::vector<std::size_t> assignment(const std::vector<std::vector<double>>& costs) {
    const auto count = costs.size();
    std::vector<double> row_potential(count + 1), column_potential(count + 1);
    std::vector<std::size_t> row_for_column(count + 1), previous_column(count + 1);
    for (std::size_t row = 1; row <= count; ++row) {
        row_for_column[0] = row;
        std::size_t column = 0;
        std::vector<double> minimum(count + 1, std::numeric_limits<double>::infinity());
        std::vector<bool> used(count + 1, false);
        do {
            used[column] = true;
            const auto current_row = row_for_column[column];
            auto delta = std::numeric_limits<double>::infinity();
            std::size_t next_column = 0;
            for (std::size_t next = 1; next <= count; ++next) {
                if (used[next]) continue;
                const auto cost = costs[current_row - 1][next - 1] -
                                  row_potential[current_row] - column_potential[next];
                if (cost < minimum[next]) {
                    minimum[next] = cost;
                    previous_column[next] = column;
                }
                if (minimum[next] < delta) {
                    delta = minimum[next];
                    next_column = next;
                }
            }
            for (std::size_t next = 0; next <= count; ++next) {
                if (used[next]) {
                    row_potential[row_for_column[next]] += delta;
                    column_potential[next] -= delta;
                } else {
                    minimum[next] -= delta;
                }
            }
            column = next_column;
        } while (row_for_column[column] != 0);
        do {
            const auto previous = previous_column[column];
            row_for_column[column] = row_for_column[previous];
            column = previous;
        } while (column != 0);
    }
    std::vector<std::size_t> result(count, count);
    for (std::size_t column = 1; column <= count; ++column) {
        result[row_for_column[column] - 1] = column - 1;
    }
    return result;
}

const PersonTrack* find_person(const std::vector<PersonTrack>& people, std::uint64_t id) {
    const auto found = std::find_if(people.begin(), people.end(), [id](const auto& person) { return person.id == id; });
    return found == people.end() ? nullptr : &*found;
}

bool larger(const PersonTrack& candidate, const PersonTrack* selected) {
    if (!selected) return true;
    const auto& a = candidate.observation.box;
    const auto& b = selected->observation.box;
    const auto a_area = static_cast<long double>(a.width) * a.height;
    const auto b_area = static_cast<long double>(b.width) * b.height;
    return a_area > b_area || (a_area == b_area && candidate.id < selected->id);
}

}

LandmarkSignature landmark_signature(const std::array<Vec2, 5>& landmarks) {
    for (const auto& point : landmarks) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            throw std::invalid_argument("Landmarks must be finite.");
        }
    }
    const auto separation = std::hypot(static_cast<long double>(landmarks[0].x) - landmarks[1].x,
                                       static_cast<long double>(landmarks[0].y) - landmarks[1].y);
    if (separation <= 0.0L) throw std::invalid_argument("Eye landmarks must have distinct positions.");
    LandmarkSignature result{};
    std::size_t index = 0;
    for (std::size_t i = 0; i < landmarks.size(); ++i) {
        for (std::size_t j = i + 1; j < landmarks.size(); ++j) {
            const auto value = std::hypot(static_cast<long double>(landmarks[i].x) - landmarks[j].x,
                                          static_cast<long double>(landmarks[i].y) - landmarks[j].y) / separation;
            if (value > std::numeric_limits<double>::max()) {
                throw std::invalid_argument("Landmark ratios exceed the supported range.");
            }
            result[index++] = static_cast<double>(value);
        }
    }
    return result;
}

PersonTracker::PersonTracker(TrackerConfig config) : config_(config) {
    validate(config_);
}

void PersonTracker::update(const std::vector<FaceObservation>& observations, std::int64_t capture_time_ns) {
    if (capture_time_ns < 0 || (last_capture_time_ns_ && capture_time_ns <= *last_capture_time_ns_) ||
        (last_mutation_time_ns_ && capture_time_ns < *last_mutation_time_ns_)) {
        throw std::invalid_argument("Frame timestamps must increase and cannot precede expiry.");
    }
    std::vector<Candidate> candidates;
    candidates.reserve(observations.size());
    for (const auto& observation : observations) {
        validate(observation);
        auto signature = landmark_signature(observation.landmarks);
        if (observation.confidence >= config_.min_confidence) candidates.push_back({observation, signature});
    }
    std::sort(candidates.begin(), candidates.end(), candidate_less);
    if (candidates.size() > std::numeric_limits<std::uint64_t>::max() - next_id_) {
        throw std::overflow_error("Person identifiers exhausted.");
    }
    auto next = *this;
    next.expire_tracks(capture_time_ns);
    const auto track_count = next.persons_.size();
    const auto candidate_count = candidates.size();
    if (candidate_count > std::numeric_limits<std::size_t>::max() - track_count) {
        throw std::length_error("Too many observations.");
    }
    const auto count = track_count + candidate_count;
    constexpr double impossible = 1'000'000.0;
    constexpr auto total_match_weight = TrackerConfig::position_match_weight +
        TrackerConfig::signature_match_weight + TrackerConfig::overlap_match_weight;
    static_assert(TrackerConfig::position_match_weight >= 0.0L &&
        TrackerConfig::signature_match_weight >= 0.0L && TrackerConfig::overlap_match_weight >= 0.0L &&
        total_match_weight > 0.0L && total_match_weight < impossible,
        "Match weights must be nonnegative with a positive total below the forbidden cost.");
    static_assert(TrackerConfig::unmatched_cost > 0.0 && TrackerConfig::unmatched_cost < impossible / 2.0,
        "Unmatched cost must be positive and less than half the forbidden cost.");
    std::vector<std::vector<double>> costs(count, std::vector<double>(count, 0.0));
    for (std::size_t row = 0; row < track_count; ++row) {
        const auto& person = next.persons_[row];
        const auto expected = predict(raw_snapshot(person, TrackingState::Locked), capture_time_ns, config_).position_m;
        for (std::size_t column = 0; column < candidate_count; ++column) {
            const auto& candidate = candidates[column];
            const auto position_error = distance(expected, candidate.observation.position_m);
            const auto feature_error = signature_distance(person.signature, candidate.signature);
            costs[row][column] = position_error > config_.max_position_distance_m ||
                feature_error > config_.max_signature_distance ? impossible :
                static_cast<double>(TrackerConfig::position_match_weight * position_error / config_.max_position_distance_m +
                TrackerConfig::signature_match_weight * feature_error / config_.max_signature_distance +
                TrackerConfig::overlap_match_weight * (1.0L - overlap(person.observation.box, candidate.observation.box)));
        }
        for (std::size_t column = candidate_count; column < count; ++column) costs[row][column] = TrackerConfig::unmatched_cost;
    }
    for (std::size_t row = track_count; row < count; ++row) {
        for (std::size_t column = 0; column < candidate_count; ++column) costs[row][column] = TrackerConfig::unmatched_cost;
    }
    const auto matches = assignment(costs);
    std::vector<bool> claimed(candidate_count, false);
    for (std::size_t row = 0; row < track_count; ++row) {
        auto& person = next.persons_[row];
        const auto column = matches[row];
        if (column >= candidate_count || costs[row][column] >= impossible) {
            person.observed = false;
            continue;
        }
        const auto& candidate = candidates[column];
        claimed[column] = true;
        if (!person.observed) person.visible_since_ns = capture_time_ns;
        person.observed = true;
        person.observation = candidate.observation;
        person.signature = candidate.signature;
        person.last_seen_ns = capture_time_ns;
        person.history.push_back({capture_time_ns, candidate.observation.position_m});
        if (person.history.size() > config_.history_limit) person.history.erase(person.history.begin());
        person.velocity_mps = velocity(person.history);
    }
    for (std::size_t column = 0; column < candidate_count; ++column) {
        if (claimed[column]) continue;
        const auto& candidate = candidates[column];
        PersonTrack person;
        person.id = next.next_id_++;
        person.observation = candidate.observation;
        person.signature = candidate.signature;
        person.history.push_back({capture_time_ns, candidate.observation.position_m});
        person.first_seen_ns = person.last_seen_ns = person.visible_since_ns = capture_time_ns;
        person.observed = true;
        next.persons_.push_back(std::move(person));
    }
    next.last_capture_time_ns_ = next.last_mutation_time_ns_ = capture_time_ns;
    next.select_active(capture_time_ns);
    *this = std::move(next);
}

void PersonTracker::expire_tracks(std::int64_t now_ns) {
    persons_.erase(std::remove_if(persons_.begin(), persons_.end(), [&](const auto& person) {
        if (now_ns - person.last_seen_ns < config_.track_timeout_ns) return false;
        if (last_selected_ && last_selected_->person_id == person.id) {
            last_selected_ = predict(raw_snapshot(person, TrackingState::Lost), now_ns, config_);
        }
        return true;
    }), persons_.end());
}

void PersonTracker::select_active(std::int64_t now_ns) {
    if (active_person_id_) {
        const auto* active = find_person(persons_, *active_person_id_);
        if (active && active->observed) {
            last_selected_ = raw_snapshot(*active, TrackingState::Locked);
            return;
        }
        if (last_selected_ && now_ns - last_selected_->observation_timestamp_ns < config_.switch_cooldown_ns) return;
        active_person_id_.reset();
    }
    const PersonTrack* selected = nullptr;
    for (const auto& person : persons_) {
        const bool returning = last_selected_ && last_selected_->person_id == person.id;
        if (person.observed && (returning || now_ns - person.visible_since_ns >= config_.acquisition_ns) &&
            larger(person, selected)) selected = &person;
    }
    if (selected) {
        active_person_id_ = selected->id;
        last_selected_ = raw_snapshot(*selected, TrackingState::Locked);
    }
}

void PersonTracker::expire(std::int64_t now_ns) {
    if (now_ns < 0 || (last_mutation_time_ns_ && now_ns < *last_mutation_time_ns_)) {
        throw std::invalid_argument("Expiry cannot move backwards in time.");
    }
    expire_tracks(now_ns);
    if (active_person_id_ && last_selected_ && now_ns > last_selected_->observation_timestamp_ns &&
        now_ns - last_selected_->observation_timestamp_ns >= config_.switch_cooldown_ns) {
        active_person_id_.reset();
    }
    last_mutation_time_ns_ = now_ns;
}

const std::vector<PersonTrack>& PersonTracker::persons() const noexcept {
    return persons_;
}

std::optional<std::uint64_t> PersonTracker::active_person_id() const noexcept {
    return active_person_id_;
}

PositionSnapshot PersonTracker::get_position(std::int64_t now_ns) const {
    if (now_ns < 0 || (last_mutation_time_ns_ && now_ns < *last_mutation_time_ns_)) {
        throw std::invalid_argument("Queries cannot precede the current tracker state.");
    }
    if (active_person_id_) {
        if (const auto* person = find_person(persons_, *active_person_id_)) {
            return predict(raw_snapshot(*person, person->observed ? TrackingState::Locked : TrackingState::Lost), now_ns, config_);
        }
    }
    if (last_selected_) {
        auto result = *last_selected_;
        result.state = TrackingState::Lost;
        return predict(result, now_ns, config_);
    }
    const PersonTrack* candidate = nullptr;
    for (const auto& person : persons_) {
        if (person.observed && now_ns - person.last_seen_ns < config_.track_timeout_ns && larger(person, candidate)) {
            candidate = &person;
        }
    }
    return candidate ? predict(raw_snapshot(*candidate, TrackingState::Acquiring), now_ns, config_) : PositionSnapshot{};
}

}
