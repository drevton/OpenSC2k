# Zstandard

Zstandard 1.5.7, https://github.com/facebook/zstd/tree/v1.5.7, under the BSD license in `LICENSE`.
The compressor and decoder are compiled into the game; no external Zstandard library is required at runtime.
Native city saves compress individual RIFF chunks. Asset packaging uses the external `zstd` command-line tool.

`zstd.c` was generated from that release in `build/single_file_libs` with:

```
python3 combine.py -r ../../lib -x legacy/zstd_legacy.h -k zstd.h -o zstd.c zstd-in.c
```

The generated `#include "../zstd.h"` was normalized to `#include "zstd.h"` for this directory.
The public `zstd.h` and `zstd_errors.h` come from the release's `lib` directory.
