#!/usr/bin/env fish
# SPDX-License-Identifier: MIT
# Configure, compile and run the test suite using the project's Release defaults.

set -l script_dir (dirname (status --current-filename))
set -l root (realpath "$script_dir/..")
set -l build_dir "$root/build"

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
or exit $status

cmake --build "$build_dir" -j(nproc)
or exit $status

ctest --test-dir "$build_dir" --output-on-failure
