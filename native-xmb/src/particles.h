#pragma once
#include <QDateTime>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector3D>
#include <memory>
#include <vector>

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
//  - a capped pool of 1536 particles (the 0x600-element reserve in FUN_0005cd14), 1529 live at
//    measured steady state (the audit's central estimate is 1492), stepped at PARTICLES.mnu's fixed
//    DELTA TIME (0.0088883 s);
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
// the only presets this build ever schedules. PARTICLES_SPE.mnu (the playback variant) and
// PARTICLES_UI.mnu (input gusts) are documented but not applied; audit/PARTICLES.md says exactly when
// they apply and why this build never reaches them.
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
    static constexpr int kCapacity = 1536; // FUN_0005cd14's 0x600-element pool

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
    // Simulation state, for determinism logging (XMB_PARTICLE_LOG) and the audit's parity table.
    int liveCount() const { return live_; }
    long long stepIndex() const { return step_; }
private:
    // One SPU record: position, velocity, life, orientation. Units are world units, where the
    // viewport is kScreenHeight tall (PARTICLES.mnu's SIZE MIDDLE then lands at ~2.6 px at 1080p
    // before the fitted size gain, matching the reference captures).
    struct Particle {
        float px,py,pz;       // centre position (world units; z is negative, away from the camera)
        float vx,vy,vz;       // velocity, world units per DELTA TIME step
        float age;            // life fraction, 0..1
        float ageRate;        // age increment per step = AGING SPEED * (1 +/- AGING VARIANCE)
        float qx,qy,qz,qw;    // orientation quaternion
        float ax,ay,az;       // spin axis (unit)
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
    };
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
    void spawnParticle();
    float random(); // Park-Miller 16807 mod 2^31-1, the firmware's RNG
    unsigned stateChecksum() const;
    // Steps up to `seconds` and refreshes the VBO when the state changed. Both passes share it.
    void sync(QOpenGLFunctions_3_3_Core& gl,double seconds);
    void drawQuads(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    void drawGlare(QOpenGLFunctions_3_3_Core& gl,int pixelWidth,int pixelHeight);
    void logState(const char* pass,int width,int height) const;
    bool ready_=false;
    QOpenGLShaderProgram program_;      // particles_quads
    QOpenGLShaderProgram glareProgram_; // particles_second
    std::unique_ptr<QOpenGLTexture> iridescent_;
    bool iridescentFromFirmware_=false;
    bool materialsLoaded_=false;
    GLuint vao_=0,vbo_=0;      // vbo_ holds the vertex records, then the static index list
    GLintptr indexOffset_=0;
    std::vector<Particle> pool_;
    std::vector<Vertex> vertices_;
    int live_=0;
    long long step_=0;         // simulation steps completed since the seed
    unsigned int seed_=0;
    float spawnAccumulator_=0;
    float aspect_=16.f/9.f;
    float globalAlpha_=1;
    // The reader's values: the base physics block, the base look, the four theme looks and the
    // blended look the shaders get.
    Physics physics_;
    Look lookBase_,look_,themes_[4]; // 0 night, 1 yoake, 2 day, 3 higure
    // normalize(PARTICLES.mnu spot pos x/y/z). The initializer is the firmware's own value, used only by the
    // no-Assets entry point (init(gl)), which cannot read the MNU; loadMaterials() replaces it with the read value.
    QVector3D spotDirection_=QVector3D(4.16f,2.63f,-7.6f).normalized(); // fallback: PARTICLES.mnu spot pos
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
