#include "gallery_controller.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace xmb {
namespace {
constexpr double Pi = 3.14159265358979323846;
// The scene's field of view. Not a firmware value (parameters.txt has none); the layout (GalleryLayoutOptions) uses
// the same number to turn PARAM_MAX_LINE_LENGTH into world units.
constexpr double FovYDegrees = 45.0;
// Port choices, not firmware values: the module boots with a rigid-body drop and a 20 s camera sweep that this port
// replaces with a short fall of the shown photos and the title logo.
constexpr double BootSeconds = 2.0;
constexpr double BootFallSeconds = 0.9;
constexpr double BootFallSpread = 0.8;
constexpr double GridMargin = 1.2;
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double expApproach(double k, double dt) { return 1.0 - std::exp(-k * dt); }
// Deterministic 0..1 from an integer (drop delays).
double hash01(quint32 v) {
    v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
    return double(v & 0xFFFFFF) / double(0x1000000);
}
}

void GalleryCamera::eye(double* x, double* y, double* z) const {
    const double a = angleDeg * Pi / 180.0;
    *x = targetX;
    *y = distance * std::sin(a);
    *z = targetZ + distance * std::cos(a);
}

QStringList GalleryController::paramNames() {
    return {"CAMERA_ANGLE_CHAOS", "CAMERA_ANGLE_CHAOS_NEAR", "CAMERA_ANGLE_GRID", "CAMERA_NEAR_LIMIT", "CAMERA_FAR_LIMIT",
            "CAMERA_NEAR_LIMIT_GRID", "CAMERA_FAR_LIMIT_GRID", "CAMERA_SPEED_ZOOM", "QUICK_ZOOM_TIME", "QUICK_ZOOM_OUT_TIME",
            "FOCUS_MOVE_SPEED", "FOCUS_MOVE_BRAKE", "FOCUS_GRAVITY", "GROUP_FOCUS_RECT_SPACE", "GROUP_FOCUS_FLOAT_SPEED_UP",
            "GROUP_FOCUS_FLOAT_SPEED_DOWN", "BOOT_CAMERA_INIT_DIST", "BOOT_CAMERA_INIT_ANGLE", "BOOT_HELP_DISP_TIME",
            "BOOT_PHYSICS_INIT_YPOS_PARAM_1", "PARAM_MIN_DX", "PARAM_MIN_DZ"};
}

bool GalleryController::setup(const GalleryParams& params, QVector<GalleryPhoto> photos, GallerySort sort, double aspect, QString* error) {
    params_ = params;
    const QString missing = params_.require(paramNames());
    if (!missing.isEmpty()) { if (error) *error = missing; return false; }
    c_.chaosAngle = params_.number("CAMERA_ANGLE_CHAOS");
    c_.chaosNearAngle = params_.number("CAMERA_ANGLE_CHAOS_NEAR");
    c_.gridAngle = params_.number("CAMERA_ANGLE_GRID");
    c_.nearLimit = params_.number("CAMERA_NEAR_LIMIT");
    c_.farLimit = params_.number("CAMERA_FAR_LIMIT");
    c_.nearGrid = params_.number("CAMERA_NEAR_LIMIT_GRID");
    c_.farGrid = params_.number("CAMERA_FAR_LIMIT_GRID");
    c_.zoomSpeed = params_.number("CAMERA_SPEED_ZOOM");
    c_.quickIn = std::max(0.05, params_.number("QUICK_ZOOM_TIME"));
    c_.quickOut = std::max(0.05, params_.number("QUICK_ZOOM_OUT_TIME"));
    c_.focusSpeed = params_.number("FOCUS_MOVE_SPEED");
    c_.brake = std::clamp(params_.number("FOCUS_MOVE_BRAKE"), 0.0, 0.999);
    c_.gravity = std::clamp(params_.number("FOCUS_GRAVITY"), 0.0, 0.99);
    c_.rectSpace = params_.number("GROUP_FOCUS_RECT_SPACE");
    c_.floatUp = params_.number("GROUP_FOCUS_FLOAT_SPEED_UP");
    c_.floatDown = params_.number("GROUP_FOCUS_FLOAT_SPEED_DOWN");
    c_.bootDist = params_.number("BOOT_CAMERA_INIT_DIST");
    c_.bootAngle = params_.number("BOOT_CAMERA_INIT_ANGLE");
    c_.helpTime = params_.number("BOOT_HELP_DISP_TIME");
    c_.bootFallHeight = params_.number("BOOT_PHYSICS_INIT_YPOS_PARAM_1");
    c_.minDx = params_.number("PARAM_MIN_DX");
    c_.minDz = params_.number("PARAM_MIN_DZ");
    aspect_ = aspect > 0.5 ? aspect : 16.0 / 9.0;
    photos_ = std::move(photos);
    mode_ = Mode::Boot;
    overlay_ = Overlay::None;
    bootTime_ = 0;
    helpCard_ = 0;
    distance_ = distanceGoal_ = std::clamp(c_.bootDist, c_.nearLimit, c_.farLimit);  // TRACED name: BOOT_CAMERA_INIT_DIST
    relayout(sort);
    if (!layout_.ok) { if (error) *error = layout_.error; return false; }
    return true;
}

