// Message surfaces: the firmware's five dialog/message shapes, drawn from the measured reference
// layouts and the RCO assets they are built from (audit/DIALOGS-FIRMWARE.md).
//
// There is no dialog RCO. msgdialog_plugin.rco holds two objects, no mbox_* exists anywhere, and every
// plane, label and button of a message box is created by vsh.elf at run time, so the layout constants
// below are not a recovered paf projection. Each one is either a pixel measurement of a firmware
// reference frame (D3 at 14.4 s, D4 at 20.6 s - the two shapes a local frame actually shows) or an
// explicit native fit (the modal box rectangle, which no local frame shows; section 5 of the audit says
// what would prove it). The distinction is stated on every fitted value.
//
// Nothing here dims the frame. Both measured reference frames have the background, ribbon, particles and
// info bar at the same luminance as the frame without a message, so the 135-alpha full-frame dim this
// build used to draw was an invention and is gone.
#include "view.h"
#include "assets.h"
#include "shell.h"
#include <QFontMetrics>
#include <QPainter>
#include <cmath>
#include <numbers>

namespace xmb {
namespace {
// ---- Measured on the firmware reference frames (1920x1080) ----------------------------------------
// D4 header: icon ink 118..154 x 153..188, title ink from x 169 (read as column runs, not by eye).
constexpr double kChromeIconX=119.,kChromeIconY=154.,kChromeIconSize=36.;
constexpr double kChromeTitleX=169.;
constexpr double kChromeTitleTop=152.;                                   // title ink centre y 174
// D4 rules: core row 198 (delta 49.7 over background 27 -> alpha 0.218) and core row 872 (delta 38.0
// over 66 -> alpha 0.201), flat across the frame. The audit's table rounds these to 197/877 and
// 0.22/0.15; the firmware frame measures 198/872 and 0.22/0.20.
constexpr double kRuleTop=198.,kRuleBottom=872.;
constexpr int kRuleTopAlpha=56,kRuleBottomAlpha=51;
// D4 body: line 1 ink y 509, line 2 ink y 543 (pitch 34 by measurement, 31..33 in the audit's rounding),
// font ~24 px. AlignTop puts the font's own ascent above the ink, so the rect starts five pixels above the
// measured ink row.
constexpr double kBodyLeft=260.,kBodyWidth=1400.,kBodyTop=504.;
constexpr double kBodyPitch=32.;
constexpr int kBodySize=24,kTitleSize=23,kInfoRowSize=22;
// D4 hint: glyph + label x 1155..1238 with ink y 889..909, glyph ~20 px; the 30x30 textures are fitted to
// the glyph box through drawImage's alpha bounds.
constexpr double kHintX=1155.,kHintInkTop=889.,kHintGlyphSize=22.,kHintLabelGap=8.,kHintGroupGap=22.;
constexpr int kHintSize=21;
// A modal box has no measured hint baseline; its row sits this far under the plate, at the same x.
constexpr double kHintBoxGap=22.;
// D3 inline line: left at the level-1 item-label column, ink centre y 504, ink height 20 px (~24-28 px
// font). 628 is the measured ink origin of the firmware's own line; the native label column's ink starts
// at 624, so the line is the reference's, not a re-fit of it.
constexpr double kInlineX=628.,kInlineCentreY=504.;
constexpr int kInlineSize=26;
// D4 keeps the analog clock face while the info bar's plate and date are hidden: centre ~(1776,93).
constexpr double kClockX=1776.,kClockY=93.;
// ---- Native fit (no local frame shows a modal box; audit 2.1 "Screen position and size") ----------
// The width is the one real anchor: tex_msgdialog_bar is 960 px wide and TRACED 0x00343FFC assigns it to
// exactly two bar objects, drawn here at the box's own width instead of stretched to 1920.
constexpr double kBoxWidth=960.,kBoxTop=250.;
constexpr double kBoxTitleX=28.,kBoxTitleY=22.,kBoxBodyTop=96.;
// Splits the shell's "Title\n\nbody" shape. A message with no heading leaves the heading empty and the
// whole text is the body.
QString takeHeading(QString& text) {
    auto lines=text.split('\n');
    if(lines.size()>1 && lines[1].isEmpty()) {
        const QString heading=lines.takeFirst();
        lines.removeFirst();
        text=lines.join('\n');
        return heading;
    }
    return {};
}
} // namespace

// ---- D4 chrome: icon + left title, the two flat rules, the analog clock ---------------------------
void View::drawPageChrome(QPainter& p,const QString& title) {
    // The icon is the focused row's own, the same source the information page has always used: the page
    // the message belongs to shows its item's icon, as D4's "Search for Media Servers" shows the network
    // search icon. A message with no heading draws no header at all.
    if(!title.isEmpty()) {
        const int depth=shell_.depth();
        const auto& rows=shell_.entriesAt(depth);
        const Entry* source=nullptr;
        const int selection=shell_.selectionAt(depth);
        if(selection>=0 && selection<rows.size())source=&rows[selection];
        if(source && source->texture=="23n" && depth>0) {
            const auto& parents=shell_.entriesAt(depth-1);
            const int parent=shell_.selectionAt(depth-1);
            if(parent>=0 && parent<parents.size())source=&parents[parent];
        }
        if(source)drawImage(p,source->container,source->texture,{kChromeIconX,kChromeIconY,kChromeIconSize,kChromeIconSize});
        drawMessageText(p,{kChromeTitleX,kChromeTitleTop,1200,44},title,kTitleSize,Qt::white);
    }
    // The rules are flat alpha rectangles, not tex_msgdialog_bar: the measured references show one
    // uniform colour across the whole width, while that texture is 960 px and centre-bright.
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,kRuleTopAlpha));p.drawRect(QRectF(0,kRuleTop,1920,1));
    p.setBrush(QColor(255,255,255,kRuleBottomAlpha));p.drawRect(QRectF(0,kRuleBottom,1920,1));
    p.restore();
    drawPageClock(p);
}
void View::drawPageClock(QPainter& p) {
    // The measured pages keep only the clock face: no plate, no date text. The hands and their drop
    // shadow are the same geometry the info bar's clock draws.
    const auto time=shell_.clockDateTime().time();
    const QPointF center(kClockX,kClockY);
    const auto hand=[&](double angle,double length){const double a=angle*std::numbers::pi/180.;return QPointF(std::sin(a)*length,-std::cos(a)*length);};
    p.save();p.setBrush(Qt::NoBrush);
    for(int shadow=1;shadow>=0;--shadow) {
        const auto c=center+QPointF(shadow*1.5,shadow*1.5);
        p.setPen(QPen(shadow?QColor(0,0,0,150):QColor(255,255,255),2.7,Qt::SolidLine,Qt::RoundCap));
        p.drawEllipse(c,14,14);p.drawLine(c,c+hand(time.minute()*6,10));p.drawLine(c,c+hand((time.hour()%12)*30+time.minute()*.5,7));
    }
    p.restore();
}

