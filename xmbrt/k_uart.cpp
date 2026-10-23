// sys_uart: the virtual UART to the AV manager (sys_uart.cpp semantics, rpcs3 0b0fa08). vsh sends PS3AV
// command packets and reads replies; HDMI plug / HDCP events arrive on the same stream. A worker thread
// parses what vsh sent, waits ~100 ms like RPCS3 ("give vsh some time"), then publishes the replies.
// Packet validation is not modelled: well-formed packets from vsh get RPCS3's replies.
#include "kernel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
    constexpr uint32_t PS3AV_VERSION = 0x205, PS3AV_REPLY_BIT = 0x80000000, PS3AV_BUF_SIZE = 0x800;
    constexpr uint32_t ST_SUCCESS = 0x00, ST_SYSCON_FAIL = 0x02, ST_INVALID_COMMAND = 0x03, ST_INVALID_PORT = 0x04, ST_NO_SEL = 0x0F;
    constexpr uint32_t EV_UNPLUGGED = 0x01, EV_PLUGGED = 0x02, EV_HDCP_DONE = 0x04, EV_HDCP_REAUTH = 0x10, EV_UNK = 0x80000000;
    constexpr uint8_t HDMI_BEHAVIOR_NORMAL = 0xFF, HDMI_BEHAVIOR_HDCP_OFF = 0x01, HDMI_BEHAVIOR_DVI = 0x40, HDMI_BEHAVIOR_EDID_PASS = 0x80;
    constexpr uint16_t AVPORT_HDMI_0 = 0x0, AVPORT_HDMI_1 = 0x1, AVPORT_AVMULTI_0 = 0x10;
    constexpr uint8_t BKSV[5] = { 0xFF, 0xFF, 0xF0, 0x00, 0x00 }, AKSV[5] = { 0x00, 0x00, 0x0F, 0xFF, 0xFF };
    constexpr uint32_t kMonitorInfoSize = 208; // sizeof(ps3av_get_monitor_info_reply)

    uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
    uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
    void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
    void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }

    enum class HdmiEvent : uint8_t { None = 0, Unplugged = 1, Plugged = 2, HdcpDone = 3 };

    // vuart_hdmi_event_handler: walks from the current state to a target state, one event every 20 ms
    struct HdmiEventHandler
    {
        static constexpr uint64_t kDurationUs = 20000, kThresholdUs = 1000;
        uint64_t next_us = 0;
        HdmiEvent current = HdmiEvent::Plugged, current_to = HdmiEvent::Plugged; // syscon starts plugged
        HdmiEvent base = HdmiEvent::None, target = HdmiEvent::None;

        static uint64_t now_us()
        {
            return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        bool available() const { return next_us != 0; }
        uint64_t until_next() const
        {
            const uint64_t t = now_us();
            return !available() || t + kThresholdUs >= next_us ? 0 : next_us - t;
        }
        void set_target(HdmiEvent start, HdmiEvent end)
        {
            base = HdmiEvent(std::min<uint8_t>(uint8_t(start) - 1, uint8_t(current_to)));
            target = end;
            if (!available()) advance();
        }
        HdmiEvent take()
        {
            if (available() && until_next() == 0)
            {
                const HdmiEvent e = current_to;
                advance();
                return e;
            }
            return HdmiEvent::None;
        }
        void advance()
        {
            current = current_to;
            while (base != target)
            {
                base = HdmiEvent(uint8_t(base) + 1);
                if ((current == HdmiEvent::Unplugged && base == HdmiEvent::Unplugged) ||
                    (current == HdmiEvent::Plugged && base == HdmiEvent::Plugged) ||
                    (current == HdmiEvent::HdcpDone && base == HdmiEvent::Plugged))
                    continue;
                current_to = base;
                next_us = now_us() + kDurationUs;
                return;
            }
            next_us = 0;
        }
    };

    struct Vuart
    {
        std::mutex m;
        std::condition_variable tx_cv;
        std::vector<uint8_t> tx, rx;           // bytes from vsh / bytes for vsh
        std::vector<uint8_t> reply, reply_sc; // replies collected while parsing (normal, syscon)
        bool initialized = false;
        uint16_t av_cmd_ver = PS3AV_VERSION;
        uint32_t events_mask = 0;
        uint8_t hdmi_behavior = HDMI_BEHAVIOR_NORMAL;
        bool hdmi_res_set = false, hdcp_first_auth = true, head_b_initialized = false;
        HdmiEventHandler hdmi;
    } g_vuart;
    std::array<uint32_t,5> g_audio_routes{3,3,3,3,3}; // HDMI0/1, analog, SPDIF0/1

    bool configure_audio(const uint8_t* p,uint16_t size)
    {
        if(size<68)return false;
        const uint32_t av=p[8],source=be32(p+32),freq=be32(p+20);
        if(freq>7)return false;
        uint32_t index,port;
        if(av<=1){index=av;port=source==1?2:0;}
        else if(av==0x10){index=2;port=0;}
        else if(av==0x20||av==0x21){index=av-0x20+3;port=index-2;}
        else return false;
        constexpr uint32_t rates[8]={48000,48000,44100,48000,88200,96000,176400,192000};
        const uint32_t bytes=(be32(p+24)==1||(port!=0&&be32(p+28)!=1))?2:4;
        rsxaudio_configure_port(port,rates[freq],bytes,av>=0x20&&source==0);
        g_audio_routes[index]=(av>=0x20&&source==0)?0:port;
        return true;
    }

    void write_resp(uint32_t cid, uint32_t status, const void* data = nullptr, uint16_t size = 0, bool syscon = false)
    {
        auto& buf = syscon ? g_vuart.reply_sc : g_vuart.reply;
        if (buf.size() + 12 + size > PS3AV_BUF_SIZE) return;
        uint8_t hdr[12];
        put16(hdr, PS3AV_VERSION);
        put16(hdr + 2, uint16_t(size + 8));
        put32(hdr + 4, cid | PS3AV_REPLY_BIT);
        put32(hdr + 8, status);
        buf.insert(buf.end(), hdr, hdr + 12);
        if (size) buf.insert(buf.end(), (const uint8_t*)data, (const uint8_t*)data + size);
        if (status != ST_SUCCESS) log_line("uart: cid %08X failed with status %02X", cid, status);
    }

    // set_hdmi_display_cfg: report a 1280x720-native monitor that supports everything
    void hdmi_monitor_info(uint8_t* r, uint8_t avport)
    {
        memset(r, 0, kMonitorInfoSize);
        if (g_vuart.hdmi_behavior != HDMI_BEHAVIOR_NORMAL && (g_vuart.hdmi_behavior & HDMI_BEHAVIOR_EDID_PASS))
        {
            r[11] = (g_vuart.hdmi_behavior & HDMI_BEHAVIOR_DVI) ? 2 : 1;
            return;
        }
        static const uint8_t mon_id[10] = { 0x4A, 0x13, 0x03, 0, 0, 0, 0, 0, 0, 0x15 };
        static const char mon_name[16] = "RPCS3 VirtMon";
        r[0] = avport;
        memcpy(r + 1, mon_id, 10);
        r[11] = 1; // HDMI
        memcpy(r + 12, mon_name, 16);
        const uint32_t native = 0x0004; // 1280x720P
        put32(r + 28, 0xFFFFFFFF); put32(r + 32, native);  // res_60
        put32(r + 36, 0xFFFFFFFF); put32(r + 40, native);  // res_50
        put32(r + 44, 0xFFFFFFFF);                         // res_other
        put32(r + 52, 1);                                  // res_vesa
        r[60] = 1 | 2 | 4;  // rgb: supported, selectable quantization, 12-bit
        r[61] = 1 | 4;      // yuv444: supported, 12-bit
        r[62] = 1;          // yuv422
        r[63] = 0x01 | 0x02 | 0x10 | 0x20 | 0x40; // colorimetry: xvYCC 601/709, MD0-2
        put16(r + 64, 1023); put16(r + 66, 0); put16(r + 68, 0); put16(r + 70, 1023);
        put16(r + 72, 0); put16(r + 74, 0); put16(r + 76, 341); put16(r + 78, 341);
        put32(r + 80, 100); // gamma
        r[84] = 1;          // supported_ai
        r[85] = 0x4F;       // speaker_info
        put16(r + 86, 7);   // audio blocks: LPCM, AC3, AAC, DTS, DDP, DTS-HD, Dolby TrueHD
        static const uint8_t audio[7][4] = { { 1, 8, 0x7F, 0x07 }, { 2, 8, 0x7F, 0xFF }, { 3, 8, 0x7F, 0xFF }, { 4, 8, 0x7F, 0xFF },
            { 5, 8, 0x7F, 0xFF }, { 6, 8, 0x7F, 0xFF }, { 7, 8, 0x7F, 0xFF } };
        memcpy(r + 88, audio, sizeof(audio));
        put16(r + 152, 60); put16(r + 154, 34); // 27-inch 16:9
        r[156] = 0x0F;                          // content types
        for (int i = 0; i < 5; i++) put32(r + 160 + i * 8, 0xFFFFFFFF); // 3D modes, no native
        r[200] = 0;
    }

    void add_event(const uint8_t* data, size_t size) { g_vuart.rx.insert(g_vuart.rx.end(), data, data + size); }

    void dispatch_hdmi_event(HdmiEvent e)
    {
        uint8_t pkt[8 + kMonitorInfoSize] = {};
        put16(pkt, g_vuart.av_cmd_ver);
        switch (e)
        {
        case HdmiEvent::Unplugged:
            if (!(g_vuart.events_mask & EV_UNPLUGGED)) return;
            put16(pkt + 2, 4);
            put32(pkt + 4, 0x10000001);
            g_vuart.hdcp_first_auth = true;
            add_event(pkt, 8);
            break;
        case HdmiEvent::Plugged:
            if (!(g_vuart.events_mask & EV_PLUGGED)) return;
            put16(pkt + 2, uint16_t(8 + kMonitorInfoSize - 8));
            put32(pkt + 4, 0x10000002);
            hdmi_monitor_info(pkt + 8, AVPORT_HDMI_0);
            add_event(pkt, 8 + kMonitorInfoSize - 4);
            break;
        case HdmiEvent::HdcpDone:
        {
            uint16_t size = 12; // offsetof(ps3av_pkt_hdmi_hdcp_done_event, ksv_arr)
            if (g_vuart.hdmi_behavior == HDMI_BEHAVIOR_NORMAL || !(g_vuart.hdmi_behavior & HDMI_BEHAVIOR_HDCP_OFF))
            {
                put32(pkt + 8, 1);
                memcpy(pkt + 12, BKSV, 5);
                size = (size + 5 + 3) & 0xFFFC;
            }
            put16(pkt + 2, uint16_t(size - 4));
            if (g_vuart.hdcp_first_auth)
            {
                if (!(g_vuart.events_mask & EV_HDCP_DONE)) return;
                g_vuart.hdcp_first_auth = false;
                put32(pkt + 4, 0x10000003);
            }
            else
            {
                if (!(g_vuart.events_mask & EV_HDCP_REAUTH)) return;
                put32(pkt + 4, 0x10000005);
            }
            add_event(pkt, size);
            break;
        }
        default:
            break;
        }
    }

    void execute(const uint8_t* p, uint16_t pkt_size)
    {
        const uint32_t cid = be32(p + 4);
        const uint16_t avport = pkt_size >= 10 ? be16(p + 8) : 0;
        switch (cid)
        {
        case 0x00000001: // AV_INIT
        {
            g_vuart.av_cmd_ver = be16(p);
            const uint32_t event_bit = be32(p + 8);
            g_vuart.events_mask |= event_bit;
            if (event_bit & EV_UNK)
            {
                const uint8_t reply[4] = {};
                write_resp(cid, ST_SUCCESS, reply, 4);
            }
            else
            {
                write_resp(cid, ST_SUCCESS);
            }
            break;
        }
        case 0x00000002: // AV_FIN
            g_vuart.events_mask = 0;
            write_resp(cid, ST_SUCCESS);
            break;
        case 0x00000003: // AV_GET_HW_CONF: 1 HDMI, 1 AV multi, 1 S/PDIF, extra bitstream support
        {
            uint8_t reply[8];
            put16(reply, 1); put16(reply + 2, 1); put16(reply + 4, 1); put16(reply + 6, 1);
            write_resp(cid, ST_SUCCESS, reply, 8);
            break;
        }
        case 0x00000004: // AV_GET_MONITOR_INFO
        {
            uint8_t reply[kMonitorInfoSize] = {};
            if (avport == AVPORT_AVMULTI_0)
            {
                reply[0] = uint8_t(AVPORT_AVMULTI_0);
                reply[11] = 3; // AV multi
                put32(reply + 28, 0xFFFFFFFF); put32(reply + 36, 0xFFFFFFFF); put32(reply + 52, 0xFFFFFFFF);
                reply[60] = reply[61] = reply[62] = 1;
                reply[85] = 1;
                put16(reply + 86, 1);
                reply[88] = 1; reply[89] = 2; reply[90] = 127; reply[91] = 7; // LPCM, 2 ch
                write_resp(cid, ST_SUCCESS, reply, kMonitorInfoSize);
            }
            else if (avport == AVPORT_HDMI_0)
            {
                hdmi_monitor_info(reply, uint8_t(avport));
                write_resp(cid, ST_SUCCESS, reply, kMonitorInfoSize - 4, true); // shorter on HDMI, as on RPCS3
            }
            else if (avport == AVPORT_HDMI_1)
            {
                write_resp(cid, ST_SYSCON_FAIL, nullptr, 0, true);
            }
            else
            {
                write_resp(cid, ST_INVALID_PORT);
            }
            break;
        }
        case 0x00000005: // AV_GET_BKSV_LIST
        {
            if (avport > AVPORT_HDMI_1) { write_resp(cid, ST_INVALID_PORT); break; }
            if (avport == AVPORT_HDMI_1) { write_resp(cid, ST_SYSCON_FAIL, nullptr, 0, true); break; }
            uint8_t reply[8 + 100] = {};
            put16(reply, avport);
            uint16_t size = 8;
            if (g_vuart.hdmi_behavior == HDMI_BEHAVIOR_NORMAL || !(g_vuart.hdmi_behavior & HDMI_BEHAVIOR_HDCP_OFF))
            {
                put32(reply + 4, 1);
                memcpy(reply + 8, BKSV, 5);
                size = (size + 5 + 3) & 0xFFFC;
            }
            write_resp(cid, ST_SUCCESS, reply, size, true);
            break;
        }
        case 0x00000006: // AV_ENABLE_EVENT
            g_vuart.events_mask |= be32(p + 8);
            write_resp(cid, ST_SUCCESS);
            break;
        case 0x00000007: // AV_DISABLE_EVENT
            g_vuart.events_mask &= ~be32(p + 8);
            write_resp(cid, ST_SUCCESS);
            break;
        case 0x0000000C: // AV_GET_AKSV
        {
            uint8_t reply[16] = {};
            put32(reply, 5);
            memcpy(reply + 4, AKSV, 5);
            write_resp(cid, ST_SUCCESS, reply, 16);
            break;
        }
        case 0x00010003: // AV_VIDEO_DISABLE_SIG
            if (avport <= AVPORT_HDMI_1)
            {
                if (avport == AVPORT_HDMI_1) { write_resp(cid, ST_SYSCON_FAIL, nullptr, 0, true); break; }
                g_vuart.hdmi_res_set = false;
                write_resp(cid, ST_SUCCESS, nullptr, 0, true);
            }
            else if (avport == AVPORT_AVMULTI_0 && g_vuart.head_b_initialized)
            {
                write_resp(cid, ST_SUCCESS, nullptr, 0, true);
            }
            else
            {
                write_resp(cid, ST_SUCCESS);
            }
            break;
        case 0x00010004: // AV_VIDEO_YTRAPCONTROL
            write_resp(cid, ST_SUCCESS, nullptr, 0, !(be16(p + 8) && be16(p + 8) != 5));
            break;
        case 0x00020002: // AV_AUDIO_MUTE
        case 0x00020003: // AV_ACP_CTRL
        case 0x00020004: // AV_SET_ACP_PACKET
            write_resp(cid, ST_SUCCESS, nullptr, 0, true);
            break;
        case 0x00040001: // AV_HDMI_MODE: replug HDMI so vsh re-reads the monitor
            g_vuart.hdmi_behavior = p[8];
            g_vuart.hdmi.set_target(HdmiEvent::Unplugged, g_vuart.hdmi_res_set ? HdmiEvent::HdcpDone : HdmiEvent::Plugged);
            write_resp(cid, ST_SUCCESS, nullptr, 0, true);
            break;
        case 0x000A0002: // AV_GET_CEC_CONFIG
        {
            uint8_t reply[4];
            put32(reply, 1);
            write_resp(cid, ST_SUCCESS, reply, 4);
            break;
        }
        case 0x01000003: // VIDEO_ROUTE: only in the PS2 GX LPAR
            write_resp(cid, ST_NO_SEL);
            break;
        case 0x01000006: // VIDEO_GET_HW_CONF: gx not available
        {
            uint8_t reply[4] = {};
            write_resp(cid, ST_SUCCESS, reply, 4);
            break;
        }
        case 0x02000001: // AUDIO_INIT
            rsxaudio_reset_hardware();g_audio_routes.fill(3);write_resp(cid,ST_SUCCESS);break;
        case 0x02000002: // AUDIO_MODE
            write_resp(cid,configure_audio(p,pkt_size)?ST_SUCCESS:0x11);break;
        case 0x02000003: // AUDIO_MUTE
            if(pkt_size>=13)
            {
                const uint32_t av=p[8];
                if(av<=1||av==0x10||av==0x11)rsxaudio_set_port_muted(0,p[12]!=0);
                else if(av==0x20||av==0x21)rsxaudio_set_port_muted(av-0x20+1,p[12]!=0);
            }
            write_resp(cid,ST_SUCCESS);break;
        case 0x02000004: // AUDIO_ACTIVE
        {
            constexpr uint32_t masks[5]={1,2,1u<<10,1u<<20,1u<<21};
            const uint32_t mask=pkt_size>=12?be32(p+8):0;
            for(unsigned i=0;i<5;++i)if((mask&masks[i])&&g_audio_routes[i]<3)rsxaudio_set_port_active(g_audio_routes[i],true);
            rsxaudio_set_port_muted(0,false);rsxaudio_set_port_muted(2,false);
            write_resp(cid,ST_SUCCESS);break;
        }
        case 0x02000005: // AUDIO_INACTIVE
            if(pkt_size>=12&&!(be32(p+8)&0x80000000))g_audio_routes.fill(3);
            for(uint32_t port=0;port<3;++port)rsxaudio_set_port_active(port,false);
            rsxaudio_set_port_muted(0,true);rsxaudio_set_port_muted(2,true);
            write_resp(cid,ST_SUCCESS);break;
        case 0x04000001: // AVB_PARAM: video packets, then AV video and AV audio packets
        {
            const uint16_t n_video = be16(p + 8), n_audio=be16(p+10), n_av_video = be16(p + 12), n_av_audio = be16(p + 14);
            const uint8_t* q = p + 16;
            for (uint16_t i = 0; i < n_video; i++)
            {
                if (be16(q + 8) == 1) g_vuart.head_b_initialized = true; // video_head == HEAD_B_ANALOG
                q += be16(q + 2) + 4;
            }
            for(uint16_t i=0;i<n_audio;++i)
            {
                const uint16_t size=be16(q+2)+4;
                if(q+size>p+pkt_size||!configure_audio(q,size)){write_resp(cid,0x11);return;}
                q+=size;
            }
            if (!n_av_video && !n_av_audio)
            {
                write_resp(cid, ST_SUCCESS);
                break;
            }
            bool hdcp_done = false;
            for (uint16_t i = 0; i < n_av_video; i++)
            {
                if (be16(q + 8) == AVPORT_HDMI_0) hdcp_done = true; // HDMI setup
                q += be16(q + 2) + 4;
            }
            g_vuart.hdmi_res_set = hdcp_done;
            if (hdcp_done) g_vuart.hdmi.set_target(HdmiEvent::HdcpDone, HdmiEvent::HdcpDone);
            bool hdmi_audio = false;
            for (uint16_t i = 0; i < n_av_audio; i++)
            {
                if (be16(q + 8) <= AVPORT_HDMI_1) hdmi_audio = true;
                q += be16(q + 2) + 4;
            }
            write_resp(cid, ST_SUCCESS, nullptr, 0, n_av_video || hdmi_audio);
            break;
        }
        case 0x00000009: case 0x0000000A: case 0x0000000B: case 0x00010002: case 0x00030001: case 0x00030002:
        case 0x00030003: case 0x00030004: case 0x000A0001: case 0x01000001: case 0x01000002: case 0x01000004:
        case 0x01000005: case 0x01000008: case 0x01000009: case 0x02000006: case 0x02000007:
            write_resp(cid, ST_SUCCESS);
            break;
        default:
            log_line("uart: unknown AV command %08X", cid);
            break;
        }
    }

    void av_thread()
    {
        std::vector<uint8_t> pending;
        for (;;)
        {
            {
                std::unique_lock lock(g_vuart.m);
                // HDMI events due now are delivered first
                for (HdmiEvent e; (e = g_vuart.hdmi.take()) != HdmiEvent::None;) dispatch_hdmi_event(e);
                const uint64_t wait_us = g_vuart.hdmi.until_next();
                if (g_vuart.tx.empty())
                {
                    if (wait_us) g_vuart.tx_cv.wait_for(lock, std::chrono::microseconds(wait_us));
                    else if (!g_vuart.hdmi.available()) g_vuart.tx_cv.wait(lock);
                }
                pending.swap(g_vuart.tx);
                g_vuart.tx.clear();
            }
            if (pending.empty()) continue;
            {
                std::lock_guard lock(g_vuart.m);
                size_t pos = 0;
                while (pos + 8 <= pending.size())
                {
                    const uint16_t version = be16(&pending[pos]), length = be16(&pending[pos + 2]);
                    const uint16_t size = uint16_t(length + 4);
                    if (version != PS3AV_VERSION)
                    {
                        write_resp(be32(&pending[pos + 4]), ST_INVALID_COMMAND);
                        break;
                    }
                    if (pos + size > pending.size()) break;
                    execute(&pending[pos], size);
                    pos += size;
                }
            }
            pending.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // RPCS3: give vsh some time
            std::lock_guard lock(g_vuart.m);
            g_vuart.rx.insert(g_vuart.rx.end(), g_vuart.reply.begin(), g_vuart.reply.end());
            g_vuart.rx.insert(g_vuart.rx.end(), g_vuart.reply_sc.begin(), g_vuart.reply_sc.end());
            g_vuart.reply.clear();
            g_vuart.reply_sc.clear();
        }
    }

    int64_t sys_uart_initialize(PPCContext&)
    {
        std::lock_guard lock(g_vuart.m);
        if (g_vuart.initialized) return CELL_EPERM;
        g_vuart.initialized = true;
        std::thread(av_thread).detach();
        return CELL_OK;
    }

    // sys_uart_receive(buffer*, size, mode): mode 1 blocks until data is available
    int64_t sys_uart_receive(PPCContext& ctx)
    {
        const uint32_t buf = ctx.r3.u32, mode = ctx.r5.u32;
        const uint64_t size = ctx.r4.u64;
        if (!size) return CELL_OK;
        if (mode & ~1u) return CELL_EINVAL;
        for (;;)
        {
            {
                std::lock_guard lock(g_vuart.m);
                if (!g_vuart.initialized) return CELL_ESRCH;
                if (!g_vuart.rx.empty())
                {
                    const size_t n = std::min<size_t>(size, g_vuart.rx.size());
                    memcpy(g_base + buf, g_vuart.rx.data(), n);
                    g_vuart.rx.erase(g_vuart.rx.begin(), g_vuart.rx.begin() + n);
                    return int64_t(n);
                }
            }
            if (!(mode & 1)) return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // sys_uart_send(buffer*, size, mode)
    int64_t sys_uart_send(PPCContext& ctx)
    {
        const uint32_t buf = ctx.r3.u32;
        const uint64_t size = ctx.r4.u64;
        if (!size) return CELL_OK;
        std::lock_guard lock(g_vuart.m);
        if (!g_vuart.initialized) return CELL_ESRCH;
        g_vuart.tx.insert(g_vuart.tx.end(), g_base + buf, g_base + buf + size);
        g_vuart.tx_cv.notify_all();
        return int64_t(size);
    }

    // sys_uart_get_params(vuart_params*): {u64 rx_buf_size, u64 tx_buf_size}
    int64_t sys_uart_get_params(PPCContext& ctx)
    {
        guest_store_u64(ctx.r3.u32, PS3AV_BUF_SIZE);
        guest_store_u64(ctx.r3.u32 + 8, PS3AV_BUF_SIZE);
        return CELL_OK;
    }
}

void register_uart_syscalls()
{
    register_syscall(367, sys_uart_initialize);
    register_syscall(368, sys_uart_receive);
    register_syscall(369, sys_uart_send);
    register_syscall(370, sys_uart_get_params);
}
