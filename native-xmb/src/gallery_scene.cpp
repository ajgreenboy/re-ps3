#include "gallery_scene.h"
#include "design_space.h"
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace xmb {
namespace {
constexpr double FovYDegrees = 45.0;   // must match gallery_controller.cpp / GalleryLayoutOptions
constexpr int TextureCap = 640;        // photo textures kept on the GPU (least recently drawn go first)
constexpr int UploadBudget = 8;        // finished thumbnails uploaded per frame (the G3 per-frame budget)
// Mode numbers shared with the fragment shader.
enum Mode { Plain = 0, Photo = 1, Floor = 2, Soft = 3, Solid = 4 };

const char* vertexSource = R"(#version 330 core
layout(location=0) in vec2 pos;
uniform mat4 viewProj;
uniform mat4 model;
out vec2 uv;
out vec3 world;
void main(){
    vec4 w=model*vec4(pos.x,0.0,pos.y,1.0);
    world=w.xyz;
    uv=pos+0.5;
    gl_Position=viewProj*w;
})";

const char* fragmentSource = R"(#version 330 core
in vec2 uv;
in vec3 world;
out vec4 outColor;
uniform int mode;
uniform sampler2D tex;
uniform sampler2D tex2;
uniform vec4 tint;
uniform vec4 uvRect;
uniform vec3 lightPos;
uniform vec3 lightK;      // diffuse, ambient, specular (LIGHT_PHOTO_*)
uniform vec3 camPos;
uniform vec3 floorColor;
uniform vec3 floorOut;
uniform vec4 floorParams; // brightness, light radius, grain scale, grain strength
uniform vec3 lightCentre;
uniform vec2 ab;          // half extents of the ellipse a Soft quad fills
void main(){
    vec2 st=uvRect.xy+uv*uvRect.zw;
    if(mode==0){
        outColor=texture(tex,st)*tint;
    } else if(mode==1){
        // INFERRED: the module's photo shader is not traced. A flat photo lit by the single LIGHT_PHOTO light:
        // (ambient + diffuse*N.L) normalised so a fully lit photo shows its own colours, plus a specular sheen read
        // from photo_envmap.png along the reflection vector.
        vec4 t=texture(tex,st);
        vec3 n=vec3(0.0,1.0,0.0);
        vec3 l=normalize(lightPos-world);
        float lit=(lightK.y+lightK.x*max(dot(n,l),0.0))/(lightK.y+lightK.x);
        vec3 v=normalize(camPos-world);
        vec3 r=reflect(-l,n);
        float spec=pow(max(dot(r,v),0.0),6.0);
        vec3 sheen=texture(tex2,clamp(vec2(0.5)+r.xz*0.5,0.0,1.0)).rgb;
        outColor=vec4(t.rgb*tint.rgb*lit+sheen*spec*lightK.z,t.a*tint.a);
    } else if(mode==2){
        // INFERRED: one lit floor. FLOOR_COLOR near the light, FLOOR_LIGHT_OUT_COLOR far from it, GrainM as a faint grain,
        // scaled by the floor-brightness setting. The firmware's 15 floor types are in the absent NPIA00006 package.
        float d=length(world.xz-lightCentre.xz);
        float fall=1.0-smoothstep(floorParams.y*0.30,floorParams.y,d);
        vec3 c=mix(floorOut,floorColor,fall);
        float g=texture(tex,world.xz*floorParams.z).r;
        c*=1.0+(g-0.5)*floorParams.w;
        outColor=vec4(c*floorParams.x,1.0);
    } else if(mode==3){
        // Soft ellipse: radial profile from circle.png (1-D, 2x61), unit-ellipse coordinates in uv.
        vec2 p=(uv*2.0-1.0);
        float d=length(p);
        float a=texture(tex,vec2(0.5,clamp(d,0.0,1.0))).a;
        outColor=vec4(tint.rgb,tint.a*a*(1.0-step(1.0,d)));
    } else {
        vec3 n=vec3(0.0,1.0,0.0);
        vec3 l=normalize(lightPos-world);
        float lit=(lightK.y+lightK.x*max(dot(n,l),0.0))/(lightK.y+lightK.x);
        outColor=vec4(tint.rgb*lit,tint.a);
    }
})";

QMatrix4x4 placeMatrix(double x, double y, double z, double rotationY, double sx, double sz) {
    QMatrix4x4 m;
    m.translate(float(x), float(y), float(z));
    if (rotationY != 0) m.rotate(float(rotationY), 0, 1, 0);
    m.scale(float(sx), 1.f, float(sz));
    return m;
}
}

GalleryScene::GalleryScene() = default;
GalleryScene::~GalleryScene() = default;

QImage GalleryScene::loadFirmwarePng(const QString& name) const {
    const QString path = QDir(assets_->root()).filePath(QStringLiteral("vsh/resource/hknw_plugin/") + name);
    QImage image(path);
    if (image.isNull()) qWarning().noquote() << "gallery: firmware texture missing" << path;
    return image;
}

