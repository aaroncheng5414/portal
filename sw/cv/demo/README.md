# Position demo

A standalone web page that reads the CV service's HTTP API. It has no CV or model dependencies. Requires Node.js 20 or newer; no npm packages need installing.

## Run

After [building the native service](../README.md#native-service-build), start its synthetic input from the repository root:

```sh
cd sw/cv
./build/portal_cv --synthetic
```

In a second terminal, start the page from `sw/cv`:

```sh
node demo/server.mjs
```

Open **http://127.0.0.1:5173** and select **Connect**. The API URL defaults to the page's hostname on port 8080. Synthetic backend input exercises the actual tracker and API without a camera. See the [CV instructions](../README.md#docker-on-jetson) for camera and GPU operation.

Alternatively, run `npm start` from this `demo` directory. To serve the page on the Jetson's network interface, use `npm start -- --host 0.0.0.0`. The backend must also be reachable from the browser; its URL is configured separately.

**Sample** displays a deterministic browser-only fixture. It does not connect to the API, open a camera, or run the tracker. Connection failures never enable it automatically.

## Display and contract

The plot and table show every entry in the response's `persons` vector using its measured `position_m`. The entry matching `selected.person_id` is highlighted. A held selected position whose ID is absent from the vector is not drawn as another person.

The plot uses X and Z in metres. Hover or select a person to inspect XYZ. Unobserved tracks remain visible while the backend retains them. Disconnection, outdated data, and source errors are labeled while the last response is held.

The page polls `GET /api/v1/state`. Expand **API response** to inspect the actual JSON, or follow **Schema** to `/api/v1/schema`. Nanosecond timestamps are server-monotonic strings rather than wall-clock dates. The supplied ages describe observation freshness. The page does not request camera images.

## Tests

```sh
npm test
```

The tests cover contract parsing, replay rejection, freshness, sample labeling, request timeouts, and cancellation.
