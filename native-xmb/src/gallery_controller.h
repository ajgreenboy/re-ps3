#pragma once
#include "gallery_model.h"
#include "gallery_params.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace xmb {
// Photo Gallery controller (audit/PHOTO-GALLERY-DESIGN.md step G5): the state machine, the camera and the focus,
// with no GL and no widgets, driven by tick(dt) and press(key) so it can be replayed headlessly (--gallery-replay).
// Every constant is read from hknw_plugin/parameters.txt at run time (GalleryParams). Behaviour is labelled in
// gallery_controller.cpp: TRACED = a constant or name read from the firmware files; INFERRED = the rule that uses
// the constant is a reading of its name and comment, not followed through the module's code.
struct GalleryReplayRequest {
    QString devFlash, dataDir, outputPath, sort = QStringLiteral("newest"), tokens;
    QStringList roots;
    double hz = 60;
};
// --gallery-replay: scan the roots, run the controller headlessly through the tokens (LEFT RIGHT UP DOWN ENTER ESC TRIANGLE
// START ZOOMIN ZOOMOUT wait=<ms>), write the state after each as JSON, and measure the focus and grid settle times at 30, 60
// and 144 Hz. Returns the process exit code. (gallery_replay.cpp)
int runGalleryReplay(const GalleryReplayRequest& request);

struct GalleryCamera {
    double targetX = 0, targetZ = 0;  // look-at point on the floor
    double distance = 40;             // eye to target
    double angleDeg = 65;             // elevation above the floor plane: 90 = straight down
    // The eye sits on the +z side of the target (screen-down is +z), up vector -z.
    void eye(double* x, double* y, double* z) const;
};

// One photo quad the scene draws this frame.
struct GalleryDraw {
    int photo = -1;       // index into the photo vector
    int pile = -1;
    float x = 0, y = 0, z = 0;
    float rotationY = 0;  // degrees
    float alpha = 1;
    bool grid = false;    // belongs to the pile that has been entered (grid cell positions)
    bool focused = false; // the grid's focused cell
};

class GalleryController {
public:
    enum class Mode { Boot, Piles, Grid };
    enum class Overlay { None, Menu, Help, QuitAsk };
    enum class Key { Left, Right, Up, Down, Cross, Circle, Triangle, Start, ZoomIn, ZoomOut };
    enum class Act { None, OpenPhoto, Slideshow, Quit };
    struct Result {
        Act act = Act::None;
        QStringList paths;   // the pile's files in grid order
        int index = 0;       // the file to open
    };
    // Entries of tex_menuItems (1200x120, 14 icons left to right; the order is the picture's, read by eye: INFERRED
    // boundaries) with the ones that map to working features marked enabled.
    struct MenuItem { QString id; int icon = 0; bool enabled = false; };

    static QStringList paramNames();
    // False (with the missing names) when parameters.txt lacks a constant the controller needs.
    bool setup(const GalleryParams& params, QVector<GalleryPhoto> photos, GallerySort sort, double aspect, QString* error);

    // ---- input ----
    Result press(Key key);
    // Analog input, -1..1 each (left stick: free focus; right stick: zoom), applied by tick().
    void setSticks(double leftX, double leftY, double rightY);
    void tick(double dt);
    // Back from the photo viewer: put the grid cursor on that file of the entered pile.
    void setGridFocus(int indexInPile);

    // ---- read-only state ----
    Mode mode() const { return mode_; }
    Overlay overlay() const { return overlay_; }
    const QVector<GalleryPhoto>& photos() const { return photos_; }
    const GalleryLayout& layout() const { return layout_; }
    GallerySort sort() const { return layout_.sort; }
    const GalleryParams& params() const { return params_; }
    GalleryCamera camera() const;
    // 0 = piles, 1 = grid (eased).
    double gridAmount() const { return smooth(blend_); }
    double bootSeconds() const { return bootTime_; }
    bool bootActive() const { return mode_ == Mode::Boot; }
    // The title logo's opacity during the boot.
    double logoAlpha() const;
    bool helpCardShown() const { return helpCard_ > 0; }
    int focusPile() const { return focusPile_; }
    // The floating pile (focus) and how far it has lifted, 0..1.
    int floatPile(double* amount) const { if (amount) *amount = float_; return floatPileId_; }
    int sinkPile(double* amount) const { if (amount) *amount = sinking_; return sinkPileId_; }
    double focusX() const { return focusX_; }
    double focusZ() const { return focusZ_; }
    double floatHeight() const { return floatHeight_; }
    int enteredPile() const { return gridPile_; }
    const QVector<GalleryGridCell>& gridCells() const { return grid_; }
    int gridFocus() const { return gridFocus_; }
    int gridColumns() const { return gridColumns_; }
    QVector<MenuItem> menu() const;
    int menuFocus() const { return menuFocus_; }
    int quitChoice() const { return quitChoice_; }  // 0 = Yes, 1 = No
    double nearLimit() const { return c_.nearLimit; }
    double farLimit() const { return c_.farLimit; }

    // Draws for this frame; cull(x, z, radius) is asked per pile and for the grid so the scene can drop what is off screen.
    void collectDraws(QVector<GalleryDraw>* out, const std::function<bool(double, double, double)>& cull) const;
    QJsonObject stateJson() const;

    static double smooth(double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }

private:
    struct Constants {
        double chaosAngle = 0, chaosNearAngle = 0, gridAngle = 0, nearLimit = 0, farLimit = 0, nearGrid = 0, farGrid = 0;
        double zoomSpeed = 0, quickIn = 0, quickOut = 0, focusSpeed = 0, brake = 0, gravity = 0, rectSpace = 0;
        double floatUp = 0, floatDown = 0, bootDist = 0, bootAngle = 0, helpTime = 0, bootFallHeight = 0;
        double minDx = 0, minDz = 0;
    };
    void enterPile(int pile);
    void leavePile();
    int nearestPile(double x, double z, double within) const;
    int neighbour(int from, int dx, int dz) const;
    void relayout(GallerySort sort);
    void fitGrid();
    Result openResult(int index) const;
    void gridTargetFor(double* x, double* z) const;

    GalleryParams params_;
    Constants c_;
    double aspect_ = 16.0 / 9.0;
    QVector<GalleryPhoto> photos_;
    GalleryLayout layout_;
    Mode mode_ = Mode::Boot;
    Overlay overlay_ = Overlay::None;
    double bootTime_ = 0, helpCard_ = 0;
    // pile camera
    double focusX_ = 0, focusZ_ = 0, velX_ = 0, velZ_ = 0;
    double distance_ = 40, distanceGoal_ = 40;
    int focusPile_ = -1;
    int floatPileId_ = -1, sinkPileId_ = -1;
    double float_ = 0, sinking_ = 0, floatHeight_ = 1.5;
    // grid
    int gridPile_ = -1;
    QVector<GalleryGridCell> grid_;
    int gridFocus_ = 0, gridColumns_ = 1;
    double gridHalfX_ = 0, gridHalfZ_ = 0;
    double gridDistance_ = 30, gridDistanceGoal_ = 30, gridScrollX_ = 0, gridScrollZ_ = 0;
    double blend_ = 0;       // 0..1 linear time, eased by smooth()
    int blendDirection_ = 0; // +1 entering, -1 leaving
    // menu / quit
    int menuFocus_ = 0, quitChoice_ = 0;
    // sticks
    double stickX_ = 0, stickZ_ = 0, stickZoom_ = 0;
};
}