// ---- Text and body --------------------------------------------------------------------------------
void View::drawMessageText(QPainter& p,const QRectF& r,const QString& value,int size,const QColor& colour,int flags) {
    if(value.isEmpty())return;
    if(textRenderer_.ready()) {
        QFont font(shell_.fontFamily());font.setPixelSize(size);
        textRenderer_.draw(*this,p,r,value,font,colour,flags);
        return;
    }
    QFont font(shell_.fontFamily());font.setPixelSize(size);font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);p.setFont(font);
    p.setPen(QColor(0,0,0,38));p.drawText(r.translated(1,1),flags,value);
    p.setPen(colour);p.drawText(r,flags,value);
}
QStringList View::wrapMessage(const QString& body,double width,int size) {
    QFont font(shell_.fontFamily());font.setPixelSize(size);
    const QFontMetrics metrics(font);
    const auto advance=[&](const QString& text){return textRenderer_.ready()?textRenderer_.measure(font,text):metrics.horizontalAdvance(text);};
    QStringList lines;
    for(const auto& paragraph:body.split('\n')) {
        if(paragraph.trimmed().isEmpty()){lines<<QString();continue;}
        QString line;
        for(const auto& word:paragraph.split(' ')) {
            const QString candidate=line.isEmpty()?word:line+' '+word;
            if(!line.isEmpty() && advance(candidate)>width){lines<<line;line=word;}
            else line=candidate;
        }
        if(!line.isEmpty())lines<<line;
    }
    return lines;
}
double View::drawMessageLines(QPainter& p,const QString& body,double top,double pitch,int size,const QColor& colour,double left,double width) {
    if(body.isEmpty())return top;
    double y=top;
    for(const auto& line:wrapMessage(body,width,size)) {
        if(!line.isEmpty())
            drawMessageText(p,{left,y,width,qreal(size)+10.},line,size,colour,Qt::AlignHCenter|Qt::AlignTop);
        y+=pitch;
    }
    return y;
}

// ---- D4 titled status page ------------------------------------------------------------------------
void View::drawMessagePage(QPainter& p) {
    QString text=messageShown_.text;
    const QString heading=takeHeading(text);
    drawPageChrome(p,heading);
    drawMessageLines(p,text,kBodyTop,kBodyPitch,kBodySize,QColor(240,240,240),kBodyLeft,kBodyWidth);
    drawHintRow(p,{{"tex_circle",assets_.text("system_plugin","msg_back","Back")}},kHintInkTop,dialogHintEase_.value);
}

