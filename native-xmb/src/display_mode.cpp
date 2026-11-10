#include "display_mode.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QDebug>
#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xmb::display {

const char* hdrName(Hdr h) {
    switch(h) {case Hdr::Off:return "off";case Hdr::On:return "on";default:return "unsupported";}
}
QString modeText(const Mode& m) { return QStringLiteral("%1x%2@%3").arg(m.width).arg(m.height).arg(m.refresh); }
bool parseModeText(const QString& text,Mode* out) {
    static const QRegularExpression re(QStringLiteral("^(\\d{3,5})x(\\d{3,5})@(\\d{1,4})$"));
    const auto m=re.match(text.trimmed());
    if(!m.hasMatch())return false;
    if(out)*out=Mode{m.captured(1).toInt(),m.captured(2).toInt(),m.captured(3).toInt()};
    return true;
}

Request requestFrom(const DisplaySpec& spec) {
    Request r;
    if(!spec.present)return r;
    r.width=spec.width;r.height=spec.height;r.refresh=spec.refresh;
    const QString hdr=spec.hdr.trimmed().toLower();
    r.hdr=hdr==QLatin1String("on")?HdrWish::On:hdr==QLatin1String("off")?HdrWish::Off:HdrWish::Keep;
    return r;
}

Plan makePlan(const Request& request,const Snapshot& now,const QVector<Mode>& available) {
    Plan plan;
    const bool wantsMode=request.width>0 || request.height>0 || request.refresh>0;
    if(wantsMode) {
        // width and height travel together; a refresh alone means "this refresh at the current size".
        const int w=request.width>0?request.width:now.current.mode.width;
        const int h=request.height>0?request.height:now.current.mode.height;
        if((request.width>0)!=(request.height>0))
            plan.skipped<<QStringLiteral("display mode skipped: width and height must be given together");
        else {
            QVector<int> rates;
            for(const auto& m:available)if(m.width==w && m.height==h && !rates.contains(m.refresh))rates.push_back(m.refresh);
            std::sort(rates.begin(),rates.end());
            const auto list=[&]{QStringList s;for(int r:rates)s<<QString::number(r);return s.join(QLatin1Char('/'));};
            if(rates.isEmpty())
                plan.skipped<<QStringLiteral("display mode skipped: %1x%2 is not listed by the monitor").arg(w).arg(h);
            else {
                int refresh=request.refresh;
                if(refresh<=0)refresh=rates.contains(now.current.mode.refresh)?now.current.mode.refresh:rates.last();
                if(!rates.contains(refresh))
                    plan.skipped<<QStringLiteral("display mode skipped: %1x%2@%3 is not listed by the monitor (it lists %4 Hz)")
                        .arg(w).arg(h).arg(refresh).arg(list());
                else {
                    const Mode target{w,h,refresh};
                    if(!(target==now.current.mode)) {
                        plan.changeMode=true;plan.target=now.current;plan.target.mode=target;
                    }
                }
            }
        }
    }
    if(request.hdr!=HdrWish::Keep) {
        const bool on=request.hdr==HdrWish::On;
        if(now.hdr==Hdr::Unsupported)
            plan.skipped<<QStringLiteral("hdr skipped: this display does not support HDR (advanced colour)");
        else if((now.hdr==Hdr::On)!=on) {plan.changeHdr=true;plan.hdrOn=on;}
    }
    return plan;
}

namespace {
QJsonObject fullToJson(const FullMode& f) {
    return QJsonObject{{"width",f.mode.width},{"height",f.mode.height},{"refresh",f.mode.refresh},{"bitsPerPixel",f.bitsPerPixel},
        {"x",f.positionX},{"y",f.positionY},{"orientation",f.orientation}};
}
bool fullFromJson(const QJsonValue& v,FullMode* f) {
    if(!v.isObject())return false;
    const auto o=v.toObject();
    f->mode={o.value("width").toInt(),o.value("height").toInt(),o.value("refresh").toInt()};
    f->bitsPerPixel=o.value("bitsPerPixel").toInt();f->positionX=o.value("x").toInt();f->positionY=o.value("y").toInt();
    f->orientation=o.value("orientation").toInt();
    return f->mode.valid() && f->mode.width<=65536 && f->mode.height<=65536 && f->mode.refresh<=2000;
}
}

