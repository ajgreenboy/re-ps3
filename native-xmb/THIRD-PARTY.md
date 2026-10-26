# Third-party code

## FreeType

The native text renderer links FreeType, using the installed system package or Qt SDK copy, under the
FreeType License: https://freetype.org/license.html . No FreeType source is copied into this repository.

Portions of this software are copyright © 2025 The FreeType Project (www.freetype.org). All rights reserved.

## MaXMB

https://github.com/MaxiBono/MaXMB, MIT License. Components translated or adapted to C++:

- `src/pad.cpp`: the DualShock 3, DualShock 4 and DualSense HID report layouts, the start-up feature reports and
  the controller watcher (from `src-tauri/src/hidpad.rs`).
- `src/shell.cpp` (`Shell::addInstalledApps`, `Shell::launch`): how RPCS3 is asked to start a title, by Title ID
  when its `games.yml` lists it, otherwise by `USRDIR/EBOOT.BIN` (from `src-tauri/src/ps3.rs`).
- `src/p3t.cpp`: the PS3 theme (`.p3t`) reader: the element tree, packed zlib files, GIM images and VAG sounds
  (from `src-tauri/src/p3t.rs`); which icons a theme replaces follows `src/js/themes.js`. MaXMB credits
  DeckyXMB's `p3t.py` (Callum Bell, BSD 3-Clause) as the origin of its reader.
- `src/interaction_audio.cpp` / `src/shell.cpp`: theme sound-role mapping and per-role fallback from
  `src/js/xmb.js` `_soundFiles` / `_applyThemeSounds` and `src/js/audio.js` `setThemeSounds`;
  native PCM validation, resampling and output use the existing C++ audio service.
- `src/playback_queue.cpp` / shell playback lifecycle: shuffle cycles and bounded failed-file skipping adapt
  `src/js/audio.js`'s queue methods. `src/photo_viewport.cpp` adapts fit/fill zoom stops, pan bounds and step
  from `src/js/photoviewer.js`; native ownership, rendering and player integration are implemented in C++.

```
MIT License

Copyright (c) 2026 Maxi Bono

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## DeckyXMB

https://github.com/Callum-Bell/DeckyXMB (Callum Bell), BSD 3-Clause License. MaXMB's `.p3t` reader, which
`src/p3t.cpp` translates, is adapted from its `py_modules/p3t.py`.

```
Copyright (c) 2026

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## linkev/PlayStation-3-XMB

https://github.com/linkev/PlayStation-3-XMB, MIT License. `src/particles.cpp` is its particle layer
(`ps3xmbwave/particles.js` and the defaults of `ps3xmbwave/particles-settings.js`): the point-sprite programs, translated
from GLSL ES 3.00 to GLSL 3.30, and the per-particle seeds. Sizes are scaled from 1080p pixels to the window.

```
MIT License

Copyright (c) 2025 Mart

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Optional Steam client interface headers

The read-only client helper compiles against external Valve Steamworks public headers and loads the user's installed Steam client DLL. Neither the headers nor Steam DLLs are bundled in this repository. The verified local build used the SDK 1.54 interface declarations published in [ValveSoftware/Proton](https://github.com/ValveSoftware/Proton/tree/proton_10.0/lsteamclient/steamworks_sdk_154). The headers retain Valve's copyright and applicable SDK terms; use an appropriately obtained SDK for distribution builds.
