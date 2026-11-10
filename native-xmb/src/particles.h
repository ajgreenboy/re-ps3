#pragma once
#include <QDateTime>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector3D>
#include "rsx_program.h"
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>
#include <deque>
#include <QPointF>

namespace xmb {
class Assets;
class RcoObjects;
class Materials;

// P1's run-time reader, shared by the four scene layers (ribbon, HDR, icons, particles). One
// RcoObjects+Materials pair per process, built on first use with no I/O: RcoObjects does not read a
// container until someone asks for one, and Materials reads a .mnu only when its accessor is called.
// The MNU files behind the scene path are parsed once through this pair (one cache, one place where a
// missing key is distinguishable from a zero), instead of each layer carrying its own parser.
// Declared here because this task's write fence is the four layer pairs; in a tree where
// rco_objects.h may be edited these two declarations belong beside Materials.
RcoObjects& sceneObjects(Assets& assets);
Materials& sceneMaterials(Assets& assets);
// Bookkeeping for the one startup-cost line (see logSceneReader): each layer reports how many MNU files
// it read and how long that took.
void sceneNoteMaterials(int files,qint64 nanoseconds);
// Prints the measured reader cost once: RCO containers actually parsed (bytes/time) plus the MNU files
// the scene path read. Called after the last scene layer that reads materials has initialized.
void logSceneReader(const char* where);

// The particles layer, reconstructed from the firmware (audit/PARTICLES.md, audit/PIPELINE.md row
// "Particles"). It replaces linkev/PlayStation-3-XMB's closed-form sparkle layer with what the 4.93
// SPU task (particles.elf) actually does:
//
//  - a capped pool of 2048 particles (FUN_00060b14's li r5,0x800; a birth into a full pool is
//    dropped), stepped once per 60 Hz tick by DELTA TIME (the tick's +0x8b0 splat), deaths at
//    age 0.99999 or outside the life box;
//  - emission in a 51.8695 deg cone, 17.4% of spawns mirrored, speed EMIT VEL MIN +
//    EMIT VEL MUL * (1 +/- EMIT VEL VAR) and no z velocity (EMIT VEL ZSCALE 0), accepted with
//    EMIT PROB from an EMIT PER FRAME (60 Hz) candidate budget;
//  - per-particle aging from AGING SPEED * (1 +/- AGING VARIANCE), friction, gravity and Brownian
//    velocity kicks, and a quaternion orientation that spins at SPIN TIME SCALE;
//  - the kernel's life envelope: full alpha until age 0.94, then a linear fade to 0 at age 1.0,
//    with the matching 0.02 clamp at the start of life;
//  - two firmware-shaped passes over one 48-byte-stride VBO and one indexed draw each:
//      pass 1  particles_quads  - the particle bodies, additive, into the HDR scene;
//      pass 2  particles_second - the same particles as GLARE SCALE times larger radial glints,
//               additive, into the HDR glare source so the mip blur blooms them.
//    Both passes sample the firmware iridescent texture (particles/proc_iridescent.tga) at the
//    orientation normal, shade with the PARTICLES.mnu spot (LAMBERT COEFF / SPECULAR COEFF /
//    SPECULAR POWER / EXPOSURE), and apply the vertex alpha chain (FRESNEL, GLOBAL ALPHA, life).
//
// Every physics, photometry and material number is read from PARTICLES.mnu at run time (P1: the
// firmware's own data, never a copied table). The look keys (SIZE MIDDLE/FAR, NEAR/FAR FOCUS, GLARE,
// GLARE SCALE/P1/P2) are read from the theme override the background clock schedules - night, yoake,
// day and higure, blended on the plugin's own keyframe table (FUN_00011600) - because those four are
// the only presets this build ever schedules. PARTICLES_SPE.mnu (the playback variant) is documented but
// not applied (audit/PARTICLES.md). PARTICLES_UI.mnu's d-pad wind is applied, driven by XMB cursor moves (trackCursor;
// not the held pad, which shows no gust on the console); each move is one 0.03 impulse to a decaying gust, so only rapid
// moves build one past the rotation threshold (runtime-traced). Its sixaxis
// shake terms have no input in this build and are not applied. The icon wind is: View hands over every icon centre
// (trackWidgets) and a moving icon writes the flow field the kernel samples (stepIconWind, step()).
//
// Determinism: every random number comes from the firmware's Park-Miller generator seeded with
// kSeed, and the simulation state is a pure function of the elapsed seconds passed to render(). No
// wall clock, no rand(), no frame-count integration - so `--render-clock <seconds>` reproduces a
// frame exactly, which the repo's A/B harness depends on. Repeating a pass for the same `seconds`
// advances no steps: the second pass draws the state the first pass already produced. The theme
// look blend reads the background clock (the same --background-clock the ribbon uses) and never
// touches the simulation state, so a particle-state checksum is stable for a given render clock.
class Particles {
public:
    static constexpr int kCapacity = 2048; // FUN_00060b14's li r5,0x800 (0x2e0ec), rounded to 512s: 4 SPU blocks of 0x6000

