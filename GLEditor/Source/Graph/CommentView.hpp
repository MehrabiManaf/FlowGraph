// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Graph/GraphStyle.hpp"
#include "Graph/GraphView.hpp"

#include <FlowGraph/Script.hpp>
#include <GLWidgets/Paint/Painter.hpp>
#include <GLWidgets/Text/Font.hpp>
#include <GLWidgets/Text/TextLayout.hpp>
#include <GLWidgets/Widgets/UiRoot.hpp>

#include <algorithm>
#include <optional>
#include <string>

namespace fged::gl {

constexpr float commentHeader = 26;     // a comment's header: what moves it
constexpr float commentGrip = 16;       // its corner: what resizes it

// A comment of the graph: a colored frame behind a group of nodes, its text in its header. Its header moves it (with the
// nodes in it), its corner resizes it; the rest of it is the canvas's (a click there selects with a rectangle).
class CommentView final : public glw::Widget {
public:
    CommentView(GraphView& graph, const fg::CommentBox& box) : graph_(graph), box_(box) {}

    const fg::CommentBox& box() const { return box_; }
    void setSize(float w, float h) {
        box_.w = w;
        box_.h = h;
        invalidateLayout();
    }
    void setSelected(bool selected) {
        if (selected != selected_) {
            selected_ = selected;
            invalidatePaint();
        }
    }

protected:
    glw::Size measureContent(glw::Size) override { return {float(box_.w), float(box_.h)}; }

    void paint(glw::PaintContext& context) override {
        glw::paint::Painter& p = context.painter;
        const glw::Rect r{0, 0, size().width, size().height};
        const float radius = std::min(context.ui.theme.node.cornerRadius, commentHeader / 2);     // as the theme's nodes are rounded
        p.fillRoundedRect(r, radius, rgb(box_.color, 0.22f));
        p.pushClip({0, 0, r.width, commentHeader});
        p.fillRoundedRect({0, 0, r.width, commentHeader + radius}, radius, rgb(box_.color, 0.75f));
        p.popClip();
        p.strokeRoundedRect(r, radius, selected_ ? 2.0f : 1.0f, selected_ ? context.ui.theme.node.selection : rgb(box_.color, 0.9f));
        if (!text_ || shown_ != box_.text) {
            shown_ = box_.text;
            text_.emplace(box_.text, glw::text::TextStyle{{&context.ui.fonts.defaultFont()}, context.ui.theme.fontSize + 1});
        }
        p.pushClip({6, 0, std::max(0.0f, r.width - 12), commentHeader});
        context.drawText(*text_, {8, (commentHeader - text_->size().height) / 2}, {1, 1, 1, 1});
        p.popClip();
        // the grip that resizes it, at its bottom right corner
        for (int i = 1; i <= 3; ++i)
            p.drawLine({r.width - 4 - 4.0f * i, r.height - 4}, {r.width - 4, r.height - 4 - 4.0f * i}, 1, {1, 1, 1, 0.35f});
    }

    void onMouse(glw::MouseEvent& event) override {
        const bool corner = glw::Rect{size().width - commentGrip, size().height - commentGrip, commentGrip, commentGrip}.contains(event.position);
        const bool header = event.position.y < commentHeader;
        if (event.type == glw::MouseEventType::Press && event.button == glw::platform::MouseButton::Left && !corner && !header)
            return;     // the canvas's
        if (event.type == glw::MouseEventType::Press && event.button != glw::platform::MouseButton::Left)
            return;
        if (event.type == glw::MouseEventType::Wheel)
            return;
        graph_.commentMouse(*this, event, corner ? 1 : 0);
    }

    std::optional<glw::platform::Cursor> cursorAt(glw::Point point) const override {
        if (glw::Rect{size().width - commentGrip, size().height - commentGrip, commentGrip, commentGrip}.contains(point))
            return glw::platform::Cursor::ResizeDiagonal;
        return std::nullopt;
    }

private:
    GraphView& graph_;
    fg::CommentBox box_;
    bool selected_ = false;
    std::optional<glw::text::TextLayout> text_;
    std::string shown_;
};

} // namespace fged::gl
