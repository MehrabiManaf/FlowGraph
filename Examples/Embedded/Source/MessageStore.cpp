#include "MessageStore.hpp"

#include <QDateTime>

namespace msgapp {

namespace {
const char* kUsers[] = {"alice", "bob", "carol", "dave", "erin"};
const char* kRoutes[] = {"orders", "users", "invoices", "reports", "search"};
const char* kFiles[] = {"report.csv", "backup.tar", "orders.db", "settings.json"};
}

QString RandomMessages::next() {
    // 70 % INFO, 20 % WARN, 10 % ERROR
    const int roll = between(0, 99);
    QString level, text;
    if (roll < 70) {
        level = "INFO ";
        switch (pick(5)) {
        case 0: text = QString("user %1 logged in").arg(kUsers[pick(5)]); break;
        case 1: text = QString("user %1 logged out").arg(kUsers[pick(5)]); break;
        case 2: text = QString("request GET /api/%1 took %2 ms").arg(kRoutes[pick(5)]).arg(between(5, 400)); break;
        case 3: text = QString("backup finished in %1 seconds").arg(between(20, 180)); break;
        default: text = QString("%1 exported: %2 rows").arg(kFiles[pick(4)]).arg(between(10, 5000)); break;
        }
    } else if (roll < 90) {
        level = "WARN ";
        switch (pick(3)) {
        case 0: text = QString("disk usage at %1 percent").arg(between(70, 95)); break;
        case 1: text = QString("slow request: GET /api/%1 took %2 ms").arg(kRoutes[pick(5)]).arg(between(1500, 6000)); break;
        default: text = QString("certificate expires in %1 days").arg(between(3, 30)); break;
        }
    } else {
        level = "ERROR";
        switch (pick(3)) {
        case 0: text = QString("failed to open /var/data/%1").arg(kFiles[pick(4)]); break;
        case 1: text = "database connection lost"; break;
        default: text = QString("out of memory while resizing image %1x%2").arg(between(1, 8) * 1024).arg(between(1, 8) * 1024); break;
        }
    }
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + " " + level + " " + text;
}

MessageStore::MessageStore(QObject* parent) : QObject(parent) {
    qRegisterMetaType<msgapp::MessagePtr>();
    connect(&timer_, &QTimer::timeout, this, [this] { addMessage(generator_.next()); });
}

MessagePtr MessageStore::addMessage(const QString& text) {
    auto m = std::make_shared<Message>();
    m->text = text.toStdString();
    m->line = nextLine_++;
    messages_.push_back(m);
    if (messages_.size() > kMaxMessages) messages_.erase(messages_.begin(), messages_.begin() + std::ptrdiff_t(messages_.size() - kMaxMessages));
    emit messageAdded(m);
    return m;
}

void MessageStore::clear() {
    messages_.clear();
    nextLine_ = 1;
    emit cleared();
}

void MessageStore::startGenerating(int intervalMs) { timer_.start(intervalMs); }
void MessageStore::stopGenerating() { timer_.stop(); }

} // namespace msgapp
