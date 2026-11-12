#pragma once
// ZF4 (audit/BOOT-TIME.md): start-up work that does not need the shell, moved off the path to the first frame.
//   startPrefetch()    called once, early, on the GUI thread after QApplication exists. It starts independent, side-effect-free
//                      work on worker threads (the cloth warm-up, the first host reading, the weather and service icons and the
//                      OpenGL driver); every result is joined where it was computed before, so what is drawn is identical.
//   afterFirstFrame()  queue work the first frame does not need (network clients, process starts). main.cpp calls runDeferred()
//                      on the first buffer swap, or after a few seconds when no swap comes; a queued job runs exactly once.
#include <QString>
#include <functional>

namespace xmb::boot {
void startPrefetch(const QString& dataDir);
void afterFirstFrame(std::function<void()> job);
void runDeferred();
// Joins the OpenGL driver warm-up. Two WGL contexts must never be created at the same time in this process (the driver crashed in
// glGetIntegerv when a worker's context and the window's overlapped), so main.cpp calls this before the first window is created.
// A no-op when no warm-up was started.
void joinOpenGlWarmup();
}
