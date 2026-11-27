#!/usr/bin/env bash
# Convenience launcher for the built binary (Linux / macOS).
# Build first: ./build.sh
set -e
cd "$(dirname "$0")"
exec ./bin/oly-persona "$@"
