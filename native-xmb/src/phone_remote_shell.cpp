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
        showMessage(tr("Phone Remote"),
            tr("Open this on a phone on your network:\n%1\n\nPairing code: %2").arg(phoneRemote_.url().toString(),phoneRemote_.pairingCode()),
            MessageKind::Page);
    }
    emit changed();
}
}  // namespace xmb
