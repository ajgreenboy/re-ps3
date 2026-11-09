#pragma once
#include "gallery_controller.h"
#include "gallery_model.h"
#include "gallery_params.h"
#include "gallery_scene.h"
#include "gallery_thumbs.h"
#include <QString>
#include <QStringList>
#include <memory>

namespace xmb {
// The open Photo Gallery: everything View owns while the library area is up (audit/PHOTO-GALLERY.md). Created by
// View::openGallery, dropped by View::closeGallery; gallery_chrome.cpp holds the View members that use it.
struct GalleryState {
    enum class Stage { Indexing, Ready, Failed };
    Stage stage = Stage::Indexing;
    QString error;
    GalleryParams params;
    QStringList roots;
    std::unique_ptr<GalleryIndexer> indexer;
    quint64 request = 0;
    std::unique_ptr<GalleryController> controller;
    std::unique_ptr<GalleryThumbs> thumbs;
    GalleryScene scene;
    bool sceneInit = false, sceneOk = false;
    bool viewerWasOpen = false;     // the photo viewer was opened from here and has not been seen closing
    int photoCount = 0;
    bool truncated = false;
    GallerySort sort = GallerySort::Newest;
};
}
