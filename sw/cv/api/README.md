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

`persons` contains the current tracked vector. Each person's `position_m` is the latest measured position. Missing people remain until their tracking timeout expires. `observed` describes the latest processed frame, not a new observation made by the HTTP request.

`selected.person_id` identifies the active viewer. The demo highlights that ID in the vector. `selected.position_m` is predicted at query time using the tracker history. It may be held after loss, even after that ID leaves `persons`; use `selected.state` and `selected.stale` before using the position. A selected ID is not proof of someone's identity.

Positions use camera coordinates in metres: X points right in the unmirrored image, Y points up, and Z points forward from the camera. They are not calibrated to the physical window. Depth uses `Z = focal_length_px * ipd_m / eye_separation_px`. The default IPD is 0.063 metres. Without an explicit focal length, the service estimates it from image width and the configured horizontal field of view. Head rotation and individual eye spacing affect the result.

## Time and freshness

IDs and nanosecond timestamps are decimal strings so browser clients do not lose integer precision. Timestamps belong to the server's monotonic clock, not wall-clock time. Use the provided ages to judge freshness; do not subtract a timestamp from `Date.now()`.

Camera timestamps mark when the application receives the frame. They do not include a verified sensor exposure timestamp, so `frame_age_ms` is not a measurement of full camera-to-display latency.

`sequence` advances when a new frame is processed. Repeated reads can return the same sequence with a later `generated_at_ns`, greater ages, and a newly predicted selected position. Fetch success does not make the underlying camera data fresh.

State and JPEG requests are independent. Compare the JPEG's `X-Frame-Sequence` response header with `video.frame_sequence` before treating them as the same frame. Camera loss can leave an old JPEG available. Check `source.status` and `source.frame_age_ms`.

## Failures and sample input

The HTTP process can remain healthy while camera capture fails. `source.status` and `source.error` report capture or processing failures. A processed frame older than `camera_stale_ms` is reported as lost even if inference stops publishing. Dimensions describe the last processed image. Tracking observations age and expire normally. CUDA is the default requested inference backend; unavailable CUDA fails startup rather than selecting the CPU. Use `--backend cpu` explicitly for a CPU build.

`--synthetic` sends generated observations through the real tracker and HTTP API without a camera or model. Its responses identify both source and detector as `synthetic`. The web app also has an explicit Sample mode for trying its display without a running API. Neither mode establishes camera, model, or GPU performance.

## Contract check

With Python's `jsonschema` package installed, run from `sw/cv` after building the service:

```sh
python3 tests/api_smoke.py build/portal_cv
```

This starts a synthetic service on a temporary local port and checks the published schema, state responses, selected viewer, frame data, CORS, read-only routes, concurrent reads, and shutdown. It then loads YuNet in explicit CPU mode and checks that a missing camera leaves HTTP available without inventing frames or tracks.
