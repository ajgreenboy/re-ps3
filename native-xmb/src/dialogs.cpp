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
// No dim is drawn here. The confirm box and the table page measure the frame behind them at the same
// luminance as a message-free frame, so the 135-alpha full-frame dim this build used to draw was an
// invention and stays gone. Two message-page references (page-network-test-error,
// page-network-connection-settings) do measure the page behind the message at a 0.70 factor; that delta
// belongs to the page composite and is recorded, not implemented, in the DIALOGS-FIRMWARE addendum.
#include "view.h"
#include "assets.h"
#include "shell.h"
#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace xmb {
namespace {
// ---- Measured on the 1920x1080 reference frames ---------------------------------------------------
// Header, and it is one header for both measured page classes: icon ink 109..149 x 154..189 (41 x 36),
// title ink from x 165 with its cap rows at 167..186. page-network-test-error,
// page-network-connection-settings and page-network-status-list all measure that box. The older D4 fit
// (icon 118..154, title from 169) came from a 1262x800 video frame and is superseded by these.
constexpr double kChromeIconX=109.,kChromeIconY=151.,kChromeIconSize=41.;
constexpr double kChromeTitleX=163.;
constexpr double kChromeTitleTop=155.;                                   // 23 px VCenter -> cap ink 167
// Rules, per page class. The modal message page (the D4 shape a message opens over) and the confirm box
// draw one flat white 1 px pair at y 199 and y 879, alpha 177/255 = 0.693: page-network-test-error
// measures 192.6 over 51.7 at y199 and 189.4 over 41.5 at y879; the dialog frame the same pair at
// 199.2 over 73.5 and 193.4 over 54.4. Both rows are uniform across the frame - 189.6 at x0, 191.1 at
// x1919 - so the extent is the full width, not the 960 px centre-bright tex_msgdialog_bar.
// This replaces the D4 fit of 198/872 at 0.218/0.201, which no 1080p message-page frame shows.
constexpr double kPageRuleTop=199.,kPageRuleBottom=879.;
constexpr int kPageRuleAlpha=177;
// The D5 information page has a 1080p frame of its own and it measures the same pair as the message
// page: out/rpcs3-ref-2026-10-28/dialogs/information-page-01.png (Game > Corrupted Data > Triangle >
// Information) puts rule 1 at y 199 as one row of 176.9 luma over a 0.0 background (alpha 0.694) and
// rule 2 at y 879, 182.4 over 17.6 (alpha 0.694). The 198/872 at 56/51 pair this page used to carry was
// the D4 800p window-recording fit; the 1080p frame supersedes it, and it is the frame audit section 8.1
// already cites (that table gives the means 176.8/182.0; the alphas are measured here). The pair stays a
// per-class constant so the two classes cannot drift back into each other, but both values are measured.
constexpr double kInfoRuleTop=199.,kInfoRuleBottom=879.;
constexpr int kInfoRuleAlphaTop=177,kInfoRuleAlphaBottom=177;
// D4 body: the 1080p frames put the first line's ink at y 529..547 - the Internet Connection Test message
// page at 530..546 and the wait page of the same class at 529..547 - on a 32 px line pitch. The 509 this
// constant used to draw is the 1262x800 window recording's row, 21 px high. AlignTop puts the font's
// ascent above the ink (a 24 px face at rect top 504 drew ink at 509), so a rect top of 525 draws the cap
// ink at 530.
constexpr double kBodyLeft=260.,kBodyWidth=1400.,kBodyTop=525.;
constexpr double kBodyPitch=32.;
constexpr int kBodySize=24,kTitleSize=23,kInfoRowSize=22;
// Hint row, one measured pair for every message-surface page: the O glyph's ink is 1155..1174 x
// 897..916 (20 x 20) and its label's ink 1192..1240 x 898..914; the box's X pair is 726..743 x
// 898..915 with its label at 762..814 x 898..914. All four measured frames (both network message pages,
// the table page, the dialog box) put the glyph's ink top at 897 and the label's at 898, so the row is
// anchored on those two ink tops. The 30x30 tex_cross/tex_circle art fills the box it is given
// (measured: a 22 px box drew 22 px of ink), so the 2 px inherited glyph-height error is the box size.
constexpr double kHintX=1155.;                                            // O glyph ink left
constexpr double kHintInkTop=897.;                                        // glyph ink top
constexpr double kHintGlyphSize=20.;                                      // glyph ink box (was 22)
constexpr double kHintLabelInk=1192.;                                     // label ink left
constexpr double kHintLabelBearing=2.;                                    // 21 px face: rect left -> ink left
constexpr double kHintLabelRectTop=4.;                                    // inkTop - this = the 26 px rect top
constexpr double kHintGroupGap=22.;
constexpr int kHintSize=21;
// D3 inline line: left at the level-1 item-label column, ink centre y 504, ink height 20 px (~24-28 px
// font). 628 is the measured ink origin of the firmware's own line; the native label column's ink starts
// at 624, so the line is the reference's, not a re-fit of it.
constexpr double kInlineX=628.,kInlineCentreY=504.;
constexpr int kInlineSize=26;
// The clock face these pages keep. The 1080p message-page frames keep the whole info bar (the plate's
// ramps at y75 and y132, the clock text, and the filled clock face at 1789.5/103.5); view.cpp suppresses
// the bar for a message screen, so this path draws only the face, at the older fit position. Both the
// suppressed plate and the face's own geometry are recorded in the DIALOGS-FIRMWARE addendum.
constexpr double kClockX=1776.,kClockY=93.;
// ---- The depth-2 read-only table page (SETTINGS-FIRMWARE.md 12.2) ---------------------------------
// Reference: out/parity-references/page-network-status-list__original__day__1080p.png, re-measured for
// this pass (same method as the audit: Rec.709 luma, ink runs, a 1 px rule read as a full-width row
// profile). The page is the D4/D5 family's second shape - a title above a rule, a two-column table, a
// lower rule, one Back hint - with one measured difference: it keeps no analog clock (the info bar is
// absent: luma 0 at x 1300/1340/1700/1900 on y 100, and the frame's top-right corner is empty).
// Rules: full width, 1 px, flat (mean luma 140.0 over 75.6 at y 199 and 134.6 over 64.7 at y 879 on
// that frame = white at 0.36). No other D4/D5 frame shows this value - the message page's 193 over 53
// on the same boot is a different page class - so the table page carries its own measured alpha.
constexpr double kTableRuleTop=199.,kTableRuleBottom=879.;
constexpr int kTableRuleAlpha=92;
// Header: icon ink x 109..149, y 154..189 (41 x 36); title ink from x 164, ink rows 167..190. The
// rects are the boxes that the native path's alpha-bounds fit and 23 px VCenter draw produce there
// (the 41 px square puts the globe's 41 x 35 alpha bounds at 109..149 / 154..188, one pixel short of the
// frame's 154..189; rect top 155 puts the title's cap ink at 167).
constexpr double kTableIconX=109.,kTableIconY=151.,kTableIconSize=41.;
constexpr double kTableTitleX=163.,kTableTitleTop=155.;
constexpr int kTableTitleSize=23;
// Table: 13 rows at ink tops 291 -> 771 on an exact 40 px pitch; label ink right-aligned at 939
// (938..940 across all 13), value ink left-aligned at 978..980. The rects sit 3 px right / 1 px left of
// those ink edges because the 22 px face's side bearings land there (measured on the native D5 page:
// a rect's right-aligned text ends 3 px short of the rect, a left-aligned one starts 1 px inside it).
constexpr double kTableLabelRight=942.,kTableValueLeft=978.;
constexpr double kTableFirstInkTop=291.,kTablePitch=40.;
constexpr int kTableRowSize=22;
// At 22 px in a 40 px AlignVCenter rect the cap ink starts 11 px below the rect top (the D5 page's own
// rows: rect top 353 -> ink 364). The first row's rect top is therefore 291 - 11 = 280.
constexpr double kTableRowInkOffset=11.;
// 291 + 14 * 40 = 851 with an 18 px cap ends at 868, inside the 879 rule; a 16th row would cross it.
// The console's table is a fixed 13 rows and the page does not scroll, so a longer host report (two
// adapters) stops at the last row that fits rather than overdrawing the rule.
constexpr int kTableMaxRows=15;
// Back hint: this page's own frame puts the ink at y 897..916 (the D4 page's is 889..909, the
// information page's 897..916); x 1155 and the glyph+label pair are the family's.
constexpr double kTableHintInkTop=897.;
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
constexpr int kBoxChoiceSize=23;      // 23 px: the frame's "Yes" ink is 41 x 19, which a 23 px face gives
// The captured box focuses No: the console's default answer, and the non-destructive one. The native's box
// has one accept path (Shell::activate, see the comment in drawMessageBox), so this is the drawn default.
// The captured box focuses No: the console default answer and the non-destructive one. That is where a
// box starts, not a constant: the live focus comes from Shell::boxChoiceSlot() (0 = Yes, 1 = No).
// Footer: the captured box keeps the modal page's rule pair - the frame's footer rule measures white at
// 0.693 (193.4 over 54.4) and its header rule the same at y199 - plus the Enter/Back hint pairs at ink
// y 897..916. X: glyph ink 726..743 x 898..915 (18 x 18), label ink 762..814 x 898..914; O: glyph ink
// 1155..1174 x 897..916 (20 x 20), label ink 1192..1240 x 898..914. The two glyphs are drawn from two
// different 30x30 textures with different ink margins, so each pair carries its own measured ink box.
constexpr double kBoxFooterInkTop=897.;                                  // label anchor (both pairs)
constexpr double kBoxFooterGlyphX[2]={726.,1155.};
constexpr double kBoxFooterGlyphTop[2]={898.,897.};
constexpr double kBoxFooterGlyphSize[2]={18.,20.};
constexpr double kBoxFooterLabelX[2]={762.,1192.};   // label INK left; the draw subtracts the bearing
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
// The icon a message page shows: the focused row's own, or its parent when the row is the settings pages'
// empty-state marker ("23n"). The page the message belongs to shows its item's icon, as D4's "Search for
// Media Servers" shows the network search icon; the depth-2 status list shows the Network Settings icon
// because the row it belongs to is a settings bullet (measured on its frame: the globe + wrench).
const Entry* pageIconSource(const Shell& shell) {
    const int depth=shell.depth();
    const auto& rows=shell.entriesAt(depth);
    const Entry* source=nullptr;
    const int selection=shell.selectionAt(depth);
    if(selection>=0 && selection<rows.size())source=&rows[selection];
    if(source && source->texture=="23n" && depth>0) {
        const auto& parents=shell.entriesAt(depth-1);
        const int parent=shell.selectionAt(depth-1);
        if(parent>=0 && parent<parents.size())source=&parents[parent];
    }
    return source;
}
// The depth-2 table is the Info family's second shape, and the row that raised the message says which
// one: the three host reports are Action::Information rows whose payload (Entry::value) is host:network,
// host:display or host:audio (Shell::activate builds them in shell.cpp). The message text is the report,
// so the class is the row's own payload, not an inference from the string. Every other Information
// message - System Information, a download's info page, Continue Watching's - keeps the D5 page.
bool hostReportTable(const Shell& shell) {
    const int depth=shell.depth();
    if(depth<=0)return false;
    const auto& rows=shell.entriesAt(depth);
    const int selection=shell.selectionAt(depth);
    if(selection<0 || selection>=rows.size())return false;
    const auto& entry=rows[selection];
    return entry.action==Action::Information &&
        (entry.value=="host:network" || entry.value=="host:display" || entry.value=="host:audio");
}
} // namespace

