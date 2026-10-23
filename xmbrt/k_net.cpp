// LV2 IPv4 sockets over Winsock. Guest structs/errno follow RPCS3 sys_net.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "kernel.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"iphlpapi.lib")

namespace
{
    struct NetSocket
    {
        std::atomic<SOCKET> fd{INVALID_SOCKET};
        int type=0;
        bool nonblocking=false;
        uint64_t receive_timeout=0,send_timeout=0;
        void close(){const SOCKET value=fd.exchange(INVALID_SOCKET);if(value!=INVALID_SOCKET)closesocket(value);}
        ~NetSocket(){close();}
    };
    std::mutex g_net_mutex;
    std::map<uint32_t,std::shared_ptr<NetSocket>> g_sockets;
    std::map<uint32_t,uint64_t> g_abort;
    int net_error(int error=WSAGetLastError())
    {
        switch(error)
        {
        case 0:return 0;case WSAEINTR:return 4;case WSAEBADF:case WSAENOTSOCK:return 9;
        case WSAEACCES:return 13;case WSAEFAULT:return 14;case WSAEINVAL:return 22;case WSAEMFILE:return 24;
        case WSAEWOULDBLOCK:return 35;case WSAEINPROGRESS:return 36;case WSAEALREADY:return 37;
        case WSAEDESTADDRREQ:return 39;case WSAEMSGSIZE:return 40;case WSAEPROTOTYPE:return 41;
        case WSAENOPROTOOPT:return 42;case WSAEPROTONOSUPPORT:return 43;case WSAEOPNOTSUPP:return 45;
        case WSAEPFNOSUPPORT:return 46;case WSAEAFNOSUPPORT:return 47;case WSAEADDRINUSE:return 48;
        case WSAEADDRNOTAVAIL:return 49;case WSAENETDOWN:return 50;case WSAENETUNREACH:return 51;
        case WSAECONNABORTED:return 53;case WSAECONNRESET:return 54;case WSAENOBUFS:return 55;
        case WSAEISCONN:return 56;case WSAENOTCONN:return 57;case WSAESHUTDOWN:return 58;
        case WSAETIMEDOUT:return 60;case WSAECONNREFUSED:return 61;case WSAEHOSTDOWN:return 64;
        case WSAEHOSTUNREACH:return 65;default:return 22;
        }
    }
    std::shared_ptr<NetSocket> socket_at(uint32_t id)
    {
        std::lock_guard lock(g_net_mutex);auto it=g_sockets.find(id);return it==g_sockets.end()?nullptr:it->second;
    }
    int add_socket(SOCKET fd,int type)
    {
        u_long enabled=1;ioctlsocket(fd,FIONBIO,&enabled);
        std::lock_guard lock(g_net_mutex);
        for(uint32_t id=24;id<1024;++id)if(!g_sockets.contains(id))
        {
            auto s=std::make_shared<NetSocket>();s->fd=fd;s->type=type;g_sockets[id]=s;return int(id);
        }
        closesocket(fd);return -24;
    }
    uint64_t abort_token(){std::lock_guard lock(g_net_mutex);return g_abort[current_thread_id()];}
    bool was_aborted(uint64_t token){return abort_token()!=token;}
    bool address_in(uint32_t address,uint32_t length,sockaddr_in& out)
    {
        if(!address||length<16||guest_load_u8(address+1)!=2)return false;
        out={};out.sin_family=AF_INET;
        memcpy(&out.sin_port,g_base+address+2,2);memcpy(&out.sin_addr,g_base+address+4,4);return true;
    }
    void address_out(uint32_t address,uint32_t length_ptr,const sockaddr_in& in)
    {
        if(!address||!length_ptr)return;
        uint8_t bytes[16]{};bytes[0]=16;bytes[1]=2;
        memcpy(bytes+2,&in.sin_port,2);memcpy(bytes+4,&in.sin_addr,4);
        memcpy(g_base+address,bytes,std::min(guest_load_u32(length_ptr),16u));guest_store_u32(length_ptr,16);
    }
    int wait_socket(const std::shared_ptr<NetSocket>& socket,short events,uint64_t timeout,uint64_t token)
    {
        const uint64_t start=system_time_us();
        for(;;)
        {
            if(was_aborted(token))return -4;
            if(timeout&&system_time_us()-start>=timeout)return -35;
            WSAPOLLFD p{socket->fd,events,0};
            const int result=WSAPoll(&p,1,10);
            if(result==SOCKET_ERROR)return -net_error();
            if(result)return 0;
        }
    }
    int flags_in(uint32_t flags)
    {
        return (flags&1?MSG_OOB:0)|(flags&2?MSG_PEEK:0)|(flags&4?MSG_DONTROUTE:0)|(flags&0x40?MSG_WAITALL:0);
    }
    int64_t net_socket(PPCContext& c)
    {
        if(c.r3.u32!=2)return -47;
        if(c.r4.u32!=1&&c.r4.u32!=2)return -43;
        SOCKET fd=::socket(AF_INET,int(c.r4.u32),int(c.r5.u32));
        return fd==INVALID_SOCKET?-net_error():add_socket(fd,int(c.r4.u32));
    }
    int64_t net_close(PPCContext& c)
    {
        std::lock_guard lock(g_net_mutex);auto it=g_sockets.find(c.r3.u32);if(it==g_sockets.end())return -9;
        it->second->close();g_sockets.erase(it);return 0;
    }
    int64_t net_bind(PPCContext& c)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;sockaddr_in a;
        if(!address_in(c.r4.u32,c.r5.u32,a))return -22;
        return ::bind(s->fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))? -net_error():0;
    }
    int64_t net_connect(PPCContext& c)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;sockaddr_in a;
        if(!address_in(c.r4.u32,c.r5.u32,a))return -22;
        if(!::connect(s->fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)))return 0;
        const int error=net_error();if(error!=35&&error!=36)return -error;
        if(s->nonblocking)return -36;
        const int result=wait_socket(s,POLLWRNORM,s->send_timeout,abort_token());if(result)return result;
        int pending=0,length=sizeof(pending);getsockopt(s->fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&pending),&length);
        return -net_error(pending);
    }
    int64_t net_name(PPCContext& c,bool peer)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;sockaddr_in a{};int length=sizeof(a);
        const int result=peer?getpeername(s->fd,reinterpret_cast<sockaddr*>(&a),&length):getsockname(s->fd,reinterpret_cast<sockaddr*>(&a),&length);
        if(result)return -net_error();address_out(c.r4.u32,c.r5.u32,a);return 0;
    }
    int64_t net_listen(PPCContext& c){auto s=socket_at(c.r3.u32);return !s?-9: ::listen(s->fd,c.r4.s32)?-net_error():0;}
    int64_t net_accept(PPCContext& c)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;const uint64_t token=abort_token();
        for(;;)
        {
            sockaddr_in a{};int length=sizeof(a);SOCKET fd=::accept(s->fd,reinterpret_cast<sockaddr*>(&a),&length);
            if(fd!=INVALID_SOCKET){address_out(c.r4.u32,c.r5.u32,a);return add_socket(fd,s->type);}
            const int error=net_error();if(error!=35||s->nonblocking)return -error;
            const int wait=wait_socket(s,POLLRDNORM,s->receive_timeout,token);if(wait)return wait;
        }
    }
    int64_t net_transfer(PPCContext& c,bool receive)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;
        const uint32_t buffer=c.r4.u32,length=c.r5.u32,flags=c.r6.u32;
        if((!buffer&&length)||length>0x7fffffff)return -14;
        const uint64_t token=abort_token();
        for(;;)
        {
            sockaddr_in a{};int alength=sizeof(a),result;
            if(receive)result=recvfrom(s->fd,reinterpret_cast<char*>(g_base+buffer),int(length),flags_in(flags),c.r7.u32?reinterpret_cast<sockaddr*>(&a):nullptr,c.r7.u32?&alength:nullptr);
            else
            {
                if(c.r7.u32&&!address_in(c.r7.u32,c.r8.u32,a))return -22;
                result=sendto(s->fd,reinterpret_cast<const char*>(g_base+buffer),int(length),flags_in(flags),c.r7.u32?reinterpret_cast<sockaddr*>(&a):nullptr,c.r7.u32?sizeof(a):0);
            }
            if(result!=SOCKET_ERROR){if(receive)address_out(c.r7.u32,c.r8.u32,a);return result;}
            const int error=net_error();if(error!=35||s->nonblocking||(flags&0x80))return -error;
            const int wait=wait_socket(s,receive?POLLRDNORM:POLLWRNORM,receive?s->receive_timeout:s->send_timeout,token);if(wait)return wait;
        }
    }
    int option_in(uint32_t option)
    {
        switch(option){case 4:return SO_REUSEADDR;case 8:return SO_KEEPALIVE;case 0x20:return SO_BROADCAST;
        case 0x80:return SO_LINGER;case 0x100:return SO_OOBINLINE;case 0x1001:return SO_SNDBUF;
        case 0x1002:return SO_RCVBUF;case 0x1007:return SO_ERROR;case 0x1008:return SO_TYPE;default:return -1;}
    }
    int64_t net_option(PPCContext& c,bool set)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;
        const uint32_t level=c.r4.u32,option=c.r5.u32,buffer=c.r6.u32;
        if(!buffer||(!set&&!c.r7.u32))return -14;
        const uint32_t length=set?c.r7.u32:guest_load_u32(c.r7.u32);
        if(option==0x1100&&level==0xffff)
        {
            if(length<4)return -22;if(set)s->nonblocking=guest_load_u32(buffer)!=0;else{guest_store_u32(buffer,s->nonblocking);guest_store_u32(c.r7.u32,4);}return 0;
        }
        if(level==0xffff&&(option==0x1005||option==0x1006))
        {
            if(length<16)return -22;auto& timeout=option==0x1005?s->send_timeout:s->receive_timeout;
            if(set)timeout=guest_load_u64(buffer)*1000000+guest_load_u64(buffer+8);
            else{guest_store_u64(buffer,timeout/1000000);guest_store_u64(buffer+8,timeout%1000000);guest_store_u32(c.r7.u32,16);}return 0;
        }
        const int native_level=level==0xffff?SOL_SOCKET:int(level),native_option=level==0xffff?option_in(option):int(option);
        if(native_option<0)return -42;
        if(option==0x80&&level==0xffff)
        {
            if(length<8)return -22;linger value{u_short(guest_load_u32(buffer)),u_short(guest_load_u32(buffer+4))};int size=sizeof(value);
            const int result=set?setsockopt(s->fd,native_level,native_option,reinterpret_cast<char*>(&value),size):getsockopt(s->fd,native_level,native_option,reinterpret_cast<char*>(&value),&size);
            if(result)return -net_error();if(!set){guest_store_u32(buffer,value.l_onoff);guest_store_u32(buffer+4,value.l_linger);guest_store_u32(c.r7.u32,8);}return 0;
        }
        if(length<4)return -22;int value=set?int(guest_load_u32(buffer)):0,size=sizeof(value);
        const int result=set?setsockopt(s->fd,native_level,native_option,reinterpret_cast<char*>(&value),size):getsockopt(s->fd,native_level,native_option,reinterpret_cast<char*>(&value),&size);
        if(result)return -net_error();if(!set){guest_store_u32(buffer,option==0x1007?net_error(value):value);guest_store_u32(c.r7.u32,4);}return 0;
    }
    struct Selection {uint32_t id;std::shared_ptr<NetSocket> socket;short events;};
    short poll_events(uint16_t events)
    {
        return (events&1?POLLRDNORM:0)|(events&2?POLLPRI:0)|(events&4?POLLWRNORM:0);
    }
    uint16_t poll_result(short events)
    {
        return (events&POLLRDNORM?1:0)|(events&POLLPRI?2:0)|(events&POLLWRNORM?4:0)|
            (events&POLLERR?8:0)|(events&POLLHUP?16:0)|(events&POLLNVAL?32:0);
    }
    int64_t net_poll(PPCContext& c)
    {
        const uint32_t address=c.r3.u32;const int count=c.r4.s32,timeout=c.r5.s32;
        if(count<0||count>1024)return -22;if(count&&!address)return -14;
        std::vector<WSAPOLLFD> native;std::vector<uint32_t> slots;
        std::vector<std::shared_ptr<NetSocket>> owners;
        int invalid=0;
        for(int i=0;i<count;++i)
        {
            const uint32_t entry=address+uint32_t(i)*8;const int32_t id=int32_t(guest_load_u32(entry));
            guest_store_u16(entry+6,0);if(id<0)continue;
            auto s=socket_at(uint32_t(id));if(!s){guest_store_u16(entry+6,32);++invalid;continue;}
            native.push_back({s->fd,poll_events(guest_load_u16(entry+4)),0});slots.push_back(entry);owners.push_back(s);
        }
        const uint64_t start=system_time_us(),token=abort_token();
        for(;;)
        {
            if(was_aborted(token))return -4;
            const int wait=(timeout==0||invalid)?0:10;
            const int result=native.empty()?0:WSAPoll(native.data(),ULONG(native.size()),wait);
            if(result==SOCKET_ERROR)return -net_error();
            if(result||invalid||timeout==0||(timeout>0&&system_time_us()-start>=uint64_t(timeout)*1000))
            {
                int ready=invalid;
                for(size_t i=0;i<native.size();++i){const uint16_t value=poll_result(native[i].revents);guest_store_u16(slots[i]+6,value);if(value)++ready;}
                return ready;
            }
            if(native.empty())std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    int64_t net_select(PPCContext& c)
    {
        const int count=c.r3.s32;if(count<0||count>1024)return -22;
        const uint32_t sets[3]={c.r4.u32,c.r5.u32,c.r6.u32};
        std::vector<Selection> selected;
        for(int id=0;id<count;++id)
        {
            short events=0;
            for(unsigned which=0;which<3;++which)if(sets[which]&&(guest_load_u32(sets[which]+(id/32)*4)&(1u<<(id%32))))events|=which==0?POLLRDNORM:which==1?POLLWRNORM:POLLPRI;
            if(events){auto s=socket_at(id);if(!s)return -9;selected.push_back({uint32_t(id),s,events});}
        }
        const uint64_t start=system_time_us(),token=abort_token();
        const bool timed=c.r7.u32!=0;
        int64_t seconds=timed?int64_t(guest_load_u64(c.r7.u32)):0,micros=timed?int64_t(guest_load_u64(c.r7.u32+8)):0;
        if(seconds<0||micros<0||micros>=1000000)return -22;
        const uint64_t duration=uint64_t(seconds)*1000000+uint64_t(micros);
        for(;;)
        {
            if(was_aborted(token))return -4;
            std::vector<WSAPOLLFD> native;for(auto& s:selected)native.push_back({s.socket->fd,s.events,0});
            const int wait=timed&&duration==0?0:10;
            int result=native.empty()?0:WSAPoll(native.data(),ULONG(native.size()),wait);
            if(result==SOCKET_ERROR)return -net_error();
            if(result||(timed&&system_time_us()-start>=duration))
            {
                for(uint32_t set:sets)if(set)memset(g_base+set,0,128);
                uint32_t ready=0;
                for(size_t i=0;i<native.size();++i)
                {
                    bool any=false;const auto& record=selected[i];
                    for(unsigned which=0;which<3;++which)
                    {
                        const short mask=which==0?POLLRDNORM|POLLHUP|POLLERR:which==1?POLLWRNORM|POLLERR:POLLPRI;
                        const short requested=which==0?POLLRDNORM:which==1?POLLWRNORM:POLLPRI;
                        if(sets[which]&&(record.events&requested)&&(native[i].revents&mask))
                        {
                            const uint32_t address=sets[which]+record.id/32*4;guest_store_u32(address,guest_load_u32(address)|(1u<<(record.id%32)));any=true;
                        }
                    }
                    if(any)++ready;
                }
                return ready;
            }
            if(native.empty())std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    int64_t net_info(PPCContext& c)
    {
        if(c.r3.u32==6)
        {
            if(!c.r4.u32)return -22;
            if(guest_load_u32(c.r4.u32)==0xffffffff&&!guest_load_u32(c.r4.u32+12))
            {std::lock_guard lock(g_net_mutex);return g_sockets.size();}
        }
        if(c.r3.u32==9)
        {
            if(!c.r4.u32)return -22;const uint32_t target=guest_load_u32(c.r4.u32+4);if(!target)return -14;
            std::string dns="127.0.0.1";ULONG size=0;
            if(GetNetworkParams(nullptr,&size)==ERROR_BUFFER_OVERFLOW)
            {
                std::vector<uint8_t> bytes(size);auto info=reinterpret_cast<FIXED_INFO*>(bytes.data());
                if(GetNetworkParams(info,&size)==NO_ERROR&&info->DnsServerList.IpAddress.String[0])dns=info->DnsServerList.IpAddress.String;
            }
            const std::string line="nameserver "+dns;guest_store_u32(c.r4.u32,0);memcpy(g_base+target,line.c_str(),line.size()+1);
        }
        return 0; // Remaining interface/control commands match RPCS3's TODO behavior.
    }
    int64_t net_ioctl(PPCContext& c)
    {
        auto s=socket_at(c.r3.u32);if(!s)return -9;
        const uint32_t command=c.r4.u32,address=c.r5.u32;
        if(command==0x8004667e){if(!address)return -14;s->nonblocking=guest_load_u32(address)!=0;return 0;}
        if(command==0x4004667f)
        {
            if(!address)return -14;u_long available=0;
            if(ioctlsocket(s->fd,FIONREAD,&available))return -net_error();guest_store_u32(address,uint32_t(available));
        }
        return 0; // Interface ioctls remain the same TODO responses as RPCS3.
    }
    int64_t net_abort(PPCContext& c)
    {
        std::lock_guard lock(g_net_mutex);
        if(c.r3.u32==2||c.r3.u32==3){++g_abort[c.r4.u32];return 0;}
        if(c.r3.u32==0){auto it=g_sockets.find(c.r4.u32);if(it==g_sockets.end())return -9;::shutdown(it->second->fd,SD_BOTH);return 0;}
        if(c.r3.u32==4){for(auto& [id,socket]:g_sockets)socket->close();g_sockets.clear();return 0;}return -22;
    }
}
void register_net_syscalls()
{
    WSADATA data{};const int error=WSAStartup(MAKEWORD(2,2),&data);
    if(error){log_line("network: Winsock initialization failed (%d)",error);return;}
    register_syscall(700,net_accept);register_syscall(701,net_bind);register_syscall(702,net_connect);
    register_syscall(703,[](PPCContext& c){return net_name(c,true);});register_syscall(704,[](PPCContext& c){return net_name(c,false);});
    register_syscall(705,[](PPCContext& c){return net_option(c,false);});register_syscall(706,net_listen);
    register_syscall(707,[](PPCContext& c){return net_transfer(c,true);});register_syscall(710,[](PPCContext& c){return net_transfer(c,false);});
    register_syscall(711,[](PPCContext& c){return net_option(c,true);});
    register_syscall(712,[](PPCContext& c)->int64_t{auto s=socket_at(c.r3.u32);return !s?-9: ::shutdown(s->fd,c.r4.s32)?-net_error():0;});
    register_syscall(713,net_socket);register_syscall(714,net_close);register_syscall(715,net_poll);register_syscall(716,net_select);
    register_syscall(721,net_abort);register_syscall(722,net_info);
    register_syscall(723,[](PPCContext&)->int64_t{return 0;});register_syscall(724,net_ioctl);register_syscall(725,[](PPCContext&)->int64_t{return 0;});
    log_line("network: IPv4 sockets and interruptible select initialized");
}