// ---- D3 inline empty-state line -------------------------------------------------------------------
void View::drawInlineMessage(QPainter& p) {
    // A list-row line, not a dialog: no plate, no rules, no hint, no frame treatment. The firmware draws
    // it in the item-label column on the focus row (measured: x 630 ink, centre y 504, ~24-28 px).
    drawMessageText(p,{kInlineX,kInlineCentreY-24.,1100,48},messageShown_.text,kInlineSize,QColor(235,235,235));
}

// ---- D1 modal box ---------------------------------------------------------------------------------
void View::drawMessageBox(QPainter& p) {
    // The plate is read from the RCO at run time: tex_msgdialog_bg is a flat 12x12 tile, so one pixel is
    // the whole fill and there are no nine-slice margins to recover (audit 2.1). It is the neutral grey
    // wash the audit measured - RGB(54,56,54) at 116/255 over the XMB - and tex_msgdialog_bg2 is the
    // opaque black the text block sits on. The literals are only the fallback for a dev_flash that cannot
    // be read. The box rect itself is a native fit (the firmware's vertices live in unresolved paf calls).
    const QImage& wash=assets_.image("system_plugin","tex_msgdialog_bg");
    const QImage& backing=assets_.image("system_plugin","tex_msgdialog_bg2");
    const QColor washColour=wash.isNull()?QColor(54,56,54,116):wash.pixelColor(0,0);
    const QColor backingColour=backing.isNull()?QColor(0,0,0,255):backing.pixelColor(0,0);
    QString text=messageShown_.text;
    const QString heading=takeHeading(text);
    const double innerWidth=kBoxWidth-2*kBoxTitleX;
    // The box is tall enough for its own text; the hint row stays on the measured baseline below it.
    const int bodyLines=std::max(1,int(wrapMessage(text,innerWidth,kBodySize).size()));
    const double textTop=kBoxTitleY-8.;
    const double textBottom=kBoxBodyTop+(bodyLines-1)*kBodyPitch+kBodySize+14.;
    const double boxHeight=std::max(220.,textBottom+28.);
    const QRectF box(960.-kBoxWidth/2.,kBoxTop,kBoxWidth,boxHeight);
    const double plate=dialogPlateEase_.value,content=dialogEase_.value;
    if(plate>.002) {
        p.save();p.setOpacity(plate);
        p.fillRect(box,washColour);
        // The opaque backing sits under the text block only (title through the last body line), which is
        // what TRACED 0x00344094's second texture slot provides.
        p.fillRect(QRectF(box.left()+kBoxTitleX-12.,box.top()+textTop,innerWidth+24.,textBottom-textTop),backingColour);
        p.restore();
        // Exactly two bars, at the box's own width: tex_msgdialog_bar is 960x1 and centre-bright, so at
        // 1:1 it ramps from nothing at the box edges to full at its centre. Stretching it to 1920 (what
        // this build used to do for every rule) is wrong.
        const QImage& bar=assets_.image("system_plugin","tex_msgdialog_bar");
        if(!bar.isNull()) {
            p.save();p.setOpacity(plate);
            p.drawImage(QRectF(box.left(),box.top(),kBoxWidth,1),bar);
            p.drawImage(QRectF(box.left(),box.bottom()-1.,kBoxWidth,1),bar);
            p.restore();
        }
    }
    if(content<=.002)return;
    p.save();
    // The box's title is left inside the plate. No icon is drawn: system_plugin ships no dialog icon and
    // which of the 100x100 tex_notification_* glyphs a box uses is UNPROVEN (audit 2.1).
    if(!heading.isEmpty())
        drawMessageText(p,{box.left()+kBoxTitleX,box.top()+kBoxTitleY,innerWidth,40},heading,kTitleSize,Qt::white);
    // The body is centred under the title at the same size and pitch as the page's, wrapped to the plate's
    // own inner width so a long message cannot run past the box.
    drawMessageLines(p,text,box.top()+kBoxBodyTop,kBodyPitch,kBodySize,QColor(240,240,240),
        box.left()+kBoxTitleX,innerWidth);
    p.restore();
    // Two buttons: Cross accepts with the firmware's msg_ok, Circle cancels with msg_cancel, on the
    // measured hint baseline. snd_decide for the accept is Shell::activate's box path.
    drawHintRow(p,{{"tex_cross",assets_.text("system_plugin","msg_ok","OK")},
                   {"tex_circle",assets_.text("system_plugin","msg_cancel","Cancel")}},
        box.bottom()+kHintBoxGap,std::min(dialogHintEase_.value,content));
}