std::unique_ptr<QOpenGLTexture> GalleryScene::upload(const QImage& image, bool mipmaps, bool repeat) {
    if (image.isNull()) return {};
    auto texture = std::make_unique<QOpenGLTexture>(image, mipmaps ? QOpenGLTexture::GenerateMipMaps : QOpenGLTexture::DontGenerateMipMaps);
    texture->setMinificationFilter(mipmaps ? QOpenGLTexture::LinearMipMapLinear : QOpenGLTexture::Linear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(repeat ? QOpenGLTexture::Repeat : QOpenGLTexture::ClampToEdge);
    return texture;
}

QStringList GalleryScene::paramNames() {
    QStringList names{"FLOOR_COLOR_R", "FLOOR_COLOR_G", "FLOOR_COLOR_B", "FLOOR_LIGHT_OUT_COLOR_R", "FLOOR_LIGHT_OUT_COLOR_G",
        "FLOOR_LIGHT_OUT_COLOR_B", "FLOOR_BRIGHTNESS_TYPE0", "FLOOR_WRAP_COUNT_LIBRARY", "LIGHT_PHOTO_POS_X", "LIGHT_PHOTO_POS_Y",
        "LIGHT_PHOTO_POS_Z", "LIGHT_PHOTO_DIFFUSE", "LIGHT_PHOTO_AMBIENT", "LIGHT_PHOTO_SPECULAR", "PHOTO_FRAME_COLOR_R",
        "PHOTO_FRAME_COLOR_G", "PHOTO_FRAME_COLOR_B", "PHOTO_FRAME_COLOR_A", "DEFAULT_PHOTO_COLOR_BASE_R", "DEFAULT_PHOTO_COLOR_BASE_G",
        "DEFAULT_PHOTO_COLOR_BASE_B", "DEFAULT_PHOTO_COLOR_ALPHA", "SHADOW_PHOTO_DIST_1", "SHADOW_PHOTO_DIST_2", "SHADOW_PHOTO_DENSITY",
        "SHADOW_PHOTO_SCALE_X", "SHADOW_PHOTO_SCALE_Z", "FOCUS_CLUSER_RADIUS_RATE", "FOCUS_CLUSER_COLOR_R", "FOCUS_CLUSER_COLOR_G",
        "FOCUS_CLUSER_COLOR_B", "FOCUS_CLUSER_COLOR_A", "CONTOUR_COLOR_R", "CONTOUR_COLOR_G", "CONTOUR_COLOR_B", "CONTOUR_COLOR_A",
        "CONTOUR_RADIUS_RATE", "GROUP_TEXT_LOD_DIST_2", "GROUP_TEXT_YEAR_LOD_DIST_2", "BORDER_LABEL_FONT_SIZE", "BORDER_YEAR_FONT_SIZE",
        "GROUP_TEXT_LOD_SIZE_0", "GROUP_TEXT_YEAR_LOD_SIZE_0", "ARROW_OPACITY", "PARAM_MIN_DX", "PARAM_MIN_DZ", "LOD_PHOTO_DIST_3",
        // Playlist area (audit/PHOTO-GALLERY-PLAYLIST.md)
        "FLOOR_PLAYLIST_COLOR_R", "FLOOR_PLAYLIST_COLOR_G", "FLOOR_PLAYLIST_COLOR_B", "FLOOR_WRAP_COUNT_PLAYLIST", "PLAYLIST_TEXT_SIZE",
        "PLAYLIST_TEXT_POS_Z", "SHADOW_JACKET_DENSITY"};
    for (int i = 0; i < 6; ++i)
        for (const char* channel : {"R", "G", "B", "A"}) names << QStringLiteral("GROUP_TEXT_COLOR_%1_%2").arg(i).arg(QLatin1String(channel));
    return names;
}

bool GalleryScene::init(QOpenGLFunctions_3_3_Core& gl, Assets& assets) {
    gl_ = &gl;
    assets_ = &assets;
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource) ||
        !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource) || !program_.link()) {
        qCritical().noquote() << "gallery: shader failed:" << program_.log();
        return false;
    }
    program_.bind();
    const auto loc = [&](const char* name) { return program_.uniformLocation(name); };
    u_ = {loc("viewProj"), loc("model"), loc("mode"), loc("tex"), loc("tex2"), loc("tint"), loc("uvRect"), loc("lightPos"), loc("lightK"),
          loc("camPos"), loc("floorColor"), loc("floorOut"), loc("floorParams"), loc("lightCentre"), loc("ab")};
    program_.setUniformValue(u_.tex, 0);
    program_.setUniformValue(u_.tex2, 1);
    program_.release();
    static const float corners[] = {-.5f, -.5f, .5f, -.5f, -.5f, .5f, .5f, -.5f, .5f, .5f, -.5f, .5f};
    gl.glGenVertexArrays(1, &vao_);
    gl.glGenBuffers(1, &vbo_);
    gl.glBindVertexArray(vao_);
    gl.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl.glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl.glBindVertexArray(0);

    QImage whiteImage(2, 2, QImage::Format_RGBA8888);
    whiteImage.fill(Qt::white);
    white_ = upload(whiteImage, false, false);
    // GrainM.png as a grain layer: its alpha when that varies, else its luminance (read at run time; INFERRED use).
    QImage grain = loadFirmwarePng(QStringLiteral("GrainM.png")).convertToFormat(QImage::Format_RGBA8888);
    if (!grain.isNull()) {
        int minA = 255, maxA = 0;
        for (int y = 0; y < grain.height(); ++y) for (int x = 0; x < grain.width(); ++x) { const int a = qAlpha(grain.pixel(x, y)); minA = std::min(minA, a); maxA = std::max(maxA, a); }
        const bool useAlpha = maxA - minA > 8;
        for (int y = 0; y < grain.height(); ++y) for (int x = 0; x < grain.width(); ++x) {
            const QRgb p = grain.pixel(x, y);
            const int v = useAlpha ? qAlpha(p) : qGray(p);
            grain.setPixel(x, y, qRgba(v, v, v, 255));
        }
    }
    grain_ = upload(grain, true, true);
    shadow_[0] = upload(loadFirmwarePng(QStringLiteral("photo_shadow_1.png")), true, false);
    shadow_[1] = upload(loadFirmwarePng(QStringLiteral("photo_shadow_2.png")), true, false);
    shadow_[2] = upload(loadFirmwarePng(QStringLiteral("photo_shadow_3.png")), true, false);
    envmap_ = upload(loadFirmwarePng(QStringLiteral("photo_envmap.png")), true, false);
    focusCluster_ = upload(loadFirmwarePng(QStringLiteral("focus_cluster.png")), true, false);
    focusClusterShadow_ = upload(loadFirmwarePng(QStringLiteral("focus_cluster_shadow.png")), true, false);
    focusGrid_ = upload(loadFirmwarePng(QStringLiteral("focus_grid.png")), true, false);
    contour_ = upload(loadFirmwarePng(QStringLiteral("circle.png")), false, false);
    // tex_arrow_anim: a 120x2280 strip of 38 frames of 120x60 (counted from the picture: INFERRED frame size).
    const QImage arrow = assets.image(QStringLiteral("hknw_plugin"), QStringLiteral("tex_arrow_anim"));
    arrowFrames_ = arrow.isNull() ? 1 : std::max(1, arrow.height() / 60);
    arrow_ = upload(arrow, true, false);
    ready_ = white_ && grain_ && shadow_[0] && shadow_[1] && shadow_[2] && envmap_ && focusCluster_ && focusClusterShadow_ && focusGrid_ && contour_;
    if (!ready_) qWarning() << "gallery: a firmware texture is missing; the scene stays off";
    return ready_;
}

