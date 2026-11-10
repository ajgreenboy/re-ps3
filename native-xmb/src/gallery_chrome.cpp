#include "view.h"
#include "gallery_view.h"
#include <QDir>
#include <QFont>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// Photo Gallery, the View side (audit/PHOTO-GALLERY-DESIGN.md steps G6 and G7): opening and closing the area, routing keys
// and frames to it, the hand-off to the existing photo viewer, and the QPainter chrome drawn over the GL scene.
// Labels: TRACED = a string, image or constant read from hknw_plugin (the RCO or parameters.txt) at run time;
// INFERRED = a layout or behaviour chosen from names and the help pictures, not read from the module's code.
namespace xmb {
namespace {
// The firmware's help strings carry a button-glyph placeholder ("%E: Enter", ": Menu"): the glyph is drawn as an image here.
QString plainHelp(QString text) {
    text.remove(QRegularExpression(QStringLiteral("^%[A-Z]:?\\s*")));
    if (text.startsWith(QLatin1Char(':'))) text.remove(0, 1);
    return text.trimmed();
}
QString capitalised(const QString& id) { return id.isEmpty() ? id : id.left(1).toUpper() + id.mid(1); }
}

QString View::galleryText(const char* id, const QString& fallback) {
    return assets_.text(QStringLiteral("hknw_plugin"), QLatin1String(id), fallback);
}

bool View::galleryOpen() const { return gallery_ != nullptr; }
bool View::galleryDrawing() const { return gallery_ && !shell_.photoOpen() && shell_.message().isEmpty(); }
bool View::galleryOwnsInput() const { return galleryDrawing(); }

void View::openGallery() {
    if (gallery_) return;
    auto state = std::make_unique<GalleryState>();
    GalleryState& g = *state;
    QString error;
    g.params = GalleryParams::load(assets_.root(), &error);
    QString problem = g.params.loaded() ? QString() : error;
    if (problem.isEmpty()) problem = g.params.require(GalleryController::paramNames() + GalleryScene::paramNames() + galleryLayoutParamNames());
    GalleryThumbs::Config thumbConfig;
    if (problem.isEmpty() && !GalleryThumbs::policyFromParams(g.params, &thumbConfig.policy, &problem)) {}
    g.roots = galleryRoots_.isEmpty() ? shell_.photoLibraryRoots() : galleryRoots_;
    if (problem.isEmpty() && g.roots.isEmpty()) problem = tr("No photo folder is set. Choose one with Photo > Choose Media Folder.");
    if (!problem.isEmpty()) {
        qWarning().noquote() << "gallery: cannot open:" << problem;
        shell_.galleryMessage(galleryText("msg_error_no_image", tr("There are no images.")));
        return;
    }
    thumbConfig.directory = QDir(shell_.dataDirectory()).filePath(QStringLiteral("cache/gallery/thumbs"));
    g.thumbs = std::make_unique<GalleryThumbs>(thumbConfig);
    g.indexer = std::make_unique<GalleryIndexer>();
    connect(g.indexer.get(), &GalleryIndexer::finished, this, [this](quint64 request, GalleryScan scan) { galleryOnIndexed(request, scan); });
    GalleryScanOptions options;
    options.roots = g.roots;
    options.limit = std::min(options.limit, std::max(1, g.params.integer("BOOT_CONTENTS_LIMIT")));
    options.indexCachePath = QDir(shell_.dataDirectory()).filePath(QStringLiteral("cache/gallery/index.json"));
    g.request = g.indexer->start(options);
    gallery_ = std::move(state);
    qInfo().noquote() << "gallery: opened; indexing" << g.roots.join(QLatin1Char(';'));
    update();
}

void View::galleryOnIndexed(quint64 request, const GalleryScan& scan) {
    if (!gallery_ || request != gallery_->request) return;
    GalleryState& g = *gallery_;
    g.photoCount = int(scan.photos.size());
    g.truncated = scan.truncated;
    qInfo().noquote() << QStringLiteral("gallery: indexed %1 photos in %2 folders (%3 EXIF dates, %4 file times, %5 from the cache)")
        .arg(scan.photos.size()).arg(scan.directories).arg(scan.exifRead).arg(scan.fileTimeUsed).arg(scan.reusedFromIndex);
    if (scan.photos.isEmpty()) {
        // INFERRED from the design note: an empty library says msg_error_no_image and returns to the row.
        closeGallery();
        shell_.galleryMessage(galleryText("msg_error_no_image", tr("There are no images.")));
        return;
    }
    g.controller = std::make_unique<GalleryController>();
    QString error;
    if (!g.controller->setup(g.params, scan.photos, g.sort, width() > 0 && height() > 0 ? double(width()) / height() : 16.0 / 9.0, &error)) {
        g.stage = GalleryState::Stage::Failed;
        g.error = error;
        qWarning().noquote() << "gallery: layout failed:" << error;
        return;
    }
    g.controller->setPlaylists(shell_.photoPlaylists());
    g.stage = GalleryState::Stage::Ready;
    qInfo().noquote() << QStringLiteral("gallery: %1 piles on %2 row(s)").arg(g.controller->layout().piles.size()).arg(g.controller->layout().rows);
    update();
}

void View::closeGallery() {
    if (!gallery_) return;
    qInfo() << "gallery: closed";
    if (gallery_->thumbs) gallery_->thumbs->cancelPending();
    if (gallery_->indexer) gallery_->indexer->cancel();
    makeCurrent();
    gallery_.reset();   // the scene's GL textures go with the context current
    doneCurrent();
    ++uiRevision_;
    shell_.noteActivity();
    update();
}

void View::galleryResult(const GalleryController::Result& result) {
    if (!gallery_) return;
    switch (result.act) {
    case GalleryController::Act::OpenPhoto:
    case GalleryController::Act::Slideshow:
        shell_.openGalleryPhotos(result.paths, result.index, result.act == GalleryController::Act::Slideshow);
        gallery_->viewerWasOpen = shell_.photoOpen();
        break;
    case GalleryController::Act::Quit:
        closeGallery();
        break;
    case GalleryController::Act::None:
        break;
    }
}

void View::galleryPress(int key) {
    if (!gallery_) return;
    GalleryState& g = *gallery_;
    if (g.stage != GalleryState::Stage::Ready || !g.controller) {
        // Indexing or failed: Circle / Escape leaves.
        if (key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Return || key == Qt::Key_Enter) closeGallery();
        return;
    }
    using K = GalleryController::Key;
    K mapped;
    switch (key) {
    case Qt::Key_Left: mapped = K::Left; break;
    case Qt::Key_Right: mapped = K::Right; break;
    case Qt::Key_Up: mapped = K::Up; break;
    case Qt::Key_Down: mapped = K::Down; break;
    case Qt::Key_Return: case Qt::Key_Enter: mapped = K::Cross; break;
    case Qt::Key_Escape: case Qt::Key_Backspace: mapped = K::Circle; break;
    case Qt::Key_T: mapped = K::Triangle; break;
    case Qt::Key_S: case Qt::Key_Space: mapped = K::Start; break;
    case Qt::Key_Plus: case Qt::Key_Equal: case Qt::Key_PageUp: mapped = K::ZoomIn; break;
    case Qt::Key_Minus: case Qt::Key_PageDown: mapped = K::ZoomOut; break;
    default: return;
    }
    galleryResult(g.controller->press(mapped));
    update();
}

// ------------------------------------------------------------------ frame

void View::paintGallery(double dt, double seconds) {
    GalleryState& g = *gallery_;
    const QSize device(int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
    if (g.viewerWasOpen) {
        // The viewer closed (it is not open on this path): the grid cursor follows the file it ended on.
        g.viewerWasOpen = false;
        if (g.controller && g.controller->mode() == GalleryController::Mode::Grid) g.controller->setGridFocus(shell_.galleryIndex());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, device.width(), device.height());
    glDisable(GL_SCISSOR_TEST);
    if (g.stage == GalleryState::Stage::Ready && g.controller) {
        g.controller->tick(dt);
        if (!g.sceneInit) {
            g.sceneInit = true;
            g.scene.attach(g.thumbs.get(), assets_.fontFamily());
            g.sceneOk = g.scene.init(*this, assets_);
        }
        if (g.sceneOk) g.scene.render(*this, device.width(), device.height(), defaultFramebufferObject(), *g.controller, seconds);
        else { glClearColor(.55f, .55f, .57f, 1.f); glClear(GL_COLOR_BUFFER_BIT); }
    } else {
        glClearColor(.22f, .23f, .26f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    const double scale = std::min(device.width() / 1920., device.height() / 1080.);
    textRenderer_.beginFrame(*this, device, scale, 1);
    QOpenGLPaintDevice paintDevice(device);
    QPainter p(&paintDevice);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.translate((device.width() - 1920 * scale) / 2, (device.height() - 1080 * scale) / 2);
    p.scale(scale, scale);
    drawGalleryChrome(p);
}

void View::drawGalleryChrome(QPainter& p) {
    GalleryState& g = *gallery_;
    const auto text = [&](const QRectF& r, const QString& value, int size, const QColor& colour, int flags = Qt::AlignLeft | Qt::AlignVCenter) {
        QFont font(shell_.fontFamily());
        font.setPixelSize(size);
        if (textRenderer_.ready()) { textRenderer_.draw(*this, p, r, value, font, colour, flags); return; }
        font.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(font);
        p.setPen(colour);
        p.drawText(r, flags, value);
    };
    const auto image = [&](const char* name, const QRectF& rect, qreal opacity = 1.0) {
        const QImage& picture = assets_.image(QStringLiteral("hknw_plugin"), QLatin1String(name));
        if (picture.isNull()) return false;
        p.save();
        p.setOpacity(opacity);
        p.drawImage(rect, picture);
        p.restore();
        return true;
    };
    const QRectF screen(0, 0, 1920, 1080);

    if (g.stage != GalleryState::Stage::Ready || !g.controller) {
        const QString message = g.stage == GalleryState::Stage::Failed ? g.error : galleryText("msg_wait", tr("Please wait..."));
        text(QRectF(160, 460, 1600, 160), message, 38, Qt::white, Qt::AlignCenter);
        return;
    }
    const GalleryController& c = *g.controller;
    const bool playlists = c.mode() == GalleryController::Mode::Playlists;
    const bool grid = c.enteredPile() >= 0;

    // ---- top band: the area and the focused pile ----
    {
        QLinearGradient band(0, 0, 0, 130);
        band.setColorAt(0, QColor(10, 12, 16, 170));
        band.setColorAt(1, QColor(10, 12, 16, 0));
        p.fillRect(QRectF(0, 0, 1920, 130), band);
        text(QRectF(90, 28, 900, 56), galleryText("msg_photo_gallery", tr("Photo Gallery")), 40, Qt::white);
        text(QRectF(90, 80, 900, 36), playlists ? tr("Playlists") : galleryText("msg_library_area", tr("Library Area")), 24, QColor(255, 255, 255, 200));
        const int pile = grid ? c.enteredPile() : c.focusPile();
        if (!playlists && pile >= 0 && pile < c.layout().piles.size()) {
            const GalleryPile& info = c.layout().piles[pile];
            QString line = info.label + QStringLiteral("   ") + tr("%1 photos").arg(info.photos.size());
            if (grid && c.gridFocus() >= 0 && c.gridFocus() < c.gridCells().size())
                line += QStringLiteral("   %1 / %2").arg(c.gridFocus() + 1).arg(c.gridCells().size());
            text(QRectF(960, 36, 870, 56), line, 34, Qt::white, Qt::AlignRight | Qt::AlignVCenter);
            QString order = c.sort() == GallerySort::Newest ? galleryText("msg_newer_order", tr("Newest First"))
                : c.sort() == GallerySort::Oldest ? galleryText("msg_older_order", tr("Oldest First")) : galleryText("msg_random", tr("Random"));
            text(QRectF(960, 84, 870, 36), order, 24, QColor(255, 255, 255, 200), Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    // ---- bottom guide panel ----
    if (c.overlay() != GalleryController::Overlay::Help) {
        QLinearGradient band(0, 1080 - 150, 0, 1080);
        band.setColorAt(0, QColor(10, 12, 16, 0));
        band.setColorAt(1, QColor(10, 12, 16, 190));
        p.fillRect(QRectF(0, 1080 - 150, 1920, 150), band);
        struct Hint { const char* glyph; QString label; };
        QVector<Hint> hints{{"tex_guidepanel_Cross", plainHelp(galleryText("msg_enter", tr("Enter")))},
            {"tex_guidepanel_Circle", plainHelp(galleryText("msg_back", tr("Back")))}};
        if (!playlists) {
            hints.push_back({"tex_guidepanel_Triangle", plainHelp(galleryText("msg_menu", tr("Menu")))});
            hints.push_back({"tex_guidepanel_Start", plainHelp(galleryText("msg_slideshow", tr("Slideshow")))});
        }
        double x = 120;
        for (const Hint& hint : hints) {
            image(hint.glyph, QRectF(x, 1080 - 96, 48, 48));
            text(QRectF(x + 58, 1080 - 96, 300, 48), hint.label, 30, Qt::white);
            x += 58 + 30 + hint.label.size() * 17 + 40;
        }
        if (!playlists) text(QRectF(1100, 1080 - 96, 720, 48), plainHelp(galleryText("msg_help_rstick_zoom", tr("Right Stick: Zoom In/Zoom Out"))), 26,
             QColor(255, 255, 255, 190), Qt::AlignRight | Qt::AlignVCenter);
    }

    if (playlists) {
        p.fillRect(QRectF(0, 130, 1920, 800), QColor(8, 10, 14, 195));
        text(QRectF(160, 150, 1600, 64), tr("Playlists"), 42, Qt::white);
        const auto& lists = c.playlists();
        if (lists.isEmpty()) text(QRectF(180, 240, 1500, 60), tr("No playlists"), 30, QColor(255, 255, 255, 190));
        const int visible = 8;
        const int first = std::max(0, c.playlistFocus() - visible + 1);
        for (int i = first; i < lists.size() && i < first + visible; ++i) {
            const QRectF row(160, 230 + (i - first) * 80, 1600, 70);
            if (i == c.playlistFocus()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 65));
                p.drawRoundedRect(row, 8, 8);
            }
            text(row.adjusted(24, 0, -300, 0), lists[i].name, 32, Qt::white);
            text(row.adjusted(1100, 0, -24, 0), tr("%1 photos").arg(lists[i].items.size()), 28, QColor(255, 255, 255, 200), Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    // ---- boot: the title logo, then the first help card ----
    if (c.logoAlpha() > 0.01) {
        const double a = c.logoAlpha();
        const QImage& logo = assets_.image(QStringLiteral("hknw_plugin"), QStringLiteral("tex_logo_boot"));
        if (!logo.isNull()) {
            p.save();
            p.setOpacity(a);
            p.drawImage(QRectF(960 - logo.width() / 2.0, 440 - logo.height() / 2.0, logo.width(), logo.height()), logo);
            p.restore();
        }
    }
    if (!playlists && c.helpCardShown() && c.overlay() == GalleryController::Overlay::None) {
        const QRectF card(1920 - 760, 150, 680, 150);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 22, 26, 215));
        p.drawRoundedRect(card, 14, 14);
        text(card.adjusted(30, 14, -20, -80), plainHelp(galleryText("msg_help_lstick_move", tr("Left Stick: Move"))), 30, Qt::white);
        text(card.adjusted(30, 70, -20, -14), plainHelp(galleryText("msg_help_rstick_zoom", tr("Right Stick: Zoom In/Zoom Out"))), 30, Qt::white);
    }

    // ---- menu strip (Triangle) ----
    if (c.overlay() == GalleryController::Overlay::Menu) {
        const QRectF strip(360, 740, 1200, 120);
        image("tex_menuItems", strip);
        const auto items = c.menu();
        const double left = strip.left() + 22, slot = (1156.0) / items.size();
        for (int i = 0; i < items.size(); ++i) {
            const QRectF cell(left + i * slot, strip.top() + 16, slot, 88);
            if (!items[i].enabled) { p.fillRect(cell, QColor(24, 26, 30, 165)); continue; }
            if (i == c.menuFocus()) {
                p.save();
                p.setPen(QPen(QColor(255, 255, 255, 230), 3));
                p.setBrush(QColor(255, 255, 255, 50));
                p.drawRoundedRect(cell.adjusted(4, 2, -4, -2), 8, 8);
                p.restore();
            }
        }
        static const QHash<QString, const char*> keys{{"tools", "msg_tool"}, {"help", "msg_help"}, {"info", "msg_info"}, {"delete", "msg_del"},
            {"rotate", "msg_rotate"}, {"move", "msg_move"}, {"edit", "msg_edit"}, {"group", "msg_group"}, {"sort", "msg_display_order"},
            {"slideshow", "msg_slideshow"}, {"add_to_playlist", "msg_add_to_playlist"}};
        const QString id = items[c.menuFocus()].id;
        // The firmware strip has no caption for Quit (its string is not in hknw_plugin.rco): the native word.
        const QString label = id == QStringLiteral("playlists") ? tr("Playlists") : keys.contains(id) ? galleryText(keys.value(id), capitalised(id)) : tr("Quit");
        text(QRectF(strip.left(), strip.bottom() + 8, strip.width(), 44), label, 32, Qt::white, Qt::AlignHCenter | Qt::AlignVCenter);
    }

    // ---- help page (Menu > Help): the controller picture with the keys this port binds ----
    if (c.overlay() == GalleryController::Overlay::Help) {
        p.fillRect(screen, QColor(8, 10, 14, 215));
        text(QRectF(120, 40, 1680, 70), galleryText("msg_help", tr("Help")), 44, Qt::white);
        const QImage& pad = assets_.image(QStringLiteral("hknw_plugin"), QStringLiteral("tex_help_controller"));
        const double k = 1.7, ox = 700, oy = 190;
        if (!pad.isNull()) p.drawImage(QRectF(ox, oy, pad.width() * k, pad.height() * k), pad);
        // Labels at the picture's line ends (read off tex_help_controller.png by eye: INFERRED anchors).
        const auto label = [&](double px, double py, const QString& value, int flags) {
            const double x = ox + px * k, y = oy + py * k;
            QRectF r = (flags & Qt::AlignRight) ? QRectF(x - 520, y - 22, 500, 44) : (flags & Qt::AlignHCenter) ? QRectF(x - 260, y, 520, 44) : QRectF(x + 20, y - 22, 520, 44);
            text(r, value, 28, Qt::white, flags | Qt::AlignVCenter);
        };
        label(133, 8, tr("Zoom Out"), Qt::AlignRight);
        label(404, 8, tr("Zoom In"), Qt::AlignLeft);
        label(540, 88, plainHelp(galleryText("msg_menu", tr("Menu"))), Qt::AlignLeft);
        label(540, 138, plainHelp(galleryText("msg_back", tr("Back"))), Qt::AlignLeft);
        label(540, 192, plainHelp(galleryText("msg_enter", tr("Enter"))), Qt::AlignLeft);
        label(300, 130, galleryText("msg_slideshow", tr("Slideshow")), Qt::AlignHCenter);
        label(205, 372, plainHelp(galleryText("msg_help_lstick_move", tr("Left Stick: Move"))), Qt::AlignHCenter);
        label(345, 418, plainHelp(galleryText("msg_help_rstick_zoom", tr("Right Stick: Zoom In/Zoom Out"))), Qt::AlignHCenter);
        const QImage& library = assets_.image(QStringLiteral("hknw_plugin"), QStringLiteral("tex_help_pic_library"));
        if (!library.isNull()) p.drawImage(QRectF(110, 250, library.width() * 1.35, library.height() * 1.35), library);
        // Keyboard bindings are this port's, not the firmware's.
        text(QRectF(120, 930, 1680, 44), tr("Keyboard: arrows move, Enter select, Esc back, T menu, S slideshow, + and - zoom."), 26, QColor(255, 255, 255, 190), Qt::AlignHCenter | Qt::AlignVCenter);
    }

    // ---- quit ask (msg_quit2_ask) ----
    if (c.overlay() == GalleryController::Overlay::QuitAsk) {
        p.fillRect(screen, QColor(0, 0, 0, 110));
        const QRectF plate(560, 360, 800, 300);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(22, 24, 28, 238));
        p.drawRoundedRect(plate, 16, 16);
        text(QRectF(plate.left() + 40, plate.top() + 36, plate.width() - 80, 100), galleryText("msg_quit2_ask", tr("Do you want to quit?")), 36, Qt::white, Qt::AlignCenter);
        const QString answers[2] = {galleryText("msg_yes", tr("Yes")), galleryText("msg_no", tr("No"))};
        for (int i = 0; i < 2; ++i) {
            const QRectF button(plate.left() + 130 + i * 290, plate.bottom() - 110, 250, 64);
            if (c.quitChoice() == i) {
                p.setBrush(QColor(255, 255, 255, 80));
                p.drawRoundedRect(button, 8, 8);
            }
            text(button, answers[i], 32, Qt::white, Qt::AlignCenter);
        }
    }
}
}