// ---- D2 error-code page ---------------------------------------------------------------------------
void View::drawErrorMessagePage(QPainter& p) {
    QString text=messageShown_.text;
    const QString heading=takeHeading(text);
    drawPageChrome(p,heading);
    // The body is vertically centred on its content and settled with the traced 150-unit animation
    // (TRACED 0x00340964, DATA 0x0068C2FC), so the first frame starts a little low and rises into place.
    const double settle=(1.-dialogSettleEase_.value)*24.;
    const auto label=[this](const char* key,const char* fallback){return assets_.text("msgdialog_plugin",key,fallback);};
    double y=kBodyTop+settle;
    drawMessageText(p,{kBodyLeft,y,kBodyWidth,30},label("msg_errorcode_cause_label","Possible Cause"),22,QColor(235,235,235),Qt::AlignHCenter|Qt::AlignTop);
    y+=36;
    y=drawMessageLines(p,text,y,30,22,QColor(235,235,235),kBodyLeft,kBodyWidth);
    y+=18;
    drawMessageText(p,{kBodyLeft,y,kBodyWidth,30},label("msg_errorcode_solution_label","Suggested Actions"),22,QColor(235,235,235),Qt::AlignHCenter|Qt::AlignTop);
    y+=36;
    for(const auto& action:messageShown_.actions) {
        if(action.isEmpty())continue;
        y=drawMessageLines(p,action,y,30,22,QColor(230,230,230),kBodyLeft,kBodyWidth);
        y+=4;
    }
    // An error-code page ends in Enter/Back labels, not the Cross/Circle glyph pair of a confirm box
    // (audit 2.2, TRACED 0x0034002C).
    drawHintRow(p,{{"tex_cross",assets_.text("system_plugin","msg_enter","Enter")},
                   {"tex_circle",assets_.text("system_plugin","msg_back","Back")}},
        kHintInkTop,dialogHintEase_.value);
}

// ---- D5 information page --------------------------------------------------------------------------
void View::drawMessageInfoPage(QPainter& p) {
    // The page keeps the geometry it already had - it matches the firmware convention of D4 - with the
    // measured rule positions and header, and the hint moved to the measured x 1155 / ink y 889.
    QString text=messageShown_.text;
    auto lines=text.split('\n');
    const QString title=lines.isEmpty()?QString{}:lines.takeFirst();
    if(!lines.isEmpty() && lines.first().isEmpty())lines.removeFirst();
    drawPageChrome(p,title);
    double height=0;for(const auto& line:lines)height+=line.isEmpty()?20:40;
    double y=std::max(230.,533-height/2+20);
    for(const auto& line:lines) {
        if(line.isEmpty()){y+=20;continue;}
        if(y>840)break;
        const int split=line.indexOf(": ");
        if(split<0)drawMessageText(p,{160,y-20,1600,40},line,kInfoRowSize,QColor(235,235,235),Qt::AlignHCenter|Qt::AlignVCenter);
        else {
            drawMessageText(p,{125,y-20,800,40},line.left(split),kInfoRowSize,QColor(235,235,235),Qt::AlignRight|Qt::AlignVCenter);
            drawMessageText(p,{961,y-20,900,40},line.mid(split+2),kInfoRowSize,QColor(235,235,235));
        }
        y+=40;
    }
    drawHintRow(p,{{"tex_circle",assets_.text("system_plugin","msg_back","Back")}},kHintInkTop,dialogHintEase_.value);
}

// ---- The button-hint row --------------------------------------------------------------------------
void View::drawHintRow(QPainter& p,const QVector<Hint>& hints,double inkTop,double opacity) {
    if(opacity<=.002 || hints.isEmpty())return;
    QFont font(shell_.fontFamily());font.setPixelSize(kHintSize);
    const QFontMetrics metrics(font);
    double x=kHintX;
    p.save();p.setOpacity(opacity);
    for(const auto& hint:hints) {
        if(!hint.glyph.isEmpty())
            drawImage(p,"system_plugin",hint.glyph,{x,inkTop,kHintGlyphSize,kHintGlyphSize});
        const double labelX=x+(hint.glyph.isEmpty()?0.:kHintGlyphSize+kHintLabelGap);
        drawMessageText(p,{labelX,inkTop-2.,300,26},hint.label,kHintSize,QColor(235,235,235));
        x=labelX+metrics.horizontalAdvance(hint.label)+kHintGroupGap;
    }
    p.restore();
}

void View::drawMessageSurface(QPainter& p) {
    switch(shell_.messageKind()) {
    case MessageKind::Inline:drawInlineMessage(p);break;
    case MessageKind::Page:drawMessagePage(p);break;
    case MessageKind::Box:drawMessageBox(p);break;
    case MessageKind::ErrorCode:drawErrorMessagePage(p);break;
    case MessageKind::Info:drawMessageInfoPage(p);break;
    case MessageKind::None:break;
    }
}
}
