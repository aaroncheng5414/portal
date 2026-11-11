# Position demo

A standalone web page that reads the CV service's HTTP API. It has no CV or model dependencies.

## Run

After [building the CV image](../README.md), start the backend from the repository root:

```sh
bash sw/cv/run.sh
```

In a second terminal, start the page in its own Node container:

```sh
bash sw/cv/demo/run.sh
```

Open **http://127.0.0.1:5173** and select **Connect**. The API URL defaults to the page's hostname on port 8080. Both scripts bind their host ports to loopback. See the [CV instructions](../README.md) for camera selection and access from a laptop.

The demo script downloads Node 22 and mounts this directory read-only. Alternatively, with Node.js 20+ installed on the host, run `node sw/cv/demo/server.mjs` from the repository root. No npm packages need installing.

**Sample** displays a deterministic browser-only fixture. It does not connect to the API, open a camera, or run the tracker. Connection failures never enable it automatically.

## Display and contract

The XY, XZ, and YZ plots and table show every entry in `persons` using its filtered `position_m` in metres. The backend averages three measurements per person by default. The entry matching `selected.person_id` is highlighted. A held selected position whose ID is absent from the vector is not drawn as another person.

Hover or select a person to inspect XYZ. Unobserved tracks remain visible while the backend retains them. Processed FPS comes from `source.inference_fps`, not browser refresh rate. Sample and stale data are labeled.

The camera panel fetches `/api/v1/frame.jpg`. The backend draws each observed person's ID, XYZ, and active status into that frame. **Save frame** downloads the displayed JPEG. Images are held in memory, with no automatic recording. Image and state requests are independent, so their frame sequences can differ.

Expand **API response** for the actual JSON, or follow **Schema** to `/api/v1/schema`. Nanosecond timestamps are server-monotonic strings; use the supplied ages for freshness.

## Tests

```sh
npm test
```

The tests cover contract parsing, replay rejection, freshness, sample labeling, request timeouts, and cancellation.
