#include "portal/config.h"
#include "portal/tracker.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/cuda.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
using portal::FaceObservation;
using portal::PersonTracker;
using portal::ServiceConfig;
using portal::TrackingState;
using portal::Vec3;
volatile std::sig_atomic_t interrupted = 0;

void interrupt_handler(int) { interrupted = 1; }

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

double milliseconds(std::int64_t duration) { return static_cast<double>(duration) / 1'000'000.0; }

double focal_length(const ServiceConfig& config, int width) {
    const auto radians = config.horizontal_fov_deg * std::acos(-1.0) / 180.0;
    return config.focal_length_px > 0.0 ? config.focal_length_px : width / (2.0 * std::tan(radians / 2.0));
}

Vec3 eye_position(const std::array<portal::Vec2, 5>& points, int width, int height,
                  double focal, double ipd) {
    const auto separation = std::hypot(points[0].x - points[1].x, points[0].y - points[1].y);
    if (!std::isfinite(separation) || separation <= 0.0) throw std::invalid_argument("Invalid eye separation.");
    const auto depth = focal * ipd / separation;
    const auto eye_x = (points[0].x + points[1].x) / 2.0;
    const auto eye_y = (points[0].y + points[1].y) / 2.0;
    return {(eye_x - width / 2.0) * depth / focal, (height / 2.0 - eye_y) * depth / focal, depth};
}

class FrameRate {
public:
    explicit FrameRate(std::size_t limit) : limit_(limit) {}
    double add(std::int64_t timestamp) {
        samples_.push_back(timestamp);
        while (samples_.size() > limit_) samples_.pop_front();
        if (samples_.size() < 2 || samples_.back() <= samples_.front()) return 0.0;
        return static_cast<double>(samples_.size() - 1) * 1'000'000'000.0 /
               static_cast<double>(samples_.back() - samples_.front());
    }
private:
    std::size_t limit_;
    std::deque<std::int64_t> samples_;
};

struct SourceState {
    std::optional<std::string> device;
    std::string status = "starting";
    std::optional<std::string> error;
    int width = 0;
    int height = 0;
    double capture_fps = 0.0;
    std::optional<std::int64_t> received_ns;
};

struct Frame {
    cv::Mat image;
    std::vector<FaceObservation> synthetic_faces;
    std::int64_t received_ns = 0;
};

struct CaptureSlot {
    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Frame> latest;
    SourceState source;
};

struct Snapshot {
    explicit Snapshot(const portal::TrackerConfig& config) : tracker(config) {}
    PersonTracker tracker;
    SourceState source;
    std::uint64_t sequence = 0;
    std::uint64_t frame_sequence = 0;
    std::optional<std::int64_t> frame_received_ns;
    int frame_width = 0;
    int frame_height = 0;
    std::optional<double> inference_ms;
    double inference_fps = 0.0;
    double focal_length_px = 0.0;
    std::shared_ptr<const std::vector<unsigned char>> jpeg;
};

class PublishedState {
public:
    explicit PublishedState(const portal::TrackerConfig& config)
        : latest_(std::make_shared<const Snapshot>(config)) {}
    void publish(const Snapshot& state) {
        auto next = std::make_shared<const Snapshot>(state);
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(next);
    }
    std::shared_ptr<const Snapshot> read() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> latest_;
};

SourceState source_at(const Snapshot& state, const ServiceConfig& config, std::int64_t timestamp) {
    auto source = state.source;
    source.width = state.frame_width;
    source.height = state.frame_height;
    if (source.status == "running" && state.frame_received_ns &&
        timestamp - *state.frame_received_ns >= static_cast<std::int64_t>(config.camera_stale_ms) * 1'000'000) {
        source.status = "lost";
        source.error = "No fresh processed frames available.";
        source.capture_fps = 0.0;
    }
    return source;
}

long long integer_argument(const std::string& text, const std::string& name) {
    std::size_t used = 0;
    const auto value = std::stoll(text, &used);
    if (used != text.size()) throw std::invalid_argument("Invalid " + name + ".");
    return value;
}

double number_argument(const std::string& text, const std::string& name) {
    std::size_t used = 0;
    const auto value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::invalid_argument("Invalid " + name + ".");
    return value;
}

