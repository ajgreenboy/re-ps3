// Network > Phone Remote (HANDOFF-2026-11-09 D11): a LAN web page that drives the XMB from a phone.
// The column row toggles the remote on and off; turning it on shows the pairing code and URL. Native-only,
// off by default. The view connects phoneRemote().keyRequested/psRequested to the key path.
#include "shell.h"
#include "phone_remote.h"

namespace xmb {
void Shell::initPhoneRemote() {
    for(auto& column:categories_) {
        if(column.id!=QLatin1String("network"))continue;
        Entry row;
        row.id=QStringLiteral("phone-remote");row.title=tr("Phone Remote");
        row.action=Action::PhoneRemote;row.value=QStringLiteral("toggle");
        row.container=QStringLiteral("explore_category_network");
        row.texture=QStringLiteral("item_tex_plain_folder");row.source=QStringLiteral("native");
        column.entries.push_back(std::move(row));
    }
}
void Shell::activatePhoneRemote(const Entry& entry) {
    Q_UNUSED(entry);
    // The row is a simple on/off toggle: turning it on starts the server and surfaces the code; turning it off
    // stops it. Off by default, so the input-injection endpoint is never exposed unless the user turns it on.
    if(phoneRemote_.isEnabled()) {
        phoneRemote_.setEnabled(false);
        notify(tr("Phone Remote"),tr("Disabled."));
    } else {
        if(!phoneRemote_.setEnabled(true)) {
            showMessage(tr("Phone Remote"),tr("The remote could not be started: %1").arg(phoneRemote_.errorString()),MessageKind::Page);
            return;
        }
        // The pairing code and URL: the URL embeds the code so a phone can open it and pair in one step.
        // The server listens on this computer only unless XMB_PHONE_REMOTE_LAN=1, and a loopback address cannot be
        // opened from a phone, so the page says so instead of inviting a phone to open it.
        const QUrl url=phoneRemote_.url();
        const bool loopback=url.host()==QLatin1String("127.0.0.1");
        showMessage(tr("Phone Remote"),
            loopback
                ?tr("The remote is limited to this computer, so a phone cannot reach it yet.\nTo allow phones on your network, start the XMB with XMB_PHONE_REMOTE_LAN=1.\n\nThis computer: %1\nPairing code: %2").arg(url.toString(),phoneRemote_.pairingCode())
                :tr("Open this on a phone on your network:\n%1\n\nPairing code: %2").arg(url.toString(),phoneRemote_.pairingCode()),
            MessageKind::Page);
    }
    emit changed();
}
}  // namespace xmb
