// Event queues are shared with other kernel parts (timers, sys_rsx, sys_audio) that post events.
#pragma once

#include "kernel.h"

#include <array>

struct KEventQueue : KObject
{
    uint32_t type = 1;
    uint32_t size = 0;
    uint64_t key = 0;
    uint64_t name = 0;
    bool destroyed = false;
    std::deque<std::array<uint64_t, 4>> events;
    std::deque<PpuThread*> sq;
};

// Delivers an event to the first receiver or queues it; false if the queue is full.
// Caller holds g_kernel.
bool event_queue_send(KEventQueue& q, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3);
std::shared_ptr<KEventQueue> find_event_queue(uint32_t id);

// Kernel-internal objects created on behalf of the guest (e.g. sys_rsx's handler queue).
// Caller holds g_kernel for both.
uint32_t create_event_queue_internal(uint32_t size, std::shared_ptr<KEventQueue>* out);
uint32_t create_local_port_internal(const std::shared_ptr<KEventQueue>& q);
// Sends through a port as sys_event_port_send would; returns a CELL error code.
int32_t event_port_send_internal(uint32_t port_id, uint64_t d1, uint64_t d2, uint64_t d3);
