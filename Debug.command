#!/bin/zsh
# Launch this checkout with construction debugging enabled.
set -eu
project_dir="${0:A:h}"
cd "$project_dir"
app="$project_dir/build/mac/Release/sc2k.app"
if [[ ! -x "$app/Contents/MacOS/sc2k" ]]; then
  python3 tools/dev.py build
fi
exec open -n "$app" --args --debug-city "$@"
