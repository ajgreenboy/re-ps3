#pragma once
#include <QObject>
#include "preferences.h"
#include "media_library.h"
#include "interaction_audio.h"
#include "profiles.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QImage>
#include <memory>
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace xmb {
class Assets;
enum class Action { Unavailable, SettingsPage, SettingChoices, SetSetting, EditName, CreateUser, SelectUser, Wallpaper, Information, Library, ChooseLibrary, File, Folder };
struct Entry {
    QString id,title,detail,container,texture;
    Action action=Action::Unavailable;
    QString value;
    QString setting;
    QVariant option;
};
struct Category { QString id,title,container,texture; QVector<Entry> entries; };
class Shell final:public QObject {
    Q_OBJECT
public:
    Shell(QString dataDirectory,Assets& assets,QObject* parent=nullptr);
    ~Shell() override;
    const QVector<Category>& categories() const { return categories_; }
    const QVector<Entry>& entries() const;
    int category() const { return category_; }
    int selection() const { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
    int depth() const { return int(pages_.size()); }
    const QVector<Entry>& entriesAt(int depth) const;
    int selectionAt(int depth) const;
    QString pageTitle() const { return pages_.isEmpty()?QString{}:pages_.last().title; }
    QString message() const { return message_; }
    int theme() const { return preferences_.value("appearance/colour").toInt(); }
    QString fontFamily() const;
    QString valueText(const Entry& entry) const;
    QString clockText() const;
    int backgroundMode() const { return preferences_.value("appearance/background").toInt(); }
    QString wallpaper() const { return preferences_.value("appearance/wallpaper").toString(); }
    bool fullscreen() const { return preferences_.value("display/fullscreen").toBool(); }
    bool photoFill() const { return preferences_.value("photo/fit").toInt()==1; }
    bool photoOpen() const;
    bool slideshowActive() const { return slideshow_.isActive(); }
    bool busy() const { return loading_; }
    QString status() const { return status_; }
    qint64 mediaPosition() const;
    qint64 mediaDuration() const;
    bool mediaOpen() const { return mediaOpen_; }
    QString mediaTitle() const { return mediaTitle_; }
    void horizontal(int delta);
    void vertical(int delta);
    void activate();
    void back();
    void chooseLibrary(const QString& folder);
    void togglePlayback();
    void chooseWallpaper(const QString& file);
    void setSystemName(const QString& name);
    void setFullscreen(bool enabled);
    void refreshDirectory();
    void createUser(const QString& name);
signals:
    void changed();
    void requestLibrary();
    void mediaFrame(const QImage& frame);
    void requestWallpaper();
    void requestSystemName(const QString& value);
    void requestQuit();
    void requestNewUser();
private:
    Assets& assets_;
    Preferences preferences_;
    MediaLibrary library_;
    InteractionAudio interactionAudio_;
    Profiles profiles_;
    quint64 scanRequest_=0,photoRequest_=0;
    bool loading_=false;
    QString status_;
    QTimer slideshow_,idle_;
    QElapsedTimer activity_;
    QImage photo_;
    int photoRotation_=0;
    QVector<Category> categories_;
    QVector<int> selections_;
    struct Page { QString title; QVector<Entry> entries; int selection=0; QString directory; };
    QVector<Page> pages_;
    QString message_,mediaTitle_;
    bool mediaOpen_=false;
    int category_=6;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audio_;
    std::unique_ptr<QVideoSink> video_;
    void openDirectory(const QString& path);
    void openFile(const Entry& entry);
    void openSettings(const QString& page);
    void openChoices(const QString& key,const QString& title);
    void applyPreferences();
    void touch();
    void stepPhoto(int delta);
    void showPhoto();
    void rebuildUsers();
    int& currentSelection() { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
};
}
