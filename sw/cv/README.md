# Per-person tracking

The tracking core keeps one vector entry per person. Every person has a stable ID, position history, velocity estimate, last observation time, face box, and landmark-distance signature. The selected viewer is one of these tracks; other visible people continue to update independently.

The C++17 tracker is also used by `portal_cv`, a webcam and HTTP service. The service runs YuNet with CUDA by default, converts eye geometry into approximate camera coordinates, and exposes positions and annotated frames through a [read-only API](api/README.md). CPU inference must be selected explicitly. The service does not drive the window renderer.

## Data flow

The position feed supplies all faces from one camera frame, together with the capture timestamp. Each face includes its box, five YuNet landmarks, confidence, and position in metres. The webcam service supplies approximate camera coordinates. Transforming these into calibrated window coordinates remains separate work.

The tracker matches observations to existing people using predicted position, box overlap, and landmark geometry. Each observation can update only one person. Unmatched observations create new entries; unmatched people keep their history until their own timeout expires. A new ID never inherits an expired person's history.

Landmark geometry contains the ten pairwise distances between the two eyes, nose, and two mouth corners. Distances are divided by eye separation so the signature does not change merely because the face moves closer or farther away. These geometric cues can help after brief occlusion but do not establish identity. Similar faces and head rotation can still cause incorrect assignments. No images or recognition embeddings are stored.

## Selection and prediction

The first selected viewer is the largest eligible visible face after the acquisition interval. A different person cannot become selected until the switch cooldown has elapsed since the selected viewer's last observation. A returning track can resume during that interval. Other tracks continue updating while the selected viewer is missing.

The latest-position query uses the selected person's cached position and velocity. Velocity is fitted to each person's measured position history. Prediction tapers velocity linearly to zero over the configured horizon, then holds the endpoint. If the person expires earlier, motion stops at expiry. Repeated queries do not change history or make an old measurement fresh.

For a horizon `H` and observation age `t` clamped to `[0, H]`, displacement is `velocity * (t - t*t/(2*H))`. A zero horizon disables extrapolation. Tracking state and observation age accompany the returned position, including when an expired track's final cached position is held.

Use capture and query timestamps from the same monotonic clock, in nanoseconds. Timestamping inference completion would hide capture and processing delay. `update()` expires old tracks when a frame arrives. Call `expire()` during camera outages too. Frame timestamps must increase and cannot precede the latest expiry call, so discard delayed frames that fall behind that time. Invalid inputs reject the whole update without changing stored tracks.

`get_position()` returns position, velocity, observation age, and tracking state. The `stale` flag marks person expiry; the prediction horizon only limits motion. One thread must own this core. Sharing snapshots with the renderer requires separate communication and synchronization.

## Defaults

Tracker and service settings live in [`include/portal/config.h`](include/portal/config.h). Edit this file and rebuild to change the defaults. The timing, history, confidence, and distance settings can also be overridden through `TrackerConfig` when constructing a tracker. Matching weights and the unmatched cost are compile-time settings. `ServiceConfig` holds the camera, detector, calibration, and HTTP defaults.

| Setting | Initial value |
| --- | --- |
| Person expiry | 1 second since that person's last observation |
| Target-switch cooldown | 1 second since the selected person's last observation |
| Prediction horizon | 1 second, with tapered velocity |
| Initial acquisition interval | 0.5 seconds of consecutive observations |
| Position history | 8 observations per person |
| Minimum detection confidence | 0.6 |
| Maximum match position distance | 0.75 metres |
| Maximum landmark-signature distance | 0.35 |
| Position match weight | 0.45 |
| Landmark match weight | 0.45 |
| Box overlap match weight | 0.10 |
| Cost per unmatched track or detection | 1.1 |

These settings are configurable starting values, not measured operating limits. Detection confidence is the detector's score, not a probability that person association is correct.

## Run the tests

From the repository root on Windows with MinGW:

```powershell
mingw32-make -C sw/cv test
```

On Linux:

```sh
make -C sw/cv test
```

The tests exercise independent updates, detection reordering, occlusion, geometric matching, expiry, selection cooldown, prediction, and invalid input. They do not establish accuracy or latency on real faces or Jetson hardware.

## Docker on Jetson

The [Dockerfile](Dockerfile) builds OpenCV with CUDA DNN and cuDNN, then builds the service and runs the tracker tests. It uses NVIDIA's [CUDA 13.3.0 and cuDNN development image](https://catalog.ngc.nvidia.com/orgs/nvidia/-/containers/cuda/13.3.0-cudnn-devel-ubuntu24.04/layers), pinned OpenCV 4.13.0 sources, and the [upstream CUDA 13 compatibility fix for cudev](https://github.com/opencv/opencv_contrib/commit/f2854f4f5e7b67d4e073ea002ae0174d437e2962). Downloads are checked by SHA-256. The default GPU architecture is Orin's 8.7 and the build uses two jobs to limit memory use. The initial build downloads and compiles substantial dependencies; later application builds reuse that layer.

