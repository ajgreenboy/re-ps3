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
// D3 inline line: left at the level-1 item-label column, ink centre y 504, ink height 20 px (~24-28 px
// font). 628 is the measured ink origin of the firmware's own line; the native label column's ink starts
// at 624, so the line is the reference's, not a re-fit of it.
constexpr double kInlineX=628.,kInlineCentreY=504.;
constexpr int kInlineSize=26;
// D4 keeps the analog clock face while the info bar's plate and date are hidden: centre ~(1776,93).
constexpr double kClockX=1776.,kClockY=93.;
// ---- The confirm box: measured on the 2026-10-28 firmware frame -----------------------------------
// Reference: out/parity-references/dialog__original__day__1080p.png (RPCS3 4.93, 1920x1080, one day pin;
// the console's delete confirmation, body "The installed game will be deleted. / Are you sure you want to
// continue?", Yes/No with No focused). Until this frame existed the whole box was a native fit (audit
// section 5, 7.6); every value below is now a single-pixel measurement of that frame, with the measuring
// method named. The frame's plate is a light wash, not the dark tile the RCO suggested: see the audit's
// 2026-10-28 section for the arithmetic.
// Plate: edge steps at x 480 (87.7 -> 101.3) and x 798 (96.2 -> 81.4), y 452 (82.8 -> 93.4) and y 627
// (80.5 -> 69.0) on a 2 px antialiased edge, so the core rect is x 480..798 x y 452..627 (319 x 176).
// Fill: white at 43/255. The measured lightening over the background fits (255 - bg) * 0.17 in every
// channel on both the plate's left and right edge (bg 100/76/43 -> +24/+28/+35; bg 85/61/28 -> +29/+33/+39),
// i.e. a 0.17 white wash. The 12x12 tex_msgdialog_bg tile (flat RGB 54/56/54, alpha 116) mixed normally
// would darken the plate instead; the frame does not show it here.
constexpr double kBoxPlateX=480.,kBoxPlateY=452.,kBoxPlateWidth=319.,kBoxPlateHeight=176.;
constexpr int kBoxPlateAlpha=43;
// The plate's content is the item's own icon at 56x56, centred on it (ink x 612..667, y 512..567, centre
// 639.5/539.5 = the plate's own centre). The frame's glyph is the console's corrupted-data icon; the native
// draws the row the message belongs to, the same source the page header uses.
constexpr double kBoxIconSize=56.;
// Body: ink starts at x 1074 and at y 476, line 2 at y 510 (pitch 34), cap height 23 px at the page's own
// 24 px. The frame's wrap (395 px then 411 px, with " Are" not fitting after "deleted.") bounds the text
// column to 411..440 px; 424 is inside that bound.
constexpr double kBoxTextX=1074.,kBoxTextTop=471.,kBoxTextWidth=424.;
constexpr double kBoxPitch=34.;
constexpr double kBoxChoiceGap=120.;
// Choices: "Yes" ink 1183..1221 x 584..601, "No" ink 1341..1376 x 582..602 (the focus halo adds the top
// and bottom rows). 120 px apart, and the pair is centred on the body column (its centre is 1279.5, the
// 1074..1484 text block's own centre), not on the screen.
constexpr double kBoxChoiceTop=579.;
constexpr int kBoxChoiceSize=22;
// The captured box focuses No: the console's default answer, and the non-destructive one. The native's box
// has one accept path (Shell::activate, see the comment in drawMessageBox), so this is the drawn default.
// The captured box focuses No: the console default answer and the non-destructive one. That is where a
// box starts, not a constant: the live focus comes from Shell::boxChoiceSlot() (0 = Yes, 1 = No).
// Footer: the captured box keeps a footer band under the box - one flat full-width 1 px rule at y 879
// (RGB 200/192/183 over an RGB 78/52/22 background = white at 177/255, uniform from x 20 to x 1900, so not
// the centre-bright 960 px tex_msgdialog_bar) and the Enter/Back hints at ink y 897..916.
constexpr double kBoxFooterRuleY=879.;
constexpr int kBoxFooterRuleAlpha=177;
constexpr double kBoxFooterInkTop=897.;
// Hint x: the X glyph's ink starts at 726 and its label at 762; the O glyph's ink at 1155 and its label at
// 1192 (the D4 row's own anchor), so the pair is drawn at these measured positions rather than stacked
// from kHintX.
constexpr double kBoxFooterGlyphX[2]={726.,1155.};
constexpr double kBoxFooterLabelX[2]={762.,1192.};
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

