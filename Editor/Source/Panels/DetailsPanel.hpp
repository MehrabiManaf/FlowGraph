#pragma once
#include "../Document.hpp"

#include <QWidget>

class QFormLayout;
class QTableWidget;

namespace fged {

// Property editor for whatever is selected: a variable, a function signature, a custom event or nodes.
class DetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailsPanel(Document* doc, QWidget* parent = nullptr);

    void showNothing();
    void showVariable(const QString& graphOwner, const QString& name);   // empty owner = global variable
    void showGraph(const QString& name);
    void showNodes(const QString& graph, const QList<int>& ids);

signals:
    void graphRenamed(const QString& from, const QString& to);

private:
    enum class Mode { None, Variable, Graph, Nodes };

    void rebuild();
    void clearForm();
    void edit(const QString& label, const std::function<void(fg::Script&)>& fn, const QString& key = QString(), bool rebuildAfter = false);

    void buildVariable();
    void buildGraph();
    void buildNodes();
    void buildEventParams(const fg::Node& node);
    QTableWidget* buildParamTable(bool inputs, const std::vector<fg::ParamDecl>& params, std::function<void(std::vector<fg::ParamDecl>)> apply);

    Document* doc_;
    Mode mode_ = Mode::None;
    QString owner_, name_;
    QList<int> ids_;
    QFormLayout* form_ = nullptr;
    QWidget* body_ = nullptr;
    bool selfEdit_ = false;
    bool rebuildQueued_ = false;
};

} // namespace fged