int bounded_integer(const std::string& text, const std::string& name, int minimum, int maximum) {
    const auto value = integer_argument(text, name);
    if (value < minimum || value > maximum) throw std::invalid_argument(name + " is out of range.");
    return static_cast<int>(value);
}

void help() {
    std::cout << "Usage: portal_cv [options]\n"
        "  --synthetic                 Generated video and observations; no hardware or model\n"
        "  --backend cuda|cpu          Default cuda; CPU must be selected explicitly\n"
        "  --target fp32|fp16          CUDA precision, default fp32\n"
        "  --device auto|N|/dev/videoN  First readable camera by default\n"
        "  --host HOST --port PORT     Default 127.0.0.1:8080\n"
        "  --model PATH                Default models/face_detection_yunet_2023mar.onnx\n"
        "  --schema PATH               Default api/openapi.json\n"
        "  --width N --height N        Requested camera size, default 1280x720\n"
        "  --fps N                     Requested camera rate, default 30\n"
        "  --ipd-m N                   Assumed physical eye spacing, default 0.063 m\n"
        "  --focal-length-px N         Calibrated focal length; zero uses FOV estimate\n"
        "  --horizontal-fov-deg N      Fallback horizontal FOV, default 60 degrees\n"
        "  --help                      Show this message\n"
        "Timestamps use a monotonic clock when frames reach this process, not hardware exposure time.\n";
}

ServiceConfig arguments(int argc, char** argv) {
    ServiceConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--synthetic") { config.synthetic = true; continue; }
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + option + ".");
        const std::string value = argv[++i];
        if (option == "--backend") config.backend = value;
        else if (option == "--target") config.target = value;
        else if (option == "--device") config.device = value;
        else if (option == "--host") config.host = value;
        else if (option == "--port") config.port = bounded_integer(value, option, 1, 65535);
        else if (option == "--model") config.model_path = value;
        else if (option == "--schema") config.schema_path = value;
        else if (option == "--width") config.width = bounded_integer(value, option, 32, 8192);
        else if (option == "--height") config.height = bounded_integer(value, option, 32, 8192);
        else if (option == "--fps") config.requested_fps = number_argument(value, option);
        else if (option == "--ipd-m") config.ipd_m = number_argument(value, option);
        else if (option == "--focal-length-px") config.focal_length_px = number_argument(value, option);
        else if (option == "--horizontal-fov-deg") config.horizontal_fov_deg = number_argument(value, option);
        else throw std::invalid_argument("Unknown option: " + option);
    }
    if (config.backend != "cuda" && config.backend != "cpu") throw std::invalid_argument("Backend must be cuda or cpu.");
    if (config.target != "fp32" && config.target != "fp16") throw std::invalid_argument("Target must be fp32 or fp16.");
    if (config.backend == "cpu" && config.target != "fp32") throw std::invalid_argument("CPU mode supports fp32 only.");
    if (config.host.empty() || config.requested_fps <= 0.0 || config.requested_fps > 240.0 ||
        config.ipd_m <= 0.0 || config.ipd_m > 0.2 || config.focal_length_px < 0.0 ||
        config.horizontal_fov_deg <= 0.0 || config.horizontal_fov_deg >= 180.0) {
        throw std::invalid_argument("Invalid camera or coordinate configuration.");
    }
    if (config.device != "auto" && !std::regex_match(config.device, std::regex("([0-9]+|/dev/video[0-9]+)"))) {
        throw std::invalid_argument("Device must be auto, a nonnegative index, or /dev/videoN.");
    }
    if (std::regex_match(config.device, std::regex("[0-9]+"))) {
        bounded_integer(config.device, "device", 0, std::numeric_limits<int>::max());
    }
    return config;
}