    // Loads both programs, the MNU values and the iridescent texture. This overload resolves the
    // firmware flash root from the process command line (--dev-flash), which main.cpp always passes,
    // because View owns the Assets service and does not hand it to this layer. It cannot read the MNU
    // files (no Assets), so it runs on the compiled fallbacks and logs that. init(gl, assets) is the
    // preferred call and the one view.cpp uses.
    bool init(QOpenGLFunctions_3_3_Core& gl);
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // True when the firmware particles/proc_iridescent.tga is bound as the LUT (false = a neutral
    // 1x1 fallback, so the dust loses its colour hint but keeps its photometry).
    bool iridescentLoaded() const { return iridescentFromFirmware_; }
    // True when PARTICLES.mnu was readable (false = the compiled fallbacks are in use; logged in init).
    bool materialsLoaded() const { return materialsLoaded_; }
    // Pass 1 (particles_quads). Steps the fixed-timestep simulation up to `seconds`, then draws the
    // live particles. With the unmodified view.cpp this is also the call that arrives for pass 2 -
    // see renderGlare() - so it forwards a detected glare-source draw to the second program.
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
    // Pass 2 (particles_second) into the bound glare source. Same simulated state, larger quads,
    // additive radial halo. Prefer calling this explicitly from the Hdr::finish glare hook.
    void renderGlare(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
    // PARTICLES.mnu GLOBAL ALPHA (_Transparency.y) during the cold boot: 0 until NormalBG2, then to 1.
    // The value is driven by View from the boot weights (RPCS3 capture), not read from the preset.
    void setGlobalAlpha(float alpha) { globalAlpha_=alpha; }
    // The emitter grid (Ribbon::sheetMotion): side x side sheet points in the particle camera's world and their
    // velocities in world units per second, row-major. Dust is born at grid points as the plugin's FUN_0002dde4 /
    // FUN_00030b84 do (see spawnFromGrid). Empty = the fitted box (no ribbon).
    void setEmitter(std::vector<QVector3D> points,std::vector<QVector3D> velocities,int side) {
        emitter_=std::move(points);emitterVelocity_=std::move(velocities);emitterSide_=side;
    }
    // The XMB cursor as the plugin's direction state (layer+0x1c0). xmb_plugin's cursor-move handler (FUN_0000a3e8)
    // sends custom_render_plugin command (group 0, id 9) once per move, which FUN_0002b548 copies into the state: a
    // category move gives 0 when the column index falls, 1 when it rises (sign set by a console burst, particles.cpp); an
    // item move gives 2 when the selection rises, 3 when it falls. Called every frame with the shell's cursor; a change at the same
    // page depth is one move event. Not the held pad: the console's held d-pad shows no gust (audit/PARTICLES.md).
    // Pinned captures take no steps after their first frame, so it never reaches a --render-clock capture.
    void trackCursor(int category,int depth,int selection);
    // The icon wind's input: every XMB icon's centre in 1920 x 1080 screen pixels, keyed by a value that stays the
    // same for one icon from frame to frame. custom_render_plugin's per-widget render hook (FUN_000122c0 ->
    // FUN_0002f90c) projects each tracked widget's translation to NDC once per tick and writes the change since its
    // last visit into the flow-field cell under it (see step()). The latest call's positions are what a tick reads.
    // `seconds` is the render clock of the frame the positions were drawn for (the clock render() steps against), so a
    // 60 Hz tick can sample each icon at its own time rather than at whichever frame the faster renderer drew last.
    void trackWidgets(const std::vector<std::pair<quint64,QPointF>>& screen,double seconds);
    // Simulation state, for determinism logging (XMB_PARTICLE_LOG) and the audit's parity table.
    int liveCount() const { return live_; }
    long long stepIndex() const { return step_; }
private:
    // One SPU record: position, velocity, life, orientation, in the particle camera's world (BackgroundView: eye
    // (0, 0, 2) looking down -z, fovy 53).
    struct Particle {
        float px,py,pz;       // centre position (world units; z is negative, away from the camera)
        float vx,vy,vz;       // velocity, world units per second (the kernel adds vel x DELTA TIME per tick)
        float age;            // life fraction, 0..1
        float ageRate;        // age increment per step = AGING SPEED * (1 +/- AGING VARIANCE)
        float qx,qy,qz,qw;    // orientation quaternion
        float ax,ay,az;       // spin axis (unit)
        bool alive=false;     // the record's slot is in use (the kernel's dead marker is w = -666)
    };
    // particles_quads vertex contract, 48 bytes: float4 pos (w = per-vertex life alpha), float2 uv0
    // (billboard corner), 8 bytes pad, float4 orientation quaternion.
    struct Vertex {
        float x,y,z,life;
        float u,v,pad0,pad1;
        float qx,qy,qz,qw;
    };
    static_assert(sizeof(Vertex)==48,"particle vertex records are 48 bytes");
    // PARTICLES.mnu's physics block (base file; identical in the day/night/yoake/higure overrides -
    // audit/PARTICLES.md 3.4). The initializers are the firmware's own 4.93 values and are used only
    // when the MNU cannot be read (no --dev-flash / a tree without vsh/resource/qgl/lines.qrc).
    struct Physics {
        double deltaTime=0.0088883;  // fallback: PARTICLES.mnu delta time (s)
        float emitPerFrame=16.6539f; // fallback: PARTICLES.mnu emit per frame
        float emitProb=0.479115f;    // fallback: emit prob
        float emitVelMin=0.15064f;   // fallback: emit vel min
        float emitVelMul=0.19f;      // fallback: emit vel mul
        float emitVelVar=0.282567f;  // fallback: emit vel var
        float emitConeDegrees=51.8695f; // fallback: emit cone angle
        float emitNegProb=0.173899f; // fallback: emit neg prob
        float emitVelZScale=0.f;     // fallback: emit vel zscale
        float agingSpeed=0.00285223f;   // fallback: aging speed
        float agingVariance=0.493003f;  // fallback: aging variance
        float friction=0.030551f;       // fallback: friction
        float gravity=-6.8e-5f;         // fallback: gravity
        float brownian=0.225311f;       // fallback: brownian scale
        float spinTimeScale=2.74f;      // fallback: spin time scale
        // PARTICLES_UI.mnu, the input wind (sim+0x1360 +0xe8.. in the menu's key order).
        float uiBrownian=0.60f;         // fallback: PARTICLES_UI brownian
        float dpadRotMax=0.000116654f;  // fallback: PARTICLES_UI dpad rot max
        float dpadScaleX=1.f;           // fallback: PARTICLES_UI dpad scale x
        float dpadScaleY=0.f;           // fallback: PARTICLES_UI dpad scale y
        // The icon wind (FUN_0002f90c; runtime sim+0x1474/+0x1478/+0x147c = 11.3877 / 0 / 1).
        float iconWind=11.3877f;        // fallback: PARTICLES_UI icon wind
        float iconWindSclX=0.f;         // fallback: PARTICLES_UI icon wind scl x
        float iconWindSclY=1.f;         // fallback: PARTICLES_UI icon wind scl y
    };
    // PARTICLES.mnu's look block. These are the base-file values; the four theme overrides replace
    // them per key (day/night/yoake/higure) and the plugin's clock keyframes blend the result.
    struct Look {
        float sizeMiddle=0.0536233f;  // fallback: size middle
        float sizeFar=0.874062f;      // fallback: size far
        float nearFocus=6.12435f;     // fallback: near focus
        float farFocus=12.8327f;      // fallback: far focus
        float glare=0.159705f;        // fallback: glare
        float glareScale=5.44386f;    // fallback: glare scale
        float glareP1=0.99f;          // fallback: glare p1
        float glareP2=4.44f;          // fallback: glare p2
        float specularPower=35.2904f; // fallback: specular power
        float specularCoeff=74.74f;   // fallback: specular coeff
        float lambertCoeff=8.f;       // fallback: lambert coeff
        float exposure=0.0390458f;    // fallback: exposure
        float fresnel=1.33319f;       // fallback: fresnel
        float iridescentExp=1.f;      // fallback: iridescent exp
        // The depth-of-field and alpha keys the firmware vpo reads (FUN_0005c254, see drawQuads).
        float sizeNear=0.0772033f;    // fallback: size near
        float nearFocusDist=1.3193f;  // fallback: near focus_dist
        float nearFocusPow=2.47206f;  // fallback: near focus_pow
        float nearDarkness=6.24028f;  // fallback: near darkness
        float nearFuzziness=3.79f;    // fallback: near fuzziness
        float farFocusDist=3.54129f;  // fallback: far focus_dist
        float farFocusPow=1.83324f;   // fallback: far focus_pow
        float farDarkness=13.9f;      // fallback: far darkness
        float nearAlign=0.722f;       // fallback: near align
        float sizeAlign=13.19f;       // fallback: size align
        float colorControl=1.f;       // fallback: color_control
        float globalAlpha=1.f;        // fallback: global alpha
    };
    // Every Look key with its PARTICLES.mnu name: one table drives the base read, the theme reads and the blend.
    static const std::vector<std::pair<const char*,float Look::*>>& lookKeys();
    bool initPrograms();
    bool initResources(QOpenGLFunctions_3_3_Core& gl);
    bool loadIridescent(const QString& flashRoot);
    bool createTexture(const QImage& image);
    // Reads the physics block and the four theme looks through the shared Materials reader. Logs the
    // fallback path when a file is missing; never throws.
    void loadMaterials(Assets& assets);
    // Blends the four theme looks on the plugin's keyframe table for the current background clock.
    // Called once per render; it never touches the simulation.
    void refreshLook();
    double dayFraction() const;
    void resetSimulation();
    void step();
    void spawnFromGrid(int column,int row);
    void spawnInBox();
    void stepIconWind();
    void orientParticle(Particle& p);
    float random(); // Park-Miller 16807 mod 2^31-1, the native's spawn RNG (the PPU emitter's own RNG is not traced)
    int takeSlot();  // pops a free slot, -1 when the pool is full
    unsigned stateChecksum() const;
    // Steps up to `seconds` and refreshes the VBO when the state changed. Both passes share it.
    void sync(QOpenGLFunctions_3_3_Core& gl,double seconds);
    void drawQuads(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    void drawGlare(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    void logState(const char* pass,int width,int height) const;
    bool ready_=false;
    QOpenGLShaderProgram program_;      // particles_quads
    QOpenGLShaderProgram glareProgram_; // particles_second
    // Both passes on the firmware's own programs out of lines.qrc: [0] lib/particles/particles_quads.vpo/.fpo,
    // [1] lib/particles/particles_second.vpo/.fpo. The hand-written program_/glareProgram_ stay as the fallbacks for a
    // tree without the qrc (or XMB_PARTICLES_FALLBACK=1).
    CgProgram firmwareVertex_[2],firmwareFragment_[2];
    QOpenGLShaderProgram firmwareProgram_[2];
    // The firmware passes' constant banks are a pure function of the look, the alpha, the light and the viewport, which hold
    // still between clock keyframes; they are rebuilt only when one moves (D12).
    std::array<float,36> passKey_[2]{};bool passValid_[2]={false,false};
    std::vector<QVector4D> passVc_[2];QVector<QVector4D> passFc_[2];
    bool firmwareReady_[2]={false,false};
    QVector3D spotAttenuation_=QVector3D(1,0,0); // fallback: PARTICLES.mnu spot attn x/y/z
    void initFirmwarePass(Assets& assets,int pass);
    void drawFirmwarePass(QOpenGLFunctions_3_3_Core& gl,int pass,int pixelWidth,int pixelHeight);
    std::unique_ptr<QOpenGLTexture> iridescent_;
    bool iridescentFromFirmware_=false;
    bool materialsLoaded_=false;
    GLuint vao_=0,vbo_=0;      // vbo_ holds the vertex records, then the static index list
    GLintptr indexOffset_=0;
    std::vector<Particle> pool_;   // kCapacity fixed slots, walked in slot order like the SPU kernel's records
    std::vector<int> free_;        // LIFO free list of slots (FUN_00059228); births pop, deaths push
    std::vector<Vertex> vertices_;
    int live_=0;
    long long step_=0;         // simulation steps completed since the seed
    unsigned int seed_=0;
    float aspect_=16.f/9.f;
    float globalAlpha_=1;
    // Input wind state (FUN_0002d02c / FUN_00031494): the direction state (layer+0x1c0, 4 idle), the last direction's
    // rotation vector (layer+0x1b0), the gust smoother (layer+0x19c, g = layer+0x194) and the brownian excitation
    // oscillator (layer+0x1d8 / +0x1dc, level layer+0x1d4). The cursor fields are the last trackCursor() input.
    int padState_=4;
    int cursorCategory_=-1,cursorDepth_=-1,cursorSelection_=-1;
    QVector3D padRotation_;
    float padGustVelocity_=0,padGain_=0;
    float excitePosition_=0,exciteVelocity_=0,exciteLevel_=0;
    std::vector<QVector3D> emitter_,emitterVelocity_;
    // The flow field (sim+0x180): 32 x 16 cells of signed-byte XYZ, row 0 at NDC y -1. The widgets' NDC positions:
    // the latest trackWidgets() input and the position each widget had at the last tick (the plugin's map at
    // layer+0x1018, node+0x20).
    std::array<qint8,32*16*3> flowGrid_{};
    std::unordered_map<quint64,QPointF> widgetLast_;
    // The last few trackWidgets() sets (NDC) with their frame times; a tick samples them one tick late (stepIconWind).
    std::deque<std::pair<double,std::unordered_map<quint64,QPointF>>> widgetFrames_;
    int emitterSide_=0;
    // The reader's values: the base physics block, the base look, the four theme looks and the
    // blended look the shaders get.
    Physics physics_;
    Look lookBase_,look_,themes_[4]; // 0 night, 1 yoake, 2 day, 3 higure
    // PARTICLES.mnu spot pos x/y/z, the point light both programs light the dust from. The initializer is the
    // firmware's own value, used only by the no-Assets entry point (init(gl)), which cannot read the MNU;
    // loadMaterials() replaces it with the read value.
    QVector3D spotPosition_=QVector3D(4.16f,2.63f,-7.6f); // fallback: PARTICLES.mnu spot pos
    QDateTime clockOverride_;   // --background-clock, the same option View resolves for the ribbon
    // Upload bookkeeping: the VBO is refreshed only when the simulation state or the vertex count
    // changed, so the second pass of a frame re-uses the first pass's buffer verbatim.
    long long uploadedStep_=-1;
    int uploadedLive_=-1;
    float uploadedAspect_=-1.f;
    // Pass-1 / pass-2 discrimination for the unmodified view.cpp, which calls render() for both
    // draws. A glare call is the call that arrives immediately after a scene draw for the same
    // elapsed seconds but with a different draw framebuffer (Hdr binds its glare source for the
    // extraGlare hook). Explicit renderGlare() calls bypass this entirely.
    bool previousWasScene_=false;
    double previousSeconds_=-1.0;
    GLint previousFbo_=-1;
};
}
