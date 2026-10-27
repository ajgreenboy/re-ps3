# XMB particles: firmware implementation and Phase 5 port groundwork

Status: analysis complete enough to start a port; the runtime facts in section 6 (particle capacity, blend
factors, emission origin) still need one capture to close. Companion to `PIPELINE.md` (layer table row
"Particles"), `VIDEO-REFERENCES.md` (prior, now-superseded CPU reconstruction) and `THIRD-PARTY.md` (the linkev
sparkle layer that is in `src/particles.cpp` today).

Everything below is read from local artifacts only:

| Artifact | Path | Size / identity |
|---|---|---|
| Program pair `particles_quads` | `out/native-reference/lines/lib/particles/particles_quads.vpo` / `.fpo` | 5296 / 3584 bytes on disk; ucode 2160 / 1968 bytes |
| Program pair `particles_second` | `out/native-reference/lines/lib/particles/particles_second.vpo` / `.fpo` | 5376 / 3152 bytes; ucode 2240 / 1552 bytes |
| Debug pair | `.../particles_quads_debug.vpo` / `.fpo` | ucode 1760 / 672 bytes (not drawn in the XMB) |
| Cg->GLSL translation | `out/native-reference/lines/glsl/particles_{quads,second}.{vpo,fpo}.glsl` | already generated; reuse these |
| SPU simulation | `out/native-reference/lines/spurs/particles/particles/particles.elf` | 34484 bytes; module name `particles.sysmodule` |
| Settings | `out/native-reference/lines/PARTICLES.mnu`, `PARTICLES_SPE.mnu`, `PARTICLES_UI.mnu` | 54 / 7 / 16 keys |
| Theme presets | `out/native-reference/lines/override/<name>/PARTICLES.mnu` | 17 override sets; 12 carry a PARTICLES.mnu |
| Textures | `particles/proc_iridescent.tga` inside `lines.qrc` (the plugin path at `custom_render_all.c:42258`) | not extracted; load via `assets_.qglFile("lines", ...)` |
| PPU driver | `out/native-reference/lines/custom_render_all.c` (custom_render_plugin.prx, 915704 bytes) | decompile |

Reproduce the SPU analysis with:

```
bun work/spu/disasm-elf.ts out/native-reference/lines/spurs/particles/particles/particles.elf work/particles-elf-disasm.txt
bun work/elf-sections.ts out/native-reference/lines/spurs/particles/particles/particles.elf
```

Evidence tags used below: **[E]** = directly read from a file/offset; **[D]** = arithmetic on [E]; **[I]** =
interpretation that still needs a capture; **[U]** = unknown, with the procedure that closes it.

---

## 1. What the firmware particle system is, in one paragraph

Two forward-rendered passes of camera-facing quads, not point sprites. `particles_quads` draws the body of each
particle: a quad oriented by a per-particle quaternion, sized by a depth-of-field curve, shaded by one iridescent
texture and one spot light (lambert + specular), cut to a soft disc, and faded by a per-vertex life alpha. A second
pass, `particles_second`, redraws the same particles as large radial glints whose energy is driven by the glare
amount and is drawn into the HDR glare source so the mip blur turns it into bloom. The motion is not in the shaders
at all: an SPU task (`particles.elf`, 78xx instructions) spawns, ages and integrates a capped pool of particles,
and writes per-frame vertex data (position, corner uv, orientation quaternion, life alpha). All physics numbers
come from `PARTICLES.mnu`; all look numbers come from `PARTICLES.mnu` plus two code-set uniforms (`_Color`,
`_Gamma`). The normal XMB theme presets (day / night / yoake / higure) change only the view-dependent parameters;
the physics block is byte-identical across them.

---

## 2. Render side

### 2.1 Two passes, and the program that feeds glare

- Pass 1 `lib/particles/particles_quads.vpo` + `.fpo` - the particle bodies.
- Pass 2 `lib/particles/particles_second.vpo` + `.fpo` - the glints / bloom seeds. Same vertex layout and
  uniform set, plus `_Glare`, minus `_NearControl` / bounds of the first pass. **[E]**
  `cg-programs.txt:462-478` (second fpo), `cg-programs.txt:559-583` (second vpo) and
  `out/native-reference/lines/glsl/particles_second.fpo.glsl:10`.
- Both are loaded in one place: `FUN_0005f9d0` at `custom_render_all.c:42891` binds
  `lib/particles/particles_quads.vpo/.fpo`, the debug pair, and `lib/particles/particles_second.vpo/.fpo`
  (`custom_render_all.c:42911-42941`).
- The second pass is additive to the glare source, not the frame. The plugin's glare source is the
  `GlareSource*`/`AccGlare` chain described in `PIPELINE.md`; the previous port already records that the glints
  "draw into the HDR glare source, so the mip blur turns it into soft bloom" (`VIDEO-REFERENCES.md:309-314`).
  **[E]** (for the program), **[I]** (which target): the draw target id is chosen in qgl's pass setup, which the
  decompile flattens.

### 2.2 Vertex format - established

From the vpo parameter table (`out/native-reference/lines/cg-programs.txt:480-484`):

```
vertex: 135 instructions, slot 0, 10 regs, inputs 0x901, outputs 0x2f4000
  in  float4  IN.pos   :POSITION
  in  float2  IN.uv0   :TEXCOORD0
  in  float4  IN.rot   :TEXCOORD3
```

`inputs 0x901` = bits 0, 8, 11 set. On RSX the attribute slots are POSITION=0, TEXCOORD0=8, TEXCOORD3=11, so the
three declarations line up exactly with the mask. **[E]**

Vertex contract (what a porter must produce per vertex):

| Attribute | GLSL location | Type | Meaning |
|---|---|---|---|
| position | 0 | float4 | particle centre in world units; `w` is consumed as the per-vertex life alpha (`glsl/particles_quads.vpo.glsl:199`) |
| uv0 | 8 | float2 | quad corner coordinate. The shader centres it (`in_8.xy - vc[455].xx`) and builds the corner offset from it, so it is (0,0)..(1,1) - a billboard corner, not an atlas uv |
| rot | 11 | float4 | per-particle orientation quaternion; normalised in the shader (`dot(in_11,in_11)`, line 65) |

