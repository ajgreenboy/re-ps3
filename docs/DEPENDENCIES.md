# Dependency checkpoint

Recorded October 23, 2026 for the first GitHub source upload.

| Dependency | Base revision | Local modifications |
|---|---|---|
| XenonRecomp (`hedge-dev/XenonRecomp`) | `ddd128bcca99fe8bfbb99bea583c972351fa6ace` | `xenon/xenonrecomp-current.patch` |
| RPCS3 (`RPCS3/rpcs3`) | `0b0fa082ae552ab77a70de4fc1f70110d165116b` | `rpcs3-current.patch` |

The current patches are complete tracked-file diffs against these revisions.
Apply each to a clean checkout of its matching base using `git apply`, then build
using the existing project scripts. Initialize the dependency's own submodules
as required. Historical narrower patches remain as investigation artifacts;
do not stack them on top of the complete current patches.

The XenonRecomp patch includes PS3 instruction semantics and PPC context changes.
The RPCS3 patch records local tracing/instrumentation used for reference evidence.
Their upstream projects retain their own licenses.

Other local prerequisites include Visual Studio 2022 C++ Build Tools, clang-cl/lld,
CMake/Ninja, Bun, a Ghidra/JDK installation, and local firmware 4.93 analysis inputs.
These tool installations and firmware inputs are not bundled. The snapshot does
not claim a clean-machine reproducible build; paths and analysis preparation are
documented in the existing handoff.
