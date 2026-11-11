# Tracking API

The native `portal_cv` process produces this API. The separate web app in `demo/` reads it. Closing or restarting the web app does not stop tracking.

| Request | Result |
| --- | --- |
| `GET /api/v1/state` | Current people and the selected viewer's predicted position |
| `GET /api/v1/frame.jpg` | Most recent annotated JPEG |
| `GET /api/v1/schema` | [OpenAPI contract](openapi.json) |
| `GET /healthz` | Whether the HTTP process is responding |

The API is read-only. Native startup defaults to `127.0.0.1:8080`. Browser clients use CORS to read it from their own server. The Docker command binds inside the container on all interfaces; publish its port on host loopback for local access.

## Positions and identity

`persons` contains the current tracked vector. Each person's `position_m` averages their latest three retained measurements by default. Set `TrackerConfig::position_average_samples` in `config.h`, or pass `--position-average-samples N`; 1 disables smoothing. The window is capped by `history_limit`. Only matched observations update the average. Missing people remain until their tracking timeout expires. `observed` describes the latest processed frame, not a new observation made by the HTTP request.

`selected.person_id` identifies the active viewer. The demo highlights that ID in the vector. `selected.position_m` is predicted at query time from the filtered position and the velocity fitted to raw position history. It may be held after loss, even after that ID leaves `persons`; use `selected.state` and `selected.stale` before using the position. A selected ID is not proof of someone's identity.

Positions use camera coordinates in metres: X points right in the unmirrored image, Y points up, and Z points forward from the camera. They are not calibrated to the physical window. Each raw depth measurement uses `Z = focal_length_px * ipd_m / eye_separation_px` before averaging. Boxes and landmarks still describe the latest raw observation. The default IPD is 0.063 metres. Without an explicit focal length, the service estimates it from image width and the configured horizontal field of view. Head rotation and individual eye spacing affect the result.

## Time and freshness

IDs and nanosecond timestamps are decimal strings so browser clients do not lose integer precision. Timestamps belong to the server's monotonic clock, not wall-clock time. Use the provided ages to judge freshness; do not subtract a timestamp from `Date.now()`.

Camera timestamps mark when the application receives the frame. They do not include a verified sensor exposure timestamp, so `frame_age_ms` is not a measurement of full camera-to-display latency.

`sequence` advances when a new frame is processed. Repeated reads can return the same sequence with a later `generated_at_ns`, greater ages, and a newly predicted selected position. Fetch success does not make the underlying camera data fresh.

State and JPEG requests are independent. Compare the JPEG's `X-Frame-Sequence` response header with `video.frame_sequence` before treating them as the same frame. Camera loss can leave an old JPEG available. Check `source.status` and `source.frame_age_ms`.

The cached JPEG labels each observed person with their ID and measured X, Y, and Z in metres. `ACTIVE` marks the selected viewer in that frame. These labels use the same tracker update as the image. Saving the JPEG preserves those annotations without storing a new capture on the backend.

`source.capture_fps` measures camera frame receipt. `source.inference_fps` measures completed processed frames per second, including detection, tracking, drawing, and JPEG encoding. It uses successful completion timestamps, so failed processing does not count as another frame. The existing field name stays unchanged for API 1.0. `detector.inference_ms` remains the time spent detecting faces and converting their observations. Browser refresh and polling rates are separate.

## Failures and sample input

The HTTP process can remain healthy while camera capture fails. `source.status` and `source.error` report capture or processing failures. A processed frame older than `camera_stale_ms` is reported as lost even if inference stops publishing. Dimensions describe the last processed image. Tracking observations age and expire normally. CUDA is the default requested inference backend; unavailable CUDA fails startup rather than selecting the CPU. Use `--backend cpu` explicitly for a CPU build.

`--synthetic` sends generated observations through the real tracker and HTTP API without a camera or model. Its responses identify both source and detector as `synthetic`. The web app also has an explicit Sample mode for trying its display without a running API. Neither mode establishes camera, model, or GPU performance.

## Contract check

With Python's `jsonschema` package installed, run from `sw/cv` after building the service:

```sh
python3 tests/api_smoke.py build/portal_cv
```

This starts a synthetic service on a temporary local port with averaging disabled to check the raw eye-geometry conversion, published schema, state responses, selected viewer, frame data, CORS, read-only routes, concurrent reads, and shutdown. Tracker tests cover averaging separately. It then loads YuNet in explicit CPU mode and checks that a missing camera leaves HTTP available without inventing frames or tracks.
