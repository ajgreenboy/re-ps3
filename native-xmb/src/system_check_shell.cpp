// Settings > System Settings > System Check (self_check.h; audit/SYSTEM-CHECK.md). A native row after System
// Information: a page of read-only checks (firmware files, fonts, data folder, free space, graphics, network,
// the configured services, Steam, RAWG), each row OK / warning / problem with its fix one Cross away.
#include "shell.h"
#include "assets.h"
#include "glyphs.h"
#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

namespace xmb {
namespace {
// The graphics strings without touching the view's context: a short-lived offscreen context on this thread.
void readGl(QString* version,QString* renderer,QString* vendor) {
    QOffscreenSurface surface;surface.create();
    QOpenGLContext context;
    if(!context.create() || !context.makeCurrent(&surface))return;
    auto* f=context.functions();
    const auto text=[f](GLenum name){const auto* s=f->glGetString(name);return s?QString::fromLatin1(reinterpret_cast<const char*>(s)):QString();};
    *version=text(GL_VERSION);*renderer=text(GL_RENDERER);*vendor=text(GL_VENDOR);
    context.doneCurrent();
}
}

void Shell::openSystemCheck() {
    if(pages_.size()>=32)return;
    Page page;page.title=tr("System Check");page.systemCheck=true;
    Entry wait;wait.id=QStringLiteral("check:wait");wait.title=tr("Checking...");wait.action=Action::Information;
    wait.container=QStringLiteral("explore_plugin_full");wait.texture=QStringLiteral("item_tex_plain_folder");wait.source=QStringLiteral("native");
    page.entries.push_back(wait);
    pages_.push_back(std::move(page));
    SelfCheckInputs inputs;
    inputs.devFlashRoot=assets_.root();
    inputs.dataDirectory=dataDirectory_;
    inputs.appVersion=QCoreApplication::applicationVersion();
    inputs.steamRoot=steamRoot_;
    inputs.tailscaleExecutable=tailscale_.executable();
    readGl(&inputs.glVersion,&inputs.glRenderer,&inputs.glVendor);
    if(!systemCheck_) {
        systemCheck_=new SelfCheck(this);
        connect(systemCheck_,&SelfCheck::finished,this,[this](const QVector<CheckResult>& results){
            QVector<Entry> rows;
            int problems=0;
            // Problems first, then warnings, then the passes, each group in the check's own order.
            for(const int level:{int(CheckResult::Fail),int(CheckResult::Warn),int(CheckResult::Ok)})
                for(const auto& r:results) {
                    if(int(r.level)!=level)continue;
                    if(r.level!=CheckResult::Ok)++problems;
                    Entry e;e.id=QStringLiteral("check:")+r.id;e.title=r.title;e.action=Action::Information;
                    e.detail=(r.level==CheckResult::Fail?tr("Problem - "):r.level==CheckResult::Warn?tr("Warning - "):QString())+r.detail;
                    e.value=r.title+QStringLiteral("\n\n")+r.detail+(r.fix.isEmpty()?QString():QStringLiteral("\n\n")+r.fix);
                    e.container=QStringLiteral("explore_plugin_full");e.texture=QStringLiteral("item_tex_plain_folder");e.source=QStringLiteral("native");
                    e.artwork=r.level==CheckResult::Ok?glyphs::check():r.level==CheckResult::Warn?glyphs::warning():glyphs::cross();
                    e.iconSized=true;
                    rows.push_back(std::move(e));
                }
            qInfo().noquote()<<"system check:"<<results.size()<<"check(s),"<<problems<<"problem(s)";
            for(auto& page:pages_)if(page.systemCheck){page.entries=rows;page.selection=0;}
            notify(tr("System Check"),problems==0?tr("All checks passed"):(problems==1?tr("1 problem found"):tr("%1 problems found").arg(problems)));
            emit changed();
        });
    }
    systemCheck_->setInputs(inputs);
    systemCheck_->run();
}

}
