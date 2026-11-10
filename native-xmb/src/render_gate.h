#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace xmb {
// Nobody is looking: the display is off, the session is locked or disconnected, or the window is minimised, cloaked or
// completely covered by other windows. The frame loop (View's frameSwapped -> update() chain) asks renderPaused()
// (ps_watcher.h), which now includes this gate, and stops re-arming; when the gate opens again it restarts the loop with
// one update(). D12, audit/IDLE-COST.md.
//
// The inputs are cheap and conservative. The display state is host::displayState() (GUID_CONSOLE_DISPLAY_STATE, already
// observed for Auto-Off). Lock and disconnect come from WTS session notifications (WM_WTSSESSION_CHANGE through a native
// event filter) with a 500 ms backstop that asks whether the input desktop can be opened. Occlusion is the window's
// client rectangle minus every opaque, visible, non-cloaked top-level window above it in the z-order; a layered,
// shaped, composition-only or same-process window never counts as covering, so a doubtful case keeps rendering.
// XMB_RENDER_GATE=0 turns the gate off.
bool renderGateClosed();

class RenderGate final:public QObject {
    Q_OBJECT
public:
    // `window` is the top-level widget that holds the GL view; `view` is updated when the gate opens.
    RenderGate(QWidget* window,QWidget* view,QObject* parent=nullptr);
    ~RenderGate() override;
    // Test hook (XMB_RENDER_GATE_FORCE=<reason> is the env equivalent): force the gate shut regardless of the inputs.
    void evaluateNow();
private:
    friend class SessionFilter;
    void evaluate();
    QWidget* window_;
    QWidget* view_;
    QTimer timer_;
    QString reason_;
    bool sessionAway_=false;
    bool inputDesktopProbe_=false;
    qint64 closedAt_=0;
    class SessionFilter* filter_=nullptr;
};
}
