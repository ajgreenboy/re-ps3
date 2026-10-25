#include "playback_queue.h"

#include <QRandomGenerator>

#include <algorithm>

namespace xmb {

void PlaybackQueue::reset(QVector<Item> items, int selected) {
    items_ = std::move(items);
    history_.clear();
    order_.clear();
    orderPosition_ = -1;
    historyPosition_ = -1;
    if (items_.isEmpty()) return;

    const int current = selected >= 0 && selected < items_.size() ? selected : 0;
    rebuildOrder(current);
    history_.push_back(current);
    historyPosition_ = 0;
}

void PlaybackQueue::clear() {
    items_.clear();
    order_.clear();
    history_.clear();
    orderPosition_ = historyPosition_ = -1;
}

int PlaybackQueue::currentIndex() const {
    if (historyPosition_ < 0 || historyPosition_ >= history_.size()) return -1;
    return history_.at(historyPosition_);
}

const PlaybackQueue::Item* PlaybackQueue::current() const {
    const int index = currentIndex();
    return index < 0 || index >= items_.size() ? nullptr : &items_.at(index);
}

void PlaybackQueue::rebuildOrder(int current, bool pinCurrent) {
    order_.clear();
    order_.reserve(items_.size());
    for (int i = 0; i < items_.size(); ++i) order_.push_back(i);
    if (shuffle_) {
        for (qsizetype i = order_.size() - 1; i > 0; --i) {
            const qsizetype j = QRandomGenerator::global()->bounded(static_cast<quint32>(i + 1));
            std::swap(order_[i], order_[j]);
        }
        if (pinCurrent && current >= 0) {
            const qsizetype position = order_.indexOf(current);
            if (position > 0) std::swap(order_[0], order_[position]);
        }
    }
    orderPosition_ = pinCurrent ? order_.indexOf(current) : -1;
    if (shuffle_ && pinCurrent && current >= 0) orderPosition_ = 0;
}

void PlaybackQueue::setShuffle(bool shuffle) {
    if (shuffle_ == shuffle) return;
    shuffle_ = shuffle;
    const int current = currentIndex();
    if (current < 0) return;
    rebuildOrder(current);
    history_.clear();
    history_.push_back(current);
    historyPosition_ = 0;
}

int PlaybackQueue::advanceOrder() {
    if (order_.isEmpty()) return -1;
    if (orderPosition_ + 1 < order_.size()) return order_.at(++orderPosition_);
    if (repeat_ != Repeat::All) return -1;

    const int previous = order_.at(orderPosition_);
    if (shuffle_) {
        rebuildOrder(-1, false);
        if (order_.size() > 1 && order_.at(0) == previous) {
            std::swap(order_[0], order_[1]);
        }
        orderPosition_ = 0;
    }
    orderPosition_ = 0;
    return order_.at(orderPosition_);
}

int PlaybackQueue::next(bool automatic) {
    if (empty()) return -1;
    const int current = currentIndex();
    if (automatic && repeat_ == Repeat::One) return current;

    if (historyPosition_ + 1 < history_.size()) {
        return history_.at(++historyPosition_);
    }
    const int index = advanceOrder();
    if (index < 0) return -1;
    history_.push_back(index);
    historyPosition_ = history_.size() - 1;
    if (history_.size() > MaxHistory) {
        history_.remove(0, history_.size() - MaxHistory);
        historyPosition_ = history_.size() - 1;
    }
    return index;
}

int PlaybackQueue::previous() {
    if (empty()) return -1;
    if (!shuffle_) {
        int target = currentIndex() - 1;
        if (target < 0) {
            if (repeat_ != Repeat::All) return -1;
            target = items_.size() - 1;
        }
        history_.clear();
        history_.push_back(target);
        historyPosition_ = 0;
        orderPosition_ = target;
        return target;
    }
    if (historyPosition_ <= 0) return -1;
    return history_.at(--historyPosition_);
}

} // namespace xmb