// ---- D1 confirm box (MEASURED 2026-10-28) ---------------------------------------------------------
void View::drawMessageBox(QPainter& p) {
    // The frame's box is a left icon plate plus a right text column, with the answers under the text and a
    // footer band; there is no heading, no second bar and no dim. The old shape (a 960 px plate at y 250
    // with the heading and a centred body inside it, two tex_msgdialog_bar rows and a Cross/Circle hint row
    // under it) was the native fit this frame replaces.
    QString text=messageShown_.text;
    takeHeading(text);                       // the captured box has no heading line: the icon carries the item
    const QRectF plate(kBoxPlateX,kBoxPlateY,kBoxPlateWidth,kBoxPlateHeight);
    const double plateAlpha=dialogPlateEase_.value, content=dialogEase_.value;
    if(plateAlpha>.002) {
        // Flat white wash at the measured alpha. No nine-slice: the frame shows one uniform fill.
        p.save();p.setPen(Qt::NoPen);p.setOpacity(plateAlpha);
        p.fillRect(plate,QColor(255,255,255,kBoxPlateAlpha));
        p.restore();
        // The plate carries the item the message belongs to, 56x56 and centred. Same source as the page
        // header icon: the focused row, or its parent when the row is the empty-state marker ("23n").
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
        if(source) {
            const QPointF centre=plate.center();
            drawImage(p,source->container,source->texture,
                {centre.x()-kBoxIconSize/2.,centre.y()-kBoxIconSize/2.,kBoxIconSize,kBoxIconSize},plateAlpha);
        }
    }
    // The footer band is part of the captured surface and fades with the box's own group.
    if(plateAlpha>.002) {
        p.save();p.setPen(Qt::NoPen);p.setOpacity(plateAlpha);
        p.setBrush(QColor(255,255,255,kBoxFooterRuleAlpha));
        p.drawRect(QRectF(0,kBoxFooterRuleY,1920,1));
        p.restore();
    }
    if(content<=.002)return;
    p.save();
    // Body: left-aligned at the measured column, one measured pitch. No heading, no centring.
    const auto lines=wrapMessage(text,kBoxTextWidth,kBodySize);
    double y=kBoxTextTop;
    for(const auto& line:lines) {
        if(!line.isEmpty())
            drawMessageText(p,{kBoxTextX,y,kBoxTextWidth,qreal(kBodySize)+10.},line,kBodySize,
                Qt::white,Qt::AlignLeft|Qt::AlignTop);
        y+=kBoxPitch;
    }
    // Choices: the firmware's own msg_yes / msg_no as text, not glyph hints, 120 px apart and centred on
    // the body column. The focused one carries the XMB's soft white halo; the other does not (measured:
    // Yes has no halo, No's halo adds ~3 px above and below its ink).
    // The focus is Shell's live choice, not the console's default: boxChoiceSlot() is 0 for Yes (the
    // frame's left answer), 1 for No (its right one) and -1 when the box has no answer row at all, so a
    // single-action Box draws no halo.
    QFont choiceFont(shell_.fontFamily());choiceFont.setPixelSize(kBoxChoiceSize);
    const auto advance=[&](const QString& value){
        return textRenderer_.ready()?textRenderer_.measure(choiceFont,value):QFontMetrics(choiceFont).horizontalAdvance(value);
    };
    const QString choices[2]={assets_.text("system_plugin","msg_yes","Yes"),
                              assets_.text("system_plugin","msg_no","No")};
    const double pairWidth=advance(choices[0])+kBoxChoiceGap+advance(choices[1]);
    // Centred on the text column, not on the body's longest line: the frame's pair centre is 1279.5, which
    // is both the centre of its 1074..1484 ink block and (within 6.5 px) the centre of this 424 px column.
    const double left=kBoxTextX+(kBoxTextWidth-pairWidth)/2.;
    const double centres[2]={left+advance(choices[0])/2.,
                             left+advance(choices[0])+kBoxChoiceGap+advance(choices[1])/2.};
    const int focused=shell_.boxChoiceSlot();
    for(int i=0;i<2;++i) {
        const QRectF rect(centres[i]-150.,kBoxChoiceTop,300.,30.);
        if(i==focused) {
            if(textRenderer_.ready())
                textRenderer_.draw(*this,p,rect,choices[i],choiceFont,Qt::white,
                    Qt::AlignHCenter|Qt::AlignTop,1.,false,false);
        }
        drawMessageText(p,rect,choices[i],kBoxChoiceSize,Qt::white,Qt::AlignHCenter|Qt::AlignTop);
    }
    // Footer hints: the firmware's glyph + label pairs at the measured anchors (X Enter, O Back). A box has
    // no Cross/Circle OK/Cancel pair: the frame's answers are the Yes/No text above, and Enter/Back are the
    // verbs the footer advertises. They fade with the hint group (100 units, the RCO's own anim_*_show/hide).
    const QString glyphs[2]={QStringLiteral("tex_cross"),QStringLiteral("tex_circle")};
    const QString labels[2]={assets_.text("system_plugin","msg_enter","Enter"),
                             assets_.text("system_plugin","msg_back","Back")};
    {
        p.save();p.setOpacity(dialogHintEase_.value);
        for(int i=0;i<2;++i) {
            drawImage(p,"system_plugin",glyphs[i],{kBoxFooterGlyphX[i],kBoxFooterInkTop,22.,22.});
            drawMessageText(p,{kBoxFooterLabelX[i],kBoxFooterInkTop-2.,300.,26},labels[i],kHintSize,
                QColor(235,235,235));
        }
        p.restore();
    }
    p.restore();
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
