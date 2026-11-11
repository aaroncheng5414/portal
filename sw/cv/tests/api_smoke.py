import argparse
import concurrent.futures
import json
import math
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

import jsonschema


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def request(base, path, method="GET"):
    req = urllib.request.Request(base + path, method=method)
    with urllib.request.urlopen(req, timeout=3) as response:
        return response.status, response.headers, response.read()


def state(base, validator):
    status, headers, body = request(base, "/api/v1/state")
    require(status == 200, "state request must succeed")
    require("application/json" in headers.get("Content-Type", ""), "state must be JSON")
    require(headers.get("Access-Control-Allow-Origin") == "*", "separate web clients need CORS")
    require("no-store" in headers.get("Cache-Control", ""), "state must not be cached")
    value = json.loads(body)
    validator.validate(value)
    require(len({person["id"] for person in value["persons"]}) == len(value["persons"]), "person IDs must be unique")
    require(int(value["generated_at_ns"]) > 0, "query timestamp must use a live monotonic clock")
    if value["selected"]["person_id"] is not None:
        require(value["selected"]["position_m"] is not None, "selected track needs a position")
        require(int(value["selected"]["observation_timestamp_ns"]) <= int(value["generated_at_ns"]), "observation cannot come from the future")
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = pathlib.Path(__file__).resolve().parents[1]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    with tempfile.TemporaryFile(mode="w+") as log:
        process = subprocess.Popen(
            [str(binary), "--synthetic", "--host", "127.0.0.1", "--port", str(port)],
            cwd=root, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 15
            while True:
                require(process.poll() is None, "service exited during startup")
                try:
                    request(base, "/healthz")
                    break
                except (urllib.error.URLError, OSError):
                    if time.monotonic() >= deadline:
                        raise AssertionError("service did not become healthy")
                    time.sleep(0.05)
            _, _, body = request(base, "/api/v1/schema")
            published = json.loads(body)
            expected = json.loads((root / "api" / "openapi.json").read_text())
            require(published == expected, "served contract must match the checked-in document")
            schema = {"$ref": "#/components/schemas/State", "components": published["components"]}
            jsonschema.Draft202012Validator.check_schema(schema)
            validator = jsonschema.Draft202012Validator(schema)
            first = state(base, validator)
            deadline = time.monotonic() + 5
            while True:
                current = state(base, validator)
                if current["sequence"] > first["sequence"] and current["selected"]["state"] == "locked":
                    break
                require(time.monotonic() < deadline, "synthetic input must advance and acquire a viewer")
                time.sleep(0.05)
            require(current["source"]["kind"] == "synthetic", "synthetic input must be labelled")
            require(current["detector"]["backend"] == "synthetic", "synthetic input must not claim GPU inference")
            require(current["coordinates"]["calibration"] == "synthetic", "synthetic coordinates must be labelled")
            require(current["source"]["status"] == "running", "synthetic source should run")
            require(len(current["persons"]) >= 1, "synthetic input must exercise real person tracks")
            focal = current["coordinates"]["focal_length_px"]
            for person in current["persons"]:
                right, left = person["landmarks_px"][:2]
                separation = math.hypot(right["x"] - left["x"], right["y"] - left["y"])
                depth = focal * current["coordinates"]["ipd_m"] / separation
                x = ((right["x"] + left["x"]) / 2 - current["source"]["width"] / 2) * depth / focal
                y = (current["source"]["height"] / 2 - (right["y"] + left["y"]) / 2) * depth / focal
                require(all(math.isclose(person["position_m"][axis], expected, abs_tol=1e-8)
                            for axis, expected in [("x", x), ("y", y), ("z", depth)]),
                        "eye geometry and published calibration must explain the measured XYZ")
            status, headers, jpeg = request(base, "/api/v1/frame.jpg")
            require(status == 200 and headers.get("Content-Type", "").startswith("image/jpeg"), "video endpoint must provide JPEG")
            require(jpeg[:2] == b"\xff\xd8" and jpeg[-2:] == b"\xff\xd9", "frame must have JPEG markers")
            require(int(headers["X-Frame-Sequence"]) >= current["video"]["frame_sequence"], "frame sequence must not move backward")
            request(base, "/api/v1/state", "OPTIONS")
            for path, method in [("/missing", "GET"), ("/api/v1/state", "POST")]:
                try:
                    request(base, path, method)
                    raise AssertionError(f"{method} {path} should be rejected")
                except urllib.error.HTTPError as error:
                    require(error.code in (404, 405), "unknown or modifying routes must be rejected")
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                snapshots = list(pool.map(lambda _: state(base, validator), range(12)))
            require(all(value["sequence"] >= first["sequence"] for value in snapshots), "concurrent reads must preserve frame ordering")
            print("PASS live HTTP schema, selection, freshness, video, CORS, read-only routes, and concurrent reads")
        except Exception:
            log.seek(0)
            print(log.read())
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise AssertionError("service did not stop cleanly")
        require(process.returncode == 0, "service should handle shutdown successfully")

    with tempfile.TemporaryFile(mode="w+") as log:
        process = subprocess.Popen(
            [str(binary), "--backend", "cpu", "--device", "/dev/video99999999", "--port", str(port)],
            cwd=root, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 20
            while True:
                require(process.poll() is None, "CPU YuNet startup should load and execute the checked-in model")
                try:
                    missing = state(base, validator)
                    if missing["source"]["status"] == "error":
                        break
                except (urllib.error.URLError, OSError):
                    pass
                require(time.monotonic() < deadline, "missing camera must be reported through the API")
                time.sleep(0.05)
            require(missing["source"]["error"], "missing camera needs a useful error")
            require(missing["persons"] == [] and missing["selected"]["person_id"] is None,
                    "camera failure must not invent tracks")
            require(missing["sequence"] == 0, "error updates must not pretend a frame was processed")
            request(base, "/healthz")
            time.sleep(0.15)
            require(state(base, validator)["sequence"] == 0, "polling and retries must not advance frame sequence")
            try:
                request(base, "/api/v1/frame.jpg")
                raise AssertionError("missing camera must not return a fabricated image")
            except urllib.error.HTTPError as error:
                require(error.code == 503, "no image should return 503")
            print("PASS CPU YuNet warmup, missing-camera reporting, stable sequence, and HTTP liveness")
        except Exception:
            log.seek(0)
            print(log.read())
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise AssertionError("camera retry loop did not stop cleanly")
        require(process.returncode == 0, "camera failure should still allow clean shutdown")


if __name__ == "__main__":
    main()
