// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// A list of handlers called with the same arguments: how the editor core tells its editor (Qt's, GLWidgets') what happened,
// without depending on either toolkit's own signals.
#include <functional>
#include <utility>
#include <vector>

namespace fged::core {

template <class... Args>
class Signal {
public:
    using Handler = std::function<void(Args...)>;

    // Returns an id for disconnect().
    int connect(Handler handler) {
        handlers_.emplace_back(++lastId_, std::move(handler));
        return lastId_;
    }
    // A handler called before those connected before it: for what must react before the views do (a run stopped before
    // the debug panel shows what is left of it).
    int connectFirst(Handler handler) {
        handlers_.emplace(handlers_.begin(), ++lastId_, std::move(handler));
        return lastId_;
    }
    void disconnect(int id) {
        std::erase_if(handlers_, [id](const auto& h) { return h.first == id; });
    }
    // Calls the handlers in the order they were connected. A handler may connect or disconnect others meanwhile: those
    // connected when it was emitted are called.
    void operator()(Args... args) const {
        const auto handlers = handlers_;
        for (auto& [id, h] : handlers) h(args...);
    }

private:
    std::vector<std::pair<int, Handler>> handlers_;
    int lastId_ = 0;
};

} // namespace fged::core
