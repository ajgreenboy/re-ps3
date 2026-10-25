# Third-party code

## MaXMB

https://github.com/MaxiBono/MaXMB, MIT License. Two pieces were translated to C++:

- `src/pad.cpp`: the DualShock 3, DualShock 4 and DualSense HID report layouts, the start-up feature reports and
  the controller watcher (from `src-tauri/src/hidpad.rs`).
- `src/shell.cpp` (`Shell::addInstalledApps`, `Shell::launch`): how RPCS3 is asked to start a title, by Title ID
  when its `games.yml` lists it, otherwise by `USRDIR/EBOOT.BIN` (from `src-tauri/src/ps3.rs`).

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
