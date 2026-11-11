#!/usr/bin/env bash
set -euo pipefail

camera_devices=()
for device in /dev/video*; do
  [[ -c "$device" ]] && camera_devices+=(--device "$device")
done

exec sudo docker run --rm --runtime=nvidia \
  -e NVIDIA_DISABLE_REQUIRE=true \
  "${camera_devices[@]}" \
  -p 127.0.0.1:8080:8080 \
  portal-cv "$@"
