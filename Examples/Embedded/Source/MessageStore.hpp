#pragma once
// The message store: the text messages the application has received. Messages arrive one at a time (addMessage); this example makes them up:
// startGenerating() produces a random log-like message at a fixed interval (every kMessageIntervalMs, two per second, by default), as if they came from a source that never
// stops. The store keeps the latest kMaxMessages (so that a script that is replaced can run over what has been received), and tells everybody
// about every new message: the tables run their scripts for it.
#include "MessageScripting.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <random>

namespace msgapp {

inline constexpr size_t kMaxMessages = 10000;
inline constexpr int kMessageIntervalMs = 500;          // the generated source: two messages per second

// Makes up messages like "2026-10-17 10:02:11 ERROR failed to open /var/data/report.csv". The same seed gives the same texts (the time stamp aside).
class RandomMessages {
public:
    explicit RandomMessages(unsigned seed = std::random_device{}()) : rng_(seed) {}
    QString next();

private:
    int pick(int n) { return int(std::uniform_int_distribution<int>(0, n - 1)(rng_)); }
    int between(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    std::mt19937 rng_;
};

class MessageStore : public QObject {
    Q_OBJECT
public:
    explicit MessageStore(QObject* parent = nullptr);

    // Appends a message (the lines are numbered 1, 2, 3... in the order they come) and tells everybody about it.
    MessagePtr addMessage(const QString& text);
    // The messages received so far (the latest kMaxMessages); they never change, so the list can be handed to other threads.
    const std::vector<MessagePtr>& messages() const { return messages_; }
    int total() const { return nextLine_ - 1; }                   // all messages ever received, also the ones that were dropped
    void clear();                                                 // forget everything; the numbering starts again

    // The source of this example: a random message every `intervalMs`.
    void startGenerating(int intervalMs = kMessageIntervalMs);
    void stopGenerating();
    bool generating() const { return timer_.isActive(); }
    void seed(unsigned seed) { generator_ = RandomMessages(seed); }

signals:
    void messageAdded(msgapp::MessagePtr message);
    void cleared();

private:
    std::vector<MessagePtr> messages_;
    int nextLine_ = 1;
    QTimer timer_;
    RandomMessages generator_;
};

} // namespace msgapp

Q_DECLARE_METATYPE(msgapp::MessagePtr)