QJsonObject RestoreState::toJson() const {
    return QJsonObject{{"version",1},{"device",device},{"title",title},{"at",double(at)},{"original",fullToJson(original)},
        {"originalHdr",QString::fromLatin1(hdrName(originalHdr))},{"changedMode",changedMode},{"changedHdr",changedHdr}};
}
bool RestoreState::fromJson(const QJsonObject& o,RestoreState* out,QString* error) {
    const auto fail=[&](const QString& m){if(error)*error=m;return false;};
    if(o.value("version").toInt()!=1)return fail(QStringLiteral("unknown version"));
    RestoreState s;
    s.device=o.value("device").toString();s.title=o.value("title").toString();s.at=qint64(o.value("at").toDouble());
    if(!fullFromJson(o.value("original"),&s.original))return fail(QStringLiteral("original mode is not valid"));
    const QString h=o.value("originalHdr").toString();
    s.originalHdr=h==QLatin1String("on")?Hdr::On:h==QLatin1String("off")?Hdr::Off:Hdr::Unsupported;
    s.changedMode=o.value("changedMode").toBool();s.changedHdr=o.value("changedHdr").toBool();
    if(s.changedHdr && s.originalHdr==Hdr::Unsupported)return fail(QStringLiteral("hdr change without a known original"));
    if(out)*out=s;
    return true;
}
RestorePlan makeRestorePlan(const RestoreState& state,const Snapshot& now) {
    RestorePlan p;
    p.restoreHdr=state.changedHdr && now.hdr!=Hdr::Unsupported && now.hdr!=state.originalHdr;
    p.restoreMode=state.changedMode && !(now.current==state.original);
    return p;
}