These commands target the Jetson Orin Nano Super on L4T R39.2.0 (JetPack 7.2). Run them in Bash on the Jetson. Docker must already be installed. Configure its NVIDIA runtime once if needed:

```sh
sudo apt update
sudo apt install -y nvidia-container
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker
```

From the repository root, build:

```sh
sudo docker build -t portal-cv sw/cv
```

From the repository root, start the API with the [startup script](run.sh). It exposes the connected video devices to Docker. The service selects the first one that opens and supplies a frame:

```bash
bash sw/cv/run.sh
```

To select one camera explicitly, pass a service command through the script:

```sh
bash sw/cv/run.sh \
  ./build/portal_cv --host 0.0.0.0 --device /dev/video2
```

`--device 2` also selects camera index 2. Auto-selection considers devices visible inside the container. The default request is 1280×720 at 30 FPS; the camera may negotiate different settings. `--width`, `--height`, and `--fps` change the request. `--target fp16` requests CUDA FP16 instead of FP32. A missing CUDA build, unavailable CUDA target, or failed model warmup stops startup; the service does not choose CPU mode automatically. OpenCV controls individual layer placement.

Inspect the output:

```sh
curl http://127.0.0.1:8080/api/v1/state
```

Open `http://127.0.0.1:8080/api/v1/frame.jpg` for the latest annotated image. The service runs without a desktop session or display-server mounts. Camera failure is reported through the API while the HTTP service stays available and tracks expire. On Linux, the V4L2 select timeout comes from `camera_read_timeout_ms`, rounded up to whole seconds. A blocked driver can still delay process shutdown.

Rebuild after pulling changes or editing `config.h`. The build context contains only the CV component, and `.dockerignore` excludes local build output. To let a browser on another machine reach the API, publish with `-p 8080:8080` and use the Jetson's address. This read-only API has no authentication and includes camera images, so use that mode only on a trusted network.

NVIDIA [recommends CUDA 13.3 with `NVIDIA_DISABLE_REQUIRE=true` for Orin Nano on R39.2](https://forums.developer.nvidia.com/t/cuda-13-2-1-runtime-image-cant-run-on-r39-2-sample-rootfs-for-orin-nano/372683/21). The flag bypasses an outdated driver-version check. It is supplied at launch rather than saved in the image. Recheck compatibility before changing the base image or JetPack version. Target Jetson camera and GPU execution still need hardware validation.

Check GPU visibility separately:

```sh
sudo docker run --rm --runtime=nvidia \
  -e NVIDIA_DISABLE_REQUIRE=true portal-cv nvidia-smi
```

A GPU listing confirms driver access inside the container. The service's model warmup is a separate inference check. To open a shell in the image, replace `nvidia-smi` with `bash` and add `-it` after `docker run`.

## CPU and sample input

For a CPU build on ARM64 or x86-64, use the same Dockerfile with CUDA disabled. This still builds the full service:

```sh
sudo docker build --build-arg BASE_IMAGE=ubuntu:24.04 \
  --build-arg WITH_CUDA=OFF \
  -t portal-cv-cpu sw/cv
sudo docker run --rm --device /dev/video0 -p 127.0.0.1:8080:8080 \
  portal-cv-cpu ./build/portal_cv --backend cpu --host 0.0.0.0
```

To exercise the same API and tracker without any camera or model inference:

```sh
sudo docker run --rm -p 127.0.0.1:8080:8080 \
  portal-cv-cpu ./build/portal_cv --synthetic --host 0.0.0.0
```

Synthetic responses are labelled explicitly. They exercise moving people and temporary loss, but do not establish detection accuracy or GPU performance.

## Native service build

With CMake 3.20+, a C++17 compiler, and OpenCV 4.8+ installed, run from `sw/cv`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/portal_cv
```

The CMake build downloads pinned HTTP and JSON libraries. OpenCV must include its DNN, videoio, imgcodecs, imgproc, objdetect, and core modules. A CUDA launch also requires OpenCV's CUDA DNN backend and cuDNN. Run `./build/portal_cv --help` for device, model, API, and calibration arguments. Paths for the model and API schema are relative to the current working directory unless supplied explicitly.

Use `-DBUILD_SERVICE=OFF` for a CMake build of only the dependency-free tracker tests. The existing Makefile `test` target also remains independent of OpenCV. The [API smoke test](api/README.md#contract-check) checks the live service and published response contract.

## Position demo

The separate [web demo](demo/README.md) shows the tracked vector and highlights the active person. With Node.js 20+ installed, run `node demo/server.mjs` from `sw/cv`, open `http://127.0.0.1:5173`, and connect to the running API. Its explicit Sample mode works without the service.
