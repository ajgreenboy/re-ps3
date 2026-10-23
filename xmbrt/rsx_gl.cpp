// RSX renderer on OpenGL (compatibility profile, so QUADS/POLYGON, alpha test and legacy enums work as on NV40).
// A Win32 window thread owns the message pump; the FIFO thread owns the GL context and executes methods.
// Colour surfaces are GL textures keyed by their local-memory offset; a flip blits the surface at the display
// buffer's offset to the window. Vertex and fragment programs are translated to GLSL here.
// Method offsets, instruction encodings and the program translation rules follow RPCS3 (0b0fa08):
// rsx/gcm_enums.h, Program/RSXVertexProgram.h, RSXFragmentProgram.h, VertexProgramDecompiler.cpp,
// FragmentProgramDecompiler.cpp, Core/RSXDrawCommands.cpp (fill_scale_offset_data), Utils/color_utils.hpp.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include "GL/glext.h" // RPCS3 3rdparty (Khronos)

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

uint32_t rsx_guest_address(uint32_t offset, uint32_t location); // k_rsx.cpp: 0 local, 1 main, 0 if unmapped

namespace
{
    // NV4097 methods
    enum : uint32_t
    {
        SURFACE_CLIP_HORIZONTAL = 0x200, SURFACE_CLIP_VERTICAL = 0x204, SURFACE_FORMAT = 0x208, SURFACE_PITCH_A = 0x20C,
        SURFACE_COLOR_AOFFSET = 0x210, SURFACE_ZETA_OFFSET = 0x214, SURFACE_COLOR_TARGET = 0x220,
        ALPHA_TEST_ENABLE = 0x304, ALPHA_FUNC = 0x308, ALPHA_REF = 0x30C, BLEND_ENABLE = 0x310,
        BLEND_FUNC_SFACTOR = 0x314, BLEND_FUNC_DFACTOR = 0x318, BLEND_COLOR = 0x31C, BLEND_EQUATION = 0x320,
        COLOR_MASK = 0x324, STENCIL_TEST_ENABLE = 0x328, STENCIL_MASK = 0x32C, STENCIL_FUNC = 0x330,
        STENCIL_FUNC_REF = 0x334, STENCIL_FUNC_MASK = 0x338, STENCIL_OP_FAIL = 0x33C, STENCIL_OP_ZFAIL = 0x340,
        STENCIL_OP_ZPASS = 0x344, LOGIC_OP_ENABLE = 0x374,
        SCISSOR_HORIZONTAL = 0x8C0, SCISSOR_VERTICAL = 0x8C4, SHADER_PROGRAM = 0x8E4,
        VIEWPORT_OFFSET = 0xA20, VIEWPORT_SCALE = 0xA30, DEPTH_FUNC = 0xA6C, DEPTH_MASK = 0xA70, DEPTH_TEST_ENABLE = 0xA74,
        TRANSFORM_PROGRAM = 0xB80, VERTEX_DATA3F_M = 0x1500, VERTEX_DATA_ARRAY_OFFSET = 0x1680,
        VERTEX_DATA_BASE_OFFSET = 0x1738, VERTEX_DATA_BASE_INDEX = 0x173C, VERTEX_DATA_ARRAY_FORMAT = 0x1740,
        BEGIN_END = 0x1808, ARRAY_ELEMENT16 = 0x180C, ARRAY_ELEMENT32 = 0x1810, DRAW_ARRAYS = 0x1814, INLINE_ARRAY = 0x1818,
        INDEX_ARRAY_ADDRESS = 0x181C, INDEX_ARRAY_DMA = 0x1820, DRAW_INDEX_ARRAY = 0x1824,
        CULL_FACE = 0x1830, FRONT_FACE = 0x1834, CULL_FACE_ENABLE = 0x183C, TEXTURE_CONTROL3 = 0x1840,
        VERTEX_DATA2F_M = 0x1880, VERTEX_DATA4UB_M = 0x1940, TEXTURE_OFFSET = 0x1A00, VERTEX_DATA4F_M = 0x1C00,
        SHADER_CONTROL = 0x1D60, ZSTENCIL_CLEAR_VALUE = 0x1D8C, COLOR_CLEAR_VALUE = 0x1D90, CLEAR_SURFACE = 0x1D94,
        RESTART_INDEX_ENABLE = 0x1DAC, RESTART_INDEX = 0x1DB0, VERTEX_DATA1F_M = 0x1E40,
        TRANSFORM_PROGRAM_LOAD = 0x1E9C, TRANSFORM_PROGRAM_START = 0x1EA0, TRANSFORM_CONSTANT_LOAD = 0x1EFC,
        TRANSFORM_CONSTANT = 0x1F00, VERTEX_ATTRIB_INPUT_MASK = 0x1FF0,
    };

    uint32_t g_regs[0x10000 / 4];
    uint32_t reg(uint32_t method) { return g_regs[method >> 2]; }
    float regf(uint32_t method) { float f; memcpy(&f, &g_regs[method >> 2], 4); return f; }

    // ---- GL entry points (beyond 1.1) ----
#define GL_FUNCS(X) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) X(PFNGLVERTEXATTRIB4FVPROC, glVertexAttrib4fv) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) X(PFNGLBLENDCOLORPROC, glBlendColor) \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D) X(PFNGLPRIMITIVERESTARTINDEXPROC, glPrimitiveRestartIndex) \
    X(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate) X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)
#define DECLARE(type, name) type name;
    GL_FUNCS(DECLARE)