// ====================================================================================================================
#ifdef Q_OS_WIN
namespace {
QString narrow(const wchar_t* s) { return QString::fromWCharArray(s); }

class Win32Backend final : public Backend {
public:
    Win32Backend(std::function<QString()> provider,bool dry):provider_(std::move(provider)),dry_(dry) {
        fakeHdr_=dry && qEnvironmentVariable("XMB_DISPLAY_FAKE_HDR")==QStringLiteral("1");
    }
    bool dryRun() const override { return dry_; }
    void pinDevice(const QString& d) override { pinned_=d; }
    QString deviceName() {
        if(!pinned_.isEmpty())return pinned_;
        QString d=provider_?provider_():QString{};
        if(!d.isEmpty())return d;
        DISPLAY_DEVICEW dd{};dd.cb=sizeof(dd);
        for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&dd,0);++i)
            if(dd.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)return narrow(dd.DeviceName);
        return QStringLiteral("\\\\.\\DISPLAY1");
    }
    bool read(Snapshot* out,QString* error) override {
        const QString dev=deviceName();
        Snapshot s;s.device=dev;
        if(simValid_&&dev==simDevice_) {s.current=simMode_;s.hdr=simHdr_;*out=s;return true;}
        DEVMODEW dm{};dm.dmSize=sizeof(dm);
        if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(dev.utf16()),ENUM_CURRENT_SETTINGS,&dm,0)) {
            if(error)*error=QStringLiteral("EnumDisplaySettings failed for %1").arg(dev);
            return false;
        }
        s.current=fromDevMode(dm);
        s.hdr=readHdr(dev);
        if(dry_ && fakeHdr_ && s.hdr==Hdr::Unsupported)s.hdr=Hdr::Off;
        if(dry_) {simValid_=true;simDevice_=dev;simMode_=s.current;simHdr_=s.hdr;}
        *out=s;
        return true;
    }
    QVector<Mode> modes() override {
        const QString dev=deviceName();
        QVector<Mode> result;
        DEVMODEW cur{};cur.dmSize=sizeof(cur);
        if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(dev.utf16()),ENUM_CURRENT_SETTINGS,&cur,0))return result;
        for(DWORD i=0;;++i) {
            DEVMODEW dm{};dm.dmSize=sizeof(dm);
            if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(dev.utf16()),i,&dm,0))break;
            if(dm.dmBitsPerPel!=cur.dmBitsPerPel)continue;
            if(dm.dmDisplayFlags&DM_INTERLACED)continue;
            const Mode m{int(dm.dmPelsWidth),int(dm.dmPelsHeight),int(dm.dmDisplayFrequency)};
            if(m.valid() && !result.contains(m))result.push_back(m);
        }
        return result;
    }
    bool setMode(const FullMode& mode,QString* error) override {
        const QString dev=deviceName();
        if(dry_) {
            qInfo().noquote()<<QString("display: DRY RUN would ChangeDisplaySettingsEx(%1, %2x%3@%4 %5bpp at %6,%7, CDS_FULLSCREEN)")
                .arg(dev).arg(mode.mode.width).arg(mode.mode.height).arg(mode.mode.refresh).arg(mode.bitsPerPixel).arg(mode.positionX).arg(mode.positionY);
            Snapshot s;QString e;read(&s,&e);   // makes sure the simulated state exists
            simMode_=mode;simDevice_=dev;simValid_=true;
            return true;
        }
        DEVMODEW dm{};dm.dmSize=sizeof(dm);
        if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(dev.utf16()),ENUM_CURRENT_SETTINGS,&dm,0)) {
            if(error)*error=QStringLiteral("EnumDisplaySettings failed for %1").arg(dev);
            return false;
        }
        dm.dmPelsWidth=DWORD(mode.mode.width);dm.dmPelsHeight=DWORD(mode.mode.height);
        dm.dmDisplayFrequency=DWORD(mode.mode.refresh);
        if(mode.bitsPerPixel>0)dm.dmBitsPerPel=DWORD(mode.bitsPerPixel);
        dm.dmPosition.x=mode.positionX;dm.dmPosition.y=mode.positionY;
        dm.dmDisplayOrientation=DWORD(mode.orientation);
        dm.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFREQUENCY|DM_BITSPERPEL|DM_POSITION|DM_DISPLAYORIENTATION;
        const auto dev16=reinterpret_cast<LPCWSTR>(dev.utf16());
        LONG r=ChangeDisplaySettingsExW(dev16,&dm,nullptr,CDS_TEST,nullptr);
        if(r!=DISP_CHANGE_SUCCESSFUL) {
            if(error)*error=QStringLiteral("the driver rejected the mode in the test pass (code %1)").arg(r);
            return false;
        }
        // CDS_FULLSCREEN: temporary, never written to the registry; Windows also drops it when this process ends.
        r=ChangeDisplaySettingsExW(dev16,&dm,nullptr,CDS_FULLSCREEN,nullptr);
        if(r!=DISP_CHANGE_SUCCESSFUL) {
            if(error)*error=QStringLiteral("ChangeDisplaySettingsEx failed (code %1)").arg(r);
            return false;
        }
        return true;
    }
    bool setHdr(bool on,QString* error) override {
        const QString dev=deviceName();
        if(dry_) {
            qInfo().noquote()<<QString("display: DRY RUN would DisplayConfigSetDeviceInfo(SET_ADVANCED_COLOR_STATE %1) on %2")
                .arg(on?"on":"off",dev);
            Snapshot s;QString e;read(&s,&e);
            simHdr_=on?Hdr::On:Hdr::Off;simDevice_=dev;simValid_=true;
            return true;
        }
        LUID adapter{};UINT32 target=0;
        if(!findTarget(dev,&adapter,&target)) {if(error)*error=QStringLiteral("no display path for %1").arg(dev);return false;}
        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE set{};
        set.header.type=DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        set.header.size=sizeof(set);set.header.adapterId=adapter;set.header.id=target;
        set.enableAdvancedColor=on?1:0;
        const LONG r=DisplayConfigSetDeviceInfo(&set.header);
        if(r!=ERROR_SUCCESS) {if(error)*error=QStringLiteral("DisplayConfigSetDeviceInfo failed (code %1)").arg(r);return false;}
        return true;
    }
