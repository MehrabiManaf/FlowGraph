// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A node of a graph, on its canvas (GraphView): its header with its title, its pins in rows - execution pins first, inputs
// at the start, outputs at the end - with their connectors, names and, for an input without a link, its value; a reroute
// as a small pill. Drawn as the compiled node says (CompiledNode: its pins, types and values), where the script places it.
#include "Graph/GraphStyle.hpp"

#include <FlowGraph/Program.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/Animation.hpp>
#include <GLWidgets/Widgets/Widget.hpp>

#include <optional>
#include <string>
#include <vector>

namespace fged::gl {

class GraphView;

class NodeView final : public glw::Widget {
public:
    NodeView(GraphView& graph, fg::NodeId id);

    fg::NodeId id() const { return id_; }
    const fg::CompiledNode& compiled() const { return node_; }
    bool reroute() const { return reroute_; }
    // What it shows now: the compiled node, and what the compiler says of it (errors, warnings, their text).
    void setNode(const fg::CompiledNode& node, bool error, bool warning, std::string diagnostics);
    bool selected() const { return selected_; }
    // A run came to it: its border lit for a moment.
    void flash();
    bool flashing() const;
    void setSelected(bool selected);

    // The middle of a pin's connector (its own coordinates); none when it has no such pin (shown).
    std::optional<glw::Point> pinCenter(const std::string& name, fg::PinDir dir) const;
    struct PinHit {
        std::string name;
        fg::PinDir dir = fg::PinDir::In;
        fg::PinKind kind = fg::PinKind::Data;
    };
    // The pin at `point` (its own coordinates): its connector or its name.
    std::optional<PinHit> pinAt(glw::Point point) const;
    // The box of an input's value at `point` (its own coordinates): what is clicked to change the value.
    struct ValueHit {
        enum class Kind : std::uint8_t { Check, Line, Choice } kind = Kind::Line;
        std::string pin;
        glw::Rect rect;                     // its own coordinates
        std::string text;                   // as the box shows it (Check: "x" when true)
        const fg::TypeDesc* type = nullptr;
        fg::Value value;
    };
    std::optional<ValueHit> valueAt(glw::Point point) const;
    // Where the box of an input's value is (its own coordinates); none when it has none (a link, no box for its type).
    std::optional<glw::Rect> valueRect(const std::string& pin) const;
    // What is being typed in the box of `pin` (none: its value): the box as wide as it, the node with it.
    void setValuePreview(const std::string& pin, std::optional<std::string> text);
    // How wide the box of a value is for `text` (of a choice: with its arrow), in dips.
    static float valueBoxWidth(const glw::UiContext& ui, const std::string& text, bool choice);

protected:
    glw::Size measureContent(glw::Size available) override;
    void paint(glw::PaintContext& context) override;
    void onMouse(glw::MouseEvent& event) override;
    void onHoverChanged(bool hovered) override;
    void onStyleChanged() override;
    void onRootChanged() override { onStyleChanged(); }
    std::optional<glw::ToolTip> toolTipAt(glw::Point point) const override;

private:
    enum class Editor : std::uint8_t { None, Check, Line, Choice };
    struct Pin {
        std::string name;
        std::string label;
        fg::PinKind kind = fg::PinKind::Data;
        fg::PinDir dir = fg::PinDir::In;
        glw::Color color, keyColor;
        int container = 0;              // 0: a single value, 1: an array, 2: a map
        bool connected = false, byRef = false, isConst = false, wildcard = false;
        Editor editor = Editor::None;
        std::string value;              // its value's text (Line, Choice); Check: "x" when true
        std::string typeName;
        const fg::TypeDesc* type = nullptr;
        fg::Value def;
        std::string doc;
        glw::Rect connector, labelRect, editorRect, lockRect;
        std::optional<glw::text::TextLayout> labelText, valueText;
    };

    void layout();                      // the pins and the rectangles, from the compiled node (with the fonts of its root)
    void layoutReroute();
    std::vector<const Pin*> pins() const;
    const Pin* pinAtPoint(glw::Point point) const;
    glw::Rect bodyRect() const;

    GraphView& graph_;
    fg::NodeId id_;
    fg::CompiledNode node_;
    bool reroute_ = false;
    bool error_ = false, warning_ = false;
    std::string diagnostics_;
    bool selected_ = false;
    bool laidOut_ = false;

    std::string title_;
    glw::Color header_;
    std::optional<glw::text::TextLayout> titleText_;
    glw::Rect titleRect_;
    float width_ = 0, height_ = 0;
    std::vector<Pin> inputs_, outputs_;     // execution pins first, each
    std::optional<PinHit> hovered_;
    std::optional<glw::TimePoint> flashed_;
    std::string previewPin_;                // the box being typed in, and what is typed
    std::optional<std::string> previewText_;         // the pin under the mouse
    bool hoveredBody_ = false;
};

} // namespace fged::gl