void GalleryScene::attach(GalleryThumbs* thumbs, const QString& fontFamily) {
    thumbs_ = thumbs;
    font_ = fontFamily;
}

void GalleryScene::reset(QOpenGLFunctions_3_3_Core&) {
    photoTextures_.clear();
    bestLevel_.clear();
    labels_.clear();
    yearLabels_.clear();
    pending_ = 0;
}

void GalleryScene::checkGl(const char* where) {
    const GLenum error = gl_->glGetError();
    if (error == GL_NO_ERROR) return;
    ++stats_.glErrors;
    if (stats_.firstError.isEmpty()) stats_.firstError = QStringLiteral("0x%1 after %2").arg(error, 0, 16).arg(QLatin1String(where));
    if (!errorReported_) { errorReported_ = true; qWarning().noquote() << "gallery: GL error" << stats_.firstError; }
}

void GalleryScene::drawQuad(const QMatrix4x4& model, int mode, const QVector4D& tint, GLuint tex, GLuint tex2, const QVector4D& uvRect, float abX, float abY) {
    program_.setUniformValue(u_.model, model);
    program_.setUniformValue(u_.mode, mode);
    program_.setUniformValue(u_.tint, tint);
    program_.setUniformValue(u_.uvRect, uvRect);
    program_.setUniformValue(u_.ab, QVector2D(abX, abY));
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glBindTexture(GL_TEXTURE_2D, tex);
    gl_->glActiveTexture(GL_TEXTURE1);
    gl_->glBindTexture(GL_TEXTURE_2D, tex2 ? tex2 : envmap_->textureId());
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glDrawArrays(GL_TRIANGLES, 0, 6);
    ++stats_.quads;
}