cv::Ptr<cv::FaceDetectorYN> detector(const ServiceConfig& config) {
    if (config.synthetic) return {};
    int backend = cv::dnn::DNN_BACKEND_OPENCV;
    int target = cv::dnn::DNN_TARGET_CPU;
    if (config.backend == "cuda") {
        if (cv::cuda::getCudaEnabledDeviceCount() <= 0) {
            throw std::runtime_error("CUDA requested but no usable CUDA device/OpenCV CUDA build was found. Build OpenCV with CUDA and cuDNN, or explicitly use --backend cpu.");
        }
        backend = cv::dnn::DNN_BACKEND_CUDA;
        target = config.target == "fp16" ? cv::dnn::DNN_TARGET_CUDA_FP16 : cv::dnn::DNN_TARGET_CUDA;
        const auto targets = cv::dnn::getAvailableTargets(cv::dnn::DNN_BACKEND_CUDA);
        if (std::find(targets.begin(), targets.end(), target) == targets.end()) {
            throw std::runtime_error("Requested CUDA DNN target is unavailable. No CPU fallback will be used.");
        }
    }
    if (!std::filesystem::is_regular_file(config.model_path)) throw std::runtime_error("YuNet model not found: " + config.model_path);
    auto result = cv::FaceDetectorYN::create(config.model_path, "", cv::Size(config.width, config.height),
        config.detector_score_threshold, config.detector_nms_threshold, config.detector_top_k, backend, target);
    cv::Mat faces;
    result->detect(cv::Mat::zeros(config.height, config.width, CV_8UC3), faces);
    return result;
}

std::vector<std::string> camera_candidates(const ServiceConfig& config) {
    if (config.device != "auto") return {config.device};
    std::vector<std::pair<unsigned long long, std::string>> devices;
#if defined(__linux__)
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
        std::smatch match;
        const auto name = entry.path().filename().string();
        if (std::regex_match(name, match, std::regex("video([0-9]+)"))) {
            devices.emplace_back(std::stoull(match[1].str()), entry.path().string());
        }
    }
#else
    for (int index = 0; index < config.camera_index_limit; ++index) devices.emplace_back(index, std::to_string(index));
#endif
    std::sort(devices.begin(), devices.end());
    std::vector<std::string> result;
    for (const auto& device : devices) result.push_back(device.second);
    return result;
}

bool open_camera(cv::VideoCapture& camera, cv::Mat& image, std::string& selected,
                 const ServiceConfig& config, const std::atomic<bool>& stop) {
    for (const auto& candidate : camera_candidates(config)) {
        if (stop.load()) return false;
        try {
#if defined(__linux__)
            constexpr int api = cv::CAP_V4L2;
#else
            constexpr int api = cv::CAP_ANY;
#endif
            const auto opened = candidate.rfind("/dev/video", 0) == 0
                ? camera.open(candidate, api) : camera.open(std::stoi(candidate), api);
            if (!opened) continue;
            if (config.request_mjpeg) camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            camera.set(cv::CAP_PROP_FRAME_WIDTH, config.width);
            camera.set(cv::CAP_PROP_FRAME_HEIGHT, config.height);
            camera.set(cv::CAP_PROP_FPS, config.requested_fps);
            camera.set(cv::CAP_PROP_BUFFERSIZE, 1);
            if (camera.read(image) && !image.empty()) { selected = candidate; return true; }
        } catch (const cv::Exception&) {}
        camera.release();
    }
    return false;
}

FaceObservation synthetic_face(const ServiceConfig& config, double time, int index) {
    const auto focal = focal_length(config, config.width);
    const Vec3 position = index == 0
        ? Vec3{0.24 * std::sin(time * 0.75), 0.07 * std::sin(time * 0.55), 1.0 + 0.12 * std::sin(time * 0.4)}
        : Vec3{-0.4 + 0.08 * std::cos(time * 0.45), 0.08, 1.6};
    const auto separation = focal * config.ipd_m / position.z;
    const auto width = separation / 0.44;
    const auto eye_x = config.width / 2.0 + position.x * focal / position.z;
    const auto eye_y = config.height / 2.0 - position.y * focal / position.z;
    FaceObservation face;
    face.box = {eye_x - width / 2.0, eye_y - width * 0.32, width, width * 1.2};
    face.landmarks = {{{eye_x - separation / 2.0, eye_y}, {eye_x + separation / 2.0, eye_y},
        {eye_x, eye_y + width * (index ? 0.26 : 0.20)},
        {eye_x - width * 0.17, eye_y + width * 0.43}, {eye_x + width * 0.17, eye_y + width * 0.43}}};
    face.position_m = eye_position(face.landmarks, config.width, config.height, focal, config.ipd_m);
    face.confidence = 0.99;
    return face;
}