private:
    static FullMode fromDevMode(const DEVMODEW& dm) {
        FullMode f;
        f.mode={int(dm.dmPelsWidth),int(dm.dmPelsHeight),int(dm.dmDisplayFrequency)};
        f.bitsPerPixel=int(dm.dmBitsPerPel);f.positionX=dm.dmPosition.x;f.positionY=dm.dmPosition.y;
        f.orientation=int(dm.dmDisplayOrientation);
        return f;
    }
    static bool findTarget(const QString& dev,LUID* adapter,UINT32* target) {
        UINT32 np=0,nm=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm)!=ERROR_SUCCESS)return false;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,paths.data(),&nm,modes.data(),nullptr)!=ERROR_SUCCESS)return false;
        paths.resize(np);
        for(const auto& p:paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);
            name.header.adapterId=p.sourceInfo.adapterId;name.header.id=p.sourceInfo.id;
            if(DisplayConfigGetDeviceInfo(&name.header)!=ERROR_SUCCESS)continue;
            if(narrow(name.viewGdiDeviceName).compare(dev,Qt::CaseInsensitive)==0) {
                *adapter=p.targetInfo.adapterId;*target=p.targetInfo.id;return true;
            }
        }
        return false;
    }
    static Hdr readHdr(const QString& dev) {
        LUID adapter{};UINT32 target=0;
        if(!findTarget(dev,&adapter,&target))return Hdr::Unsupported;
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info{};
        info.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        info.header.size=sizeof(info);info.header.adapterId=adapter;info.header.id=target;
        if(DisplayConfigGetDeviceInfo(&info.header)!=ERROR_SUCCESS)return Hdr::Unsupported;
        if(!info.advancedColorSupported)return Hdr::Unsupported;
        return info.advancedColorEnabled?Hdr::On:Hdr::Off;
    }
    std::function<QString()> provider_;
    QString pinned_;
    bool dry_=false,fakeHdr_=false,simValid_=false;
    QString simDevice_;FullMode simMode_;Hdr simHdr_=Hdr::Unsupported;
};
}  // namespace
#endif

bool dryRunRequested() { return qEnvironmentVariable("XMB_DISPLAY_DRY_RUN")==QStringLiteral("1"); }

namespace {
// Non-Windows build: nothing to read or write.
class NullBackend final : public Backend {
public:
    bool read(Snapshot*,QString* e) override {if(e)*e=QStringLiteral("not available on this platform");return false;}
    QVector<Mode> modes() override { return {}; }
    bool setMode(const FullMode&,QString* e) override {if(e)*e=QStringLiteral("not available");return false;}
    bool setHdr(bool,QString* e) override {if(e)*e=QStringLiteral("not available");return false;}
};
}
std::unique_ptr<Backend> makeWin32Backend(std::function<QString()> provider,bool dry) {
#ifdef Q_OS_WIN
    return std::make_unique<Win32Backend>(std::move(provider),dry);
#else
    Q_UNUSED(provider);Q_UNUSED(dry);
    return std::make_unique<NullBackend>();
#endif
}

// ====================================================================================================================
Controller::Controller(std::unique_ptr<Backend> backend,const QString& stateFile):backend_(std::move(backend)),stateFile_(stateFile) {}

bool Controller::pending() const { return !stateFile_.isEmpty() && QFileInfo::exists(stateFile_); }

bool Controller::saveState(const RestoreState& state) {
    if(stateFile_.isEmpty())return false;
    QDir().mkpath(QFileInfo(stateFile_).absolutePath());
    QSaveFile f(stateFile_);
    if(!f.open(QIODevice::WriteOnly))return false;
    f.write(QJsonDocument(state.toJson()).toJson());
    return f.commit();
}