const GalleryScene::Label* GalleryScene::labelFor(qint64 key, const QString& text, int pixelHeight) {
    auto& cache = pixelHeight > 100 ? yearLabels_ : labels_;
    if (const auto found = cache.constFind(key); found != cache.constEnd()) return &*found;
    QFont font(font_.isEmpty() ? QStringLiteral("Segoe UI") : font_);
    font.setPixelSize(int(pixelHeight * 0.78));
    font.setBold(true);
    font.setStyleStrategy(QFont::PreferAntialias);
    const QFontMetrics metrics(font);
    const int width = metrics.horizontalAdvance(text) + pixelHeight / 2, height = pixelHeight;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, width, height), Qt::AlignCenter, text);
    }
    Label label;
    label.texture = upload(image, true, false);
    label.aspect = float(width) / float(height);
    ++stats_.labels;
    return &*cache.insert(key, std::move(label));
}

GLuint GalleryScene::photoTexture(const QString& path, float* aspect) {
    const int level = bestLevel_.value(path, 0);
    if (level <= 0) return 0;
    const auto found = photoTextures_.find(path + QLatin1Char('#') + QString::number(level));
    if (found == photoTextures_.end()) return 0;
    found->used = tick_;
    *aspect = found->aspect;
    return found->texture->textureId();
}

void GalleryScene::planThumbnails(const GalleryController& controller, const QVector<GalleryDraw>& draws, const GalleryCamera& camera) {
    if (!thumbs_) return;
    const auto& photos = controller.photos();
    QVector<GalleryThumbs::Item> items;
    items.reserve(draws.size());
    for (const GalleryDraw& d : draws) if (d.alpha > 0.05f) items.push_back({d.photo, d.x, d.z});
    const auto requests = thumbs_->plan(camera.targetX, camera.targetZ, camera.distance, items, 64);
    int issued = 0;
    for (const auto& request : requests) {
        const QString& path = photos[request.index].path;
        if (bestLevel_.value(path, 0) >= int(request.level)) continue;
        thumbs_->request(path, request.level);
        ++issued;
    }
    stats_.requested += issued;
    pending_ = thumbs_->inFlight() + thumbs_->readyCount();
}

