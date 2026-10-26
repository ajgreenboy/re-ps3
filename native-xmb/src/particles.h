#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <vector>

namespace xmb {
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
//  - an additive draw from one VBO in one call, carrying the particles_quads vertex contract
//    (float4 pos with the per-vertex life alpha in w, float2 uv0 corner, float4 orientation
//    quaternion) and shaded like particles_quads.fpo (spot lambert + specular, 1-e^-k exposure,
//    iridescent tint, soft disc). particles_second's radial glare term is folded into the same quad,
//    because the real second pass draws into the HDR glare source, which lives in hdr.cpp - outside
//    this file.
//
// Determinism: every random number comes from the firmware's Park-Miller generator seeded with
// kSeed, and the simulation state is a pure function of the elapsed seconds passed to render(). No
// wall clock, no rand(), no frame-count integration - so `--render-clock <seconds>` reproduces a
// frame exactly, which the repo's A/B harness depends on.
class Particles {
public:
    static constexpr int kCapacity = 1536; // FUN_0005cd14's 0x600-element pool
    bool init(QOpenGLFunctions_3_3_Core& gl);
    bool ready() const { return ready_; }
    // Adds the dust to the bound framebuffer. Steps the fixed-timestep simulation up to `seconds`,
    // then draws the live particles.
    void render(QOpenGLFunctions_3_3_Core& gl,double seconds,int pixelWidth,int pixelHeight);
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
    void resetSimulation();
    void step();
    void spawnParticle();
    float random(); // Park-Miller 16807 mod 2^31-1, the firmware's RNG
    unsigned stateChecksum() const;
    bool ready_=false;
    QOpenGLShaderProgram program_;
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
};
}
