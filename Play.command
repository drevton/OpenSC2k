#!/bin/zsh
# Launch the normal native app, preserving independently updated graphics.
set -eu
project_dir="${0:A:h}"
cd "$project_dir"
native_app="$project_dir/build/mac/Release/sc2k.app"
if [[ ! -x "$native_app/Contents/MacOS/sc2k" ]]; then
  python3 tools/dev.py build
fi
# Keep an existing instance available so its unsaved city is not discarded.
open -n "$native_app"
