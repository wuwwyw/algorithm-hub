#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
ulimit -c 0

# Explicit path is supported; each machine otherwise uses its own project .env.
export ALGORITHM_CONFIG_FILE="${ALGORITHM_CONFIG_FILE:-$project_dir/.env}"
if [[ -n "${ALGORITHM_BINARY:-}" ]]; then
    server_binary="$ALGORITHM_BINARY"
elif [[ -x "$project_dir/build-cloud/algorithm_hub" ]]; then
    server_binary="$project_dir/build-cloud/algorithm_hub"
else
    server_binary="$project_dir/build/algorithm_hub"
fi
if [[ ! -x "$server_binary" ]]; then
    echo "Server executable not found; build the project first." >&2
    exit 1
fi
exec "$server_binary" "$@"