// ---- D4 chrome: icon + left title, the page class's two flat rules, the analog clock -----------------
void View::drawPageChrome(QPainter& p,const QString& title,double ruleTop,int ruleTopAlpha,double ruleBottom,int ruleBottomAlpha) {
    // The icon is the focused row's own, the same source the information page has always used: the page
    // the message belongs to shows its item's icon, as D4's "Search for Media Servers" shows the network
    // search icon. A message with no heading draws no header at all.
    if(!title.isEmpty()) {
        if(const Entry* source=pageIconSource(shell_))
            drawImage(p,source->container,source->texture,{kChromeIconX,kChromeIconY,kChromeIconSize,kChromeIconSize});
        drawMessageText(p,{kChromeTitleX,kChromeTitleTop,1200,44},title,kTitleSize,Qt::white);
    }
    // The rules are flat alpha rectangles, not tex_msgdialog_bar: the measured references show one
    // uniform colour across the whole width, while that texture is 960 px and centre-bright. The y and
    // the alpha are the page class's own (kPageRule* for a modal message page, kInfoRule* for the D5
    // page); both rows run the full frame width on every measured frame.
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,ruleTopAlpha));p.drawRect(QRectF(0,ruleTop,1920,1));
    p.setBrush(QColor(255,255,255,ruleBottomAlpha));p.drawRect(QRectF(0,ruleBottom,1920,1));
    p.restore();
    // The clock: a modal message page keeps the info bar's clock (the bar is chrome and draws over the
    // wash), so its own face is suppressed. The D5 information page has no bar and keeps the page clock.
    // The busy page has no bar of its own either, but view.cpp's wash/bar gate puts it in the modalPage
    // class (the console's wait frame keeps the bar's plate, clock text and face), so it gets the bar's
    // measured face and must not also get this one.
    if(!modalMessagePage() && !busyPageUp())drawPageClock(p);
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
    drawPageChrome(p,heading,kPageRuleTop,kPageRuleAlpha,kPageRuleBottom,kPageRuleAlpha);
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
    // The rule pair is part of the captured surface and fades with the box's own group: the frame draws the
    // same modal pair as a message page, the header rule at y199 and the footer rule at y879, both white at
    // 177/255, not one footer rule alone.
    if(plateAlpha>.002) {
        p.save();p.setPen(Qt::NoPen);p.setOpacity(plateAlpha);
        p.setBrush(QColor(255,255,255,kPageRuleAlpha));
        p.drawRect(QRectF(0,kPageRuleTop,1920,1));
        p.drawRect(QRectF(0,kPageRuleBottom,1920,1));
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
    // Centred on the frame's measured pair centre, not on the 424 px rect: the frame's "Yes" ink starts at
    // 1182 and "No" ends at 1379, so the pair centre is 1280.5, while the rect's own centre is 1286. The
    // 6.5 px difference is what the earlier "within 6.5 px" note left on the table.
    constexpr double kBoxChoiceCentre=1280.5;
    const double left=kBoxChoiceCentre-pairWidth/2.;
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
            // Same measured pair as drawHintRow: the glyph's ink box is 20 px (a 22 px box drew 22 px of
            // ink, the 2 px inherited height error), the label's ink sits at x - 2 and y = 898 on the
            // 21 px face, so its VCenter rect starts 4 px above the glyph's ink top.
            drawImage(p,"system_plugin",glyphs[i],
                {kBoxFooterGlyphX[i],kBoxFooterGlyphTop[i],kBoxFooterGlyphSize[i],kBoxFooterGlyphSize[i]});
            drawMessageText(p,{kBoxFooterLabelX[i]-kHintLabelBearing,kBoxFooterInkTop-kHintLabelRectTop,300.,26},
                labels[i],kHintSize,QColor(235,235,235));
        }
        p.restore();
    }
    p.restore();
}

