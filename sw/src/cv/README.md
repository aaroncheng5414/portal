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
mingw32-make -C sw/src/cv test
```

On Linux:

```sh
make -C sw/src/cv test
```

The tests exercise independent updates, detection reordering, occlusion, geometric matching, expiry, selection cooldown, prediction, and invalid input. They do not establish accuracy or latency on real faces or Jetson hardware.