void source_error(CaptureSlot& slot, const std::string& status, const std::string& message) {
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.source.status = status;
    slot.source.error = message;
    slot.source.capture_fps = 0.0;
    slot.ready.notify_all();
}

void capture_loop(const ServiceConfig& config, CaptureSlot& slot, std::atomic<bool>& stop) {
    cv::VideoCapture camera;
    FrameRate rate(config.fps_window_frames);
    std::int64_t last_received = -1;
    const auto started = Clock::now();
    auto synthetic_deadline = started;
    std::string selected;
    while (!stop.load()) {
        try {
            Frame frame;
            if (config.synthetic) {
                const auto time = std::chrono::duration<double>(Clock::now() - started).count();
                frame.image = cv::Mat(config.height, config.width, CV_8UC3, cv::Scalar(29, 24, 19));
                if (std::fmod(time, 12.0) < 8.0 || std::fmod(time, 12.0) > 9.4) {
                    frame.synthetic_faces.push_back(synthetic_face(config, time, 0));
                }
                frame.synthetic_faces.push_back(synthetic_face(config, time, 1));
                cv::putText(frame.image, "SYNTHETIC INPUT - NO CAMERA", cv::Point(24, 36),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(185, 190, 195), 1, cv::LINE_AA);
            } else if (!camera.isOpened()) {
                if (!open_camera(camera, frame.image, selected, config, stop)) {
                    source_error(slot, "error", "No requested camera could be opened and read. Retrying.");
                    std::unique_lock<std::mutex> lock(slot.mutex);
                    slot.ready.wait_for(lock, std::chrono::milliseconds(config.camera_retry_ms), [&] { return stop.load(); });
                    continue;
                }
                rate = FrameRate(config.fps_window_frames);
            } else if (!camera.read(frame.image) || frame.image.empty()) {
                camera.release();
                source_error(slot, "lost", "Camera stopped delivering frames. Reconnecting.");
                continue;
            }
            frame.received_ns = now_ns();
            if (frame.received_ns <= last_received) continue;
            last_received = frame.received_ns;
            {
                std::lock_guard<std::mutex> lock(slot.mutex);
                slot.source.device = config.synthetic ? std::nullopt : std::optional<std::string>(selected);
                slot.source.status = "running";
                slot.source.error.reset();
                slot.source.width = frame.image.cols;
                slot.source.height = frame.image.rows;
                slot.source.received_ns = frame.received_ns;
                slot.source.capture_fps = rate.add(frame.received_ns);
                slot.latest = std::move(frame);
            }
            slot.ready.notify_one();
            if (config.synthetic) {
                synthetic_deadline += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / config.requested_fps));
                if (synthetic_deadline < Clock::now()) synthetic_deadline = Clock::now();
                std::unique_lock<std::mutex> lock(slot.mutex);
                slot.ready.wait_until(lock, synthetic_deadline, [&] { return stop.load(); });
            }
        } catch (const std::exception& error) {
            camera.release();
            source_error(slot, "error", error.what());
            std::unique_lock<std::mutex> lock(slot.mutex);
            slot.ready.wait_for(lock, std::chrono::milliseconds(config.camera_retry_ms), [&] { return stop.load(); });
        }
    }
    camera.release();
}

std::vector<FaceObservation> observations(const cv::Mat& faces, const cv::Size& size, const ServiceConfig& config) {
    std::vector<FaceObservation> result;
    const auto focal = focal_length(config, size.width);
    for (int row = 0; row < faces.rows; ++row) {
        if (faces.cols < 15 || faces.type() != CV_32FC1) throw std::runtime_error("Unexpected YuNet output shape.");
        const auto* values = faces.ptr<float>(row);
        bool valid = true;
        for (int index = 0; index < 15; ++index) valid = valid && std::isfinite(values[index]);
        if (!valid || values[2] <= 0.0F || values[3] <= 0.0F || values[14] < config.tracker.min_confidence || values[14] > 1.0F) continue;
        FaceObservation face;
        face.box = {values[0], values[1], values[2], values[3]};
        for (std::size_t index = 0; index < face.landmarks.size(); ++index) {
            face.landmarks[index] = {values[4 + index * 2], values[5 + index * 2]};
        }
        try {
            face.position_m = eye_position(face.landmarks, size.width, size.height, focal, config.ipd_m);
            face.confidence = values[14];
            if (std::isfinite(face.position_m.x) && std::isfinite(face.position_m.y) && std::isfinite(face.position_m.z)) result.push_back(face);
        } catch (const std::invalid_argument&) {}
    }
    return result;
}

