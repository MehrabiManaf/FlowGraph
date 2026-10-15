#pragma once
#include "GraphScene.hpp"

#include <QFrame>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace fged {

// Searchable list of the nodes that can be created in a graph. When opened by dragging from a pin only
// the nodes that can be connected to that pin are listed.
class PalettePopup : public QFrame {
    Q_OBJECT
public:
    PalettePopup(GraphScene* scene, const PinRef& from, QWidget* parent = nullptr);
    void popup(const QPoint& screenPos);

signals:
    void chosen(const fg::NodeTemplate& t);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void rebuild();
    void accept(QTreeWidgetItem* item);

    GraphScene* scene_;
    PinRef from_;
    std::vector<fg::NodeTemplate> all_;
    QLineEdit* search_;
    QTreeWidget* tree_;
};

} // namespace fged
