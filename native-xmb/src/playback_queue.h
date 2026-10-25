#pragma once

#include <QVector>
#include <QString>

namespace xmb {

class PlaybackQueue {
public:
    struct Item { QString path; QString title; };
    enum class Repeat { Off, One, All };

    void reset(QVector<Item> items, int selected);
    void clear();
    bool empty() const { return items_.isEmpty(); }
    int currentIndex() const;
    const Item* current() const;
    const QVector<Item>& items() const { return items_; }
    void setRepeat(Repeat repeat) { repeat_ = repeat; }
    void setShuffle(bool shuffle);
    bool shuffle() const { return shuffle_; }
    Repeat repeat() const { return repeat_; }

    int next(bool automatic = true);
    int previous();

private:
    static constexpr qsizetype MaxHistory = 4096;
    void rebuildOrder(int current, bool pinCurrent = true);
    int advanceOrder();

    QVector<Item> items_;
    QVector<int> order_;
    qsizetype orderPosition_ = -1;
    QVector<int> history_;
    qsizetype historyPosition_ = -1;
    Repeat repeat_ = Repeat::Off;
    bool shuffle_ = false;
};

} // namespace xmb
