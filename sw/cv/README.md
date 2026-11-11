# CV tracking

On the Jetson, from the repository root:

```bash
sudo docker build -t portal-cv sw/cv
bash sw/cv/run.sh
```

Start the demo in a second terminal, also from the repository root:

```sh
node sw/cv/demo/server.mjs
```

Open **http://127.0.0.1:5173** and click **Connect**. Stop each process with **Ctrl+C**.

Requires Docker with its NVIDIA runtime and Node.js 20+. No npm install is needed. Skip the build on later runs unless native code or configuration changed. The first build compiles OpenCV.

The backend defaults to CUDA and the first readable camera. It does not fall back to CPU. Jetson camera and CUDA execution remain unverified.

## Camera and settings

Choose a camera explicitly:

```bash
bash sw/cv/run.sh ./build/portal_cv --host 0.0.0.0 --device /dev/video2
```

Use `--synthetic` instead of `--device /dev/video2` for generated input. The default camera request is 1280×720 at 30 FPS; `--width`, `--height`, and `--fps` override it. Camera support determines the actual settings.

Defaults live in [config.h](include/portal/config.h). Rebuild after editing it. See [demo instructions](demo/README.md) and the [API contract](api/README.md) for more detail.

## View from a laptop

Keep both processes running on the Jetson. On the laptop, replace `JETSON_IP` and run:

```bash
ssh -L 5173:127.0.0.1:5173 -L 8080:127.0.0.1:8080 samuel_jetson@JETSON_IP
```

Open **http://127.0.0.1:5173** on the laptop. The servers stay accessible through loopback.

## Docker on Jetson

For the Orin Nano Super on L4T R39.2.0, configure the NVIDIA runtime once if needed:

```sh
sudo apt update
sudo apt install -y nvidia-container
sudo nvidia-ctk runtime configure --runtime=docker
sudo systemctl restart docker
```

The [Dockerfile](Dockerfile) builds pinned OpenCV 4.13 sources with CUDA 13.3, cuDNN, and Orin GPU architecture 8.7. The [startup script](run.sh) includes `NVIDIA_DISABLE_REQUIRE=true` for the [R39.2 driver-version check workaround](https://forums.developer.nvidia.com/t/cuda-13-2-1-runtime-image-cant-run-on-r39-2-sample-rootfs-for-orin-nano/372683/21).

## Tracking and output

YuNet observations update one vector entry per person. Matching uses position, box overlap, and landmark distances. The selected viewer has a predicted position; the demo highlights its ID among the measured positions. Defaults are one second for expiry, switching cooldown, and prediction, with a half-second initial acquisition interval.

Positions are approximate camera coordinates in metres. Eye spacing estimates depth; it is not calibrated to the window. Geometry helps match faces after brief occlusion but does not establish identity.

The API serves positions at `/api/v1/state`, the latest annotated image at `/api/v1/frame.jpg`, and its schema at `/api/v1/schema`. See [API details](api/README.md) for freshness and failure handling.

## Native service build

Requires CMake 3.20+, C++17, and OpenCV 4.8+ with core, imgproc, imgcodecs, videoio, objdetect, and dnn. CUDA mode also requires OpenCV's CUDA DNN backend and cuDNN. From `sw/cv`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/portal_cv
```

Use `--backend cpu` for explicit CPU inference or `--help` for all arguments. Model and schema paths are relative to the working directory.

## Tests

From the repository root, run `make -C sw/cv test` on Linux or `mingw32-make -C sw/cv test` on Windows. These tracker tests need no OpenCV. After building the service, run the [API smoke test](api/README.md#contract-check). Tests do not establish real-camera accuracy or GPU performance.
