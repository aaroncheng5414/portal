#!/usr/bin/env bash
set -euo pipefail

demo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

exec sudo docker run --rm --init \
  -p 127.0.0.1:5173:5173 \
  -v "${demo_dir}:/app:ro" \
  -w /app \
  node:22-bookworm-slim node server.mjs --host 0.0.0.0