void annotate(cv::Mat& image, const PersonTracker& tracker) {
    const auto pixel = [&](double value) { return static_cast<int>(std::clamp(value, -8192.0, 16384.0)); };
    for (const auto& person : tracker.persons()) {
        if (!person.observed) continue;
        const auto& box = person.observation.box;
        const auto color = tracker.active_person_id() == person.id ? cv::Scalar(125, 220, 100) : cv::Scalar(220, 170, 95);
        cv::rectangle(image, cv::Point(pixel(box.x), pixel(box.y)), cv::Point(pixel(box.x + box.width), pixel(box.y + box.height)), color, 2);
        for (const auto& point : person.observation.landmarks) cv::circle(image, cv::Point(pixel(point.x), pixel(point.y)), 3, color, -1);
        cv::putText(image, "Person " + std::to_string(person.id), cv::Point(pixel(box.x), pixel(box.y - 8)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 1, cv::LINE_AA);
    }
}

void processing_loop(const ServiceConfig& config, cv::Ptr<cv::FaceDetectorYN> model,
                     CaptureSlot& slot, PublishedState& published, std::atomic<bool>& stop) {
    Snapshot state(config.tracker);
    FrameRate inference_rate(config.fps_window_frames);
    std::int64_t watermark = -1;
    bool marked_lost = false;
    std::optional<std::string> processing_error;
    while (!stop.load()) {
        std::optional<Frame> frame;
        SourceState source;
        {
            std::unique_lock<std::mutex> lock(slot.mutex);
            slot.ready.wait_for(lock, std::chrono::milliseconds(config.worker_tick_ms), [&] { return stop.load() || slot.latest.has_value(); });
            if (stop.load()) break;
            frame = std::move(slot.latest);
            slot.latest.reset();
            source = slot.source;
        }
        const auto clock = now_ns();
        if (source.received_ns && clock - *source.received_ns >= static_cast<std::int64_t>(config.camera_stale_ms) * 1'000'000 && source.status == "running") {
            source.status = "lost";
            source.error = "No fresh camera frames received.";
            source.capture_fps = 0.0;
        }
        try {
            if (frame && frame->received_ns > watermark) {
                if (frame->image.channels() == 1) cv::cvtColor(frame->image, frame->image, cv::COLOR_GRAY2BGR);
                else if (frame->image.channels() == 4) cv::cvtColor(frame->image, frame->image, cv::COLOR_BGRA2BGR);
                const auto before = now_ns();
                auto detected = frame->synthetic_faces;
                if (!config.synthetic) {
                    cv::Mat faces;
                    model->setInputSize(frame->image.size());
                    model->detect(frame->image, faces);
                    detected = observations(faces, frame->image.size(), config);
                }
                const auto after = now_ns();
                auto next_tracker = state.tracker;
                next_tracker.update(detected, frame->received_ns);
                annotate(frame->image, next_tracker);
                auto jpeg = std::make_shared<std::vector<unsigned char>>();
                if (!cv::imencode(".jpg", frame->image, *jpeg, {cv::IMWRITE_JPEG_QUALITY, config.jpeg_quality})) {
                    throw std::runtime_error("Could not encode the preview frame.");
                }
                state.tracker = std::move(next_tracker);
                watermark = frame->received_ns;
                marked_lost = false;
                state.inference_ms = milliseconds(after - before);
                state.inference_fps = inference_rate.add(after);
                state.jpeg = std::move(jpeg);
                state.focal_length_px = focal_length(config, frame->image.cols);
                state.frame_received_ns = frame->received_ns;
                state.frame_width = frame->image.cols;
                state.frame_height = frame->image.rows;
                state.frame_sequence = ++state.sequence;
                processing_error.reset();
            } else {
                if ((source.status == "lost" || source.status == "error") && !marked_lost) {
                    state.tracker.update({}, clock);
                    marked_lost = true;
                } else {
                    state.tracker.expire(clock);
                }
                watermark = clock;
                if (source.status != "running") state.inference_fps = 0.0;
            }
        } catch (const std::exception& error) {
            processing_error = error.what();
            const auto failed_at = now_ns();
            if (failed_at > watermark) state.tracker.update({}, failed_at);
            watermark = failed_at;
            marked_lost = true;
            state.inference_fps = 0.0;
        }
        if (processing_error) {
            source.status = "error";
            source.error = processing_error;
        }
        state.source = std::move(source);
        published.publish(state);
    }
}

Json vector_json(Vec3 vector) { return { {"x", vector.x}, {"y", vector.y}, {"z", vector.z} }; }

std::string tracking_state(TrackingState state) {
    if (state == TrackingState::Acquiring) return "acquiring";
    if (state == TrackingState::Locked) return "locked";
    return "lost";
}

Json state_json(const Snapshot& state, const ServiceConfig& config, std::int64_t timestamp) {
    auto tracker = state.tracker;
    tracker.expire(timestamp);
    const auto selected = tracker.get_position(timestamp);
    const auto source = source_at(state, config, timestamp);
    const auto source_running = source.status == "running";
    Json people = Json::array();
    for (const auto& person : tracker.persons()) {
        Json landmarks = Json::array();
        for (const auto& point : person.observation.landmarks) landmarks.push_back({{"x", point.x}, {"y", point.y}});
        const auto& box = person.observation.box;
        people.push_back({{"id", std::to_string(person.id)}, {"position_m", vector_json(person.observation.position_m)},
            {"velocity_mps", vector_json(person.velocity_mps)}, {"age_ms", milliseconds(timestamp - person.last_seen_ns)},
            {"observed", person.observed && source_running}, {"box_px", {{"x", box.x}, {"y", box.y}, {"width", box.width}, {"height", box.height}}},
            {"landmarks_px", std::move(landmarks)}});
    }
    const auto has_person = selected.person_id.has_value();
    return {
        {"schema_version", "1.0"}, {"sequence", state.sequence}, {"generated_at_ns", std::to_string(timestamp)},
        {"source", {{"kind", config.synthetic ? "synthetic" : "camera"},
            {"device", source.device ? Json(*source.device) : Json(nullptr)}, {"status", source.status},
            {"error", source.error ? Json(*source.error) : Json(nullptr)},
            {"width", source.width}, {"height", source.height}, {"requested_fps", config.requested_fps},
            {"capture_fps", source.capture_fps}, {"inference_fps", source_running ? state.inference_fps : 0.0},
            {"frame_age_ms", state.frame_received_ns ? Json(milliseconds(timestamp - *state.frame_received_ns)) : Json(nullptr)},
            {"timestamp_basis", "frame_received"}}},
        {"detector", {{"backend", config.synthetic ? "synthetic" : config.backend},
            {"target", config.synthetic ? Json(nullptr) : Json(config.target)},
            {"model", config.synthetic ? Json(nullptr) : Json(config.model_path)},
            {"inference_ms", state.inference_ms ? Json(*state.inference_ms) : Json(nullptr)}}},
        {"coordinates", {{"frame", "camera"}, {"units", "m"},
            {"axes", {{"x", "image_right"}, {"y", "up"}, {"z", "forward"}}},
            {"calibration", config.synthetic ? "synthetic" : "approximate_ipd"},
            {"ipd_m", config.ipd_m}, {"focal_length_px", state.focal_length_px}}},
        {"selected", {{"person_id", has_person ? Json(std::to_string(*selected.person_id)) : Json(nullptr)},
            {"position_m", has_person ? vector_json(selected.position_m) : Json(nullptr)},
            {"velocity_mps", has_person ? vector_json(selected.velocity_mps) : Json(nullptr)},
            {"observation_timestamp_ns", has_person ? Json(std::to_string(selected.observation_timestamp_ns)) : Json(nullptr)},
            {"age_ms", has_person ? Json(milliseconds(selected.age_ns)) : Json(nullptr)},
            {"state", source_running ? tracking_state(selected.state) : "lost"},
            {"predicted", selected.predicted}, {"stale", selected.stale}}},
        {"persons", std::move(people)},
        {"video", {{"snapshot_url", "/api/v1/frame.jpg"}, {"frame_sequence", state.frame_sequence}}},
    };
}

std::string read_schema(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("API schema not found: " + path);
    Json schema;
    stream >> schema;
    return schema.dump();
}

}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") { help(); return 0; }
    }
    try {
        const auto config = arguments(argc, argv);
        const auto schema = read_schema(config.schema_path);
#if defined(__linux__)
        if (!config.synthetic) {
            const auto timeout = std::to_string(std::max(1, (config.camera_read_timeout_ms + 999) / 1000));
            if (::setenv("OPENCV_VIDEOIO_V4L_SELECT_TIMEOUT", timeout.c_str(), 1) != 0) {
                throw std::runtime_error("Could not configure the V4L2 read timeout.");
            }
        }
#endif
        auto model = detector(config);
        CaptureSlot capture;
        PublishedState published(config.tracker);
        std::atomic<bool> stop{false};
        std::atomic<bool> http_finished{false};
        std::atomic<bool> failed{false};
        httplib::Server server;
        server.new_task_queue = [&] { return new httplib::ThreadPool(config.http_threads); };
        server.set_default_headers({{"Access-Control-Allow-Origin", "*"}, {"Cache-Control", "no-store"},
            {"Access-Control-Allow-Methods", "GET, OPTIONS"}, {"Access-Control-Allow-Headers", "Content-Type"},
            {"Access-Control-Expose-Headers", "X-Frame-Sequence"}});
        server.Options(".*", [](const httplib::Request&, httplib::Response& response) { response.status = 204; });
        server.Get("/api/v1/schema", [&](const httplib::Request&, httplib::Response& response) { response.set_content(schema, "application/json"); });
        server.Get("/api/v1/state", [&](const httplib::Request&, httplib::Response& response) {
            const auto snapshot = published.read();
            response.set_content(state_json(*snapshot, config, now_ns()).dump(), "application/json");
        });
        server.Get("/api/v1/frame.jpg", [&](const httplib::Request&, httplib::Response& response) {
            const auto snapshot = published.read();
            if (!snapshot->jpeg) {
                response.status = 503;
                response.set_content("{\"error\":\"No processed video frame is available.\"}", "application/json");
                return;
            }
            response.set_header("X-Frame-Sequence", std::to_string(snapshot->frame_sequence));
            response.set_content(reinterpret_cast<const char*>(snapshot->jpeg->data()), snapshot->jpeg->size(), "image/jpeg");
        });
        server.Get("/healthz", [&](const httplib::Request&, httplib::Response& response) {
            const auto snapshot = published.read();
            response.set_content(Json{{"status", "ok"}, {"source_status", source_at(*snapshot, config, now_ns()).status}}.dump(), "application/json");
        });
        if (!server.bind_to_port(config.host, config.port)) throw std::runtime_error("Could not bind HTTP service to " + config.host + ":" + std::to_string(config.port));
        std::signal(SIGINT, interrupt_handler);
        std::signal(SIGTERM, interrupt_handler);
        std::thread capture_thread;
        std::thread worker_thread;
        std::thread http_thread;
        const auto finish = [&] {
            stop.store(true);
            capture.ready.notify_all();
            server.stop();
            if (http_thread.joinable()) http_thread.join();
            if (worker_thread.joinable()) worker_thread.join();
            if (capture_thread.joinable()) capture_thread.join();
        };
        const auto run = [&](auto function) {
            try { function(); }
            catch (const std::exception& error) {
                std::cerr << "Portal CV worker: " << error.what() << '\n';
                failed.store(true);
                stop.store(true);
            }
        };
        try {
            capture_thread = std::thread([&] { run([&] { capture_loop(config, capture, stop); }); });
            worker_thread = std::thread([&] { run([&] { processing_loop(config, model, capture, published, stop); }); });
            http_thread = std::thread([&] {
                run([&] { if (!server.listen_after_bind() && !stop.load()) failed.store(true); });
                http_finished.store(true);
            });
            std::cout << "Portal CV listening at http://" << config.host << ':' << config.port << '\n'
                      << "Backend: " << (config.synthetic ? "synthetic" : config.backend) << ". Timestamp basis: frame_received.\n" << std::flush;
            while (!interrupted && !http_finished.load() && !stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.shutdown_poll_ms));
            }
        } catch (...) {
            finish();
            throw;
        }
        finish();
        return failed.load() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Portal CV: " << error.what() << '\n';
        return 1;
    }
}
