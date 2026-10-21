#include "PalettePopup.hpp"
#include "../Common/SearchField.hpp"

#include <QApplication>
#include <algorithm>
#include <map>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScreen>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fged {

PalettePopup::PalettePopup(GraphScene* scene, const PinRef& from, QWidget* parent) : QFrame(parent, Qt::Popup), scene_(scene), from_(from) {
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    search_ = new QLineEdit(this);
    makeSearchField(search_, from.valid() ? "Search nodes that fit the pin..." : "Search nodes...");      // like every search box of the editor
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(2);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(14);
    lay->addWidget(search_);
    lay->addWidget(tree_);
    resize(420, 460);

    const fg::Script& script = scene->document()->script();
    fg::NodeBuildCtx ctx{scene->registry(), &script, script.findGraph(scene->graphName().toStdString())};
    all_ = fg::buildPalette(ctx);
    if (from.valid()) {
        fg::PinFilter f;
        f.kind = from.kind;
        f.dir = from.dir;
        f.type = from.wildcard ? nullptr : from.type;
        all_ = fg::filterPalette(scene->registry(), all_, f);
    }
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) { accept(it); });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) { accept(it); });
    search_->installEventFilter(this);
    rebuild();
}

void PalettePopup::popup(const QPoint& screenPos) {
    QRect avail = QGuiApplication::screenAt(screenPos) ? QGuiApplication::screenAt(screenPos)->availableGeometry() : QRect(0, 0, 1920, 1080);
    QPoint p = screenPos;
    if (p.x() + width() > avail.right()) p.setX(avail.right() - width());
    if (p.y() + height() > avail.bottom()) p.setY(avail.bottom() - height());
    move(p);
    show();
    search_->setFocus();
}

void PalettePopup::rebuild() {
    tree_->clear();
    const QString q = search_->text().trimmed();

    // Which templates are shown, in which order. Without a query: everything sorted by category and title.
    // With a query: only the matches. They keep the category tree; the folders are ordered by their best match
    // and inside a folder the best match comes first.
    std::vector<size_t> order;
    if (q.isEmpty()) {
        order.resize(all_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (all_[a].category != all_[b].category) return all_[a].category < all_[b].category;
            return all_[a].title < all_[b].title;
        });
    } else {
        auto hits = fg::searchPalette(all_, ss(q));       // best match first
        std::vector<size_t> ranked;
        for (auto& h : hits)
            for (size_t i = 0; i < all_.size(); ++i)
                if (all_[i].defId == h.defId && all_[i].title == h.title && all_[i].props == h.props && all_[i].category == h.category) {
                    ranked.push_back(i);
                    break;
                }
        std::map<std::string, size_t> bestRank;           // category -> position of its best match
        for (size_t pos = 0; pos < ranked.size(); ++pos) bestRank.emplace(all_[ranked[pos]].category, pos);
        std::vector<size_t> position(all_.size(), 0);
        for (size_t pos = 0; pos < ranked.size(); ++pos) position[ranked[pos]] = pos;
        order = ranked;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            size_t ra = bestRank[all_[a].category], rb = bestRank[all_[b].category];
            if (ra != rb) return ra < rb;
            return position[a] < position[b];
        });
    }

    std::map<QString, QTreeWidgetItem*> folders;
    std::map<size_t, QTreeWidgetItem*> leafOf;
    for (size_t i : order) {
        QStringList path = qs(all_[i].category).split('|', Qt::SkipEmptyParts);
        QTreeWidgetItem* parent = nullptr;
        QString key;
        for (auto& part : path) {
            key += "|" + part;
            auto it = folders.find(key);
            if (it == folders.end()) {
                auto* f = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
                f->setText(0, part);
                f->setFlags(Qt::ItemIsEnabled);
                QFont font = f->font(0);
                font.setBold(true);
                f->setFont(0, font);
                it = folders.emplace(key, f).first;
            }
            parent = it->second;
        }
        const fg::NodeTemplate& t = all_[i];
        auto* leaf = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        leaf->setText(0, qs(t.title));
        leaf->setData(0, Qt::UserRole, qulonglong(i));
        leaf->setToolTip(0, qs(t.doc));
        leafOf[i] = leaf;
    }
    if (!q.isEmpty() || from_.valid()) tree_->expandAll();
    if (!q.isEmpty() && !order.empty()) {
        // the best match is the first leaf of the first folder
        QTreeWidgetItem* best = leafOf[order.front()];
        tree_->setCurrentItem(best);
        tree_->scrollToItem(best);
    }
}

void PalettePopup::accept(QTreeWidgetItem* item) {
    if (!item) return;
    QVariant v = item->data(0, Qt::UserRole);
    if (!v.isValid()) return;
    fg::NodeTemplate t = all_[size_t(v.toULongLong())];
    close();
    emit chosen(t);
}

bool PalettePopup::eventFilter(QObject* o, QEvent* e) {
    if (o == search_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        switch (ke->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
            QApplication::sendEvent(tree_, e);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept(tree_->currentItem());
            return true;
        case Qt::Key_Escape: close(); return true;
        default: break;
        }
    }
    return QFrame::eventFilter(o, e);
}

} // namespace fged
