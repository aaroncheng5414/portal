# P.O.R.T.A.L.

A browser concept demo for the ECE 364D perspective-aware window project. Move a cursor or your head to look through a fixed frame into a 3D scene.

**[Open the demo](https://aaroncheng5414.github.io/p.o.r.t.a.l./)**

## Try it

- Move your cursor across the window. On a phone, drag inside it.
- Focus the window and use arrow keys for keyboard control. Home recenters the view.
- Choose **Camera** to use an available webcam. Hold still while the view centers, then move gently from side to side. Choose **Cursor** to release the camera.
- Switch between the gallery and landscape, adjust movement and viewing distance, or select **Play demo**.

This is an interactive concept mockup. It does not demonstrate completed hardware or measured performance of the final device. Camera depth uses relative eye spacing and is approximate. It is not a calibrated measurement of physical position.

## Run locally

The native per-person tracking component is in [`sw/cv/`](sw/cv/README.md). It maintains face tracks, matches returning faces, and provides predicted positions. Its tests use synthetic observations; camera capture and YuNet/CUDA inference are separate integration work.

For container builds and Jetson setup, see [Docker on Jetson](sw/cv/README.md#docker-on-jetson).

The app lives in `sw/web/`. Requires Node.js 22.12+ or 24+ and npm.

```sh
cd sw/web
npm ci
npm run dev
```

Open the localhost address printed by Vite. Camera mode requires localhost or HTTPS. A current browser with WebGL 2 and WebAssembly is required. The cursor demo works without camera permission.

```sh
npm test
npm run build
npm run preview
```

The unit tests cover the fixed-screen projection, head-position mapping, late camera/model results, GPU fallback, and camera cleanup. With the development server running, `/tests/browser-smoke.html` checks the real MediaPipe runtime against synthetic canvas video without opening a physical camera. That test page is excluded from the production build.

## How it works

Three.js renders geometry behind a fixed virtual screen. The camera translates with the viewer and uses an off-axis projection, keeping the screen corners fixed. Objects at different depths shift by different amounts.

MediaPipe Face Landmarker runs only after Camera is selected. Video and landmarks remain in the browser; there is no backend, recording, analytics, or upload. The model and WebAssembly runtime are served from this site. Fonts load from Google Fonts, with system fonts as fallback.

The runtime files are copied from the pinned npm package by the `predev` and `prebuild` scripts. The face model is checked into `sw/web/public/models/face_landmarker.task` from Google's [published model](https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task). See the [MediaPipe documentation](https://ai.google.dev/edge/mediapipe/solutions/vision/face_landmarker) and its terms for model use.

## GitHub Pages

Set **Settings → Pages → Source** to **GitHub Actions**. Pushes to `main` run the tests and build from `sw/web/`, then publish `sw/web/dist` with the included workflow. Relative asset URLs support the repository path `/p.o.r.t.a.l./` and local previews.

The two scenes use procedural geometry and materials created for this demo. Dependencies remain under their respective licenses.