void GalleryController::relayout(GallerySort sort) {
    GalleryLayoutOptions options;
    options.sort = sort;
    options.aspect = aspect_;
    options.fovYDegrees = FovYDegrees;
    layout_ = layoutGallery(photos_, params_, options);
    gridPile_ = -1;
    grid_.clear();
    blend_ = 0;
    blendDirection_ = 0;
    if (mode_ == Mode::Grid) mode_ = Mode::Piles;
    focusPile_ = layout_.piles.isEmpty() ? -1 : 0;
    floatPileId_ = focusPile_;
    sinkPileId_ = -1;
    float_ = 1;
    sinking_ = 0;
    velX_ = velZ_ = 0;
    if (focusPile_ >= 0) { focusX_ = layout_.piles[0].x; focusZ_ = layout_.piles[0].z; }
}

// ------------------------------------------------------------------ camera

GalleryCamera GalleryController::camera() const {
    GalleryCamera pile;
    pile.targetX = focusX_;
    pile.targetZ = focusZ_;
    pile.distance = distance_;
    // INFERRED: CAMERA_ANGLE_CHAOS (65) is the view at the boot distance (BOOT_CAMERA_INIT_ANGLE is also 65 at
    // BOOT_CAMERA_INIT_DIST) and CAMERA_ANGLE_CHAOS_NEAR (80) is the view at the near limit; in between it is linear.
    const double span = std::max(1e-6, c_.bootDist - c_.nearLimit);
    const double near = std::clamp((c_.bootDist - distance_) / span, 0.0, 1.0);
    pile.angleDeg = lerp(c_.chaosAngle, c_.chaosNearAngle, near);
    const double s = smooth(blend_);
    if (gridPile_ < 0 || s <= 0) return pile;
    GalleryCamera grid;
    const GalleryPile& p = layout_.piles[gridPile_];
    grid.targetX = p.x + gridScrollX_;
    grid.targetZ = p.z + gridScrollZ_;
    grid.distance = gridDistance_;
    grid.angleDeg = c_.gridAngle;
    GalleryCamera out;
    out.targetX = lerp(pile.targetX, grid.targetX, s);
    out.targetZ = lerp(pile.targetZ, grid.targetZ, s);
    out.distance = lerp(pile.distance, grid.distance, s);
    out.angleDeg = lerp(pile.angleDeg, grid.angleDeg, s);
    return out;
}

double GalleryController::logoAlpha() const {
    if (mode_ != Mode::Boot) return 0;
    const double t = bootTime_;
    return std::clamp(t / 0.4, 0.0, 1.0) * std::clamp((BootSeconds - t) / 0.5, 0.0, 1.0);
}

// ------------------------------------------------------------------ focus and grid

