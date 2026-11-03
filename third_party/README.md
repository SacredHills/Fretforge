# Third-party code

| Folder | Project | Version | Licence |
|---|---|---|---|
| `nam/NAM` | [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) by Steven Atkinson | commit 0b3d3c97 (2026-09-18) | MIT (`nam/LICENSE`) |
| `nam/nlohmann` | [nlohmann/json](https://github.com/nlohmann/json) single header, as shipped with NeuralAmpModelerCore | 3.x | MIT (header) |
| `eigen/Eigen` | [Eigen](https://eigen.tuxfamily.org) headers | 3.4.0 | MPL 2.0 (`eigen/COPYING`); built with `EIGEN_MPL2_ONLY` |

The sources are unmodified. They are vendored (instead of fetched at configure time) so that every
platform builds the exact same amp engine without network access.

`src/app/tone3000/Tone3000.cpp` follows the structure of TONE3000's JUCE example
([github.com/tone-3000/api](https://github.com/tone-3000/api), `juce/Source/T3K`, MIT licence, Copyright (c) 2026 TONE3000).
