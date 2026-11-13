// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#include "Document.hpp"

#include "Common/NodeTexts.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace fged {

Document::Document(fg::Runtime& rt, QObject* parent) : QObject(parent), core_(rt) {
    core_.setBeforeCompile([&rt] { loadPluginTranslations(rt); });      // plugins loaded since (a launch configuration's) bring their texts
    loadPluginTranslations(rt);
    core_.changed.connect([this](bool structural) { emit changed(structural); });
    core_.compiled.connect([this] { emit compiled(); });
    core_.scriptReplaced.connect([this] { emit scriptReplaced(); });
    core_.scriptOpened.connect([this] { emit scriptOpened(); });
    core_.titleChanged.connect([this] { emit titleChanged(); });
    core_.undoStateChanged.connect([this] { emit undoStateChanged(); });
}

QString Document::displayName() const {
    const fg::Script& s = core_.script();
    QString n = !core_.path().empty()    ? QFileInfo(path()).fileName()
                : !core_.title().empty() ? QString::fromStdString(core_.title())
                : s.name == "Untitled"   ? QCoreApplication::translate("fged::Document", "Untitled")       // the new script's name, in the editor's language
                                         : QString::fromStdString(s.name);
    return isDirty() ? n + "*" : n;
}

QString Document::graphTitle(const std::string& name) {
    return name == "EventGraph" ? QCoreApplication::translate("fged::Document", "EventGraph") : QString::fromStdString(name);
}

QString Document::structNameProblem(const QString& name, const QString& current) const {
    switch (core_.structNameProblem(name.toStdString(), current.toStdString())) {
    case core::StructNameProblem::NotAName: return QCoreApplication::translate("fged::Document", "A struct name is one word, without spaces or < > ,");
    case core::StructNameProblem::StructExists: return QCoreApplication::translate("fged::Document", "A struct with that name exists.");
    case core::StructNameProblem::TypeExists: return QCoreApplication::translate("fged::Document", "A type of this application has that name.");
    case core::StructNameProblem::None: break;
    }
    return QString();
}

bool Document::load(const QString& path, QString* error) {
    std::string e;
    const bool ok = core_.load(path.toStdString(), &e);
    if (!ok && error) *error = QString::fromStdString(e);
    return ok;
}

bool Document::save(const QString& path, QString* error) {
    std::string e;
    const bool ok = core_.save(path.toStdString(), &e);
    if (!ok && error) *error = QString::fromStdString(e);
    return ok;
}

bool Document::exportTo(const QString& path, QString* error) const {
    std::string e;
    const bool ok = core_.exportTo(path.toStdString(), &e);
    if (!ok && error) *error = QString::fromStdString(e);
    return ok;
}

} // namespace fged