int GalleryController::nearestPile(double x, double z, double within) const {
    int best = -1;
    double bestD = within * within;
    for (int i = 0; i < layout_.piles.size(); ++i) {
        const double dx = layout_.piles[i].x - x, dz = layout_.piles[i].z - z;
        const double d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

// Left / Right walk the placement order (time order along the line); Up / Down jump to the nearest pile in x on the
// neighbouring row. INFERRED: the firmware moves a free focus (FOCUS_MOVE_SPEED) that gravity snaps to a pile; discrete
// keys need a defined step, and this one reaches every pile.
int GalleryController::neighbour(int from, int dx, int dz) const {
    const int count = int(layout_.piles.size());
    if (from < 0 || from >= count) return count ? 0 : -1;
    if (dx) return std::clamp(from + dx, 0, count - 1);
    const GalleryPile& here = layout_.piles[from];
    const int row = here.row + dz;
    int best = -1;
    double bestD = 1e300;
    for (int i = 0; i < count; ++i) {
        if (layout_.piles[i].row != row) continue;
        const double d = std::abs(layout_.piles[i].x - here.x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best < 0 ? from : best;
}

void GalleryController::fitGrid() {
    const double tanHalf = std::tan(FovYDegrees * Pi / 360.0);
    const double need = std::max((gridHalfZ_ + GridMargin) / tanHalf, (gridHalfX_ + GridMargin) / (tanHalf * aspect_));
    gridDistance_ = gridDistanceGoal_ = std::clamp(need, c_.nearGrid, c_.farGrid);
}

void GalleryController::gridTargetFor(double* x, double* z) const {
    // The camera follows the cursor only when the grid is larger than the view; otherwise it centres the grid.
    const double tanHalf = std::tan(FovYDegrees * Pi / 360.0);
    const double visibleH = gridDistance_ * tanHalf, visibleW = visibleH * aspect_;
    const double overX = gridHalfX_ + GridMargin - visibleW, overZ = gridHalfZ_ + GridMargin - visibleH;
    *x = *z = 0;
    if (grid_.isEmpty() || gridFocus_ < 0 || gridFocus_ >= grid_.size()) return;
    if (overX > 0) *x = std::clamp(grid_[gridFocus_].x, -overX, overX);
    if (overZ > 0) *z = std::clamp(grid_[gridFocus_].z, -overZ, overZ);
}

void GalleryController::enterPile(int pile) {
    if (pile < 0 || pile >= layout_.piles.size()) return;
    gridPile_ = pile;
    grid_ = layoutGalleryGrid(layout_.piles[pile], params_, aspect_);
    gridColumns_ = 1;
    if (!grid_.isEmpty()) {
        const double z0 = grid_[0].z;
        gridColumns_ = 0;
        for (const GalleryGridCell& cell : std::as_const(grid_)) { if (std::abs(cell.z - z0) < 1e-6) ++gridColumns_; else break; }
        gridColumns_ = std::max(1, gridColumns_);
    }
    const int rows = (int(grid_.size()) + gridColumns_ - 1) / gridColumns_;
    gridHalfX_ = gridColumns_ * c_.minDx / 2.0;
    gridHalfZ_ = rows * c_.minDz / 2.0;
    gridFocus_ = 0;
    fitGrid();
    gridTargetFor(&gridScrollX_, &gridScrollZ_);
    mode_ = Mode::Grid;
    blendDirection_ = 1;
    overlay_ = Overlay::None;
}

void GalleryController::leavePile() {
    if (gridPile_ < 0) return;
    mode_ = Mode::Piles;
    blendDirection_ = -1;
    // The pile stays the focus: the camera flies back to it.
    focusPile_ = gridPile_;
    focusX_ = layout_.piles[gridPile_].x;
    focusZ_ = layout_.piles[gridPile_].z;
    velX_ = velZ_ = 0;
}

void GalleryController::setGridFocus(int indexInPile) {
    if (gridPile_ < 0 || grid_.isEmpty()) return;
    gridFocus_ = std::clamp(indexInPile, 0, int(grid_.size()) - 1);
}

GalleryController::Result GalleryController::openResult(int index) const {
    Result result;
    if (gridPile_ < 0 || gridPile_ >= layout_.piles.size()) return result;
    result.act = Act::OpenPhoto;
    for (const GalleryGridCell& cell : grid_) result.paths << photos_[cell.photo].path;
    result.index = std::clamp(index, 0, std::max(0, int(result.paths.size()) - 1));
    return result;
}

QVector<GalleryController::MenuItem> GalleryController::menu() const {
    // Names are tex_menuItems' own icons; only what maps to a working feature of this port is enabled.
    return {{"quit", 0, true}, {"tools", 1, false}, {"help", 2, true}, {"info", 3, false}, {"delete", 4, false},
            {"rotate", 5, false}, {"move", 6, false}, {"edit", 7, false}, {"group", 8, false}, {"sort", 9, true},
            {"slideshow", 10, true}, {"playlists", 11, mode_ == Mode::Piles}, {"undo", 12, false}, {"redo", 13, false}};
}

// ------------------------------------------------------------------ input

GalleryController::Result GalleryController::press(Key key) {
    Result none;
    if (mode_ == Mode::Boot) {
        // Any press ends the drop; the key itself still acts.
        mode_ = Mode::Piles;
        bootTime_ = BootSeconds;
        helpCard_ = c_.helpTime;
    } else if (helpCard_ > 0) {
        helpCard_ = 0;
    }
    const auto menuItems = menu();
    const auto stepMenu = [&](int delta) {
        int at = menuFocus_;
        for (int tries = 0; tries < menuItems.size(); ++tries) {
            at = (at + delta + int(menuItems.size())) % int(menuItems.size());
            if (menuItems[at].enabled) { menuFocus_ = at; return; }
        }
    };
    if (overlay_ == Overlay::QuitAsk) {
        switch (key) {
        case Key::Left: case Key::Right: case Key::Up: case Key::Down: quitChoice_ ^= 1; break;
        case Key::Cross: if (quitChoice_ == 0) { Result quit; quit.act = Act::Quit; return quit; } overlay_ = Overlay::None; break;
        case Key::Circle: case Key::Triangle: overlay_ = Overlay::None; break;
        default: break;
        }
        return none;
    }
    if (overlay_ == Overlay::Help) {
        if (key == Key::Cross || key == Key::Circle || key == Key::Triangle) overlay_ = Overlay::None;
        return none;
    }
    if (overlay_ == Overlay::Menu) {
        switch (key) {
        case Key::Left: stepMenu(-1); break;
        case Key::Right: stepMenu(1); break;
        case Key::Circle: case Key::Triangle: overlay_ = Overlay::None; break;
        case Key::Cross: {
            const QString id = menuItems[menuFocus_].id;
            if (id == "quit") { overlay_ = Overlay::QuitAsk; quitChoice_ = 0; }
            else if (id == "help") overlay_ = Overlay::Help;
            else if (id == "playlists") { overlay_ = Overlay::None; mode_ = Mode::Playlists; }
            else if (id == "sort") {
                const GallerySort next = layout_.sort == GallerySort::Newest ? GallerySort::Oldest
                    : layout_.sort == GallerySort::Oldest ? GallerySort::Random : GallerySort::Newest;
                overlay_ = Overlay::None;
                relayout(next);
            } else if (id == "slideshow") {
                overlay_ = Overlay::None;
                return press(Key::Start);
            }
            break;
        }
        default: break;
        }
        return none;
    }
    if (mode_ == Mode::Playlists) {
        if (key == Key::Circle) mode_ = Mode::Piles;
        else if (key == Key::Up && playlistFocus_ > 0) --playlistFocus_;
        else if (key == Key::Down && playlistFocus_ + 1 < playlists_.size()) ++playlistFocus_;
        else if (key == Key::Cross && !playlists_.isEmpty()) {
            Result result;
            result.act = Act::OpenPhoto;
            result.paths = playlists_[playlistFocus_].items;
            return result;
        }
        return none;
    }
    switch (key) {
    case Key::Triangle:
        overlay_ = Overlay::Menu;
        if (!menuItems[menuFocus_].enabled) stepMenu(1);
        break;
    case Key::Circle:
        if (mode_ == Mode::Grid) leavePile();
        else { overlay_ = Overlay::QuitAsk; quitChoice_ = 0; }   // INFERRED: the module asks msg_quit2_ask on leaving the area
        break;
    case Key::ZoomIn: case Key::ZoomOut: {
        // INFERRED: CAMERA_SPEED_ZOOM is the zoom rate; a key step is 6% of it per notch, geometric so near and far feel alike.
        const double factor = std::exp((key == Key::ZoomIn ? -1.0 : 1.0) * c_.zoomSpeed * 0.06);
        if (mode_ == Mode::Grid) gridDistanceGoal_ = std::clamp(gridDistanceGoal_ * factor, c_.nearGrid, c_.farGrid);
        else distanceGoal_ = std::clamp(distanceGoal_ * factor, c_.nearLimit, c_.farLimit);
        break;
    }
    case Key::Left: case Key::Right: case Key::Up: case Key::Down: {
        const int dx = key == Key::Left ? -1 : key == Key::Right ? 1 : 0, dz = key == Key::Up ? -1 : key == Key::Down ? 1 : 0;
        if (mode_ == Mode::Grid) {
            if (grid_.isEmpty()) break;
            const int count = int(grid_.size());
            int next = gridFocus_ + dx + dz * gridColumns_;
            if (dz > 0 && next >= count && gridFocus_ / gridColumns_ < (count - 1) / gridColumns_) next = count - 1;
            if (next >= 0 && next < count) gridFocus_ = next;
        } else if (mode_ == Mode::Piles && gridPile_ < 0) {
            const int to = neighbour(focusPile_, dx, dz);
            if (to >= 0) { focusPile_ = to; velX_ = velZ_ = 0; }
        }
        break;
    }
    case Key::Cross:
        if (mode_ == Mode::Grid) return openResult(gridFocus_);
        if (mode_ == Mode::Piles && gridPile_ < 0) enterPile(focusPile_);
        break;
    case Key::Start: {
        Result result;
        int pile = mode_ == Mode::Grid ? gridPile_ : focusPile_;
        if (pile < 0 || pile >= layout_.piles.size()) return none;
        if (mode_ != Mode::Grid) {   // slideshow from the focused pile without entering it
            result.act = Act::Slideshow;
            for (const int photo : layout_.piles[pile].photos) result.paths << photos_[photo].path;
            return result;
        }
        result = openResult(gridFocus_);
        result.act = Act::Slideshow;
        return result;
    }
    }
    return none;
}

void GalleryController::setSticks(double leftX, double leftY, double rightY) {
    const auto dead = [](double v) { return std::abs(v) < 0.2 ? 0.0 : v; };
    stickX_ = dead(leftX);
    stickZ_ = dead(leftY);   // +y is down on the pad, and +z is down the screen
    stickZoom_ = dead(rightY);
}

void GalleryController::tick(double dt) {
    dt = std::clamp(dt, 0.0, 0.1);
    if (mode_ == Mode::Boot) {
        bootTime_ += dt;
        if (bootTime_ >= BootSeconds) { mode_ = Mode::Piles; helpCard_ = c_.helpTime; }
    } else if (helpCard_ > 0) {
        helpCard_ = std::max(0.0, helpCard_ - dt);
    }
    // Area change (pile <-> grid): QUICK_ZOOM_TIME in, QUICK_ZOOM_OUT_TIME out. INFERRED mapping of the two names.
    if (blendDirection_ > 0) { blend_ = std::min(1.0, blend_ + dt / c_.quickIn); if (blend_ >= 1) blendDirection_ = 0; }
    else if (blendDirection_ < 0) {
        blend_ = std::max(0.0, blend_ - dt / c_.quickOut);
        if (blend_ <= 0) { blendDirection_ = 0; gridPile_ = -1; grid_.clear(); }
    }
    const bool inGrid = mode_ == Mode::Grid;
    // Zoom: right stick (analog) on top of the key steps.
    if (stickZoom_ != 0 && mode_ != Mode::Playlists) {
        const double factor = std::exp(stickZoom_ * c_.zoomSpeed * 0.2 * dt);
        if (inGrid) gridDistanceGoal_ = std::clamp(gridDistanceGoal_ * factor, c_.nearGrid, c_.farGrid);
        else distanceGoal_ = std::clamp(distanceGoal_ * factor, c_.nearLimit, c_.farLimit);
    }
    const double zoomK = expApproach(10.0, dt);
    distance_ += (distanceGoal_ - distance_) * zoomK;
    gridDistance_ += (gridDistanceGoal_ - gridDistance_) * zoomK;
    if (gridPile_ >= 0) {
        double tx = 0, tz = 0;
        gridTargetFor(&tx, &tz);
        const double k = expApproach(8.0, dt);
        gridScrollX_ += (tx - gridScrollX_) * k;
        gridScrollZ_ += (tz - gridScrollZ_) * k;
    }
    // Focus in the pile view.
    if (!layout_.piles.isEmpty() && gridPile_ < 0) {
        const double frames = dt * 60.0;   // the firmware's constants are per 60 Hz frame (FOCUS_MOVE_BRAKE, FOCUS_GRAVITY)
        if (mode_ != Mode::Playlists && (stickX_ != 0 || stickZ_ != 0)) {
            // INFERRED: FOCUS_MOVE_SPEED scales the stick into a speed in world units per second that grows with the
            // camera distance; FOCUS_MOVE_BRAKE is the per-frame retention of the velocity.
            const double speed = c_.focusSpeed * distance_ * 0.5;
            const double keep = std::pow(c_.brake, frames);
            velX_ = velX_ * keep + stickX_ * speed * (1 - keep);
            velZ_ = velZ_ * keep + stickZ_ * speed * (1 - keep);
            focusX_ += velX_ * dt;
            focusZ_ += velZ_ * dt;
            focusPile_ = nearestPile(focusX_, focusZ_, c_.rectSpace);
        } else {
            const double keep = std::pow(c_.brake, frames);
            velX_ *= keep; velZ_ *= keep;
            focusX_ += velX_ * dt;
            focusZ_ += velZ_ * dt;
            if (focusPile_ < 0) focusPile_ = nearestPile(focusX_, focusZ_, c_.rectSpace);
            if (focusPile_ >= 0) {
                // FOCUS_GRAVITY is the fraction of the remaining distance closed per 60 Hz frame (INFERRED reading of "attraction").
                const double k = 1.0 - std::pow(1.0 - c_.gravity, frames);
                focusX_ += (layout_.piles[focusPile_].x - focusX_) * k;
                focusZ_ += (layout_.piles[focusPile_].z - focusZ_) * k;
            }
        }
        // The line's extent, with the focus rect as margin.
        double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
        for (const GalleryPile& p : std::as_const(layout_.piles)) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z); }
        focusX_ = std::clamp(focusX_, minX, maxX);
        focusZ_ = std::clamp(focusZ_, minZ, maxZ);
    }
    // The focused pile floats (GROUP_FOCUS_FLOAT_SPEED_UP / _DOWN, per second; TRACED names, the height is INFERRED).
    if (focusPile_ != floatPileId_ && floatPileId_ >= 0) {
        // The pile that lost the focus sinks while the new one rises.
        sinkPileId_ = floatPileId_;
        sinking_ = float_;
        floatPileId_ = focusPile_;
        float_ = 0;
    } else if (floatPileId_ < 0) {
        floatPileId_ = focusPile_;
    }
    if (floatPileId_ >= 0) float_ = std::min(1.0, float_ + c_.floatUp * dt);
    if (sinkPileId_ >= 0) { sinking_ -= c_.floatDown * dt; if (sinking_ <= 0) { sinking_ = 0; sinkPileId_ = -1; } }
}

// ------------------------------------------------------------------ draws

void GalleryController::collectDraws(QVector<GalleryDraw>* out, const std::function<bool(double, double, double)>& cull) const {
    out->clear();
    const double s = smooth(blend_);
    const double baseY = 0.05, stack = 0.02;
    for (int p = 0; p < layout_.piles.size(); ++p) {
        const GalleryPile& pile = layout_.piles[p];
        if (p == gridPile_) continue;
        const double alpha = gridPile_ >= 0 ? 1.0 - s : 1.0;
        if (alpha < 0.01) continue;
        if (!cull(pile.x, pile.z, std::max(pile.halfWidth, pile.halfDepth) + 2.0)) continue;
        double lift = 0;
        if (p == floatPileId_) lift = smooth(float_) * floatHeight_;
        else if (p == sinkPileId_) lift = smooth(sinking_) * floatHeight_;
        int k = 0;
        for (const GalleryPlacedPhoto& placed : pile.shown) {
            GalleryDraw d;
            d.photo = placed.photo;
            d.pile = p;
            d.x = float(placed.x);
            d.z = float(placed.z);
            d.y = float(baseY + k * stack + lift);
            d.rotationY = float(placed.rotationY);
            d.alpha = float(alpha);
            if (mode_ == Mode::Boot) {
                // The short fall that stands in for the physics boot: each photo drops from BOOT_PHYSICS_INIT_YPOS_PARAM_1
                // after its own delay, accelerating.
                const double delay = hash01(quint32(placed.photo) * 2654435761u) * BootFallSpread;
                const double t = std::clamp((bootTime_ - delay) / BootFallSeconds, 0.0, 1.0);
                d.y += float(c_.bootFallHeight * (1.0 - t * t));
                d.alpha *= float(std::min(1.0, t * 6.0));
            }
            ++k;
            out->push_back(d);
        }
    }
    if (gridPile_ >= 0 && s > 0) {
        const GalleryPile& pile = layout_.piles[gridPile_];
        QHash<int, int> shownAt;
        for (int k = 0; k < pile.shown.size(); ++k) shownAt.insert(pile.shown[k].photo, k);
        for (int i = 0; i < grid_.size(); ++i) {
            const GalleryGridCell& cell = grid_[i];
            const double toX = pile.x + cell.x, toZ = pile.z + cell.z;
            if (!cull(toX, toZ, 2.0)) continue;
            GalleryDraw d;
            d.photo = cell.photo;
            d.pile = gridPile_;
            d.grid = true;
            d.focused = i == gridFocus_;
            const auto found = shownAt.constFind(cell.photo);
            double fromX = pile.x, fromZ = pile.z, fromRot = 0, fromAlpha = 0;
            if (found != shownAt.constEnd()) {
                const GalleryPlacedPhoto& placed = pile.shown[*found];
                fromX = placed.x; fromZ = placed.z; fromRot = placed.rotationY; fromAlpha = 1;
            }
            d.x = float(lerp(fromX, toX, s));
            d.z = float(lerp(fromZ, toZ, s));
            d.y = float(baseY + (i % 50) * stack + (1.0 - s) * smooth(float_) * floatHeight_);
            d.rotationY = float(lerp(fromRot, 0, s));
            d.alpha = float(lerp(fromAlpha, 1, s));
            out->push_back(d);
        }
    }
}

QJsonObject GalleryController::stateJson() const {
    const GalleryCamera cam = camera();
    const char* modeName = mode_ == Mode::Boot ? "boot" : mode_ == Mode::Piles ? "piles" : mode_ == Mode::Grid ? "grid" : "playlists";
    const char* overlayName = overlay_ == Overlay::None ? "none" : overlay_ == Overlay::Menu ? "menu" : overlay_ == Overlay::Help ? "help" : "quit-ask";
    QJsonObject out{{"mode", modeName}, {"overlay", overlayName}, {"sort", gallerySortName(layout_.sort)},
        {"piles", layout_.piles.size()}, {"focusPile", focusPile_}, {"gridPile", gridPile_}, {"gridFocus", gridFocus_},
        {"playlists", playlists_.size()}, {"playlistFocus", playlistFocus_},
        {"gridCells", grid_.size()}, {"gridColumns", gridColumns_}, {"gridAmount", std::round(gridAmount() * 1000) / 1000},
        {"camera", QJsonObject{{"x", std::round(cam.targetX * 100) / 100}, {"z", std::round(cam.targetZ * 100) / 100},
            {"distance", std::round(cam.distance * 100) / 100}, {"angle", std::round(cam.angleDeg * 100) / 100}}},
        {"focusX", std::round(focusX_ * 100) / 100}, {"focusZ", std::round(focusZ_ * 100) / 100}, {"bootSeconds", std::round(bootTime_ * 100) / 100}};
    if (focusPile_ >= 0 && focusPile_ < layout_.piles.size()) {
        out.insert("focusLabel", layout_.piles[focusPile_].label);
        out.insert("focusPhotos", layout_.piles[focusPile_].photos.size());
    }
    return out;
}
}
