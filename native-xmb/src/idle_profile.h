#pragma once
// XMB_IDLE_PROFILE=1: cumulative CPU time per named section, logged every 5 s by View::paintGL (D12 idle cost).
// Off by default; a disabled ProfScope costs one load and a branch.
#include <QElapsedTimer>
#include <QtGlobal>
#include <cstring>

namespace xmb::idleprofile {
struct Slot { const char* name; qint64 ns; qint64 calls; };
inline Slot table[24];
inline int slotCount=0;
inline const bool enabled=qEnvironmentVariableIsSet("XMB_IDLE_PROFILE");

struct Scope {
    Slot* entry=nullptr;QElapsedTimer timer;
    explicit Scope(const char* name) {
        if(!enabled)return;
        for(int i=0;i<slotCount;++i)if(std::strcmp(table[i].name,name)==0){entry=&table[i];break;}
        if(!entry && slotCount<24){table[slotCount]={name,0,0};entry=&table[slotCount++];}
        timer.start();
    }
    ~Scope(){if(entry){entry->ns+=timer.nsecsElapsed();++entry->calls;}}
};
}

#ifdef Q_OS_WIN
// XMB_IDLE_SAMPLE=1: a 1 ms sampler of the GUI thread's instruction pointer, bucketed by module (and by symbol where a
// PDB is present), printed every 10 s. A measurement tool for D12 only; nothing runs without the variable.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <atomic>
namespace xmb::idleprofile {
inline void startSampler() {
    if(!qEnvironmentVariableIsSet("XMB_IDLE_SAMPLE"))return;
    HANDLE source=nullptr;
    DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&source,THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,0);
    std::thread([source]{
        SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME|SYMOPT_LOAD_LINES);
        SymInitialize(GetCurrentProcess(),nullptr,TRUE);
        std::map<std::string,long> modules,symbols;long total=0;auto last=GetTickCount64();
        for(;;) {
            Sleep(1);
            if(SuspendThread(source)==DWORD(-1))continue;
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
            const bool ok=GetThreadContext(source,&context);
            ResumeThread(source);
            if(!ok)continue;
            const DWORD64 ip=context.Rip;++total;
            HMODULE module=nullptr;char name[MAX_PATH]="?";
            if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(ip),&module))GetModuleFileNameA(module,name,MAX_PATH);
            std::string base=name;base=base.substr(base.find_last_of("\\/")+1);
            ++modules[base];
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO)+256]{};
            auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;
            DWORD64 displacement=0;
            if(SymFromAddr(GetCurrentProcess(),ip,&displacement,symbol))++symbols[base+"!"+symbol->Name];
            else { char text[32];std::snprintf(text,sizeof text,"+%llx",(unsigned long long)(ip-reinterpret_cast<DWORD64>(module))>>4<<4);++symbols[base+text]; }
            if(GetTickCount64()-last>=10000) {
                last=GetTickCount64();
                std::fprintf(stderr,"idle-sample: %ld samples\n",total);
                const auto dump=[&](std::map<std::string,long>& map,int count){
                    std::vector<std::pair<long,std::string>> rows;for(auto& [k,v]:map)rows.emplace_back(v,k);
                    std::sort(rows.rbegin(),rows.rend());
                    for(int i=0;i<count&&i<int(rows.size());++i)std::fprintf(stderr,"  %5.1f%% %s\n",100.*rows[size_t(i)].first/total,rows[size_t(i)].second.c_str());
                };
                dump(modules,8);std::fprintf(stderr," --\n");dump(symbols,25);std::fflush(stderr);
                modules.clear();symbols.clear();total=0;
            }
        }
    }).detach();
}
}
#else
namespace xmb::idleprofile { inline void startSampler() {} }
#endif
