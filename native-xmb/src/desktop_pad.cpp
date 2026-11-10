#include "desktop_pad.h"
#include "pad.h"
#include "ps_watcher.h"
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace xmb::desktoppad {
namespace {
// Win32 virtual-key codes, spelled out so the mapper builds (and is tested) without windows.h.
constexpr int kEscape=0x1B,kReturn=0x0D,kTab=0x09,kLeftShift=0xA0,kLeftAlt=0xA4,kLeftWin=0x5B;
constexpr int kLeft=0x25,kUp=0x26,kRight=0x27,kDown=0x28;
constexpr int kExtendedBit=0x10000;
}

double speedMultiplier(int level) {
    static const double table[]{0.5,0.75,1.0,1.5,2.2};
    return table[std::clamp(level,1,5)-1];
}

Config configFromJson(const QJsonObject& o) {
    Config c;
    const auto number=[&](const char* key,double& into){if(o.contains(key))into=o.value(key).toDouble(into);};
    const auto integer=[&](const char* key,int& into){if(o.contains(key))into=o.value(key).toInt(into);};
    number("pointerSpeed",c.pointerSpeed);number("pointerDeadzone",c.pointerDeadzone);
    number("pointerMaxPxPerSecond",c.pointerMaxPxPerSecond);number("pointerExponent",c.pointerExponent);
    number("pointerFinePxPerSecond",c.pointerFinePxPerSecond);number("scrollDeadzone",c.scrollDeadzone);
    number("scrollMaxNotchesPerSecond",c.scrollMaxNotchesPerSecond);number("scrollExponent",c.scrollExponent);
    integer("scrollMinimumUnits",c.scrollMinimumUnits);integer("repeatDelayMs",c.repeatDelayMs);
    integer("repeatIntervalMs",c.repeatIntervalMs);integer("switcherCommitMs",c.switcherCommitMs);
    return c;
}

QJsonObject toJson(const Action& a) {
    QJsonObject o{{"t",double(a.t)}};
    switch(a.kind) {
    case Kind::Move:o.insert("kind","move");o.insert("dx",a.dx);o.insert("dy",a.dy);break;
    case Kind::Button:o.insert("kind","button");o.insert("button",a.button==0?"left":"right");o.insert("down",a.down);break;
    case Kind::Wheel:o.insert("kind","wheel");o.insert("v",a.wheelV);o.insert("h",a.wheelH);break;
    case Kind::Key:o.insert("kind","key");o.insert("vk",a.vk);o.insert("down",a.down);o.insert("extended",a.extended);o.insert("repeat",a.repeat);break;
    case Kind::ToggleKeyboard:o.insert("kind","toggleKeyboard");break;
    }
    if(!a.why.isEmpty())o.insert("why",a.why);
    return o;
}

// ---- Mapper ----------------------------------------------------------------------------------------------
double Mapper::pointerSpeedPxPerSecond(const Config& c,double deflection) {
    deflection=std::clamp(deflection,0.0,1.0);
    if(deflection<=c.pointerDeadzone)return 0;
    const double m=(deflection-c.pointerDeadzone)/(1-c.pointerDeadzone);   // 0..1 after the radial deadzone
    return c.pointerSpeed*(c.pointerFinePxPerSecond*m+(c.pointerMaxPxPerSecond-c.pointerFinePxPerSecond)*std::pow(m,c.pointerExponent));
}
double Mapper::scrollUnitsPerSecond(const Config& c,double deflection) {
    deflection=std::clamp(deflection,0.0,1.0);
    if(deflection<=c.scrollDeadzone)return 0;
    const double m=(deflection-c.scrollDeadzone)/(1-c.scrollDeadzone);
    return 120.0*c.scrollMaxNotchesPerSecond*std::pow(m,c.scrollExponent);
}

void Mapper::key(std::vector<Action>& out,qint64 now,int vk,bool down,bool extended,const char* why,bool repeat) {
    const int token=vk|(extended?kExtendedBit:0);
    const auto it=std::find(keysDown_.begin(),keysDown_.end(),token);
    const bool isDown=it!=keysDown_.end();
    if(down && isDown && !repeat)return;
    if(!down && !isDown)return;
    if(down && !isDown)keysDown_.push_back(token);
    if(!down)keysDown_.erase(it);
    Action a;a.kind=Kind::Key;a.t=now;a.vk=vk;a.down=down;a.extended=extended;a.repeat=repeat;a.why=QString::fromLatin1(why);
    out.push_back(a);
}
void Mapper::mouse(std::vector<Action>& out,qint64 now,int button,bool down,const char* why) {
    if(mouseDown_[button]==down)return;
    mouseDown_[button]=down;
    Action a;a.kind=Kind::Button;a.t=now;a.button=button;a.down=down;a.why=QString::fromLatin1(why);
    out.push_back(a);
}
void Mapper::closeSwitcher(std::vector<Action>& out,qint64 now,const char* why) {
    if(!altDown_)return;
    key(out,now,kLeftAlt,false,false,why);
    altDown_=false;
}

bool Mapper::anythingHeld() const { return altDown_ || mouseDown_[0] || mouseDown_[1] || !keysDown_.empty(); }

std::vector<Action> Mapper::releaseAll(qint64 now) {
    std::vector<Action> out;
    for(int b=0;b<2;++b)mouse(out,now,b,false,"release all");
    const auto held=keysDown_;
    for(auto it=held.rbegin();it!=held.rend();++it)key(out,now,*it&0xFFFF,false,(*it&kExtendedBit)!=0,"release all");
    altDown_=false;
    accX_=accY_=accV_=accH_=0;
    return out;
}

std::vector<Action> Mapper::suspend(const Sample& sample,qint64 now) {
    auto out=releaseAll(now);
    // Whatever is down on the pad right now was pressed before this driver was live (a Cross that chose the Switch to Desktop
    // row, a button held through a pause): it does nothing until it is released and pressed again.
    previous_=sample.buttons&quint16(~btn::Guide);
    swallowed_=previous_;
    suppressed_=false;
    last_=now;
    return out;
}

std::vector<Action> Mapper::update(const Sample& s,qint64 now) {
    std::vector<Action> out;
    double dt=last_<0?0.0:double(now-last_)/1000.0;
    if(dt>0.25)dt=0;             // a stall is not a long move
    dt=std::min(dt,0.05);
    last_=now;

    // Guide/PS belongs to the XMB (a tap returns to it). While it is down nothing else acts, so pressing it together with a
    // face button cannot click anything; buttons held across it are ignored until released.
    if(s.buttons&btn::Guide) {
        if(!suppressed_) {
            suppressed_=true;
            out=releaseAll(now);
        }
        previous_=s.buttons&quint16(~btn::Guide);
        swallowed_|=previous_;
        return out;
    }
    suppressed_=false;
    const quint16 masked=s.buttons;

    if(altDown_ && now-switchLast_>=config_.switcherCommitMs)closeSwitcher(out,now,"window switcher: commit (idle)");
    const quint16 pressed=quint16(masked&~previous_),released=quint16(previous_&~masked);

    // While the window switcher is open (Alt held), Cross/Triangle commit the choice and Circle cancels it; the press is
    // consumed so it does not also click or press Enter.
    quint16 consumed=0;
    if(altDown_) {
        if(pressed&(btn::Cross|btn::Triangle)) {
            closeSwitcher(out,now,"window switcher: commit (Cross/Triangle)");
            consumed|=quint16(pressed&(btn::Cross|btn::Triangle));
        }
        if(pressed&btn::Circle) {
            key(out,now,kEscape,true,false,"window switcher: cancel (Circle)");
            key(out,now,kEscape,false,false,"window switcher: cancel (Circle)");
            closeSwitcher(out,now,"window switcher: cancel (Circle)");
            consumed|=btn::Circle;
        }
    }
    swallowed_|=consumed;
    const quint16 pressedNow=quint16(pressed&~consumed),releasedNow=quint16(released&~swallowed_);
    swallowed_&=masked;   // a swallowed button is forgotten once it is up

    for(const quint16 bit:{btn::LB,btn::RB}) {
        if(!(pressedNow&bit))continue;
        const bool backwards=bit==btn::LB;
        const char* why=backwards?"LB -> Alt+Shift+Tab":"RB -> Alt+Tab";
        if(!altDown_){key(out,now,kLeftAlt,true,false,why);altDown_=true;}
        if(backwards)key(out,now,kLeftShift,true,false,why);
        key(out,now,kTab,true,false,why);
        key(out,now,kTab,false,false,why);
        if(backwards)key(out,now,kLeftShift,false,false,why);
        switchLast_=now;
    }

    if(pressedNow&btn::Cross)mouse(out,now,0,true,"Cross -> left button down");
    if(releasedNow&btn::Cross)mouse(out,now,0,false,"Cross -> left button up");
    if(pressedNow&btn::Square)mouse(out,now,1,true,"Square -> right button down");
    if(releasedNow&btn::Square)mouse(out,now,1,false,"Square -> right button up");

    struct KeyMap { quint16 bit; int vk; bool extended; const char* name; };
    static const KeyMap simple[]{{btn::Circle,kEscape,false,"Circle -> Escape"},{btn::Triangle,kReturn,false,"Triangle -> Enter"},
        {btn::Back,kLeftWin,true,"Back/View -> Windows key"}};
    for(const auto& m:simple) {
        if(pressedNow&m.bit)key(out,now,m.vk,true,m.extended,m.name);
        if(releasedNow&m.bit)key(out,now,m.vk,false,m.extended,m.name);
    }
    static const KeyMap arrows[]{{btn::Up,kUp,true,"D-pad Up -> Up arrow"},{btn::Down,kDown,true,"D-pad Down -> Down arrow"},
        {btn::Left,kLeft,true,"D-pad Left -> Left arrow"},{btn::Right,kRight,true,"D-pad Right -> Right arrow"}};
    for(int i=0;i<4;++i) {
        const auto& m=arrows[i];
        if(pressedNow&m.bit) {
            key(out,now,m.vk,true,true,m.name);
            nextRepeat_[i]=now+config_.repeatDelayMs;
        } else if((masked&m.bit) && !(swallowed_&m.bit) && keysDownContains(m.vk) && now>=nextRepeat_[i]) {
            key(out,now,m.vk,true,true,m.name,true);
            nextRepeat_[i]+=config_.repeatIntervalMs;   // carried, so a 10 ms tick does not stretch 45 ms to 50
            if(nextRepeat_[i]<=now)nextRepeat_[i]=now+config_.repeatIntervalMs;
        }
        if(releasedNow&m.bit)key(out,now,m.vk,false,true,m.name);
    }

    if(pressedNow&btn::Start) {
        Action a;a.kind=Kind::ToggleKeyboard;a.t=now;a.why=QStringLiteral("Start -> on-screen keyboard");
        out.push_back(a);
    }

    // Left stick: pointer. Radial deadzone, then speed grows with deflection^exponent; sub-pixel motion is carried over.
    const double mag=std::hypot(double(s.lx),double(s.ly));
    const double speed=pointerSpeedPxPerSecond(config_,mag);
    if(speed>0 && mag>0) {
        accX_+=s.lx/mag*speed*dt;
        accY_+=s.ly/mag*speed*dt;
        const int dx=int(std::trunc(accX_)),dy=int(std::trunc(accY_));
        if(dx||dy) {
            accX_-=dx;accY_-=dy;
            Action a;a.kind=Kind::Move;a.t=now;a.dx=dx;a.dy=dy;
            out.push_back(a);
        }
    } else accX_=accY_=0;

    // Right stick: scroll. Each axis has its own deadzone; stick down scrolls down, stick right scrolls right.
    const double sv=-(s.ry<0?-1.0:1.0)*scrollUnitsPerSecond(config_,std::abs(double(s.ry)));
    const double sh=(s.rx<0?-1.0:1.0)*scrollUnitsPerSecond(config_,std::abs(double(s.rx)));
    accV_=sv==0?0:accV_+sv*dt;
    accH_=sh==0?0:accH_+sh*dt;
    int wheelV=0,wheelH=0;
    if(std::abs(accV_)>=config_.scrollMinimumUnits){wheelV=int(std::trunc(accV_));accV_-=wheelV;}
    if(std::abs(accH_)>=config_.scrollMinimumUnits){wheelH=int(std::trunc(accH_));accH_-=wheelH;}
    if(wheelV||wheelH) {
        Action a;a.kind=Kind::Wheel;a.t=now;a.wheelV=wheelV;a.wheelH=wheelH;
        out.push_back(a);
    }
    previous_=masked;
    return out;
}

// ---- --desktop-pad-test ------------------------------------------------------------------------------------
int runPadTest(const QString& input,const QString& output) {
    QFile in(input);
    if(!in.open(QIODevice::ReadOnly)){qCritical().noquote()<<"desktop-pad-test: cannot read"<<input<<in.errorString();return 2;}
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(in.readAll(),&parseError);
    if(!document.isObject()){qCritical().noquote()<<"desktop-pad-test: bad JSON:"<<parseError.errorString();return 2;}
    const QJsonObject root=document.object();
    const Config config=configFromJson(root.value("config").toObject());
    const int tick=std::max(1,root.value("tickMs").toInt(10));
    struct Timed { qint64 t; Sample s; };
    std::vector<Timed> samples;
    for(const auto& v:root.value("samples").toArray()) {
        const auto o=v.toObject();
        samples.push_back({qint64(o.value("t").toDouble()),Sample{quint16(o.value("buttons").toInt()),float(o.value("lx").toDouble()),
            float(o.value("ly").toDouble()),float(o.value("rx").toDouble()),float(o.value("ry").toDouble())}});
    }
    std::stable_sort(samples.begin(),samples.end(),[](const Timed& a,const Timed& b){return a.t<b.t;});
    qint64 end=qint64(root.value("endMs").toDouble(samples.empty()?0:double(samples.back().t)));
    Mapper mapper(config);
    QJsonArray actions;
    int ticks=0;
    size_t next=0;
    Sample current;
    for(qint64 t=0;t<=end;t+=tick,++ticks) {
        while(next<samples.size() && samples[next].t<=t)current=samples[next++].s;
        for(const auto& a:mapper.update(current,t))actions.push_back(toJson(a));
    }
    for(const auto& a:mapper.releaseAll(end))actions.push_back(toJson(a));
    QJsonObject effective{{"pointerSpeed",config.pointerSpeed},{"pointerDeadzone",config.pointerDeadzone},
        {"pointerMaxPxPerSecond",config.pointerMaxPxPerSecond},{"pointerExponent",config.pointerExponent},
        {"scrollDeadzone",config.scrollDeadzone},{"scrollMaxNotchesPerSecond",config.scrollMaxNotchesPerSecond},
        {"repeatDelayMs",config.repeatDelayMs},{"repeatIntervalMs",config.repeatIntervalMs},{"switcherCommitMs",config.switcherCommitMs}};
    // The curves sampled at a few deflections, so the documentation table and the test read the same numbers.
    QJsonArray curve;
    for(const double d:{0.0,0.1,0.2,0.25,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0})
        curve.push_back(QJsonObject{{"deflection",d},{"pointerPxPerSecond",Mapper::pointerSpeedPxPerSecond(config,d)},
            {"scrollUnitsPerSecond",Mapper::scrollUnitsPerSecond(config,d)}});
    QJsonObject out{{"ticks",ticks},{"endMs",double(end)},{"config",effective},{"curve",curve},{"actions",actions},
        {"held",mapper.anythingHeld()}};
    QFile file(output);
    const auto bytes=QJsonDocument(out).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()){qCritical().noquote()<<"desktop-pad-test:"<<file.errorString();return 2;}
    qInfo().noquote()<<QString("desktop-pad-test: %1 ticks, %2 actions -> %3").arg(ticks).arg(actions.size()).arg(output);
    return 0;
}
}
