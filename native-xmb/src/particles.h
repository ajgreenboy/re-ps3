#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <memory>
#include <vector>

namespace xmb {
class Assets;

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
// Determinism: every random number comes from the firmware's Park-Miller generator seeded with
// kSeed, and the simulation state is a pure function of the elapsed seconds passed to render(). No
// wall clock, no rand(), no frame-count integration - so `--render-clock <seconds>` reproduces a
// frame exactly, which the repo's A/B harness depends on. Repeating a pass for the same `seconds`
// advances no steps: the second pass draws the state the first pass already produced.
class Particles {
public:
    static constexpr int kCapacity = 1536; // FUN_0005cd14's 0x600-element pool

    // Loads both programs and the iridescent texture. This overload resolves the firmware flash root
    // from the process command line (--dev-flash), which main.cpp always passes, because View owns
    // the Assets service and does not hand it to this layer. init(gl, assets) is the preferred call
    // and the only change view.cpp would need; see the audit note in PARTICLES.md.
    bool init(QOpenGLFunctions_3_3_Core& gl);
    bool init(QOpenGLFunctions_3_3_Core& gl,Assets& assets);
    bool ready() const { return ready_; }
    // True when the firmware particles/proc_iridescent.tga is bound as the LUT (false = a neutral
    // 1x1 fallback, so the dust loses its colour hint but keeps its photometry).
    bool iridescentLoaded() const { return iridescentFromFirmware_; }
    // Pass 1 (particles_quads). Steps the fixed-timestep simulation up to `seconds`, then draws the
    // live particles. With the unmodified view.cpp this is also the call that arrives for pass 2 -
    // see renderGlare() - so it forwards a detected glare-source draw to the second program.
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
    // Pass 2 (particles_second) into the bound glare source. Same simulated state, larger quads,
    // additive radial halo. Prefer calling this explicitly from the Hdr::finish glare hook.
    void renderGlare(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
    // PARTICLES.mnu GLOBAL ALPHA (_Transparency.y) during the cold boot: 0 until NormalBG2, then to 1.
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
    bool initPrograms();
    bool initResources(QOpenGLFunctions_3_3_Core& gl);
    bool loadIridescent(const QString& flashRoot);
    bool createTexture(const QImage& image);
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