void GalleryScene::render(QOpenGLFunctions_3_3_Core& gl, int pixelWidth, int pixelHeight, GLuint targetFbo, const GalleryController& controller, double seconds) {
    if (!ready_) return;
    gl_ = &gl;
    {
        const int errors = stats_.glErrors;
        const QString first = stats_.firstError;
        stats_ = Stats{};
        stats_.glErrors = errors;
        stats_.firstError = first;
    }
    ++tick_;
    const GalleryParams& P = controller.params();
    const auto n = [&](const char* name) { return P.number(name); };
    const auto& photos = controller.photos();
    const GalleryLayout& layout = controller.layout();
    const GalleryCamera camera = controller.camera();
    constexpr double aspect = 16.0 / 9.0;

    // ---- uploads: finished thumbnails, budgeted ----
    if (thumbs_) {
        for (const auto& ready : thumbs_->takeReady(UploadBudget)) {
            if (ready.image.isNull() || ready.level < GalleryThumbs::Small) continue;
            Tex tex;
            tex.texture = upload(ready.image, true, false);
            tex.level = int(ready.level);
            tex.aspect = ready.image.height() > 0 ? float(ready.image.width()) / float(ready.image.height()) : 4.f / 3.f;
            tex.used = tick_;
            photoTextures_.insert(ready.path + QLatin1Char('#') + QString::number(int(ready.level)), std::move(tex));
            bestLevel_[ready.path] = std::max(bestLevel_.value(ready.path, 0), int(ready.level));
            ++stats_.uploads;
        }
        if (photoTextures_.size() > TextureCap) {
            QVector<QPair<quint64, QString>> order;
            for (auto it = photoTextures_.constBegin(); it != photoTextures_.constEnd(); ++it) if (it->used + 2 < tick_) order.push_back({it->used, it.key()});
            std::sort(order.begin(), order.end());
            for (int i = 0; i < order.size() && photoTextures_.size() > TextureCap - 32; ++i) {
                const QString key = order[i].second;
                photoTextures_.remove(key);
                const QString path = key.left(key.lastIndexOf(QLatin1Char('#')));
                // Recompute the best level still resident.
                int best = 0;
                for (int level = 1; level <= 3; ++level) if (photoTextures_.contains(path + QLatin1Char('#') + QString::number(level))) best = level;
                if (best) bestLevel_[path] = best; else bestLevel_.remove(path);
            }
        }
    }
    checkGl("uploads");

    // ---- camera ----
    double ex, ey, ez;
    camera.eye(&ex, &ey, &ez);
    QMatrix4x4 projection, view;
    projection.perspective(float(FovYDegrees), float(aspect), float(std::max(0.5, camera.distance * 0.08)), float(camera.distance * 4 + 600));
    view.lookAt(QVector3D(float(ex), float(ey), float(ez)), QVector3D(float(camera.targetX), 0.f, float(camera.targetZ)), QVector3D(0, 0, -1));
    viewProj_ = projection * view;
    const auto onScreen = [&](double x, double z, double radius) {
        const QVector4D clip = viewProj_ * QVector4D(float(x), 0.f, float(z), 1.f);
        if (clip.w() <= 0) return radius > camera.distance * 0.5;   // behind the eye: only huge things matter
        const double nx = clip.x() / clip.w(), ny = clip.y() / clip.w();
        // radius in clip units at that depth
        const double rx = radius / (clip.w() * std::tan(FovYDegrees * 3.14159265358979 / 360.0) * aspect);
        const double ry = radius / (clip.w() * std::tan(FovYDegrees * 3.14159265358979 / 360.0));
        return std::abs(nx) <= 1.0 + rx && std::abs(ny) <= 1.0 + ry;
    };
    QVector<GalleryDraw> draws;
    controller.collectDraws(&draws, onScreen);
    planThumbnails(controller, draws, camera);

    // ---- state ----
    gl.glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    DesignSpace(pixelWidth,pixelHeight).viewport(gl);
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glDisable(GL_STENCIL_TEST);
    gl.glDisable(GL_CULL_FACE);
    gl.glDepthMask(GL_TRUE);
    // The bars of a window that is not 16:9 stay black like every other page; only the 16:9 area takes the scene's grey.
    {
        const DesignSpace area(pixelWidth, pixelHeight);
        gl.glClearColor(0.f, 0.f, 0.f, 1.f);
        gl.glClearDepth(1.0);
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.glEnable(GL_SCISSOR_TEST);
        gl.glScissor(qRound(area.left), qRound(area.top), qRound(1920. * area.x), qRound(1080. * area.y));
        gl.glClearColor(0.5f, 0.5f, 0.52f, 1.f);
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.glDisable(GL_SCISSOR_TEST);
    }
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthFunc(GL_LEQUAL);
    gl.glEnable(GL_BLEND);
    gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.glBindVertexArray(vao_);
    program_.bind();
    program_.setUniformValue(u_.viewProj, viewProj_);
    program_.setUniformValue(u_.lightPos, QVector3D(float(n("LIGHT_PHOTO_POS_X")), float(n("LIGHT_PHOTO_POS_Y")), float(n("LIGHT_PHOTO_POS_Z"))));
    program_.setUniformValue(u_.lightK, QVector3D(float(n("LIGHT_PHOTO_DIFFUSE")), float(n("LIGHT_PHOTO_AMBIENT")), float(n("LIGHT_PHOTO_SPECULAR"))));
    program_.setUniformValue(u_.camPos, QVector3D(float(ex), float(ey), float(ez)));

    // ---- floor ----
    // The Playlist area's floor uses FLOOR_PLAYLIST_COLOR_* and FLOOR_WRAP_COUNT_PLAYLIST (TRACED names; the package's
    // wood floor of tex_help_pic_playlist is absent, so it is the same lit quad as the library's).
    const bool playlistArea = controller.area() == GalleryController::Area::Playlist;
    if (playlistArea)
        program_.setUniformValue(u_.floorColor, QVector3D(float(n("FLOOR_PLAYLIST_COLOR_R")), float(n("FLOOR_PLAYLIST_COLOR_G")), float(n("FLOOR_PLAYLIST_COLOR_B"))));
    else
        program_.setUniformValue(u_.floorColor, QVector3D(float(n("FLOOR_COLOR_R")), float(n("FLOOR_COLOR_G")), float(n("FLOOR_COLOR_B"))));
    program_.setUniformValue(u_.floorOut, QVector3D(float(n("FLOOR_LIGHT_OUT_COLOR_R")), float(n("FLOOR_LIGHT_OUT_COLOR_G")), float(n("FLOOR_LIGHT_OUT_COLOR_B"))));
    // The grain tile repeats FLOOR_WRAP_COUNT_LIBRARY times across a 2000-unit floor (INFERRED reading of the name).
    const double floorSize = 2000.0;
    program_.setUniformValue(u_.floorParams, QVector4D(float(n("FLOOR_BRIGHTNESS_TYPE0")), float(camera.distance * 3.0),
        float(n(playlistArea ? "FLOOR_WRAP_COUNT_PLAYLIST" : "FLOOR_WRAP_COUNT_LIBRARY") / floorSize), 0.10f));
    program_.setUniformValue(u_.lightCentre, QVector3D(float(camera.targetX), 0.f, float(camera.targetZ)));
    drawQuad(placeMatrix(camera.targetX, 0, camera.targetZ, 0, floorSize, floorSize), Floor, QVector4D(1, 1, 1, 1), grain_->textureId());
    checkGl("floor");

    gl.glDepthMask(GL_FALSE);
    const double gridAmount = controller.gridAmount();
    const double pileAlpha = 1.0 - gridAmount;
    const double minDx = n("PARAM_MIN_DX"), minDz = n("PARAM_MIN_DZ");
    const bool inGrid = controller.enteredPile() >= 0;
    const bool bootDrop = controller.bootActive();

    const auto groupColor = [&](int index, double alphaScale) {
        const auto channel = [&](const char* c) { return n(qPrintable(QStringLiteral("GROUP_TEXT_COLOR_%1_%2").arg(index).arg(QLatin1String(c)))); };
        return QVector4D(float(std::clamp(channel("R"), 0.0, 1.0)), float(std::clamp(channel("G"), 0.0, 1.0)), float(std::clamp(channel("B"), 0.0, 1.0)),
                         float(channel("A") * alphaScale));
    };
    // ---- Playlist area: each jacket's name under it, and the card of an empty playlist ----
    // PLAYLIST_TEXT_SIZE (2.5, "relative to the library space") and PLAYLIST_TEXT_POS_Z (1.9) are loaded by FUN_001a0488
    // (TRACED: the 2.5f and 1.9 constants it reads); how it applies them is INFERRED: the label is the library label
    // height times PLAYLIST_TEXT_SIZE, its centre PLAYLIST_TEXT_POS_Z half label heights below the jacket's lower edge.
    if (playlistArea && pileAlpha > 0.01) {
        const double labelHeight = n("BORDER_LABEL_FONT_SIZE") * 0.45 * n("PLAYLIST_TEXT_SIZE");
        const int labelPixels = int(n("GROUP_TEXT_LOD_SIZE_0"));
        const QVector3D placeholder(float(n("DEFAULT_PHOTO_COLOR_BASE_R")), float(n("DEFAULT_PHOTO_COLOR_BASE_G")), float(n("DEFAULT_PHOTO_COLOR_BASE_B")));
        for (int i = 0; i < layout.piles.size(); ++i) {
            const GalleryPile& jacket = layout.piles[i];
            if (i == controller.enteredPile()) continue;
            if (!onScreen(jacket.x, jacket.z, std::max(jacket.halfWidth, jacket.halfDepth) + labelHeight * 3)) continue;
            if (jacket.shown.isEmpty()) {
                // An empty playlist: the module's default photo colour card where the cover would be (INFERRED).
                drawQuad(placeMatrix(jacket.x, 0.05, jacket.z, 0, jacket.halfWidth * 2, jacket.halfDepth * 2), Solid,
                    QVector4D(placeholder.x() * 0.9f, placeholder.y() * 0.9f, placeholder.z() * 0.92f, float(0.8 * pileAlpha)));
            }
            if (const Label* label = labelFor(-1 - qint64(i), jacket.label, labelPixels); label && label->texture) {
                const double lz = jacket.z + jacket.halfDepth + labelHeight * n("PLAYLIST_TEXT_POS_Z") * 0.5;
                drawQuad(placeMatrix(jacket.x, 0.012, lz, 0, labelHeight * label->aspect, labelHeight), Plain,
                    groupColor(jacket.colorIndex, pileAlpha), label->texture->textureId());
            }
        }
    }
    // ---- contour (soft ellipse under each pile), labels, arrows ----
    if (!playlistArea && pileAlpha > 0.01 && !bootDrop) {
        const QVector4D contour(float(n("CONTOUR_COLOR_R")), float(n("CONTOUR_COLOR_G")), float(n("CONTOUR_COLOR_B")), float(n("CONTOUR_COLOR_A") * pileAlpha));
        const double radiusRate = n("CONTOUR_RADIUS_RATE");
        const double labelDistance = n("GROUP_TEXT_LOD_DIST_2"), yearDistance = n("GROUP_TEXT_YEAR_LOD_DIST_2");
        const double labelHeight = n("BORDER_LABEL_FONT_SIZE") * 0.45;   // INFERRED: the unit of BORDER_LABEL_FONT_SIZE
        const double yearHeight = n("BORDER_YEAR_FONT_SIZE") * 0.45;
        const int labelPixels = int(n("GROUP_TEXT_LOD_SIZE_0")), yearPixels = int(n("GROUP_TEXT_YEAR_LOD_SIZE_0"));
        const double arrowAlpha = n("ARROW_OPACITY");
        const int frame = int(seconds * 30.0) % arrowFrames_;   // INFERRED: 30 frames per second
        for (int i = 0; i < layout.piles.size(); ++i) {
            const GalleryPile& pile = layout.piles[i];
            const double reach = std::max(pile.halfWidth, pile.halfDepth) + 6.0;
            if (!onScreen(pile.x, pile.z, reach)) continue;
            gl.glDepthMask(GL_FALSE);
            const double a = (pile.halfWidth + 1.0) * radiusRate, b = (pile.halfDepth + 0.8) * radiusRate;
            drawQuad(placeMatrix(pile.x, 0.01, pile.z, 0, a * 2, b * 2), Soft, contour, contour_->textureId());
            if (camera.distance > labelDistance) continue;
            const QColor color = QColor::fromRgbF(float(std::clamp(n(qPrintable(QStringLiteral("GROUP_TEXT_COLOR_%1_R").arg(pile.colorIndex))), 0.0, 1.0)),
                float(std::clamp(n(qPrintable(QStringLiteral("GROUP_TEXT_COLOR_%1_G").arg(pile.colorIndex))), 0.0, 1.0)),
                float(std::clamp(n(qPrintable(QStringLiteral("GROUP_TEXT_COLOR_%1_B").arg(pile.colorIndex))), 0.0, 1.0)));
            const double alpha = n(qPrintable(QStringLiteral("GROUP_TEXT_COLOR_%1_A").arg(pile.colorIndex))) * pileAlpha;
            if (const Label* label = labelFor(pile.id, pile.label, labelPixels); label && label->texture) {
                const double lz = pile.z + pile.halfDepth + labelHeight * 0.9;
                drawQuad(placeMatrix(pile.x, 0.012, lz, 0, labelHeight * label->aspect, labelHeight), Plain,
                    QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), float(alpha)), label->texture->textureId());
            }
            // Arrow to the next pile of the same row.
            if (i + 1 < layout.piles.size() && layout.piles[i + 1].row == pile.row && arrow_) {
                const GalleryPile& next = layout.piles[i + 1];
                const double dx = next.x - pile.x, dz = next.z - pile.z, length = std::hypot(dx, dz);
                const double room = length - (pile.halfWidth + next.halfWidth) * 0.55;
                if (room > 1.2) {
                    const double angle = -std::atan2(dz, dx) * 180.0 / 3.14159265358979;
                    const double size = std::min(2.4, room * 0.7);
                    drawQuad(placeMatrix((pile.x + next.x) / 2, 0.011, (pile.z + next.z) / 2 + pile.halfDepth * 0.25, angle, size, size * 0.5), Plain,
                        QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), float(arrowAlpha * 1.6 * pileAlpha)), arrow_->textureId(), 0,
                        QVector4D(0, float(frame) / float(arrowFrames_), 1, 1.f / float(arrowFrames_)));
                }
            }
            if (pile.yearBreakBefore && camera.distance <= yearDistance) {
                if (const Label* label = labelFor(1000000 + pile.yearBreakBefore, QString::number(pile.yearBreakBefore), yearPixels); label && label->texture) {
                    const double x = i > 0 ? (layout.piles[i - 1].x + pile.x) / 2 : pile.x - 2, z = (i > 0 ? (layout.piles[i - 1].z + pile.z) / 2 : pile.z) - yearHeight * 0.9 - pile.halfDepth;
                    drawQuad(placeMatrix(x, 0.012, z, 0, yearHeight * label->aspect, yearHeight), Plain,
                        QVector4D(float(color.redF()), float(color.greenF()), float(color.blueF()), float(alpha * 0.8)), label->texture->textureId());
                }
            }
        }
    }
    checkGl("labels");

    // ---- focus ring under the floating pile ----
    int floating = -1;
    double lift = 0;
    floating = controller.floatPile(&lift);
    if (!playlistArea && floating >= 0 && floating < layout.piles.size() && pileAlpha > 0.01 && !bootDrop) {
        const GalleryPile& pile = layout.piles[floating];
        const double radius = (std::max(pile.halfWidth, pile.halfDepth) + 1.6) * n("FOCUS_CLUSER_RADIUS_RATE");
        const float k = float(GalleryController::smooth(lift) * pileAlpha);
        drawQuad(placeMatrix(pile.x, 0.02, pile.z, 0, radius * 2.4, radius * 2.4), Plain, QVector4D(0, 0, 0, 0.5f * k), focusClusterShadow_->textureId());
        drawQuad(placeMatrix(pile.x, 0.025, pile.z, 0, radius * 2.0, radius * 2.0), Plain,
            QVector4D(float(n("FOCUS_CLUSER_COLOR_R")), float(n("FOCUS_CLUSER_COLOR_G")), float(n("FOCUS_CLUSER_COLOR_B")), float(n("FOCUS_CLUSER_COLOR_A")) * k), focusCluster_->textureId());
    }

    // ---- shadows ----
    // INFERRED: SHADOW_PHOTO_DIST_1..3 are heights above the floor (1, 8, 50): photo_shadow_1 (96 px, tight) for a photo
    // lying on the floor, the two 32 px blurs as it floats higher. Density SHADOW_PHOTO_DENSITY, scale SHADOW_PHOTO_SCALE_*;
    // divided by a stack factor because a pile stacks several photos on one spot.
    const double d1 = n("SHADOW_PHOTO_DIST_1"), d2 = n("SHADOW_PHOTO_DIST_2");
    const double density = n("SHADOW_PHOTO_DENSITY"), sx = n("SHADOW_PHOTO_SCALE_X"), sz = n("SHADOW_PHOTO_SCALE_Z");
    const auto photoSize = [&](float aspectRatio, double* w, double* h) {
        const double boxW = minDx * 0.96, boxH = minDz * 0.96;
        if (aspectRatio >= boxW / boxH) { *w = boxW; *h = boxW / aspectRatio; } else { *h = boxH; *w = boxH * aspectRatio; }
    };
    for (const GalleryDraw& d : draws) {
        float ratio = 4.f / 3.f;
        photoTexture(photos[d.photo].path, &ratio);
        double w, h;
        photoSize(ratio, &w, &h);
        w *= d.scale;
        h *= d.scale;
        // A jacket's shadow density is SHADOW_JACKET_DENSITY (TRACED name; INFERRED use, as for the photos).
        const double height = d.y;
        const int which = height <= d1 ? 0 : height <= d2 ? 1 : 2;
        const double spread = 1.0 + std::min(1.5, height * 0.15);
        const double shade = d.jacket ? n("SHADOW_JACKET_DENSITY") : density;
        drawQuad(placeMatrix(d.x + height * 0.12, 0.015, d.z + height * 0.06, d.rotationY, w * sx * spread, h * sz * spread), Plain,
            QVector4D(0, 0, 0, float(shade * 0.22 * d.alpha)), shadow_[which]->textureId());
    }
    checkGl("shadows");

    // ---- photos ----
    gl.glDepthMask(GL_TRUE);
    const QVector4D frameColor(float(n("PHOTO_FRAME_COLOR_R")), float(n("PHOTO_FRAME_COLOR_G")), float(n("PHOTO_FRAME_COLOR_B")), float(n("PHOTO_FRAME_COLOR_A")));
    const float placeholderAlpha = float(n("DEFAULT_PHOTO_COLOR_ALPHA"));
    const QVector3D placeholderColor(float(n("DEFAULT_PHOTO_COLOR_BASE_R")), float(n("DEFAULT_PHOTO_COLOR_BASE_G")), float(n("DEFAULT_PHOTO_COLOR_BASE_B")));
    for (const GalleryDraw& d : draws) {
        float ratio = 4.f / 3.f;
        const GLuint texture = photoTexture(photos[d.photo].path, &ratio);
        double w, h;
        photoSize(ratio, &w, &h);
        w *= d.scale;
        h *= d.scale;
        const double border = 0.07 * d.scale;
        drawQuad(placeMatrix(d.x, d.y, d.z, d.rotationY, w + border * 2, h + border * 2), Solid,
            QVector4D(frameColor.x(), frameColor.y(), frameColor.z(), frameColor.w() * (texture ? d.alpha : d.alpha * 0.5f)));
        if (texture) {
            drawQuad(placeMatrix(d.x, d.y + 0.004, d.z, d.rotationY, w, h), Photo, QVector4D(1, 1, 1, d.alpha), texture, envmap_->textureId());
            ++stats_.photos;
        } else {
            // The thumbnail has not arrived: the module's DEFAULT_PHOTO_COLOR (a pale card).
            drawQuad(placeMatrix(d.x, d.y + 0.004, d.z, d.rotationY, w, h), Solid,
                QVector4D(placeholderColor.x() * 0.9f, placeholderColor.y() * 0.9f, placeholderColor.z() * 0.92f, std::max(placeholderAlpha, 0.55f) * d.alpha));
        }
    }
    checkGl("photos");

    // ---- grid cursor ----
    gl.glDepthMask(GL_FALSE);
    gl.glDisable(GL_DEPTH_TEST);
    if (inGrid && gridAmount > 0.6) {
        for (const GalleryDraw& d : draws) {
            if (!d.focused) continue;
            const float pulse = 0.85f + 0.15f * float(std::sin(seconds * 5.0));
            drawQuad(placeMatrix(d.x, d.y + 0.05, d.z, 0, minDx * 1.18, minDz * 1.3), Plain, QVector4D(1.f, 1.f, 1.f, float(gridAmount) * pulse), focusGrid_->textureId());
        }
    }
    // The focused jacket: INFERRED. The module's jacket focus is not traced (focus_cluster's ring reads as a grey slab at a
    // jacket's size), so the jacket lifts (controller) and carries the gallery's own focus_grid frame, scaled to it.
    if (playlistArea && !inGrid && floating >= 0 && pileAlpha > 0.01) {
        for (const GalleryDraw& d : draws) {
            if (!d.jacket || d.pile != floating) continue;
            const float pulse = 0.85f + 0.15f * float(std::sin(seconds * 5.0));
            drawQuad(placeMatrix(d.x, d.y + 0.05, d.z, d.rotationY, minDx * 1.18 * d.scale, minDz * 1.3 * d.scale), Plain,
                QVector4D(1.f, 1.f, 1.f, float(GalleryController::smooth(lift) * pileAlpha) * pulse), focusGrid_->textureId());
        }
    }
    checkGl("cursor");
    gl.glBindTexture(GL_TEXTURE_2D, 0);
    gl.glBindVertexArray(0);
    program_.release();
    gl.glDepthMask(GL_TRUE);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_BLEND);
    ++frame_;
}
}