bool Controller::apply(const DisplaySpec& spec,const QString& title,QString* note) {
    const Request request=requestFrom(spec);
    if(!request.any())return false;
    if(pending())restore(QStringLiteral("a previous change was still pending"));
    Snapshot now;QString error;
    if(!backend_->read(&now,&error)) {
        qWarning().noquote()<<"display: cannot read the current mode, nothing applied:"<<error;
        if(note)*note=error;
        return false;
    }
    const auto available=backend_->modes();
    const Plan plan=makePlan(request,now,available);
    qInfo().noquote()<<QString("display: %1 asks %2 hdr=%3; now %4 hdr=%5 on %6%7")
        .arg(title,request.width>0||request.height>0||request.refresh>0?
             QString("%1x%2@%3").arg(request.width).arg(request.height).arg(request.refresh):QStringLiteral("(mode kept)"),
             request.hdr==HdrWish::On?"on":request.hdr==HdrWish::Off?"off":"keep",modeText(now.current.mode),
             QString::fromLatin1(hdrName(now.hdr)),now.device,backend_->dryRun()?QStringLiteral(" [dry run]"):QString{});
    for(const auto& s:plan.skipped)qWarning().noquote()<<"display:"<<s;
    if(!plan.anything()) {
        if(note)*note=plan.skipped.isEmpty()?QStringLiteral("already in the requested mode"):plan.skipped.join(QStringLiteral("; "));
        qInfo()<<"display: nothing to apply";
        return false;
    }
    // The state to restore is written BEFORE anything changes, so a crash at any point after this is repaired later.
    RestoreState state;
    state.device=now.device;state.title=title;state.at=QDateTime::currentMSecsSinceEpoch();
    state.original=now.current;state.originalHdr=now.hdr;
    state.changedMode=plan.changeMode;state.changedHdr=plan.changeHdr;
    if(!saveState(state)) {
        qWarning().noquote()<<"display: cannot save the restore state to"<<stateFile_<<"- not changing the display";
        if(note)*note=QStringLiteral("restore state could not be saved");
        return false;
    }
    bool applied=false;
    if(plan.changeMode) {
        if(backend_->setMode(plan.target,&error)) {
            qInfo().noquote()<<"display: mode set to"<<modeText(plan.target.mode)<<(backend_->dryRun()?"(dry run)":"(temporary, CDS_FULLSCREEN)");
            applied=true;
        } else {
            qWarning().noquote()<<"display: mode"<<modeText(plan.target.mode)<<"failed:"<<error<<"- left as it was";
            state.changedMode=false;
        }
    }
    if(plan.changeHdr) {
        if(backend_->setHdr(plan.hdrOn,&error)) {
            qInfo().noquote()<<"display: hdr set to"<<(plan.hdrOn?"on":"off")<<(backend_->dryRun()?"(dry run)":"");
            applied=true;
        } else {
            qWarning().noquote()<<"display: hdr change failed:"<<error<<"- left as it was";
            state.changedHdr=false;
        }
    }
    if(!state.changedMode && !state.changedHdr)QFile::remove(stateFile_);
    else saveState(state);
    if(note)*note=applied?QStringLiteral("applied"):QStringLiteral("nothing applied");
    return applied;
}

bool Controller::restore(const QString& why) {
    QFile f(stateFile_);
    if(stateFile_.isEmpty() || !f.exists())return true;
    RestoreState state;QString error;
    if(!f.open(QIODevice::ReadOnly) || !RestoreState::fromJson(QJsonDocument::fromJson(f.readAll()).object(),&state,&error)) {
        f.close();
        qWarning().noquote()<<"display: restore state unreadable"<<error<<"- removing it";
        QFile::remove(stateFile_);
        return true;
    }
    f.close();
    qInfo().noquote()<<QString("display: restore begin (%1) for \"%2\" original %3 hdr=%4")
        .arg(why,state.title,modeText(state.original.mode),QString::fromLatin1(hdrName(state.originalHdr)));
    backend_->pinDevice(state.device);
    Snapshot now;
    bool ok=true;
    if(!backend_->read(&now,&error)) {
        qWarning().noquote()<<"display: restore cannot read the display"<<state.device<<":"<<error;
        backend_->pinDevice(QString{});
        // The display is gone (unplugged, renamed): there is nothing left to put back on it.
        QFile::remove(stateFile_);
        return true;
    }
    const RestorePlan plan=makeRestorePlan(state,now);
    if(plan.restoreHdr) {
        if(backend_->setHdr(state.originalHdr==Hdr::On,&error))qInfo().noquote()<<"display: hdr restored to"<<hdrName(state.originalHdr);
        else {qWarning().noquote()<<"display: hdr restore failed:"<<error;ok=false;}
    }
    if(plan.restoreMode) {
        if(backend_->setMode(state.original,&error))qInfo().noquote()<<"display: mode restored to"<<modeText(state.original.mode);
        else {qWarning().noquote()<<"display: mode restore failed:"<<error;ok=false;}
    }
    if(!plan.restoreHdr && !plan.restoreMode)qInfo()<<"display: restore found the display already as it was";
    Snapshot after;
    if(backend_->read(&after,&error))
        qInfo().noquote()<<QString("display: restore end now %1 hdr=%2 (%3)").arg(modeText(after.current.mode),QString::fromLatin1(hdrName(after.hdr)),
            ok?"ok":"INCOMPLETE");
    backend_->pinDevice(QString{});
    if(ok)QFile::remove(stateFile_);
    else qWarning()<<"display: the restore state is kept and will be tried again at the next start";
    return ok;
}

bool Controller::restoreIfPending() {
    if(!pending())return true;
    qWarning()<<"display: a changed display was left by the previous run; restoring it";
    return restore(QStringLiteral("startup"));
}

