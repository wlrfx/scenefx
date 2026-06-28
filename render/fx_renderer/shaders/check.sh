#!/bin/sh -eu
# Validate GLSL shaders via temp-file preprocessing.
cd "$(dirname "$0")"
exec python3 validate_shaders.py "$@"
