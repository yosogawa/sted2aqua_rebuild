# TinySoundFont (vendored)

Local copy of [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`) for planned runtime use and SF2 metadata tools.

- Upstream: https://github.com/schellingb/TinySoundFont
- License: MIT (see `LICENSE.txt` and the header comment in `tsf.h`)
- This tree is intended to be modified in-place; do not treat it as a git submodule.

Usage (when wiring up):

```c
#define TSF_IMPLEMENTATION
#include "tsf.h"
```

Define `TSF_IMPLEMENTATION` in exactly one translation unit.