QString Controller::describe() {
    Snapshot s;QString error;
    if(!backend_->read(&s,&error))return QStringLiteral("not reported (%1)").arg(error);
    const QString hdr=s.hdr==Hdr::Unsupported?QStringLiteral("HDR not supported"):s.hdr==Hdr::On?QStringLiteral("HDR on"):QStringLiteral("HDR off");
    return QStringLiteral("%1 x %2 at %3 Hz, %4").arg(s.current.mode.width).arg(s.current.mode.height).arg(s.current.mode.refresh).arg(hdr);
}

namespace { Controller* g_active=nullptr; }
void setActiveController(Controller* c) { g_active=c; }
Controller* activeController() { return g_active; }

QString currentSummary() {
    if(g_active)return g_active->describe();
    Controller temp(makeWin32Backend({},false),QString{});
    return temp.describe();
}

// ====================================================================================================================
#ifdef Q_OS_WIN
namespace {
Controller* g_testController=nullptr;
BOOL WINAPI testCtrlHandler(DWORD) {
    // Ctrl-C / console close during the test: put the display back before the process goes.
    if(g_testController)g_testController->restore(QStringLiteral("console interrupt"));
    return FALSE;
}
}
#endif

int runModeTest(const QString& spec,int seconds,const QString& dataDir) {
    Mode wanted;
    if(!parseModeText(spec,&wanted)){qCritical().noquote()<<"display-test: use <w>x<h>@<hz>, e.g. 1920x1080@60";return 2;}
    seconds=std::clamp(seconds,1,15);   // a test is a few seconds, never a lasting change
    Controller controller(makeWin32Backend({},dryRunRequested()),QDir(dataDir).filePath("display-restore.json"));
    // A previous run that died mid-test left its state file: put that back before looking at anything.
    controller.restoreIfPending();
    Snapshot before;QString error;
    if(!controller.backend()->read(&before,&error)){qCritical().noquote()<<"display-test: cannot read the display:"<<error;return 2;}
    qInfo().noquote()<<QString("display-test: before %1 hdr=%2 on %3%4").arg(modeText(before.current.mode),QString::fromLatin1(hdrName(before.hdr)),
        before.device,controller.backend()->dryRun()?QStringLiteral(" [dry run]"):QString{});
    const auto available=controller.backend()->modes();
    QStringList listed;for(const auto& m:available)listed<<modeText(m);
    qInfo().noquote()<<"display-test: the monitor lists"<<available.size()<<"modes";
    if(!available.contains(wanted)){
        qCritical().noquote()<<"display-test: refused:"<<modeText(wanted)<<"is not a mode this monitor lists. Listed:"<<listed.join(' ');
        return 3;
    }
#ifdef Q_OS_WIN
    g_testController=&controller;SetConsoleCtrlHandler(testCtrlHandler,TRUE);
#endif
    DisplaySpec ds;ds.present=true;ds.width=wanted.width;ds.height=wanted.height;ds.refresh=wanted.refresh;
    QString note;
    const bool applied=controller.apply(ds,QStringLiteral("--display-mode-test"),&note);
    qInfo().noquote()<<"display-test: apply ->"<<(applied?"applied":"not applied")<<note;
    if(applied) {
        Snapshot during;
        if(controller.backend()->read(&during,&error))
            qInfo().noquote()<<QString("display-test: during %1 (holding %2 s)").arg(modeText(during.current.mode)).arg(seconds);
        QThread::msleep(ulong(seconds)*1000);
    }
    // Always restore, applied or not.
    const bool restored=controller.restore(QStringLiteral("display-mode-test end"));
#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(testCtrlHandler,FALSE);g_testController=nullptr;
#endif
    Snapshot after;
    const bool read=controller.backend()->read(&after,&error);
    const bool same=read && after.current==before.current && after.hdr==before.hdr;
    qInfo().noquote()<<QString("display-test: after %1 hdr=%2; restored exactly=%3 restoreFile=%4")
        .arg(read?modeText(after.current.mode):QStringLiteral("?"),read?QString::fromLatin1(hdrName(after.hdr)):QStringLiteral("?"),
             same?"yes":"NO",controller.pending()?"still present":"removed");
    return (restored && same)?0:1;
}

}  // namespace xmb::display