Minimum stride 16 + 8 + 16 = 40 bytes. The SPU kernel walks 48-byte records (`particles.elf` 0x7264:
`ai $86, $86, 48`), so the natural padded stride is **48 bytes** (float4 pos, float2 uv + 8 pad, float4 rot).
**[D]/[I]** - the 48-byte per-record stride is [E]; that it is the vertex stride is inferred from it landing on
16-byte alignment.

Quad expansion: the vertex shader receives the centre once per vertex and derives the corner from uv0 and the
quaternion, so 4 vertices per particle (strip 0,1,2,3 or 2 triangles) with the same pos/rot and uvs
(0,0),(1,0),(0,1),(1,1). **[I]** from the shader body; no index buffer was captured.

Vertex-shader output varyings (mask `0x2f4000`, so the fragment stage receives these):

| Varying | Register | Content | Used by fpo as |
|---|---|---|---|
| main.uv0 | TEXCOORD0 | 0..1 corner | `IN.uv0` - disc coordinate |
| main.uv2 | TEXCOORD2 | life / bounds term | `IN.uv2` |
| main.uv3 | TEXCOORD3 | the alpha chain result (`o[10]`) | `IN.uv3.w` multiplies the output |
| main.normal | TEXCOORD4 | quad normal | `IN.normal` (lambert) |
| main.pos | TEXCOORD5 | world position | `IN.pos` (spot lighting) |
| main.normal_v | TEXCOORD7 | view-space normal | `IN.normal_v` (iridescence lookup) |

### 2.3 Uniforms and where they live

Vertex constants (`res=2178`), from `cg-programs.txt:485-516` and `glsl/particles_quads.vpo.glsl:2-39`:

| Name | vc index | Type | Set by (`custom_render_all.c`) |
|---|---|---|---|
| `_ModelviewProjection` | 256-259 | mat4 | camera, `FUN_0008a128` at draw |
| `_Modelview` | 260-263 | mat4 | camera |
| `_LightPack[4]` | 264-267 | vec4 x4 | spot light + exposure, from PARTICLES.mnu |
| `_ParticleSize` | 467 | float3 | size near/middle/far |
| `_Transparency` | 466 | float2 | (fresnel, GLOBAL ALPHA) |
| `_Focus` | 465 | float4 | depth-of-field near/far |
| `_FocusCurves` | 464 | float3 | DoF powers / SIZE ALIGN |
| `_FrontFacingQuaternion` | 463 | float4 | near particles face the camera |
| `_NearControl` | 462 | float4 | NEAR ALIGN, SIZE ALIGN, NEAR FUZZINESS |
| `_LifeBoundsMax` | 460 | float3 | life-bounds box |
| `_LifeBoundsMin` | 461 | float3 | life-bounds box |
| `_Darkness` | 459 | float2 | NEAR/FAR DARKNESS |
| `_Glare` | 459 (2nd pass) | vec4 | GLARE, GLARE SCALE, GLARE P1/P2 |

Baked internal constants (overwritten nowhere; they are literal ucode constants) **[E]**:

| vc | particles_quads | particles_second |
|---|---|---|
| 455 | (0.5, 0.6, 5, 3) | (1.5708, 0.6, 5, 3) |
| 456 | (2, 0.4, 0.2, 0) | (0.121239, 0.195636, 0.332995, 0.999996) |
| 457 | (0.195636, 0.332995, 0.999996, 1.5708) | (0.4, 0.2, -0.0134805, 0.0574773) |
| 458 | (1, -0.0134805, 0.0574773, 0.121239) | (2, 1, 0, 0.5) |

Fragment constants **[E]** (`glsl/particles_quads.fpo.glsl:19-54`): `fc[5]=0.5, fc[7]=1, fc[8]=1, fc[9]=1,
fc[14]=-0.5, fc[15]=(0.6345,0,0.05,0), fc[17]=0.5, fc[19]=-0.85, fc[20]=0.85, fc[21]=0.5, fc[22]=3, fc[23]=1,
fc[27]=1.4427, fc[31..33]=1.4427`. `fc[27]/fc[31..33]` are log2(e): the program converts `log2` to `ln` to build
`1 - e^(-k*x)` exposure.

Fragment uniforms and the constant slots they occupy (`custom_render_all.c:43115-43198` registers them;
`slots=` in `cg-programs.txt:435-445` gives the packing):

| Uniform | slots (quads) |
|---|---|
| `_LightPack[0]` | 29, 28, 2, 1 |
| `_LightPack[1]` | 26, 24, 10, 4 |
| `_LightPack[2]` | 30, 25, 3, 0 |
| `_LightPack[3]` | 16, 6 |
| `_NearControl` | 18 |
| `_Color` | 34 |
| `_IridescentExp` | 13, 12, 11 |
| `_Gamma` | 35 |

