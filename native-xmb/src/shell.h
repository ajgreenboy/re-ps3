#pragma once
#include <QObject>
#include <QSettings>
#include <QVector>
#include <QImage>
#include <memory>
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;

namespace xmb {
class Assets;
enum class Action { Unavailable, Theme, Colours, Colour, Information, Library, File, Folder };
struct Entry {
    QString id,title,detail,container,texture;
    Action action=Action::Unavailable;
    QString value;
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
    int theme() const { return settings_.value("appearance/colour",0).toInt(); }
    bool mediaOpen() const { return mediaOpen_; }
    QString mediaTitle() const { return mediaTitle_; }
    void horizontal(int delta);
    void vertical(int delta);
    void activate();
    void back();
    void chooseLibrary(const QString& folder);
    void togglePlayback();
signals:
    void changed();
    void requestLibrary();
    void mediaFrame(const QImage& frame);
private:
    QSettings settings_;
    QVector<Category> categories_;
    QVector<int> selections_;
    struct Page { QString title; QVector<Entry> entries; int selection=0; };
    QVector<Page> pages_;
    QString message_,mediaTitle_;
    bool mediaOpen_=false;
    int category_=6;
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audio_;
    std::unique_ptr<QVideoSink> video_;
    void openDirectory(const QString& path);
    void openFile(const Entry& entry);
    int& currentSelection() { return pages_.isEmpty()?selections_[category_]:pages_.last().selection; }
};
}