#undef DECLARE

    HWND g_hwnd;
    HDC g_hdc;
    HGLRC g_glrc;
    DWORD g_gl_thread;
    std::atomic<bool> g_window_ready{ false }, g_gl_failed{ false };
    std::atomic<uint64_t> g_pending_flip{ 0 }; // offset | width << 32 | height << 48, presented by the GL thread

    LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        if (msg == WM_CLOSE)
        {
            log_line("rsx: window closed");
            fatal_exit(0);
        }
        return DefWindowProcW(h, msg, w, l);
    }

    void window_thread()
    {
        WNDCLASSW wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"xmbrt";
        RegisterClassW(&wc);
        RECT r{ 0, 0, 1280, 720 };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        g_hwnd = CreateWindowW(L"xmbrt", L"xmbrt", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
        g_hdc = GetDC(g_hwnd);
        PIXELFORMATDESCRIPTOR pfd{ sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
        SetPixelFormat(g_hdc, ChoosePixelFormat(g_hdc, &pfd), &pfd);
        g_window_ready = true;
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    GLuint g_vbo, g_ibo;

    // Runs on the FIFO thread, which owns the context from then on.
    bool ensure_gl()
    {
        if (g_glrc) return true;
        if (g_gl_failed) return false;
        while (!g_window_ready) std::this_thread::yield();
        g_gl_thread = GetCurrentThreadId();
        g_glrc = wglCreateContext(g_hdc);
        if (!g_glrc || !wglMakeCurrent(g_hdc, g_glrc))
        {
            log_line("rsx: OpenGL context creation failed (%lu)", GetLastError());
            g_gl_failed = true;
            return false;
        }
#define LOAD(type, name) name = reinterpret_cast<type>(wglGetProcAddress(#name));
        GL_FUNCS(LOAD)
#undef LOAD
        glGenBuffers(1, &g_vbo);
        glGenBuffers(1, &g_ibo);
        log_line("rsx: OpenGL %s on %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
        return true;
    }

    // ---- surfaces ----
    struct Surface
    {
        GLuint tex = 0, fbo = 0, depth = 0;
        uint32_t width = 0, height = 0;
    };
    std::unordered_map<uint32_t, Surface> g_surfaces; // by colour offset (local memory)

    Surface& surface(uint32_t offset, uint32_t w, uint32_t h)
    {
        Surface& s = g_surfaces[offset];
        if (s.tex && s.width >= w && s.height >= h) return s;
        if (!s.tex)
        {
            glGenTextures(1, &s.tex);
            glGenFramebuffers(1, &s.fbo);
            glGenRenderbuffers(1, &s.depth);
        }
        s.width = std::max(s.width, w);
        s.height = std::max(s.height, h);
        glBindTexture(GL_TEXTURE_2D, s.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.width, s.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, s.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s.width, s.height);
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s.depth);
        // A new surface starts black (RSX memory contents are not mirrored)
        glClearColor(0, 0, 0, 0);
        glClearDepth(1);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        return s;
    }

    uint32_t clip_w() { return reg(SURFACE_CLIP_HORIZONTAL) >> 16; }
    uint32_t clip_h() { return reg(SURFACE_CLIP_VERTICAL) >> 16; }

    Surface* bind_surface()
    {
        if ((reg(SURFACE_COLOR_TARGET) & 0xF) == 0) return nullptr; // CELL_GCM_SURFACE_TARGET_NONE
        const uint32_t w = clip_w(), h = clip_h();
        if (!w || !h) return nullptr;
        Surface* s = &surface(reg(SURFACE_COLOR_AOFFSET), w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        glViewport(0, 0, w, h);
        return s;
    }

    void clear(uint32_t mask)
    {
        if (!(mask & 0xF3) || !ensure_gl() || !bind_surface()) return;
        glDisable(GL_SCISSOR_TEST);
        GLbitfield bits = 0;
        if (mask & 0xF0)
        {
            const uint32_t c = reg(COLOR_CLEAR_VALUE); // A8R8G8B8
            glColorMask((mask >> 4) & 1, (mask >> 5) & 1, (mask >> 6) & 1, (mask >> 7) & 1); // CELL_GCM_CLEAR_R/G/B/A = bits 4-7
            glClearColor(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, (c >> 24) / 255.f);
            bits |= GL_COLOR_BUFFER_BIT;
        }
        const uint32_t zs = reg(ZSTENCIL_CLEAR_VALUE); // z24s8
        if (mask & 1)
        {
            glDepthMask(GL_TRUE);
            glClearDepth((zs >> 8) / double(0xFFFFFF));
            bits |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask & 2)
        {
            glStencilMask(0xFF);
            glClearStencil(zs & 0xFF);
            bits |= GL_STENCIL_BUFFER_BIT;
        }
        glClear(bits);
        glColorMask(1, 1, 1, 1);
    }

    // ---- program translation helpers ----
    const char* const kSwz = "xyzw";

    std::string swizzle4(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
    {
        if (x == 0 && y == 1 && z == 2 && w == 3) return "";
        return std::string(".") + kSwz[x] + kSwz[y] + kSwz[z] + kSwz[w];
    }

    std::string write_mask(bool x, bool y, bool z, bool w)
    {
        std::string m;
        if (x) m += 'x';
        if (y) m += 'y';
        if (z) m += 'z';
        if (w) m += 'w';
        return m;
    }

    // cond: lt=1 eq=2 gt=4
    std::string compare_cc(uint32_t cond, const std::string& cc)
    {
        static const char* const fn[8] = { "", "lessThan", "equal", "lessThanEqual", "greaterThan", "notEqual", "greaterThanEqual", "" };
        return std::string(fn[cond & 7]) + "(" + cc + ", vec4(0.))";
    }

    // Emits "dest.mask = value" honouring a condition (bvec4 expression or empty for always).
    void emit_assign(std::string& out, const std::string& dest, const std::string& mask, const std::string& value, const std::string& cond)
    {
        if (mask.empty()) return;
        if (cond.empty())
        {
            out += "  " + dest + "." + mask + " = (" + value + ")." + mask + ";\n";
            return;
        }
        out += "  { vec4 t_ = vec4(" + value + "); bvec4 c_ = " + cond + ";";
        for (char c : mask) out += std::string(" if (c_.") + c + ") " + dest + "." + c + " = t_." + c + ";";
        out += " }\n";
    }

    uint64_t fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
        return h;
    }

    // ---- vertex programs ----
    uint32_t g_vp[512 * 4];
    uint32_t g_vp_word = 0;
    uint32_t g_vc[468 * 4];

    struct VpTranslation
    {
        std::string glsl;
        uint32_t inputs = 0; // attribute mask read by the program
    };

    VpTranslation translate_vp(uint32_t start)
    {
        VpTranslation t;
        std::string body;
        bool warned_branch = false;
        for (uint32_t i = start; i < 512; i++)
        {
            const uint32_t d0 = g_vp[i * 4], d1 = g_vp[i * 4 + 1], d2 = g_vp[i * 4 + 2], d3 = g_vp[i * 4 + 3];
            const uint32_t src_bits[3] = { ((d2 >> 23) & 0x1FF) | (((d1 >> 0) & 0xFF) << 9), (d2 >> 6) & 0x1FFFF, ((d3 >> 21) & 0x7FF) | ((d2 & 0x3F) << 11) };
            const bool end = d3 & 1;
            const uint32_t vec_op = (d1 >> 22) & 0x1F, sca_op = (d1 >> 27) & 0x1F;
            if (!(src_bits[0] & 3) || !(src_bits[1] & 3) || !(src_bits[2] & 3)) break; // RPCS3: src check failed, abort

            const uint32_t input_src = (d1 >> 8) & 0xF, const_src = (d1 >> 12) & 0x3FF;
            const bool index_const = (d3 >> 1) & 1;
            auto src = [&](int n) {
                const uint32_t s = src_bits[n];
                std::string r;
                switch (s & 3)
                {
                case 1: r = "r[" + std::to_string((s >> 2) & 0x3F) + "]"; break;
                case 2: r = "in_" + std::to_string(input_src); t.inputs |= 1u << input_src; break;
                default:
                    if (index_const)
                        r = "vc[clamp(a[" + std::to_string((d0 >> 24) & 1) + "]." + kSwz[d0 & 3] + " + " + std::to_string(const_src) + ", 0, 467)]";
                    else
                        r = const_src < 468 ? "vc[" + std::to_string(const_src) + "]" : "vec4(0.)";
                    break;
                }
                r += swizzle4((s >> 14) & 3, (s >> 12) & 3, (s >> 10) & 3, (s >> 8) & 3);
                if ((d0 >> (21 + n)) & 1) r = "abs(" + r + ")";
                if ((s >> 16) & 1) r = "-" + r;
                return "(" + r + ")";
            };

            const uint32_t cond = (d0 >> 10) & 7;
            const bool cond_test = (d0 >> 13) & 1;
            const std::string cc = "cc[" + std::to_string((d0 >> 25) & 1) + "]" + swizzle4((d0 >> 8) & 3, (d0 >> 6) & 3, (d0 >> 4) & 3, (d0 >> 2) & 3);
            const std::string cnd = (cond_test && cond != 7) ? compare_cc(cond, cc) : "";
            const bool sat = (d0 >> 26) & 1;
            const bool vec_result = (d0 >> 30) & 1;
            const uint32_t dst_out = (d3 >> 2) & 0x1F;

            auto set_dst = [&](bool is_sca, std::string value, bool address = false) {
                if (cond == 0) return;
                const std::string mask = is_sca ? write_mask((d3 >> 20) & 1, (d3 >> 19) & 1, (d3 >> 18) & 1, (d3 >> 17) & 1)
                                                : write_mask((d3 >> 16) & 1, (d3 >> 15) & 1, (d3 >> 14) & 1, (d3 >> 13) & 1);
                if (sat) value = "clamp(" + value + ", 0., 1.)";
                const uint32_t tmp = is_sca ? (d3 >> 7) & 0x3F : (d0 >> 15) & 0x3F;
                const bool is_result = is_sca ? !vec_result : vec_result;
                bool wrote = false;
                if (is_result && dst_out != 0x1F && dst_out < 16)
                {
                    emit_assign(body, "o[" + std::to_string(dst_out) + "]", mask, value, cnd);
                    wrote = true;
                }
                if (tmp != 0x3F)
                {
                    if (address)
                        emit_assign(body, "a[" + std::to_string((d0 >> 24) & 1) + "]", mask, "ivec4(" + value + ")", cnd);
                    else
                        emit_assign(body, "r[" + std::to_string(tmp) + "]", mask, value, cnd);
                    wrote = true;
                }
                if (!wrote && (!is_result || ((d0 >> 14) & 1) || ((d0 >> 29) & 1)))
                    emit_assign(body, "cc[" + std::to_string((d0 >> 25) & 1) + "]", mask, value, cnd);
            };

            const std::string s0 = src(0), s1 = src(1), s2 = src(2);
            switch (vec_op)
            {
            case 0x00: break;
            case 0x01: set_dst(false, s0); break;
            case 0x02: set_dst(false, s0 + " * " + s1); break;
            case 0x03: set_dst(false, s0 + " + " + s2); break;
            case 0x04: set_dst(false, "fma(" + s0 + ", " + s1 + ", " + s2 + ")"); break;
            case 0x05: set_dst(false, "vec4(dot(" + s0 + ".xyz, " + s1 + ".xyz))"); break;
            case 0x06: set_dst(false, "vec4(dot(vec4(" + s0 + ".xyz, 1.), " + s1 + "))"); break;
            case 0x07: set_dst(false, "vec4(dot(" + s0 + ", " + s1 + "))"); break;
            case 0x08: set_dst(false, "vec4(1., " + s0 + ".y * " + s1 + ".y, " + s0 + ".z, " + s1 + ".w)"); break;
            case 0x09: set_dst(false, "min(" + s0 + ", " + s1 + ")"); break;
            case 0x0A: set_dst(false, "max(" + s0 + ", " + s1 + ")"); break;
            case 0x0B: set_dst(false, "vec4(lessThan(" + s0 + ", " + s1 + "))"); break;
            case 0x0C: set_dst(false, "vec4(greaterThanEqual(" + s0 + ", " + s1 + "))"); break;
            case 0x0D: set_dst(false, "floor(" + s0 + ")", true); break; // ARL
            case 0x0E: set_dst(false, "fract(" + s0 + ")"); break;
            case 0x0F: set_dst(false, "floor(" + s0 + ")"); break;
            case 0x10: set_dst(false, "vec4(equal(" + s0 + ", " + s1 + "))"); break;
            case 0x11: set_dst(false, "vec4(0.)"); break;
            case 0x12: set_dst(false, "vec4(greaterThan(" + s0 + ", " + s1 + "))"); break;
            case 0x13: set_dst(false, "vec4(lessThanEqual(" + s0 + ", " + s1 + "))"); break;
            case 0x14: set_dst(false, "vec4(notEqual(" + s0 + ", " + s1 + "))"); break;
            case 0x15: set_dst(false, "vec4(1.)"); break;
            case 0x16: set_dst(false, "sign(" + s0 + ")"); break;
            default: body += "  // vec op " + std::to_string(vec_op) + "\n"; break;
            }
            switch (sca_op)
            {
            case 0x00: break;
            case 0x01: set_dst(true, s2); break;
            case 0x02: set_dst(true, "1.0 / " + s2); break;
            case 0x03: set_dst(true, "clamp(1.0 / " + s2 + ", 5.42101e-20, 1.884467e19)"); break;
            case 0x04: set_dst(true, "vec4(1. / sqrt(max(" + s2 + ".x, 1e-10)))"); break;
            case 0x05: set_dst(true, "exp(" + s2 + ")"); break;
            case 0x06: set_dst(true, "log(" + s2 + ")"); break;
            case 0x07: set_dst(true, "lit_(" + s2 + ")"); break;
            case 0x0D: set_dst(true, "log2(max(" + s2 + ", 1e-10))"); break;
            case 0x0E: set_dst(true, "exp2(" + s2 + ")"); break;
            case 0x0F: set_dst(true, "sin(" + s2 + ")"); break;
            case 0x10: set_dst(true, "cos(" + s2 + ")"); break;
            default:
                if (!warned_branch)
                {
                    log_line("rsx: vertex program flow-control op %u ignored (start %u, instruction %u)", sca_op, start, i);
                    warned_branch = true;
                }
                break;
            }
            if (end) break;
        }

        std::string& g = t.glsl;
        g = "#version 430 compatibility\n"
            "uniform vec4 vc[468];\n"
            "uniform vec4 so_scale, so_offset;\n";
        for (uint32_t i = 0; i < 16; i++)
            if (t.inputs & (1u << i)) g += "layout(location=" + std::to_string(i) + ") in vec4 in_" + std::to_string(i) + ";\n";
        g += "out vec4 v_col0, v_col1, v_fog;\nout vec4 v_tc[10];\n"
             "vec4 lit_(vec4 v) { vec4 c = v; c.x = max(v.x, 0.); c.y = max(v.y, 0.);"
             " return vec4(1., c.x, c.x > 0. ? exp(c.w * log(max(c.y, 1e-10))) : 0., 1.); }\n"
             "void main() {\n  vec4 r[32]; ivec4 a[2] = ivec4[2](ivec4(0), ivec4(0)); vec4 cc[2] = vec4[2](vec4(0.), vec4(0.)); vec4 o[16];\n"
             "  for (int i = 0; i < 32; i++) r[i] = vec4(0.);\n  for (int i = 0; i < 16; i++) o[i] = vec4(0., 0., 0., 1.);\n";
        g += body;
        g += "  vec4 p = o[0];\n"
             "  p.xyz = p.xyz * so_scale.xyz + p.w * so_offset.xyz;\n"
             "  p.z = (p.z + p.z) - p.w;\n"
             "  gl_Position = p;\n"
             "  v_col0 = o[1]; v_col1 = o[2]; v_fog = o[5];\n"
             "  for (int i = 0; i < 8; i++) v_tc[i] = o[7 + i];\n"
             "  v_tc[8] = o[15]; v_tc[9] = o[6];\n}\n";
        return t;
    }

    // ---- fragment programs ----
    uint32_t fp_word(uint32_t addr) // RSX stores fragment program words with their 16-bit halves swapped
    {
        const uint32_t v = guest_load_u32(addr);
        return (v << 16) | (v >> 16);
    }

    struct FpTranslation
    {
        std::string glsl;
        std::vector<uint32_t> constant_slots; // byte offsets of inline constants, in fc[] order
        uint32_t textures = 0;                // sampler mask
    };

    // tex_cube: mask of units bound as cube maps
    FpTranslation translate_fp(uint32_t addr, uint32_t ctrl, uint32_t tex_cube)
    {
        FpTranslation t;
        std::string body;
        std::vector<std::pair<uint32_t, std::string>> closers; // (slot, code) emitted before that slot
        int loop_depth = 0;
        for (uint32_t pc = 0; pc < 4096; pc++)
        {
            for (auto it = closers.begin(); it != closers.end();)
            {
                if (it->first == pc)
                {
                    body += it->second;
                    if (it->second.find("loop") != std::string::npos) loop_depth--;
                    it = closers.erase(it);
                }
                else ++it;
            }
            const uint32_t base = addr + pc * 16;
            const uint32_t dst = fp_word(base), s0w = fp_word(base + 4), s1w = fp_word(base + 8), s2w = fp_word(base + 12);
            const uint32_t opcode = ((dst >> 24) & 0x3F) | ((s1w >> 31) << 6);
            const bool end = dst & 1;
            const bool has_const = ((s0w & 3) == 2) || ((s1w & 3) == 2) || ((s2w & 3) == 2);
            const bool flow = opcode >= 0x40;
            std::string fc;
            if (has_const && !flow)
            {
                fc = "fc[" + std::to_string(t.constant_slots.size()) + "]";
                t.constant_slots.push_back((pc + 1) * 16);
            }

            const uint32_t prec_mods = (s1w >> 19) & 0x1FF;
            auto src = [&](uint32_t s, int n) {
                std::string r;
                switch (s & 3)
                {
                case 0: r = std::string(((s >> 8) & 1) ? "h[" : "r[") + std::to_string((s >> 2) & 0x3F) + "]"; break;
                case 1:
                {
                    const uint32_t id = (s2w >> 30) & 1 ? ((s2w >> 19) & 0x7FF) + 4 : (dst >> 13) & 0xF;
                    switch (id)
                    {
                    case 0: r = "gl_FragCoord"; break;
                    case 1: r = "clamp(v_col0, 0., 1.)"; break;
                    case 2: r = "clamp(v_col1, 0., 1.)"; break;
                    case 3: r = "v_fog"; break;
                    case 14: r = "vec4(gl_FrontFacing ? 1. : -1.)"; break;
                    default:
                        r = id < 14 ? "v_tc[" + std::to_string(id - 4) + "]" : "vec4(0.)";
                        if (id >= 4 && id < 14 && ((s2w >> 31) & 1)) r = "(" + r + " * gl_FragCoord.w)";
                        break;
                    }
                    break;
                }
                case 2: r = fc; break;
                default: r = "vec4(0.)"; break;
                }
                r += swizzle4((s >> 9) & 3, (s >> 11) & 3, (s >> 13) & 3, (s >> 15) & 3);
                const bool abs = n == 0 ? (s0w >> 29) & 1 : (s >> 18) & 1;
                if (abs) r = "abs(" + r + ")";
                if (((prec_mods >> (n * 3)) & 7) == 4) r = "clamp(" + r + ", 0., 1.)"; // RSX_FP_PRECISION_SATURATE
                if ((s >> 17) & 1) r = "-" + r;
                return "(" + r + ")";
            };

            const bool lt = (s0w >> 18) & 1, eq = (s0w >> 19) & 1, gr = (s0w >> 20) & 1;
            const uint32_t cond = (lt ? 1 : 0) | (eq ? 2 : 0) | (gr ? 4 : 0);
            const std::string cc = "cc[" + std::to_string((s0w >> 31) & 1) + "]" + swizzle4((s0w >> 21) & 3, (s0w >> 23) & 3, (s0w >> 25) & 3, (s0w >> 27) & 3);
            const std::string cnd = cond == 7 ? "" : compare_cc(cond, cc);
            const std::string any_cnd = cond == 7 ? "true" : "any(" + cnd + ")";
            const std::string mask = write_mask((dst >> 9) & 1, (dst >> 10) & 1, (dst >> 11) & 1, (dst >> 12) & 1);
            const uint32_t tex = (dst >> 17) & 0xF;
            const bool cube = (tex_cube >> tex) & 1;

            auto set_dst = [&](std::string value) {
                if (cond == 0) return;
                switch ((s1w >> 28) & 7)
                {
                case 1: value = "(" + value + ") * 2."; break;
                case 2: value = "(" + value + ") * 4."; break;
                case 3: value = "(" + value + ") * 8."; break;
                case 5: value = "(" + value + ") / 2."; break;
                case 6: value = "(" + value + ") / 4."; break;
                case 7: value = "(" + value + ") / 8."; break;
                default: break;
                }
                if ((dst >> 31) & 1) value = "clamp(" + value + ", 0., 1.)";
                const std::string cc_dst = "cc[" + std::to_string((s0w >> 30) & 1) + "]";
                if ((dst >> 30) & 1) // no_dest
                {
                    if ((dst >> 8) & 1) emit_assign(body, cc_dst, mask, value, cnd);
                    return;
                }
                const std::string d = std::string(((dst >> 7) & 1) ? "h[" : "r[") + std::to_string((dst >> 1) & 0x3F) + "]";
                emit_assign(body, d, mask, value, cnd);
                if ((dst >> 8) & 1) emit_assign(body, cc_dst, mask, d, "");
            };

            auto sample = [&](const std::string& kind) {
                t.textures |= 1u << tex;
                const std::string s = "tex" + std::to_string(tex), c = src(s0w, 0), sc = "texscale[" + std::to_string(tex) + "]";
                std::string v;
                if (cube)
                {
                    if (kind == "lod") v = "textureLod(" + s + "c, " + c + ".xyz, " + c + ".w)";
                    else if (kind == "proj") v = "texture(" + s + "c, " + c + ".xyz / " + c + ".w)";
                    else v = "texture(" + s + "c, " + c + ".xyz)";
                }
                else if (kind == "proj") v = "texture(" + s + ", (" + c + ".xy / " + c + ".w) * " + sc + ".xy)";
                else if (kind == "lod") v = "textureLod(" + s + ", " + c + ".xy * " + sc + ".xy, " + c + ".w)";
                else if (kind == "bias") v = "texture(" + s + ", " + c + ".xy * " + sc + ".xy, " + c + ".w)";
                else if (kind == "grad") v = "textureGrad(" + s + ", " + c + ".xy * " + sc + ".xy, " + src(s1w, 1) + ".xy, " + src(s2w, 2) + ".xy)";
                else v = "texture(" + s + ", " + c + ".xy * " + sc + ".xy)";
                if ((dst >> 21) & 1) v = "(" + v + " * 2. - 1.)"; // _bx2
                set_dst(v);
            };

            const std::string a = src(s0w, 0), b = src(s1w, 1), c = src(s2w, 2);
            switch (opcode)
            {
            case 0x00: break;
            case 0x01: set_dst(a); break;
            case 0x02: set_dst(a + " * " + b); break;
            case 0x03: set_dst(a + " + " + b); break;
            case 0x04: set_dst("fma(" + a + ", " + b + ", " + c + ")"); break;
            case 0x05: set_dst("vec4(dot(" + a + ".xyz, " + b + ".xyz))"); break;
            case 0x06: set_dst("vec4(dot(" + a + ", " + b + "))"); break;
            case 0x07: set_dst("vec4(1., " + a + ".y * " + b + ".y, " + a + ".z, " + b + ".w)"); break;
            case 0x08: set_dst("min(" + a + ", " + b + ")"); break;
            case 0x09: set_dst("max(" + a + ", " + b + ")"); break;
            case 0x0A: set_dst("vec4(lessThan(" + a + ", " + b + "))"); break;
            case 0x0B: set_dst("vec4(greaterThanEqual(" + a + ", " + b + "))"); break;
            case 0x0C: set_dst("vec4(lessThanEqual(" + a + ", " + b + "))"); break;
            case 0x0D: set_dst("vec4(greaterThan(" + a + ", " + b + "))"); break;
            case 0x0E: set_dst("vec4(notEqual(" + a + ", " + b + "))"); break;
            case 0x0F: set_dst("vec4(equal(" + a + ", " + b + "))"); break;
            case 0x10: set_dst("fract(" + a + ")"); break;
            case 0x11: set_dst("floor(" + a + ")"); break;
            case 0x12: body += "  if (" + any_cnd + ") discard;\n"; break; // KIL
            case 0x13: set_dst("vec4(uintBitsToFloat(packSnorm4x8(" + a + ")))"); break;
            case 0x14: set_dst("unpackSnorm4x8(floatBitsToUint(" + a + ".x))"); break;
            case 0x15: set_dst("dFdx(" + a + ")"); break;
            case 0x16: set_dst("dFdy(" + a + ")"); break;
            case 0x17: sample("tex"); break;
            case 0x18: sample("proj"); break;
            case 0x19: sample("grad"); break;
            case 0x1A: set_dst("vec4(1. / " + a + ".x)"); break;
            case 0x1B: set_dst("vec4(inversesqrt(abs(" + a + ".x)))"); break;
            case 0x1C: set_dst("vec4(exp2(" + a + ".x))"); break;
            case 0x1D: set_dst("vec4(log2(" + a + ".x))"); break;
            case 0x1E: set_dst("lit_(" + a + ")"); break;
            case 0x1F: set_dst("mix(" + c + ", " + b + ", " + a + ")"); break;
            case 0x20: set_dst("vec4(1.)"); break;
            case 0x21: set_dst("vec4(0.)"); break;
            case 0x22: set_dst("vec4(cos(" + a + ".x))"); break;
            case 0x23: set_dst("vec4(sin(" + a + ".x))"); break;
            case 0x24: set_dst("vec4(uintBitsToFloat(packHalf2x16(" + a + ".xy)))"); break;
            case 0x25: set_dst("unpackHalf2x16(floatBitsToUint(" + a + ".x)).xyxy"); break;
            case 0x26: set_dst("vec4(pow(" + a + ".x, " + b + ".x))"); break;
            case 0x27: set_dst("vec4(uintBitsToFloat(packUnorm4x8(" + a + ")))"); break;
            case 0x28: set_dst("unpackUnorm4x8(floatBitsToUint(" + a + ".x))"); break;
            case 0x29: set_dst("vec4(uintBitsToFloat(packUnorm2x16(" + a + ".xy)))"); break;
            case 0x2A: set_dst("unpackUnorm2x16(floatBitsToUint(" + a + ".x)).xyxy"); break;
            case 0x2B: set_dst(a + ".xyxy + " + b + ".xxxx * " + c + ".xzxz + " + b + ".yyyy * " + c + ".ywyw"); break;
            case 0x2E: set_dst("vec4(" + a + ".x * " + b + ".x + " + a + ".y * " + b + ".y + " + c + ".z)"); break;
            case 0x2F: sample("lod"); break;
            case 0x31: sample("bias"); break;
            case 0x36: set_dst(a + " - 2.0 * dot(" + a + ", " + b + ") * " + b); break;
            case 0x38: set_dst("vec4(dot(" + a + ".xy, " + b + ".xy))"); break;
            case 0x39: set_dst("vec4(normalize(" + a + ".xyz), 0.).xyzz"); break;
            case 0x3A: set_dst(a + " / " + b + ".x"); break;
            case 0x3B: set_dst(a + " / sqrt(" + b + ".x)"); break;
            case 0x3C: set_dst("lit_(" + a + ")"); break;
            case 0x3D: case 0x3E: break; // FENCT / FENCB
            case 0x40: if (loop_depth) body += "  if (" + any_cnd + ") break;\n"; break;
            case 0x42: // IFE
            {
                const uint32_t else_slot = (s1w & 0x7FFFFFFF) >> 2, end_slot = s2w >> 2;
                body += "  if (" + any_cnd + ") {\n";
                if (else_slot != end_slot && else_slot > pc) closers.push_back({ else_slot, "  } else {\n" });
                closers.push_back({ end_slot, "  }\n" });
                break;
            }
            case 0x43: case 0x44: // LOOP / REP
            {
                const uint32_t n = (s1w >> 2) & 0xFF, end_slot = s2w >> 2;
                const std::string i = "i" + std::to_string(loop_depth++);
                body += "  if (" + any_cnd + ") for (int " + i + " = 0; " + i + " < " + std::to_string(n) + "; " + i + "++) {\n";
                closers.push_back({ end_slot, "  } // loop\n" });
                break;
            }
            case 0x45: body += "  if (" + any_cnd + ") { emit_(); return; }\n"; break;
            default: body += "  // fp op " + std::to_string(opcode) + "\n"; break;
            }
            if (has_const && !flow) pc++;
            if (end) break;
        }
        for (auto& c : closers) body += c.second;

        std::string& g = t.glsl;
        g = "#version 430 compatibility\n"
            "in vec4 v_col0, v_col1, v_fog;\nin vec4 v_tc[10];\n"
            "uniform vec4 texscale[16];\n";
        if (!t.constant_slots.empty()) g += "uniform vec4 fc[" + std::to_string(t.constant_slots.size()) + "];\n";
        for (uint32_t i = 0; i < 16; i++)
            if (t.textures & (1u << i))
                g += ((tex_cube >> i) & 1) ? "uniform samplerCube tex" + std::to_string(i) + "c;\n" : "uniform sampler2D tex" + std::to_string(i) + ";\n";
        g += "vec4 r[48]; vec4 h[96]; vec4 cc[2];\n"
             "vec4 lit_(vec4 v) { vec4 c = v; c.x = max(v.x, 0.); c.y = max(v.y, 0.);"
             " return vec4(1., c.x, c.x > 0. ? exp(c.w * log(max(c.y, 1e-10))) : 0., 1.); }\n";
        const bool exports32 = ctrl & 0x40; // RSX_SHADER_CONTROL_32_BITS_EXPORTS
        g += std::string("void emit_() {\n  gl_FragColor = ") + (exports32 ? "r[0]" : "h[0]") + ";\n";
        if (ctrl & 0xE) g += std::string("  gl_FragDepth = ") + (exports32 ? "r[1].z" : "h[2].z") + ";\n";
        g += "}\nvoid main() {\n  for (int i = 0; i < 48; i++) r[i] = vec4(0.);\n  for (int i = 0; i < 96; i++) h[i] = vec4(0.);\n"
             "  cc[0] = vec4(0.); cc[1] = vec4(0.);\n";
        g += body;
        g += "  emit_();\n}\n";
        return t;
    }

    // ---- programs ----
    struct Program
    {
        GLuint id = 0;
        GLint vc = -1, so_scale = -1, so_offset = -1, fc = -1, texscale = -1;
        GLint tex[16];
        uint32_t inputs = 0, textures = 0;
        std::vector<uint32_t> constant_slots;
    };
    std::unordered_map<uint64_t, GLuint> g_vs_cache, g_fs_cache;
    std::unordered_map<uint64_t, VpTranslation> g_vp_info;
    std::unordered_map<uint64_t, FpTranslation> g_fp_info;
    std::unordered_map<uint64_t, Program> g_programs;

    GLuint compile(GLenum type, const std::string& src)
    {
        const GLuint s = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char info[2048];
            glGetShaderInfoLog(s, sizeof(info), nullptr, info);
            log_line("rsx: %s shader compile failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
            log_line("rsx: source:\n%s", src.c_str());
        }
        return s;
    }

    Program* get_program()
    {
        // Vertex program: instructions from the start slot up to the end bit
        const uint32_t start = reg(TRANSFORM_PROGRAM_START) & 511;
        uint32_t vp_end = start;
        while (vp_end < 511 && !(g_vp[vp_end * 4 + 3] & 1)) vp_end++;
        const uint64_t vp_key = fnv(&g_vp[start * 4], (vp_end - start + 1) * 16, start);

        // Fragment program: walk to the end bit, hashing instructions but not inline constants
        const uint32_t sp = reg(SHADER_PROGRAM);
        const uint32_t fp_addr = rsx_guest_address(sp & ~3u, (sp & 3) - 1);
        if (!fp_addr) return nullptr;
        uint32_t tex_cube = 0;
        for (uint32_t i = 0; i < 16; i++)
            if (reg(TEXTURE_OFFSET + i * 0x20 + 4) & 4) tex_cube |= 1u << i;
        const uint32_t ctrl = reg(SHADER_CONTROL) & 0x4E;
        uint64_t fp_key = fnv(&ctrl, 4, tex_cube);
        for (uint32_t pc = 0; pc < 4096; pc++)
        {
            const uint32_t base = fp_addr + pc * 16;
            fp_key = fnv(g_base + base, 16, fp_key);
            const uint32_t dst = fp_word(base), s0 = fp_word(base + 4), s1 = fp_word(base + 8), s2 = fp_word(base + 12);
            const uint32_t opcode = ((dst >> 24) & 0x3F) | ((s1 >> 31) << 6);
            if (opcode < 0x40 && (((s0 & 3) == 2) || ((s1 & 3) == 2) || ((s2 & 3) == 2))) pc++;
            if (dst & 1) break;
        }

        const uint64_t key = vp_key * 31 + fp_key;
        if (auto it = g_programs.find(key); it != g_programs.end()) return &it->second;

        auto vi = g_vp_info.find(vp_key);
        if (vi == g_vp_info.end())
        {
            vi = g_vp_info.emplace(vp_key, translate_vp(start)).first;
            g_vs_cache[vp_key] = compile(GL_VERTEX_SHADER, vi->second.glsl);
        }
        auto fi = g_fp_info.find(fp_key);
        if (fi == g_fp_info.end())
        {
            fi = g_fp_info.emplace(fp_key, translate_fp(fp_addr, ctrl, tex_cube)).first;
            g_fs_cache[fp_key] = compile(GL_FRAGMENT_SHADER, fi->second.glsl);
        }
        Program p;
        p.id = glCreateProgram();
        glAttachShader(p.id, g_vs_cache[vp_key]);
        glAttachShader(p.id, g_fs_cache[fp_key]);
        glLinkProgram(p.id);
        GLint ok = 0;
        glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char info[2048];
            glGetProgramInfoLog(p.id, sizeof(info), nullptr, info);
            log_line("rsx: program link failed: %s", info);
        }
        p.vc = glGetUniformLocation(p.id, "vc");
        p.so_scale = glGetUniformLocation(p.id, "so_scale");
        p.so_offset = glGetUniformLocation(p.id, "so_offset");
        p.fc = glGetUniformLocation(p.id, "fc");
        p.texscale = glGetUniformLocation(p.id, "texscale");
        for (uint32_t i = 0; i < 16; i++)
        {
            const std::string n = "tex" + std::to_string(i) + (((tex_cube >> i) & 1) ? "c" : "");
            p.tex[i] = glGetUniformLocation(p.id, n.c_str());
        }
        p.inputs = vi->second.inputs;
        p.textures = fi->second.textures;
        p.constant_slots = fi->second.constant_slots;
        static uint32_t count;
        if (++count <= 64 || count % 256 == 0) log_line("rsx: program %u (vp start %u, fp %08X, %zu fp constants)", count, start, fp_addr, p.constant_slots.size());
        return &g_programs.emplace(key, std::move(p)).first->second;
    }

    // ---- textures ----
    struct Texture
    {
        GLuint id = 0;
        uint64_t hash = 0;
    };
    std::unordered_map<uint64_t, Texture> g_textures;

    uint32_t swizzle_offset(uint32_t x, uint32_t y, uint32_t log2w, uint32_t log2h)
    {
        uint32_t offset = 0, shift = 0;
        for (uint32_t bit = 0; bit < log2w || bit < log2h; bit++)
        {
            if (bit < log2w) offset |= ((x >> bit) & 1) << shift++;
            if (bit < log2h) offset |= ((y >> bit) & 1) << shift++;
        }
        return offset;
    }

    uint32_t log2u(uint32_t v)
    {
        uint32_t l = 0;
        while ((1u << l) < v) l++;
        return l;
    }

    GLint wrap_mode(uint32_t w)
    {
        switch (w)
        {
        case 1: return GL_REPEAT;
        case 2: return GL_MIRRORED_REPEAT;
        case 3: return GL_CLAMP_TO_EDGE;
        case 4: return GL_CLAMP_TO_BORDER;
        case 5: return GL_CLAMP;
        default: return GL_CLAMP_TO_EDGE;
        }
    }

    // Binds fragment texture unit i; returns its coordinate scale (1/size for unnormalized textures).
    void bind_texture(uint32_t i, float* scale)
    {
        const uint32_t b = TEXTURE_OFFSET + i * 0x20;
        const uint32_t offset = reg(b), format = reg(b + 4), address = reg(b + 8), filter = reg(b + 0x14), rect = reg(b + 0x18);
        const uint32_t remap = reg(b + 0x10), pitch_reg = reg(TEXTURE_CONTROL3 + i * 4) & 0xFFFFF;
        const uint32_t w = rect >> 16, h = rect & 0xFFFF;
        const uint32_t fmt = (format >> 8) & 0xFF, base_fmt = fmt & ~0x60u;
        const bool linear = fmt & 0x20, unnormalized = fmt & 0x40;
        scale[0] = unnormalized && w ? 1.f / w : 1.f;
        scale[1] = unnormalized && h ? 1.f / h : 1.f;
        scale[2] = scale[3] = 1.f;
        glActiveTexture(GL_TEXTURE0 + i);
        const uint32_t location = (format & 3) - 1;

        // Render-to-texture: sample the GL surface directly
        if (location == 0)
            if (auto it = g_surfaces.find(offset); it != g_surfaces.end())
            {
                glBindTexture(GL_TEXTURE_2D, it->second.tex);
                if (!unnormalized && it->second.width && it->second.height)
                {
                    scale[0] = float(w) / it->second.width;
                    scale[1] = float(h) / it->second.height;
                }
                else if (unnormalized)
                {
                    scale[0] = 1.f / it->second.width;
                    scale[1] = 1.f / it->second.height;
                }
                return;
            }

        const uint32_t addr = rsx_guest_address(offset, location);
        if (!addr || !w || !h)
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            return;
        }
        uint32_t bpp = 4;
        bool compressed = false;
        switch (base_fmt)
        {
        case 0x81: bpp = 1; break;                       // B8
        case 0x82: case 0x83: case 0x84: case 0x8B: case 0x8F: case 0x92: case 0x97: case 0x9D: bpp = 2; break;
        case 0x86: case 0x87: case 0x88: compressed = true; break;
        case 0x9A: bpp = 8; break;                       // W16_Z16_Y16_X16_FLOAT
        case 0x9B: bpp = 16; break;                      // W32_Z32_Y32_X32_FLOAT
        default: break;
        }
        const uint32_t block = base_fmt == 0x86 ? 8 : 16;
        const uint32_t pitch = compressed ? ((w + 3) / 4) * block : (linear && pitch_reg ? pitch_reg : w * bpp);
        const uint32_t size = compressed ? pitch * ((h + 3) / 4) : (linear ? pitch * h : w * h * bpp);
        const uint64_t key = (uint64_t(addr) << 32) ^ (uint64_t(format) << 8) ^ rect;
        Texture& t = g_textures[key];
        const uint64_t hash = fnv(g_base + addr, size, remap);
        if (!t.id) glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        if (t.hash != hash)
        {
            t.hash = hash;
            if (compressed)
            {
                const GLenum gl = base_fmt == 0x86 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : base_fmt == 0x87 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, gl, w, h, 0, size, g_base + addr);
            }
            else
            {
                // Decode to RGBA8 (A/R/G/B source channels kept in place), remap applied through texture swizzle
                std::vector<uint8_t> px(size_t(w) * h * 4);
                const uint32_t lw = log2u(w), lh = log2u(h);
                for (uint32_t y = 0; y < h; y++)
                    for (uint32_t x = 0; x < w; x++)
                    {
                        const uint32_t texel = linear ? y * pitch + x * bpp : swizzle_offset(x, y, lw, lh) * bpp;
                        const uint8_t* s = g_base + addr + texel;
                        uint8_t* d = &px[(size_t(y) * w + x) * 4];
                        uint32_t a = 255, r = 0, g = 0, bl = 0;
                        const uint32_t v16 = bpp == 2 ? (s[0] << 8) | s[1] : 0;
                        switch (base_fmt)
                        {
                        case 0x81: a = r = g = bl = s[0]; break;
                        case 0x82: a = (v16 >> 15) * 255; r = ((v16 >> 10) & 31) * 255 / 31; g = ((v16 >> 5) & 31) * 255 / 31; bl = (v16 & 31) * 255 / 31; break;
                        case 0x9D: r = ((v16 >> 10) & 31) * 255 / 31; g = ((v16 >> 5) & 31) * 255 / 31; bl = (v16 & 31) * 255 / 31; break;
                        case 0x97: r = (v16 >> 11) * 255 / 31; g = ((v16 >> 6) & 31) * 255 / 31; bl = ((v16 >> 1) & 31) * 255 / 31; a = (v16 & 1) * 255; break;
                        case 0x83: a = (v16 >> 12) * 17; r = ((v16 >> 8) & 15) * 17; g = ((v16 >> 4) & 15) * 17; bl = (v16 & 15) * 17; break;
                        case 0x84: r = (v16 >> 11) * 255 / 31; g = ((v16 >> 5) & 63) * 255 / 63; bl = (v16 & 31) * 255 / 31; break;
                        case 0x8B: a = g = s[0]; r = bl = s[1]; break; // G8B8
                        case 0x92: a = r = g = bl = s[0]; break;       // X16 (high byte)
                        case 0x9E: r = s[1]; g = s[2]; bl = s[3]; break; // D8R8G8B8
                        default: a = s[0]; r = s[1]; g = s[2]; bl = s[3]; break; // A8R8G8B8 and anything unhandled
                        }
                        d[0] = uint8_t(r); d[1] = uint8_t(g); d[2] = uint8_t(bl); d[3] = uint8_t(a);
                    }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            }
            static const GLint from[4] = { GL_ALPHA, GL_RED, GL_GREEN, GL_BLUE }; // remap source order A, R, G, B
            const uint32_t rm = (base_fmt == 0x9A || base_fmt == 0x9B || base_fmt == 0x9C) ? (remap & 0xFF00) | 0xE4 : remap;
            GLint out_argb[4];
            for (int c = 0; c < 4; c++)
            {
                const uint32_t ctl = (rm >> (8 + c * 2)) & 3;
                out_argb[c] = ctl == 0 ? GL_ZERO : ctl == 1 ? GL_ONE : from[(rm >> (c * 2)) & 3];
            }
            const GLint swz[4] = { out_argb[1], out_argb[2], out_argb[3], out_argb[0] };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swz);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        }
        const uint32_t minf = (filter >> 16) & 7, magf = (filter >> 24) & 7;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (minf == 1 || minf == 3 || minf == 5) ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magf == 1 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode(address & 0xF));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode((address >> 8) & 0xF));
    }

    // ---- draws ----
    uint32_t g_primitive = 0;
    struct Range { uint32_t first, count; };
    std::vector<Range> g_ranges;
    bool g_indexed = false;
    std::vector<uint32_t> g_inline_data, g_immediate_indices;
    float g_attrib_reg[16][4];
    uint64_t g_draw_count = 0;

    float half_to_float(uint16_t h)
    {
        const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 0x3FF;
        uint32_t f;
        if (e == 0) f = (s << 31) | (m ? 0 : 0); // flush denormals
        else if (e == 31) f = (s << 31) | 0x7F800000 | (m << 13);
        else f = (s << 31) | ((e + 112) << 23) | (m << 13);
        float r;
        memcpy(&r, &f, 4);
        return r;
    }

    // Reads attribute `attr` of vertex `index` into out[4].
    void fetch_attrib(uint32_t attr, uint32_t base_addr, uint32_t fmt, uint32_t index, float* out)
    {
        const uint32_t type = fmt & 0xF, size = (fmt >> 4) & 0xF, stride = (fmt >> 8) & 0xFF;
        out[0] = out[1] = out[2] = 0.f;
        out[3] = 1.f;
        const uint32_t a = base_addr + index * stride;
        for (uint32_t c = 0; c < size && c < 4; c++)
        {
            switch (type)
            {
            case 1: out[c] = std::max(-1.f, int16_t(guest_load_u32(a + c * 2) >> 16) / 32767.f); break;       // S1
            case 2: { const uint32_t v = guest_load_u32(a + c * 4); memcpy(&out[c], &v, 4); break; }      // F
            case 3: out[c] = half_to_float(uint16_t(guest_load_u32(a + c * 2) >> 16)); break;              // SF
            case 4: out[c] = g_base[a + c] / 255.f; break;                                                // UB
            case 5: out[c] = float(int16_t(guest_load_u32(a + c * 2) >> 16)); break;                      // S32K
            case 7: out[c] = float(g_base[a + c]); break;                                                 // UB256
            case 6: // CMP: 11:11:10 signed normalized
            {
                const uint32_t v = guest_load_u32(a);
                const int32_t x = int32_t(v << 21) >> 21, y = int32_t((v >> 11) << 21) >> 21, z = int32_t(v) >> 22;
                out[0] = x / 1023.f; out[1] = y / 1023.f; out[2] = z / 511.f; out[3] = 1.f;
                return;
            }
            default: break;
            }
        }
        (void)attr;
    }

    GLenum gl_compare(uint32_t f) { return f >= 0x200 && f <= 0x207 ? f : GL_ALWAYS; }

    void apply_state(Surface* s)
    {
        // Scissor (RSX y is top-down, which is how surfaces are stored)
        const uint32_t sh = reg(SCISSOR_HORIZONTAL), sv = reg(SCISSOR_VERTICAL);
        glEnable(GL_SCISSOR_TEST);
        glScissor(sh & 0xFFFF, sv & 0xFFFF, sh >> 16, sv >> 16);
        (void)s;

        const uint32_t cm = reg(COLOR_MASK);
        glColorMask((cm >> 16) & 1, (cm >> 8) & 1, cm & 1, (cm >> 24) & 1);

        if (reg(BLEND_ENABLE))
        {
            glEnable(GL_BLEND);
            const uint32_t sf = reg(BLEND_FUNC_SFACTOR), df = reg(BLEND_FUNC_DFACTOR), eq = reg(BLEND_EQUATION);
            glBlendFuncSeparate(sf & 0xFFFF, df & 0xFFFF, sf >> 16, df >> 16);
            auto fix_eq = [](uint32_t e) -> GLenum { return (e >= 0x8006 && e <= 0x800B) ? e : GL_FUNC_ADD; };
            glBlendEquationSeparate(fix_eq(eq & 0xFFFF), fix_eq(eq >> 16));
            const uint32_t bc = reg(BLEND_COLOR);
            glBlendColor(((bc >> 16) & 0xFF) / 255.f, ((bc >> 8) & 0xFF) / 255.f, (bc & 0xFF) / 255.f, (bc >> 24) / 255.f);
        }
        else glDisable(GL_BLEND);

        if (reg(ALPHA_TEST_ENABLE))
        {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(gl_compare(reg(ALPHA_FUNC)), (reg(ALPHA_REF) & 0xFF) / 255.f);
        }
        else glDisable(GL_ALPHA_TEST);

        if (reg(DEPTH_TEST_ENABLE))
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(gl_compare(reg(DEPTH_FUNC)));
        }
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(reg(DEPTH_MASK) ? GL_TRUE : GL_FALSE);

        if (reg(STENCIL_TEST_ENABLE))
        {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(gl_compare(reg(STENCIL_FUNC)), reg(STENCIL_FUNC_REF) & 0xFF, reg(STENCIL_FUNC_MASK) & 0xFF);
            glStencilOp(reg(STENCIL_OP_FAIL), reg(STENCIL_OP_ZFAIL), reg(STENCIL_OP_ZPASS));
            glStencilMask(reg(STENCIL_MASK) & 0xFF);
        }
        else glDisable(GL_STENCIL_TEST);

        if (reg(CULL_FACE_ENABLE))
        {
            glEnable(GL_CULL_FACE);
            const uint32_t cf = reg(CULL_FACE);
            glCullFace(cf == GL_FRONT || cf == GL_BACK || cf == GL_FRONT_AND_BACK ? cf : GL_BACK);
            // Surfaces are stored top-down, which mirrors winding
            glFrontFace(reg(FRONT_FACE) == GL_CW ? GL_CCW : GL_CW);
        }
        else glDisable(GL_CULL_FACE);
    }

    void draw()
    {
        if (!ensure_gl() || (g_ranges.empty() && g_inline_data.empty() && g_immediate_indices.empty())) return;
        Surface* s = bind_surface();
        if (!s) return;
        Program* p = get_program();
        if (!p) return;
        g_draw_count++;
        glUseProgram(p->id);

        // Vertex index list for this draw (rebased onto a compact vertex buffer)
        std::vector<uint32_t> indices;
        const uint32_t base_index = reg(VERTEX_DATA_BASE_INDEX);
        if (!g_immediate_indices.empty()) indices = g_immediate_indices;
        else if (g_indexed)
        {
            const uint32_t dma = reg(INDEX_ARRAY_DMA);
            const uint32_t ia = rsx_guest_address(reg(INDEX_ARRAY_ADDRESS), dma & 1);
            const bool u16 = ((dma >> 4) & 0xF) == 1;
            if (!ia) return;
            for (const Range& r : g_ranges)
                for (uint32_t i = 0; i < r.count; i++)
                {
                    const uint32_t e = r.first + i;
                    indices.push_back(u16 ? guest_load_u32(ia + e * 2) >> 16 : guest_load_u32(ia + e * 4));
                }
        }
        else
            for (const Range& r : g_ranges)
                for (uint32_t i = 0; i < r.count; i++) indices.push_back(r.first + i);

        const bool restart = reg(RESTART_INDEX_ENABLE) & 1;
        const uint32_t restart_index = reg(RESTART_INDEX);
        uint32_t lo = ~0u, hi = 0;
        for (uint32_t v : indices)
            if (!(restart && v == restart_index)) { lo = std::min(lo, v); hi = std::max(hi, v); }
        const bool inline_mode = !g_inline_data.empty();
        uint32_t vertex_count = inline_mode ? 0 : (lo <= hi ? hi - lo + 1 : 0);

        // Gather enabled attributes as float4
        uint32_t inline_stride = 0;
        if (inline_mode)
        {
            for (uint32_t a = 0; a < 16; a++)
            {
                const uint32_t fmt = reg(VERTEX_DATA_ARRAY_FORMAT + a * 4);
                if ((fmt >> 4) & 0xF) inline_stride += ((fmt >> 4) & 0xF) * 4; // inline arrays are float32
            }
            vertex_count = inline_stride ? uint32_t(g_inline_data.size() * 4 / inline_stride) : 0;
        }
        if (!vertex_count || vertex_count > 4 * 1024 * 1024) return;
        std::vector<float> data(size_t(vertex_count) * 16 * 4);
        uint32_t used = 0, inline_off = 0;
        for (uint32_t a = 0; a < 16; a++)
        {
            if (!(p->inputs & (1u << a))) continue;
            const uint32_t fmt = reg(VERTEX_DATA_ARRAY_FORMAT + a * 4);
            const uint32_t size = (fmt >> 4) & 0xF;
            if (!size)
            {
                glDisableVertexAttribArray(a);
                glVertexAttrib4fv(a, g_attrib_reg[a]);
                continue;
            }
            float* dst = &data[size_t(used) * vertex_count * 4];
            if (inline_mode)
            {
                for (uint32_t v = 0; v < vertex_count; v++)
                    for (uint32_t c = 0; c < 4; c++)
                    {
                        float f = c == 3 ? 1.f : 0.f;
                        if (c < size) { const uint32_t w = g_inline_data[(v * inline_stride + inline_off) / 4 + c]; memcpy(&f, &w, 4); }
                        dst[v * 4 + c] = f;
                    }
                inline_off += size * 4;
            }
            else
            {
                const uint32_t off = reg(VERTEX_DATA_ARRAY_OFFSET + a * 4);
                const uint32_t base = rsx_guest_address((off & 0x7FFFFFFF) + reg(VERTEX_DATA_BASE_OFFSET), off >> 31);
                if (!base) continue;
                const uint32_t add = g_indexed ? base_index : 0;
                for (uint32_t v = 0; v < vertex_count; v++) fetch_attrib(a, base, fmt, lo + v + add, &dst[v * 4]);
            }
            glEnableVertexAttribArray(a);
            glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<void*>(size_t(used) * vertex_count * 16));
            used++;
        }
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glBufferData(GL_ARRAY_BUFFER, size_t(used) * vertex_count * 16, data.data(), GL_STREAM_DRAW);
        // Pointers were set before the buffer was bound to it: rebind them now
        used = 0;
        for (uint32_t a = 0; a < 16; a++)
        {
            if (!(p->inputs & (1u << a)) || !((reg(VERTEX_DATA_ARRAY_FORMAT + a * 4) >> 4) & 0xF)) continue;
            glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<void*>(size_t(used) * vertex_count * 16));
            used++;
        }

        // Uniforms
        glUniform4fv(p->vc, 468, reinterpret_cast<const float*>(g_vc));
        const float cw = float(clip_w()), ch = float(clip_h());
        const float scale[4] = { regf(VIEWPORT_SCALE) / (cw / 2), regf(VIEWPORT_SCALE + 4) / (ch / 2), regf(VIEWPORT_SCALE + 8), 0 };
        const float offs[4] = { (regf(VIEWPORT_OFFSET) - cw / 2) / (cw / 2), (regf(VIEWPORT_OFFSET + 4) - ch / 2) / (ch / 2), regf(VIEWPORT_OFFSET + 8), 0 };
        glUniform4fv(p->so_scale, 1, scale);
        glUniform4fv(p->so_offset, 1, offs);
        if (!p->constant_slots.empty())
        {
            const uint32_t sp = reg(SHADER_PROGRAM);
            const uint32_t fp_addr = rsx_guest_address(sp & ~3u, (sp & 3) - 1);
            std::vector<uint32_t> fc;
            for (uint32_t slot : p->constant_slots)
                for (uint32_t c = 0; c < 4; c++) fc.push_back(fp_word(fp_addr + slot + c * 4));
            glUniform4fv(p->fc, GLsizei(p->constant_slots.size()), reinterpret_cast<const float*>(fc.data()));
        }
        float texscale[16][4];
        for (uint32_t i = 0; i < 16; i++)
        {
            texscale[i][0] = texscale[i][1] = texscale[i][2] = texscale[i][3] = 1.f;
            if (!(p->textures & (1u << i))) continue;
            bind_texture(i, texscale[i]);
            glUniform1i(p->tex[i], i);
        }
        glUniform4fv(p->texscale, 16, &texscale[0][0]);

        apply_state(s);
        if (restart)
        {
            glEnable(GL_PRIMITIVE_RESTART);
            glPrimitiveRestartIndex(restart_index - lo);
        }
        else glDisable(GL_PRIMITIVE_RESTART);
        const GLenum mode = g_primitive >= 1 && g_primitive <= 10 ? GLenum(g_primitive - 1) : GL_TRIANGLES;
        if (inline_mode) glDrawArrays(mode, 0, vertex_count);
        else
        {
            for (uint32_t& v : indices) v -= lo; // restart index stays distinct after rebasing
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * 4, indices.data(), GL_STREAM_DRAW);
            glDrawElements(mode, GLsizei(indices.size()), GL_UNSIGNED_INT, nullptr);
        }
        for (uint32_t a = 0; a < 16; a++) glDisableVertexAttribArray(a);
    }

    void present(uint32_t offset, uint32_t width, uint32_t height)
    {
        if (!ensure_gl()) return;
        RECT rc;
        GetClientRect(g_hwnd, &rc);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, rc.right, rc.bottom);
        const auto it = g_surfaces.find(offset);
        if (it == g_surfaces.end())
        {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        else
        {
            // Surfaces are stored top row first; flip vertically into the window.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, it->second.fbo);
            glBlitFramebuffer(0, 0, width, height, 0, rc.bottom, rc.right, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        SwapBuffers(g_hdc);
        static uint32_t frames;
        if (++frames % 600 == 1) log_line("rsx: presented frame %u (%llu draws so far)", frames, (unsigned long long)g_draw_count);
    }
}

void rsx_gl_method(uint32_t method, uint32_t arg)
{
    g_regs[(method & 0xFFFF) >> 2] = arg;
    if (method >= TRANSFORM_PROGRAM && method < TRANSFORM_PROGRAM + 0x80)
    {
        if (g_vp_word < 512 * 4) g_vp[g_vp_word++] = arg;
        return;
    }
    if (method >= TRANSFORM_CONSTANT && method < TRANSFORM_CONSTANT + 0x80)
    {
        const uint32_t word = reg(TRANSFORM_CONSTANT_LOAD) * 4 + (method - TRANSFORM_CONSTANT) / 4;
        if (word < 468 * 4) g_vc[word] = arg;
        return;
    }
    if (method >= VERTEX_DATA4F_M && method < VERTEX_DATA4F_M + 0x100)
    {
        const uint32_t i = (method - VERTEX_DATA4F_M) / 4;
        memcpy(&g_attrib_reg[i / 4][i % 4], &arg, 4);
        return;
    }
    if (method >= VERTEX_DATA2F_M && method < VERTEX_DATA2F_M + 0x80)
    {
        const uint32_t i = (method - VERTEX_DATA2F_M) / 4;
        memcpy(&g_attrib_reg[i / 2][i % 2], &arg, 4);
        g_attrib_reg[i / 2][2] = 0.f;
        g_attrib_reg[i / 2][3] = 1.f;
        return;
    }
    if (method >= VERTEX_DATA1F_M && method < VERTEX_DATA1F_M + 0x40)
    {
        const uint32_t i = (method - VERTEX_DATA1F_M) / 4;
        memcpy(&g_attrib_reg[i][0], &arg, 4);
        g_attrib_reg[i][1] = g_attrib_reg[i][2] = 0.f;
        g_attrib_reg[i][3] = 1.f;
        return;
    }
    if (method >= VERTEX_DATA4UB_M && method < VERTEX_DATA4UB_M + 0x40)
    {
        const uint32_t i = (method - VERTEX_DATA4UB_M) / 4;
        for (uint32_t c = 0; c < 4; c++) g_attrib_reg[i][c] = ((arg >> (c * 8)) & 0xFF) / 255.f;
        return;
    }
    switch (method)
    {
    case TRANSFORM_PROGRAM_LOAD: g_vp_word = (arg & 511) * 4; break;
    case CLEAR_SURFACE: clear(arg); break;
    case BEGIN_END:
        if (arg)
        {
            g_primitive = arg;
            g_ranges.clear();
            g_inline_data.clear();
            g_immediate_indices.clear();
            g_indexed = false;
        }
        else
            draw();
        break;
    case DRAW_ARRAYS: g_ranges.push_back({ arg & 0xFFFFFF, (arg >> 24) + 1 }); break;
    case DRAW_INDEX_ARRAY: g_indexed = true; g_ranges.push_back({ arg & 0xFFFFFF, (arg >> 24) + 1 }); break;
    case INLINE_ARRAY: g_inline_data.push_back(arg); break;
    case ARRAY_ELEMENT16: g_immediate_indices.push_back(arg & 0xFFFF); g_immediate_indices.push_back(arg >> 16); break;
    case ARRAY_ELEMENT32: g_immediate_indices.push_back(arg); break;
    default: break;
    }
}

// Presents the colour surface at a display buffer offset (the guest's flip). Flips requested from PPU
// threads (sys_rsx_context_attribute 0x102) are handed to the FIFO thread, which owns the context.
void rsx_gl_flip(uint32_t offset, uint32_t width, uint32_t height)
{
    if (g_glrc && GetCurrentThreadId() == g_gl_thread)
        present(offset, width, height);
    else
        g_pending_flip = offset | uint64_t(width) << 32 | uint64_t(height) << 48;
}

// Called by the FIFO thread between commands.
void rsx_gl_poll()
{
    if (const uint64_t f = g_pending_flip.exchange(0); f && ensure_gl())
        present(uint32_t(f), uint32_t(f >> 32) & 0xFFFF, uint32_t(f >> 48));
}

void rsx_gl_init()
{
    for (auto& a : g_attrib_reg) a[0] = a[1] = a[2] = 0.f, a[3] = 1.f;
    std::thread(window_thread).detach();
}