`_LightPack` is packed by column (the previous port's reading, consistent with every shader use and the vpo):
x = eye, y = spot position / SPECULAR POWER, z = attenuation coefficient, w = LAMBERT COEFF / SPECULAR COEFF /
EXPOSURE / COLOR CONTROL. `_Color` (vec3) and `_Gamma` (float) have no `PARTICLES.mnu` key; they are set by code
(`_Gamma` at fpo field +0xb0 via `FUN_0005c200`, `custom_render_all.c:40860`). Value unknown - see section 6.

### 2.4 Draw path, material state and the disc cut

The particle draw is `FUN_0005c254` (`custom_render_all.c:40817-40891`). It binds, in order: the two material
objects (`FUN_00040c24(*param_2 + 0x30, 3, 0)` and `FUN_00040c24(*param_2 + 0x90, 0, 0)`), the camera matrices
(`FUN_0008a128(..., +0xf0)` and `+0x180`), the DoF / bounds / light uniforms
(`FUN_0004c838` on +0x210, +0x270, +0x2a0, +0x2d0, +0x240, +0x360; `FUN_000409ec` on +0x330 and +0x1e0;
`FUN_0005bec0` on +0x150, +0x2d0, +0x300, +0x240), then the fragment-program uniforms on `*param_3`
(`FUN_0005c200(+0xb0)`, `FUN_00048cc0(+0xe0, +0x190)`, `FUN_00040aa4(+0x100, +0x160)`) and draws with
`FUN_0008af98` / `FUN_00089e18`. **[E]**

Alpha test / disc: `custom_render_all.c:40874-40876` calls the import stub `FUN_00086fb8` (defined at 0x86fb8,
line 59644) with a byte operand, immediately before the draw. That stub is an RSX state setter; the shader has
`kill 0` (`cg-programs.txt:435`), i.e. the disc is **not** discarded in the fragment program - the edge comes
from the radial alpha term and the hardware test. **[E]** that a test is set; exact reference value not extracted.

Blending: the plugin sets blend through the import stub `FUN_00086ff0` (`FUN_00040c80` wrapper at line 24065)
with the argument pair used elsewhere in the scene as `(0x302, 0x303, 0x302, 0x303)`
(`custom_render_all.c:6108`, `47772`), i.e. SRC_ALPHA / ONE_MINUS_SRC_ALPHA. The **particle draw does not call
`FUN_00040c80`**; it selects a qgl material blend mode `3` (`FUN_00040c24(..., 3, 0)`). The mapping from qgl
material mode 3 to RSX factors is not in the decompile. **[U]** - see section 6, item B1.

### 2.5 Alpha chain (derived from the vpo, authoritative)

`glsl/particles_quads.vpo.glsl:186-203`:

```
fresnel   = 1 - abs(dot(view_dir, normal))                 # line 188
T         = clamp(1 - fresnel^_Transparency.x, 0, 1)       # lines 192-196 (log2/exp2)
alpha     = T * _Transparency.y                            # line 198: GLOBAL ALPHA
          * in_0.w                                         # line 199: per-vertex life
          * f_bounds                                       # line 200 (life-bounds box)
          * f_darkness                                     # line 201 (_Darkness)
          * f_focus                                        # line 202 (DoF / near-far)
o[10].w   = alpha * f_extra                                # line 203
```

`_Transparency = (PARTICLES.mnu FRESNEL, GLOBAL ALPHA)` - confirmed by `PIPELINE.md:225` ("PARTICLES
`_Transparency.y` (GLOBAL ALPHA) stays 0 until NormalBG2"). So the shader multiplies four independent fades:
fresnel edge, global alpha (theme/boot), the SPU-driven per-particle life, and the depth-of-field / bounds
darkening. `_Transparency.x = 1.33319` for every standard preset; `_Transparency.y = 1` for day/night/yoake/
higure/welcome/music, `0` for black/coldboot1/coldboot2. **[E]**

### 2.6 Fragment shader: what it computes

The translated GLSL is complete and runnable; do not retranslate. Structure of
`glsl/particles_quads.fpo.glsl` (123 ucode instructions, 97 in `particles_second`):

1. `IN.normal_v * 0.5 + 0.5` -> `texture(_IridescentTex).xyzw` (the iridescence lookup; `proc_iridescent.tga`).
2. `_IridescentExp` applies three channel exponents (`(t+1)^exp`, built as `exp2(exp*log2(t+1))`), lines
   84-120. The exact per-channel use is best read off the GLSL rather than re-derived here. **[I]**
3. Spot light from `_LightPack`: attenuation `c + l*d + q*d^2`, lambert `max(N.L,0)`, specular
   `pow(max(N.H,0), specular power)`; the eye is `_LightPack[0..2].x`. Lines 85-121.
4. Exposure as `1 - e^(-EXPOSURE * light)` (the `1.4427` constants are log2(e)) - line 122-153.
5. Disc mask from `IN.uv0` (centred, `_NearControl` and the `0.6345/0.05` constants shape the soft edge) and the
   `-0.85/0.85/0.5/3` constants - lines 109-127. This is the "blur" edge; alpha test (2.4) hard-cuts the outside.
6. Output: `rgb = _Color * lit * _Gamma`, `a = life_alpha * exposure * disc` - lines 148-153.

`particles_second.fpo` reduces the chain to a radial halo built from the centred uv: the shader takes
`r = clamp(|uv-0.5|*2, ...)`, raises it to `_Glare.z` (GLARE P1, 0.99), negates and multiplies by `_Glare.w`
(GLARE P2, 4.44) and exponentiates, then scales by `_Glare.x` (GLARE, 0.16-0.20). That is
`intensity = GLARE * e^(-P2 * r^P1)`; it is drawn into the glare source. **[E]** for the constant use
(`glsl/particles_second.fpo.glsl:63-128`, `fc[13]/fc[16]/fc[27]` are `_Glare.z/.y/.x`), **[I]** for the
`_Glare` component naming (from the key order and the prior port's fit).

---

## 3. Simulation side

### 3.1 Identity and wiring

`particles.elf` ELF header **[E]**: `e_entry = 0x3070`, `.SpuGUID@0x3000`, `.init@0x3010+0x50`,
`.text@0x3060+0x7930`, `.rodata@0xa9f0+0x4b0`, `.bss@0xaf80+0x2520`; `.note.spu_name` = `particles.sysmodule`.
The PPU creates the SPURS task in `FUN_00062cf0` (`custom_render_all.c:44782-44808`) with
`FUN_000078f4(auStack_68, "particles/particles")` and **0x3d400 bytes of local store**
(`custom_render_all.c:44800-44801`). Per-frame job arguments are 7 u64 = **56 bytes** per record
(`FUN_00060e54`, `custom_render_all.c:43487-43507`) - the SPU task main walks 56-byte work items
(`particles.elf` 0x858c: `ai $88, $88, 56`). So the SPU task is dispatched with 7-word descriptors
(pointers, sizes, flags) and DMAs its own input/output via MFC channel writes
(`wrch ch16..ch21` at 0x84cc-0x857c of the task main, `rdch ch24`).

Two driver paths exist and the plugin logs which one ran **[E]** (`FUN_00060380`, `custom_render_all.c:43202`):
- `"spu, parallel"` (`FUN_00062464`, line 44387) - the normal path: PPU builds commands, SPU does update + write;
- `"ppu, no spu manager"` (line 43224) - the fallback: PPU simulates.

The debug HUD exposes `_capacity`, `_alive_particles_count`, `_real_va_size` and timings for commands / update /
write (lines 43214-43241). This is the cheapest capture handle for the open capacity questions.

### 3.2 Kernel shape

The disassembler labels only BRSL targets, so find true function starts by the SPU prologue
`stqd $0, 16, $1`. In the compute region (0x6ed0-0x80e8) the starts are 0x6f34, 0x73a4, 0x743c, 0x7634, 0x77ac,
0x7984, 0x7ae4, 0x7c44, 0x7cfc, 0x7ef4, 0x8014. **[E]**

The main update kernel is **0x6f34**:

- Reads its parameter/state block through the address in LS `0xBB00` (`lqr $87, 47872` at 0x6ed8), fields at
  +0x890, +0x8A0, +0x8B0 (physics constants), and uses RNG state at LS 0xBB20/0xBB24/0xBB28.
- Calls the RNG wrapper at 0x33d8 (0x6fe4); that wrapper implements Park-Miller: `seed = seed*16807 mod
  (2^31-1)` (`il $13, 16807` + `mpyu/mpyh` at 0x30e8-0x3110). **[E]**
- Calls the sin/cos wrappers at 0x4590 and 0x4668 (which call 0x44d0 and 0x45b0; those embed the classic
  polynomials - `0.6366197` = 2/pi, `1.570796` = pi/2, `-0.1666665` = -1/6), and a four-way vector helper at
  0x3bf0. **[E]**
- Processes **64 records of 48 bytes** per pass: `ceqi $91, 64` at 0x7254, `ai $86, $86, 48` at 0x7264. **[E]**
- Bakes the trigonometry constants `6.283185` (2*pi) at 0x70a4-0x70b4, `0.37` (0x3ebd70a3, materialised
  0x70a8-0x70b0) applied 0x70b8, `0.17` (0x3e2e147a, 0x70c8) applied 0x70cc, `0.31` (0x3e9eb851, 0x70dc) applied
  0x70e0, and `0.01666667` (1/60 = 0x3c888888, materialised 0x70f0-0x7100) used 0x7160-0x7170. **[E]**
- Life envelope at 0x71e0-0x7228: the constants are `0.94` (0x3f70a3d7, materialised 0x71ec-0x71f4), `0.02`
  (0x3ca3d70a, 0x71e8-0x71f0) and `-16.66666` (0xc1855554, 0x71fc-0x7208). The branchless form computes
  `f = 1 - max(age-0.94, 0)/0.06` (clamped at 0), i.e. a linear fade over the last 6% of life, and the `0.02`
  term is the matching start-of-life clamp; the result scales a 1/60 term. **[D]/[I]** (from the `fcgt`/`selb`/
  `fma` sequence).

Output region per work item: the task main advances the output pointer by **0x6000 = 24576 bytes** per item
(`particles.elf` 0x8314 `il $49, 24576`; 0x8318 `a $82,$82,$49`). At 48 bytes/record that is 512 records;
at 4 vertices/particle that is 128 particles per work item. Both decompositions are consistent with
capacity 1536 / 12 items (see 3.5). **[D]/[I]**

### 3.3 Physics model

All of this is data, not extrapolation: `PARTICLES.mnu` is the firmware's own parameter block, and the preset
copies prove the values are the ones used. The formulas are the semantic reading of the key names plus the SPU
evidence in 3.2.

| Key | Value (standard) | Meaning / formula |
|---|---|---|
| EMIT PER FRAME | 16.6539 | candidate spawns per frame |
| EMIT PROB | 0.479115 | per-candidate accept probability -> **7.979 spawns/frame** |
| EMIT VEL MIN | 0.15064 | speed floor |
| EMIT VEL MUL | 0.19 | speed multiplier |
| EMIT VEL VAR | 0.282567 | speed spread |
| EMIT CONE ANGLE | 51.8695 | emission cone half-angle, degrees |
| EMIT NEG PROB | 0.173899 | 17.39% of spawns go the opposite way |
| EMIT VEL ZSCALE | 0 | emission flattened in z (z velocity component 0) |
| AGING SPEED | 0.00285223 | age increment per step |
| AGING VARIANCE | 0.493003 | per-particle lifetime spread |
| FRICTION | 0.030551 | per-step velocity drag |
| GRAVITY | -6.8e-05 | constant acceleration (y) |
| BROWNIAN SCALE | 0.225311 | random velocity kick per step |
| WIND DIR x/y/z | 0.340188 / 0 / 0.35 | wind direction (normalised xz) |
| WIND SCALE | 0 | wind off in the normal preset |
| SPIN TIME SCALE | 2.74 | quaternion spin rate (per unit time) |
| DELTA TIME | 0.0088883 | simulation step, seconds |
| GLOBAL ALPHA | 1 | `_Transparency.y` |
| FRESNEL | 1.33319 | `_Transparency.x` |
| SPOT POS | (4.16, 2.63, -7.6) | light position |
| SPOT ATTN | (1, 0, 0) | attenuation c + l d + q d^2 -> constant |
| SPECULAR POWER / COEFF | 35.2904 / 74.74 | spot highlight |
| LAMBERT COEFF | 8 | spot diffuse |
| EXPOSURE | 0.0390458 | `1 - e^(-k light)` |
| COLOR CONTROL | 1 | `_LightPack.w` |
| IRIDESCENT EXP | 1 | `_IridescentExp` |
| SIZE NEAR / MIDDLE / FAR | 0.0772033 / 0.0536233 / 0.874062 | `_ParticleSize` (DoF bands) |
| NEAR/FAR FOCUS, FOCUS_DIST, FOCUS_POW | 6.12435 / 1.3193 / 2.47206, 12.8327 / 3.54129 / 1.83324 | `_Focus` / `_FocusCurves` |
| NEAR/FAR DARKNESS | 6.24028 / 13.9 | `_Darkness` |
| NEAR ALIGN, SIZE ALIGN, NEAR FUZZINESS | 0.722 / 13.19 / 3.79 | `_NearControl` |
| GLARE / SCALE / P1 / P2 | 0.159705 / 5.44386 / 0.99 / 4.44 | `_Glare` (second pass) |

**Lifetime, and the self-consistency check** **[D]**:

```
lifetime_steps = 1 / AGING_SPEED                 = 1/0.00285223 = 350.6 steps
lifetime_s     = 350.6 * DELTA_TIME              = 350.6 * 0.0088883 = 3.116 s
per-particle   = 1/(AGING_SPEED*(1 +/- VARIANCE)) -> 234.8 .. 691.6 steps = 2.09 .. 6.15 s
spawn rate     = EMIT_PER_FRAME * EMIT_PROB * 60  = 7.979 * 60 = 478.7 /s
steady live    = 478.7 * 3.116                   = 1492 particles
```

1492 is 97% of 1536 (3.5), which is the expected occupancy for a pool sized just above steady state. This
cross-check is why the capacity reading below is treated as confirmed-by-arithmetic, not just a guess.

Velocity **[D]/[I]**: the prior CPU attempt read the emission as `speed = EMIT_VEL_MIN + EMIT_VEL_MUL *
(1 +/- EMIT_VEL_VAR)`, giving 0.287 .. 0.394 units per step, in a cone of 51.87 deg around an axis, with 17.4%
of spawns mirrored. That reading fits the key names but was not re-verified against the SPU; treat it as the
starting formula and fit it against a capture.

Acceleration: gravity is tiny (over a 3.1 s life a 6.8e-5 constant adds < 1e-3 units/s), friction and Brownian
dominate. Whether FRICTION is applied as `v *= 1 - f*dt` or `v *= 1 - f` per step is **[U]**; the two differ by
orders of magnitude over 350 steps (0.91x vs 1.6e-5x) and a 2-second capture of a single particle decides it.

### 3.4 Presets - the physics is theme-independent

Seventeen override sets exist, but only twelve carry a `PARTICLES.mnu`: black, coldboot1, coldboot2, day,
gameboot3, gameboot4, higure, music_1, night, welcome_1, welcome_2, yoake. `bright`, `gameboot1`, `gameboot2`,
`gameboot5` and `initial_setting` have only BACKGROUND / HDR / LINE1 and therefore inherit the base
`PARTICLES.mnu` unchanged. **[E]** (directory listing under `out/native-reference/lines/override/`).

Exact diff of every preset against the base `PARTICLES.mnu` (base: SIZE MIDDLE 0.0536233, FAR FOCUS 12.8327,
FAR FOCUS_DIST 3.54129, GLARE 0.159705, EMIT VEL MUL 0.19, GLOBAL ALPHA 1):

| Preset | Keys that differ |
|---|---|
| day | SIZE MIDDLE 0.0464771, FAR FOCUS 12.2008, FAR FOCUS_DIST 4.02735, GLARE 0.201367 |
| night | FAR FOCUS_DIST 4.09678 (only) |
| yoake | FAR FOCUS 12.0064, FAR FOCUS_DIST 4.02735, GLARE 0.180536 |
| higure | SIZE MIDDLE 0.0464771, FAR FOCUS 12.6869, FAR FOCUS_DIST 4.02735, GLARE 0.18748 |
| music_1 | SIZE MIDDLE 0.0464771, FAR FOCUS 12.0064, GLARE 0.201367 |
| black | GLOBAL ALPHA 0, plus the music_1 look set (SIZE MIDDLE 0.0464771, FAR FOCUS 12.0064, GLARE 0.201367) |
| coldboot1, coldboot2 | GLOBAL ALPHA 0, EMIT VEL MUL 3.7496, plus the music_1 look set |
| gameboot3 | physics: EMIT VEL MIN 0.029756, EMIT VEL MUL 1.52761, AGING SPEED 0.0034476, AGING VARIANCE 0, DELTA TIME 0.08, BROWNIAN 0.0556335, WIND SCALE 0.000152351, WIND DIR X -5.34e-05. look: SIZE MIDDLE 0.073865, SIZE NEAR 0.186308, SIZE FAR 0.0389094, NEAR/FAR FOCUS 7 / 9.81914, NEAR/FAR FOCUS_DIST 2.4303 / 0.138874, NEAR/FAR FOCUS_POW 3.05 / 1.72, NEAR DARKNESS 6.63954, GLARE 0.423566 |
| gameboot4 | gameboot3 plus GLOBAL ALPHA 0 |
| welcome_1, welcome_2 | physics: EMIT PER FRAME 70.5765, EMIT PROB 0.493003, EMIT VEL MIN 0.131537, EMIT VEL MUL 4.16622, EMIT VEL ZSCALE 0.208311, AGING SPEED 0.00101112, AGING VARIANCE 0.5, FRICTION 0.0619405, DELTA TIME 0.0172207, BROWNIAN 0.0794061, SPIN 2.99327, WIND X/Y (0.361019, -0.0347719), WIND SCALE 0.000544572. look: SIZE 0.073865 / 0.0143576 / 0.00746469, NEAR ALIGN 0.118043, SIZE ALIGN 14.1652, FOCUS 7 / 6.31877, FOCUS_DIST 1.59705 / 6.31877, FOCUS_POW 2.22209 / 1.63882, DARKNESS 5.49606 / 4.46526, SPECULAR POWER 29.3486, GLARE 0.222198 / SCALE 6.22156 / P1 1.6344 / P2 8.41834 |

**Conclusion:** for the four ordinary XMB themes (day / night / yoake / higure) and for music playback the
physics block is byte-identical to the base file; only glare, DoF and size-middle move. Particle *motion* does
not depend on clock time, month, theme colour or the ribbon sheet. This removes any reason to couple emission to
`ribbon.cpp` - and explains why the earlier CPU port's ribbon-anchored motion (`VIDEO-REFERENCES.md:320-329`)
felt wrong.

### 3.5 Capacity

`FUN_0005cd14` (`custom_render_all.c:41086-41105`) initialises the particle bank and reserves
`FUN_0005ac50(param_1 + 0xa8, 0x600)` - a std::vector reserve of **0x600 = 1536 elements**. **[E]** for the
call and the constant; the debug HUD reads `_capacity` from that object (`FUN_0005c988`,
`custom_render_all.c:40957-40966`, capacity = `*(*(param_1+0xa4)+0x18)`), and the arithmetic in 3.3 lands at
1492 live for a 1536 pool. **[D]**

Caveat: a second container in the same class reserves 0x200 = 512 (`FUN_0005d444`,
`custom_render_all.c:41431`), and the 0x600 vector is 4-byte elements, so "1536 particles" is an
interpretation of which array is the pool. Verify by reading `_capacity` from the debug HUD or from the draw's
vertex count. **[U]/[I]** - section 6, item A1.

### 3.6 What motion depends on

| Hypothesis | Verdict | Evidence |
|---|---|---|
| elapsed seconds | **yes** - the simulation integrates per step; spin uses accumulated time; the vpo/fpo have no time uniform, so all motion is baked into the per-frame vertex data | vpo uniform list has no time (`cg-programs.txt:485-516`); kernel uses DELTA TIME |
| clock time / day-night | **no** for motion; yes for look (GLARE, FAR FOCUS, SIZE MIDDLE) | preset diff table 3.4 |
| ribbon sheet geometry | **no evidence of coupling**; the prior CPU port tied particles to the ribbon arc and was rejected for it (`PIPELINE.md:18`, `VIDEO-REFERENCES.md:320-329`) | PARTICLES.mnu has no ribbon keys; theme presets that change LINE1 do not change particle physics; the plugin's particle state block (LS 0xBB00) fields are physics + camera, not a ribbon vertex stream |
| category/input wind (`PARTICLES_UI.mnu`) | **yes** for interaction gusts only: ICON WIND 11.3877, DPAD SCALE X 1, DSHAKE/RSHAKE coefficients | `PARTICLES_UI.mnu`; this is the interaction layer, a separate input to the same pool |
| emission origin | **unknown** - could be a fixed 3D volume, the wave's horizon line, or a camera-relative band. No emitter geometry asset exists, so it is computed. See 6, A2 | - |

---

## 4. Comparison with what `src/particles.cpp` does today

Today's layer is linkev/PlayStation-3-XMB's sparkle layer: 2000 `GL_POINTS`, one GLSL 3.30 program, additive
(`glBlendFunc(GL_ONE, GL_ONE)`), a per-seed closed form and a Gaussian-ish dot (`particles.cpp:11-47,74-85`).
It is drawn **after** `Hdr::finish` (`view.cpp:326-328`), with opacity from the cold-boot GLOBAL ALPHA weight
(`view.cpp:563`). It never touches `particles_quads` / `particles_second`, `particles.elf`, or `PARTICLES.mnu`.

| # | Behaviour | Firmware | Today (`src/particles.cpp`) | Gap |
|---|---|---|---|---|
| 1 | Primitive | 4-vertex camera-facing quads, per-particle orientation quaternion | GL point sprites | geometry |
| 2 | Count | pool 1536, ~1492 live | 2000 fixed | similar order, different basis |
| 3 | Spawning | stochastic: 16.6539 x 0.479115 per frame, cone 51.87 deg, 17.4% mirrored, speed MIN+MUL(1+/-VAR) | none - seeds are static, nothing respawns | whole system |
| 4 | Lifetime | 350.6 steps x 0.0088883 s = 3.12 s mean, per-particle variance 0.493, linear fade over last 6% of life | none - lifetime is a phase term `fract(aSeed.x + time*0.00285)` inside one formula; no death/fade envelope | whole system |
| 5 | Velocity / accel | FRICTION 0.030551, GRAVITY -6.8e-5, BROWNIAN 0.225311, WIND (off), spin 2.74 | closed form `sin`/`fract` of seed and time; no velocity, no forces | whole system |
| 6 | Motion source | per-step integration; no clock/theme/ribbon coupling | elapsed seconds x screen-aspect `ratio` (`particles.cpp:22-34,79`) | clock/aspect dependence removed in firmware |
| 7 | Vertex program | `particles_quads.vpo` + `particles_second.vpo` (firmware ucode) | linkev point-sprite GLSL | shader |
| 8 | Fragment program | iridescent texture x spot lambert/specular x `1-e^-k` exposure x disc mask | radial `(1-d)^2` white dot | shading |
| 9 | Colour | `_Color` x lit x `_Gamma`; iridescence from `proc_iridescent.tga`; light is PARTICLES.mnu spot | `vec3(a)` (white, alpha in rgb) | colour model |
| 10 | Alpha chain | fresnel^1.33319 x GLOBAL ALPHA x life x life-bounds x darkness x DoF | `opVar^2 * (1 - fract(...)) * 0.75` | alpha model |
| 11 | Blending | qgl material mode 3 + HW alpha test (factors unresolved, section 6 B1) | `GL_ONE, GL_ONE` additive | blend |
| 12 | Second pass | `particles_second` glints feeding the HDR glare source | none | missing pass |
| 13 | Compositing | into the HDR scene, before tone map + glare, so particles are tone-mapped and bloom | after `Hdr::finish` on the display target, never tone-mapped or glinted | frame position |
| 14 | Global alpha | `_Transparency.y` from the preset; 0 for black/coldboot1/2 | `opacity * globalAlpha_` from the boot weights only | preset source |
| 15 | DOF / near-far | `_ParticleSize`, `_Focus`, `_FocusCurves`, `_Darkness`, `_LifeBounds*`, near-facing quaternion | none | depth model |
| 16 | Theme response | physics identical day/night/yoake/higure; only glare/DoF/size-middle change | no theme input at all | preset wiring |
| 17 | Input response | `PARTICLES_UI` wind/shake gusts | none (the old CPU port had a gust; it went with the replacement) | interaction |
| 18 | Where the code lives | SPU task + PPU driver + 2 program pairs + MNU presets | one 87-line translation unit + 1 shader | architecture |

---

## 5. Porting plan

Ordered so each step has a cheap visual check. Files are all under `native-xmb/src/`; the audit dir is not
touched by the port.

### Phase 0 - assets and shaders (half day)

1. Load the firmware programs from the existing `lines.qrc` accessor (`assets_.qglFile`):
   `lib/particles/particles_quads.vpo/.fpo` and `lib/particles/particles_second.vpo/.fpo`. Do **not** port the
   Cg source; reuse the translated GLSL already checked in under `out/native-reference/lines/glsl/`, the same
   way `ribbon.cpp` and `icons.cpp` consume their programs. `particles_quads_debug` is not drawn by the XMB.
2. Parse the `PARTICLES*.mnu` presets. The MNU loader already exists for LINE1 / BACKGROUND / HDR; register
   `PARTICLES`, `PARTICLES_UI`, `PARTICLES_SPE` and the override sets that carry a PARTICLES.mnu exactly as the
   ribbon does for LINE1 (`Ribbon` preset plumbing in `ribbon.cpp`).
3. Bind `particles/proc_iridescent.tga` (TGA inside `lines.qrc`) as `_IridescentTex`.

Acceptance: a static test that loads all six programs + MNU + texture and prints the uniform values for the day
preset; every value matches the tables in section 2.3 and 3.3.

### Phase 1 - a deterministic simulation (1-2 days)

New files `src/particle_sim.{h,cpp}` (do not edit `particles.cpp` until the renderer exists; it is the fallback).

- State per particle: position (float3), velocity (float3), age (float, 0..1), life scale, orientation
  quaternion, spin axis, seed. Capacity 1536 (confirm, 6-A1).
- Step at `DELTA TIME` (0.0088883 s) in a fixed-step accumulator, independent of render rate - the firmware
  integrates at a fixed step, and the vpo has no time input, so the vertex data must be produced per sim step.
- Per step: spawn count from EMIT PER FRAME/PROB; speed from MIN+MUL(1+/-VAR); direction in the 51.87 deg cone
  with 17.4% mirrored and z flattened (ZSCALE 0); then gravity, friction, Brownian (one RNG draw per axis),
  optional wind (0 in the normal preset); age += AGING SPEED * (1 +/- VARIANCE).
- Alpha envelope from the kernel: `1` until age 0.94, then linear to 0 at age 1.0.
- RNG: Park-Miller 16807 with a fixed seed reproduces the firmware's distribution; do not use `std::mt19937`
  if bit-comparable output is wanted (it is not required visually, but keep the seed exposed for diffing).
- Emitter origin: parameterise it (fixed volume vs horizon line) so 6-A2 can be settled without a rewrite.

Acceptance: with a fixed seed, the live count settles at 1492 +/- 5% after 4 s, and the age histogram is flat
(section 3.3); no NaNs after 10 minutes.

### Phase 2 - quad renderer (2 days)

Rewrite `Particles::render` to consume the vertex array produced by Phase 1:

- 4 vertices per particle, 48-byte stride, attributes exactly as section 2.2 (pos float4 with life in w,
  uv0 float2, rot/quaternion float4). Draw a triangle strip with primitive restart, or two triangles.
- Feed the firmware GLSL for `particles_quads`; supply `_ModelviewProjection`/`_Modelview` from the same
  camera the ribbon uses (world units are 1080p pixels - see `PIPELINE.md` icons section for the same
  convention), and the PARTICLES.mnu uniforms of section 2.3.
- Bind `_Transparency = (FRESNEL, GLOBAL ALPHA)` and keep `setGlobalAlpha` as the boot hook (it maps directly
  to `_Transparency.y`).
- Draw into the HDR scene, not the display target: move the call before `hdr_.finish` in
  `View::renderBackground` (`view.cpp:326`), alongside the ribbon.
- Second pass `particles_second` into the glare source: this is the piece that makes the sparkle bloom. If the
  current `hdr.cpp` has no separate glare-source target, add one (the glare chain already exists - see
  `PIPELINE.md` "HDR tone map + glare").

Acceptance: a side-by-side frame against a reference capture at the same camera; the disc, the orientation
change during a scroll, and the soft bloom around glints are all visible. The `XMB_BG_SCENE` dump
(`view.cpp:308-324`) is the built-in instrument for checking the raw pre-tone-map scene.

### Phase 3 - presets, interaction, boot (1 day)

- Blend `GLARE`, `SIZE MIDDLE`, `FAR FOCUS`, `FAR FOCUS_DIST` on the same `presetBlend` the background and
  ribbon already use (`view.cpp:290`, `presetBlend_`). Physics is not blended.
- Wire `PARTICLES_UI`: ICON WIND 11.3877 x DPAD SCALE X 1 as a gust on category change, DSHAKE/RSHAKE
  thresholds for pad shake. Those are additive velocity impulses into the same pool.
- Cold boot: `GLOBAL ALPHA` follows the existing `Ribbon::bootWeights` -> `toClock` mapping
  (`view.cpp:563`), and `EMIT VEL MUL` blends from 3.7496 (coldboot) to 0.19 (clock preset) over the same
  two-stage schedule. `PARTICLES_SPE` is the speech/music playback variant (DELTA TIME 0.00346295,
  SPECULAR POWER -29.3657, SIZE MIDDLE 0.0218883, GLOBAL ALPHA -0.555603) - schedule it only if the music
  flow is ported.

Acceptance: the boot sequence shows no particles until NormalBG2, then fade in; day/night cross-fade changes
only the glint strength and DoF, never the motion.

### Phase 4 - parity evidence (gate for Phase 5 sign-off)

1. **Simulation parity**: a unit test that replays a fixed seed for 600 steps and compares live count, mean
   age, mean speed and mean position against a table captured from RPCS3 (`XMB_VTX_DUMP` of the particle draw
   - the existing `rpcs3-rsx-capture.patch` captures vertex arrays per draw; target the 2160-byte vpo ucode).
2. **Vertex-format parity**: the captured particle vertex buffer decoded with the 48-byte/3-attribute layout
   must reproduce the captured draw; if the stride is 40, the capture shows it immediately.
3. **Frame parity**: render the same camera/time and compare region luma plus a difference image against an
   RPCS3 frame, as `work/region-luma.ts` already does for the motion work.
4. **Clock independence**: run the app at 06:00, 12:00 and 00:00; particle positions should be identical for
   the same seed/time-of-flight, with only glare/DoF changing.

---

## 6. Unknowns, and exactly how to close them

### A1. Capacity = 1536? [highest value, cheap]

Evidence: `0x600` vector reserve in `FUN_0005cd14` (`custom_render_all.c:41098`); the arithmetic in 3.3 lands
at 1492 live. But the debug HUD reads capacity from a buffer object field, and the class also has a 0x200
container. Close it by one of:
- read `_capacity` from the debug HUD in RPCS3 (it is printed by `FUN_00060380`, `custom_render_all.c:43214`);
- or capture the particle draw's vertex count (`XMB_VTX_DUMP` on the 2160-byte vpo) and divide by 4.

### A2. Emission origin [highest value, medium]

No emitter geometry asset exists, so it is computed. Candidates: a fixed 3D volume, a line at the ribbon's
horizon, or a camera-relative band. The negative evidence (prior port rejected for ribbon coupling,
`VIDEO-REFERENCES.md:320-329`; physics identical across presets) argues against a true ribbon-mesh coupling,
but does not rule out a horizon line that happens to sit under the wave. Close it with an `XMB_VTX_DUMP` capture
of two frames at different ribbon states and compare the *birth positions* of new particles (youngest = highest
`pos.w`/life). Add a one-line debug dump of the first N particle positions at spawn.

### A3. FRICTION semantics [medium]

`v *= (1 - f*dt)` vs `v *= (1 - f)` per step differ by 4 orders of magnitude over a lifetime. Decide from the
same capture: fit the displacement curve of one isolated particle.

### A4. `DELTA TIME` rate [medium]

0.0088883 s is ~112.5 Hz, which is not 60. Either the sim runs on its own fixed-step clock or the value is a
scale factor. The capture timing (particle displacement between two known frame numbers) resolves it; the
capacity arithmetic in 3.3 is only self-consistent if the 3.116 s lifetime is right.

### B1. Blending [blocks "looks identical", cheap to capture]

The draw selects qgl material mode 3 (`FUN_00040c24(*param_2 + 0x30, 3, 0)`, `custom_render_all.c:40837`);
the mapping to RSX factors is not in the decompile, and the particle draw does not call the explicit
`SetBlendFunc(0x302, 0x303)` wrapper. Two candidates: non-premultiplied alpha (SRC_ALPHA/ONE_MINUS_SRC_ALPHA)
or additive (ONE, ONE). Capture the RSX blend state of the particle draw with the existing RPCS3 patch, or read
the mode-3 branch in `qglbase.sprx` (`FUN_0008ae80` dispatch table). Until then, start with additive (matches
the HDR-scene look and the second pass's purpose) and keep it a single constant.

### B2. `_Color` and `_Gamma` values [blocks colour parity, medium]

Neither has a `PARTICLES.mnu` key. `_Gamma` is set at fpo field +0xb0 (`FUN_0005c200`, `custom_render_all.c:40860`);
`_Color` is a float3 set elsewhere. Capture the particle draw's fragment constants with `XMB_FP_SERIES` /
`re-ps3-native --decode-fp` (the tooling already exists for `particles_quads`, ucode length 1968).
The prior port's fitted 2.5 for `_Gamma` (`VIDEO-REFERENCES.md:312-314`) is a fit, not a trace - confirm it.

### B3. Exact `_Focus` / `_FocusCurves` / `_NearControl` component packing [medium]

The vpo consumes `Focus.w-Focus.z`, `Focus.w-Focus.y`, `Focus.z-0.2`, `Focus.z-0.4`, `Focus.z-0.6` and
`ParticleSize.y - ParticleSize.x` plus the 0.2/0.4/0.6 bands ("lines 59-126 of
`glsl/particles_quads.vpo.glsl`"). That is reproducible from the GLSL without knowing which MNU key is which
component, so the port can proceed by feeding the captured constants rather than the MNU names. To finish the
mapping, capture the vpo constants with `XMB_FP_SERIES` and match against the preset values.

### B4. Quad topology / winding [low]

Whether the firmware draws a strip of 4 or a 6-index triangle list, and the uv orientation, is not in the
program. The capture in A1 also gives the index/primitive type. Visually this only affects back-face culling;
test both and keep whichever matches the reference.

### B5. Third pass `particles_quads_debug` [low]

Loaded but never drawn in the retail XMB flow. Do not port.

---

## 7. Handoff summary

Firmware particles = 1536-particle SPU-simulated pool (~1492 live, 3.1 s mean life, cone emission, gravity/
friction/Brownian, physics identical across themes) rendered as two firmware-shader passes of oriented,
DoF-sized, iridescent, spot-lit quads - pass 1 into the HDR scene, pass 2 into the glare source - driven by
`PARTICLES.mnu`. Today's `src/particles.cpp` is 2000 additive point sprites with no simulation, no lifetime,
no quad, one pass, after tone mapping. None of the firmware simulation parameters depend on clock time or the
ribbon; only the glare/DoF inputs follow the day-night preset.
## Applied: the pool simulation is in src/particles.cpp

The sparkle layer is replaced by the firmware-shaped simulation (same public API, plus `kCapacity`,
`liveCount()`, `stepIndex()`):

- fixed step 0.0088883 s, state a pure function of elapsed seconds and a fixed Park-Miller seed, so a
  `--render-clock` frame is reproducible (canonical capture: PSNR Infinity, SSIM 1.0000);
- spawn to the cap (1536) with the PARTICLES.mnu emission constants, mirror probability, cone, Brownian
  kick, gravity, friction, age variance, quaternion spin, life envelope in `pos.w`, GLOBAL ALPHA uniform;
- one 48-byte-stride VBO and one additive `glDrawElements` call, no per-frame allocation;
- measured pool: 1529 live at t=30 s (audit band 1492 +/- 5%), 1526 after a 600 s run, zero non-finite.

Drawn into the HDR scene before `Hdr::finish` (view.cpp), and - since 2026-10-26 - a second draw feeds the glare
source through `Hdr::finish`'s `extraGlare` hook, so the mip blur blooms the dust like `particles_second`. Both draws
re-use the same simulated state (a repeat call for the same `seconds` advances no steps). Measured effect: 52.59 dB /
0.9993 SSIM between the pre- and post-glare frames, all six layer gates and the canonical capture still pass, and the
three named-state self-references were deliberately refreshed. Still open: the in-shader radial halo replaces the
separate `particles_second` program, `proc_iridescent.tga` and the untraced `_Color`/`_Gamma`/spot packing,
the fresnel/life-bounds/darkness alpha chain, and the true emission volume (violume is a uniform box, so
dust does not cluster on the wave yet). Blending stays additive until qgl material mode 3 is resolved.

Because the dust now lands where the reference frame has its own particles at a different phase, the
`work/ab-layers.ts` capture baselines for frame / background / icon row / item label row were re-measured
(see `revisionNote` in the thresholds file); the gate passes 6/6 against the new numbers.