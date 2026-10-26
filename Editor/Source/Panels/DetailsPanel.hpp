// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Document.hpp"

#include <QSet>
#include <QWidget>

class QFormLayout;
class QLabel;
class QLineEdit;
class QTableWidget;
class CCollapsiblePanel;

namespace fged {

// Property editor for whatever is selected: a variable, a function signature, a struct, a custom event or nodes.
class DetailsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DetailsPanel(Document* doc, QWidget* parent = nullptr);

    void showNothing();
    void showVariable(const QString& graphOwner, const QString& name);   // empty owner = global variable
    void showGraph(const QString& name);
    void showStruct(const QString& name);
    void showNodes(const QString& graph, const QList<int>& ids);

signals:
    void graphRenamed(const QString& from, const QString& to);

private:
    enum class Mode { None, Variable, Graph, Struct, Nodes };

    void rebuild();
    bool shownExists() const;  // the variable / function / struct shown is still in the script
    void applyFilter();         // hides the rows that do not contain the search text
    void clearForm();
    void edit(const QString& label, const std::function<void(fg::Script&)>& fn, const QString& key = QString(), bool rebuildAfter = false);

    QWidget* valueEditor(const fg::TypeDesc* t, const fg::Value& stored, std::function<void(const fg::Value&)> set);
    void buildVariable();
    void buildGraph();
    void buildStruct();
    void buildGraphFor(const QString& graphName);
    void buildNodes();
    void buildNodePins(const fg::Node& node, const fg::CompiledNode& cn, const QString& graph);
    // A section (Inputs, Outputs, Fields) opens as the user left the section of that `key` the last time - whatever is shown,
    // another node or another function - and is remembered in the settings.
    void openAsBefore(CCollapsiblePanel* panel, const QString& key);
    // What the "+" of a section adds, at once; a closed section then opens, animated (after openAsBefore).
    void addOnPlus(CCollapsiblePanel* panel, std::function<void()> add);
    QTableWidget* buildParamTable(bool inputs, const std::vector<fg::ParamDecl>& params, std::function<void(std::vector<fg::ParamDecl>)> apply);

    Document* doc_;
    Mode mode_ = Mode::None;
    QString owner_, name_;
    QString graphTarget_;      // graph shown by the function editor (name_ or owner_)
    QList<int> ids_;
    QSet<QString> closedSections_;   // the keys of openAsBefore the user closed
    QSet<QString> openingSections_;  // closed sections whose "+" was pressed: they open animated when built again
    int fieldRow_ = 0;         // the field of the struct whose default and description are shown
    QFormLayout* form_ = nullptr;
    QWidget* body_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* noMatch_ = nullptr;
    bool selfEdit_ = false;
    bool rebuildQueued_ = false;
};

} // namespace fged