// ---- D2 error-code page (UNREACHABLE - no console frame exists) ------------------------------------
// The firmware's numeric error page is traced (TRACED 0x0034002C: mbox_try_button, the scroll body, the
// Enter/Back getters, msgdialog_plugin's msg_errorcode_* strings) but no console frame of it exists on this
// rig: DIALOGS-FIRMWARE 9.3 tried four routes and none produces an 800xxxxx page - the console answers a
// cable condition with the D4 page (page-network-test-error), and a classified failure needs PSN, a PKG or
// a disc. The native's own route is gone as well: Shell::showError() (MessageKind::ErrorCode) has no call
// site, so this path never draws today. It is kept as the traced structure - the two headings, the body,
// the Enter/Back label pair - and its chrome now measures the family's 199/879 at 0.693; its body slot
// moved with the message page's when the 1080p frames put the ink at 529..547. Nothing here is scored.
void View::drawErrorMessagePage(QPainter& p) {
    QString text=messageShown_.text;
    const QString heading=takeHeading(text);
    drawPageChrome(p,heading,kPageRuleTop,kPageRuleAlpha,kPageRuleBottom,kPageRuleAlpha);
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
    // The page keeps the D4 chrome, with the hint on the family's measured anchors (glyph ink x 1155 /
    // y 897, label ink x 1192 / y 898). Its rule pair stays the D4 fit: no 1080p reference exists for the
    // D5 shape, so kInfoRule* is unchanged rather than re-fitted to a frame it does not have.
    QString text=messageShown_.text;
    auto lines=text.split('\n');
    const QString title=lines.isEmpty()?QString{}:lines.takeFirst();
    if(!lines.isEmpty() && lines.first().isEmpty())lines.removeFirst();
    drawPageChrome(p,title,kInfoRuleTop,kInfoRuleAlphaTop,kInfoRuleBottom,kInfoRuleAlphaBottom);
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

// ---- Depth-2 read-only table page (SETTINGS-FIRMWARE.md 12.2) --------------------------------------
void View::drawMessageTablePage(QPainter& p) {
    // The class: title over the measured rule pair, a label/value table on a fixed 40 px grid, one Back
    // hint, no focusable row, no info bar and no dim. The body is the host report the row produced, one
    // row per line; the values differ from the console's by construction and the parity state already
    // annotates that (contentDiffers on the status-rows region). The title is the report's own heading -
    // for the status list that is the firmware's msg_netlist_current, resolved in host::networkStatus.
    QString text=messageShown_.text;
    auto lines=text.split('\n');
    const QString title=lines.isEmpty()?QString{}:lines.takeFirst();
    if(!lines.isEmpty() && lines.first().isEmpty())lines.removeFirst();
    // Header. Same icon source and type as the family; this page's measured anchors and no clock.
    if(!title.isEmpty()) {
        if(const Entry* source=pageIconSource(shell_))
            drawImage(p,source->container,source->texture,{kTableIconX,kTableIconY,kTableIconSize,kTableIconSize});
        drawMessageText(p,{kTableTitleX,kTableTitleTop,1200,44},title,kTableTitleSize,Qt::white);
    }
    p.save();p.setPen(Qt::NoPen);p.setBrush(QColor(255,255,255,kTableRuleAlpha));
    p.drawRect(QRectF(0,kTableRuleTop,1920,1));
    p.drawRect(QRectF(0,kTableRuleBottom,1920,1));
    p.restore();
    // Rows: a fixed grid from the one measured origin, not a centred block. The frames measure one
    // 13-row table; the host report's row count is its own (nine lines for one adapter here), so the
    // grid starts where the measured one does and ends where the report ends. A blank line separates
    // adapters in the report and is not a table row, so it is skipped and the grid stays unbroken.
    int row=0;
    for(const auto& line:lines) {
        if(line.isEmpty())continue;
        if(row>=kTableMaxRows)break;
        const double top=kTableFirstInkTop-kTableRowInkOffset+row*kTablePitch;
        const int split=line.indexOf(": ");
        if(split<0) {
            // A line with no label - the adapter's own name, the audio endpoint's - starts in the value
            // column, where a value starts; the label column carries labels only.
            drawMessageText(p,{kTableValueLeft,top,1920.-kTableValueLeft,qreal(kTablePitch)},line,
                kTableRowSize,QColor(235,235,235));
        } else {
            drawMessageText(p,{140.,top,kTableLabelRight-140.,qreal(kTablePitch)},line.left(split),
                kTableRowSize,QColor(235,235,235),Qt::AlignRight|Qt::AlignVCenter);
            drawMessageText(p,{kTableValueLeft,top,1900.-kTableValueLeft,qreal(kTablePitch)},line.mid(split+2),
                kTableRowSize,QColor(235,235,235));
        }
        ++row;
    }
    drawHintRow(p,{{"tex_circle",assets_.text("system_plugin","msg_back","Back")}},kTableHintInkTop,dialogHintEase_.value);
}

// ---- The busy / wait page (MEASURED 2026-10-28, full frame) ---------------------------------------
// The console's wait page is not a message: page_busy owns the screen while a list loads or a write runs
// (DIALOGS-FIRMWARE 2.8; FIDELITY-BEHAVIOUR N12/X5), and the shell raises it with one flag (Shell::busy(),
// "View draws page_busy from this one flag"). Its only full-frame console reference is the Internet
// Connection Test wait, burst error/ictest2-burst/ (full frame, 57.9 fps, Cross at 908 ms); the settled
// frame f_0003013.png measures the D4 chrome - the header title's cap ink starts at y 168, the row the
// message page measures, and the rule pair is y199/y879 at 0.693 - with `Please wait...` as the body (ink
// 885..1033 x 529..547, centre 959, cap 19 px), the O Back hint at the family's anchors (ink
// 1155..1240 x 897..916), the page behind it dimmed (the ghosted page content reads 28 luma in the band
// y 480..525, max 35) and the info bar kept with its clock. So the surface is the D4 page class with a
// status line where the body goes.
//
// The determinate form - the progress row - has NO console frame: this rig's VFS holds no copyable object,
// so copy/delete progress was never photographed (DIALOGS-FIRMWARE 8.2 item 5 and 8.3). It is drawn from
// the firmware's own Progress art and model (system_plugin tex_default_progress_base / _slider, the 24x20
// pair the music panel's progress_bar_playing_time object draws as paf's default progression art,
// MEDIA-PANELS 11.5), at a rect stated as a FIT here: the art's own 20 px height (1:1, so no vertical
// stretch), 360 px wide and centred on x 960, top 561 - the second line of the measured body pitch
// (529 + 32). The percentage is native text: no RCO string in any container carries a percentage.
//
// The D8 sweep (page_busy's busy_icon, tex_busy 36x36 with the two 8x36 strips) has no recoverable screen
// rect - every page_busy child's pos/size is 0 in the RCO because the plugin lays it out at run time - and
// no console frame shows it on a page (the circle in the wait frame's top-right corner is the info bar's
// analog clock face, at the clock's measured 1774..1805 x 88..119). It is therefore drawn only when a
// caller supplies a phase, at a stated fit, and the default draws none: the measured page has no spinner.
constexpr double kBusyBarX=780.,kBusyBarTop=561.,kBusyBarWidth=360.,kBusyBarHeight=20.;
constexpr double kBusySpinnerX=1500.,kBusySpinnerY=523.;
bool View::busyPageUp() const {
    // The page owns the screen only when nothing else does: a message surface, the media player and the
    // photo viewer each draw their own chrome (the player's own "Loading..." line is view.cpp's).
    if(!shell_.busy())return false;
    if(!shell_.message().isEmpty())return false;
    if(shell_.mediaOpen()||shell_.photoOpen())return false;
    return true;
}
void View::drawBusySurface(QPainter& p,double progress,double spinnerPhase) {
    // Header: the page the wait belongs to. The console's wait page is titled with its own page ("Internet
    // Connection Test"); pageTitle() is the pushed page's title and the focused row's is the fallback at
    // the root. The icon is drawPageChrome's usual source (the focused row, or its parent for a settings
    // bullet's "23n" marker), which is the page header's icon on the measured frame too.
    QString title=shell_.pageTitle();
    if(title.isEmpty()) {
        const auto& rows=shell_.entriesAt(shell_.depth());
        const int selection=shell_.selectionAt(shell_.depth());
        if(selection>=0 && selection<rows.size())title=rows[selection].title;
    }
    drawPageChrome(p,title,kPageRuleTop,kPageRuleAlpha,kPageRuleBottom,kPageRuleAlpha);
    // Body: the shell's own status where it has one, else the firmware's wait string (system_plugin
    // msg_wait, "Please wait..."). It goes on the measured body slot, the same one the message page uses.
    const QString status=shell_.status().isEmpty()
        ?assets_.text("system_plugin","msg_wait","Please wait..."):shell_.status();
    drawMessageLines(p,status,kBodyTop,kBodyPitch,kBodySize,QColor(240,240,240),kBodyLeft,kBodyWidth);
    // The determinate row (RCO-derived, see the fit above). progress < 0 is the measured wait page.
    if(progress>=0.) {
        const double ratio=std::clamp(progress,0.,1.);
        const QRectF bar(kBusyBarX,kBusyBarTop,kBusyBarWidth,kBusyBarHeight);
        const QImage& base=assets_.image("system_plugin","tex_default_progress_base");
        const QImage& fill=assets_.image("system_plugin","tex_default_progress_slider");
        if(!base.isNull())p.drawImage(bar,base);
        if(!fill.isNull() && ratio>0.)
            p.drawImage(QRectF(bar.left(),bar.top(),bar.width()*ratio,bar.height()),fill);
        drawMessageText(p,{bar.right()+16.,kBusyBarTop-2.,140.,kBusyBarHeight+4.},
            QString::number(int(std::lround(ratio*100.)))+QStringLiteral("%"),kHintSize,QColor(235,235,235));
    }
    // The D8 sweep: only when a caller supplies one (the measured page has none; see the block comment).
    if(spinnerPhase>=0.) {
        const double turns=spinnerPhase-std::floor(spinnerPhase);
        p.save();
        p.translate(kBusySpinnerX+18.,kBusySpinnerY+18.);
        p.rotate(turns*360.);
        drawImage(p,"system_plugin","tex_busy",{-18.,-18.,36.,36.});
        p.restore();
    }
    // One Back hint, at the family's measured anchors. The wait page keeps it at full strength for as long
    // as it is up (the 100-unit hint fade is the button group's entrance on a *message*, and no burst of a
    // wait page's entrance exists), so it does not read dialogHintEase_, which is 0 while no message is up.
    drawHintRow(p,{{"tex_circle",assets_.text("system_plugin","msg_back","Back")}},kHintInkTop,1.);
}

// ---- The button-hint row --------------------------------------------------------------------------
void View::drawHintRow(QPainter& p,const QVector<Hint>& hints,double inkTop,double opacity) {
    if(opacity<=.002 || hints.isEmpty())return;
    QFont font(shell_.fontFamily());font.setPixelSize(kHintSize);
    const QFontMetrics metrics(font);
    // The first pair sits on the measured anchors; a second pair (the error page's Enter + Back) flows
    // from it by the label's own advance and the 22 px group gap, because no reference measures a pair
    // layout. The glyph's ink is the 20 px box, the label's ink starts at the anchor minus the face's
    // 2 px left bearing and its VCenter rect 4 px above the glyph's ink top (measured: rect 887 -> ink
    // 892..908 for the old 889 row, and the frames put the cap rows at 898..914).
    double glyphX=kHintX, labelInkX=kHintLabelInk;
    p.save();p.setOpacity(opacity);
    for(const auto& hint:hints) {
        if(!hint.glyph.isEmpty())
            drawImage(p,"system_plugin",hint.glyph,{glyphX,inkTop,kHintGlyphSize,kHintGlyphSize});
        const double labelInk=hint.glyph.isEmpty()?glyphX:labelInkX;
        drawMessageText(p,{labelInk-kHintLabelBearing,inkTop-kHintLabelRectTop,300.,26},hint.label,
            kHintSize,QColor(235,235,235));
        glyphX=labelInk+metrics.horizontalAdvance(hint.label)+kHintGroupGap;
        labelInkX=glyphX+37.;                 // 1155 -> 1192 measured; the X pair is 726 -> 762 in the box
    }
    p.restore();
}

void View::drawMessageSurface(QPainter& p) {
    switch(shell_.messageKind()) {
    case MessageKind::Inline:drawInlineMessage(p);break;
    case MessageKind::Page:drawMessagePage(p);break;
    case MessageKind::Box:drawMessageBox(p);break;
    case MessageKind::ErrorCode:drawErrorMessagePage(p);break;
    case MessageKind::Info:
        // The Info family has two measured shapes: the D5 information page (its own columns) and the
        // depth-2 read-only table (SETTINGS 12.2). The host-report rows are the table; every other
        // Information message keeps the D5 page byte for byte.
        if(hostReportTable(shell_))drawMessageTablePage(p);else drawMessageInfoPage(p);
        break;
    case MessageKind::None:break;
    }
}
}
