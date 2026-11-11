# Per-person tracking

The tracking core keeps one vector entry per person. Every person has a stable ID, position history, velocity estimate, last observation time, face box, and landmark-distance signature. The selected viewer is one of these tracks; other visible people continue to update independently.

This is a C++17 library tested with synthetic observations. It does not yet capture a camera, run YuNet/CUDA, or connect to a separate renderer process.

## Data flow

The position feed supplies all faces from one camera frame, together with the capture timestamp. Each face includes its box, five YuNet landmarks, confidence, and a calibrated position in metres relative to the window.

The tracker matches observations to existing people using predicted position, box overlap, and landmark geometry. Each observation can update only one person. Unmatched observations create new entries; unmatched people keep their history until their own timeout expires. A new ID never inherits an expired person's history.

Landmark geometry contains the ten pairwise distances between the two eyes, nose, and two mouth corners. Distances are divided by eye separation so the signature does not change merely because the face moves closer or farther away. These geometric cues can help after brief occlusion but do not establish identity. Similar faces and head rotation can still cause incorrect assignments. No images or recognition embeddings are stored.

## Selection and prediction

The first selected viewer is the largest eligible visible face after the acquisition interval. A different person cannot become selected until the switch cooldown has elapsed since the selected viewer's last observation. A returning track can resume during that interval. Other tracks continue updating while the selected viewer is missing.

The latest-position query uses the selected person's cached position and velocity. Velocity is fitted to each person's measured position history. Prediction tapers velocity linearly to zero over the configured horizon, then holds the endpoint. If the person expires earlier, motion stops at expiry. Repeated queries do not change history or make an old measurement fresh.

For a horizon `H` and observation age `t` clamped to `[0, H]`, displacement is `velocity * (t - t*t/(2*H))`. A zero horizon disables extrapolation. Tracking state and observation age accompany the returned position, including when an expired track's final cached position is held.

Use capture and query timestamps from the same monotonic clock, in nanoseconds. Timestamping inference completion would hide capture and processing delay. `update()` expires old tracks when a frame arrives. Call `expire()` during camera outages too. Frame timestamps must increase and cannot precede the latest expiry call, so discard delayed frames that fall behind that time. Invalid inputs reject the whole update without changing stored tracks.

`get_position()` returns position, velocity, observation age, and tracking state. The `stale` flag marks person expiry; the prediction horizon only limits motion. One thread must own this core. Sharing snapshots with the renderer requires separate communication and synchronization.

## Defaults

Tracker settings live in [`include/portal/config.h`](include/portal/config.h). Edit this file and rebuild to change the defaults. The timing, history, confidence, and distance settings can also be overridden through `TrackerConfig` when constructing a tracker. Matching weights and the unmatched cost are compile-time settings.

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

The [Dockerfile](Dockerfile) builds and runs the tracking tests. Its default base is NVIDIA's [CUDA 13.3.0 and cuDNN development image](https://catalog.ngc.nvidia.com/orgs/nvidia/-/containers/cuda/13.3.0-cudnn-devel-ubuntu24.04/layers). The image includes CUDA development tools, but the current tracker tests run on the CPU. OpenCV, YuNet inference, and camera capture are not included.

These commands target the Jetson Orin Nano Super on L4T R39.2.0 (JetPack 7.2). Run them in Bash on the Jetson. Docker must already be installed. Configure its NVIDIA runtime once if needed:

```sh
sudo apt update
sudo apt install -y nvidia-container
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker
```

From the repository root, build and run:

```sh
sudo docker build -t portal-cv sw/cv
sudo docker run --rm --runtime=nvidia \
  -e NVIDIA_DISABLE_REQUIRE=true portal-cv
```

The build runs the tests and fails if any test fails. Running the image repeats the tests and should end with `18/18 tests passed`. Rebuild after pulling changes or editing `config.h`. The build context contains only the CV component, and `.dockerignore` excludes local build output.

NVIDIA [recommends CUDA 13.3 with `NVIDIA_DISABLE_REQUIRE=true` for Orin Nano on R39.2](https://forums.developer.nvidia.com/t/cuda-13-2-1-runtime-image-cant-run-on-r39-2-sample-rootfs-for-orin-nano/372683/21). The flag bypasses an outdated driver-version check. It is supplied at launch rather than saved in the image. Recheck compatibility before changing the base image or JetPack version. The cited report tested the runtime image; this Dockerfile uses the corresponding development image, which still needs validation on the target Jetson.

Check GPU visibility separately:

```sh
sudo docker run --rm --runtime=nvidia \
  -e NVIDIA_DISABLE_REQUIRE=true portal-cv nvidia-smi
```

A GPU listing confirms driver access inside the container. It does not test CUDA kernels or YuNet inference. To open a shell in the image, replace `nvidia-smi` with `bash` and add `-it` after `docker run`.

For a smaller image that only runs CPU tests, use the same Dockerfile with an Ubuntu base. This works on ARM64 or x86-64 without the NVIDIA runtime:

```sh
sudo docker build --build-arg BASE_IMAGE=ubuntu:24.04 \
  -t portal-cv-cpu sw/cv
sudo docker run --rm portal-cv-cpu
```
