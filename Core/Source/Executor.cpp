#include <FlowGraph/Executor.hpp>

#include <algorithm>
#include <chrono>

namespace fg {

// ====================================================================== Executor
Executor::Executor(std::shared_ptr<const Program> program, DebugHooks* hooks) : program_(std::move(program)) {
    inst_ = std::make_unique<Instance>(program_);
    if (hooks) inst_->setDebugHooks(hooks);
}

Executor::Handle Executor::add(std::shared_ptr<Task> t, const std::string& name, Handle forced) {
    Entry e;
    e.handle = forced ? forced : next_++;
    if (forced && forced >= next_) next_ = forced + 1;
    e.name = name;
    e.task = std::move(t);
    entries_.push_back(std::move(e));
    return entries_.back().handle;
}

Executor::Handle Executor::postEvent(const std::string& name, std::vector<Value> args, std::string* error, Handle forced) {
    auto t = inst_->startEvent(name, std::move(args), error);
    return t ? add(std::move(t), name, forced) : 0;
}

Executor::Handle Executor::postFunction(const std::string& name, std::vector<Value> args, std::string* error, Handle forced) {
    auto t = inst_->startFunction(name, std::move(args), error);
    return t ? add(std::move(t), name, forced) : 0;
}

bool Executor::hasRunnable() const {
    for (auto& e : entries_) {
        TaskState s = e.task->state();
        if (s == TaskState::Ready || (s == TaskState::Paused && e.resume)) return true;
    }
    return false;
}

void Executor::finish(size_t index) {
    Completion c;
    c.handle = entries_[index].handle;
    c.name = entries_[index].name;
    c.result = entries_[index].task->result();
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    if (completion_) completion_(c);
}

Executor::PumpStats Executor::pump(double dt, uint64_t nodesPerTask) {
    PumpStats st;
    for (auto& t : inst_->advanceWaits(dt))
        if (wake_) wake_(handleOf(t.get()));
    // work on a snapshot of the handles: completion handlers may post or abort tasks
    std::vector<Handle> order;
    for (auto& e : entries_) order.push_back(e.handle);
    for (Handle h : order) {
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.handle == h; });
        if (it == entries_.end()) continue;
        TaskState s = it->task->state();
        if (s == TaskState::Ready || (s == TaskState::Paused && it->resume)) {
            it->resume = false;
            std::shared_ptr<Task> task = it->task;
            task->run(nodesPerTask);
            ++st.ran;
        }
        it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.handle == h; });
        if (it != entries_.end() && it->task->done()) finish(static_cast<size_t>(it - entries_.begin()));
    }
    for (auto& e : entries_) {
        TaskState s = e.task->state();
        if (s == TaskState::Waiting) ++st.waiting;
        else if (s == TaskState::Paused) ++st.paused;
    }
    st.nextWake = inst_->nextWakeIn();
    st.runnable = hasRunnable();
    return st;
}

void Executor::resume(Handle h) {
    for (auto& e : entries_)
        if (e.handle == h) e.resume = true;
}

void Executor::abort(Handle h) {
    for (auto& e : entries_)
        if (e.handle == h) e.task->abort();
    // aborted tasks are reported by the next pump()
}

void Executor::abortAll() {
    for (auto& e : entries_) e.task->abort();
}

std::shared_ptr<Task> Executor::task(Handle h) const {
    for (auto& e : entries_)
        if (e.handle == h) return e.task;
    return nullptr;
}

Executor::Handle Executor::handleOf(const Task* t) const {
    for (auto& e : entries_)
        if (e.task.get() == t) return e.handle;
    return 0;
}

// ====================================================================== AsyncExecutor
AsyncExecutor::AsyncExecutor(std::shared_ptr<const Program> program, Callbacks callbacks, Debugger* debugger)
    : debugger_(debugger), cb_(std::move(callbacks)), exec_(std::move(program), nullptr) {
    if (!debugger_) {
        ownedDebugger_ = std::make_unique<Debugger>();
        debugger_ = ownedDebugger_.get();
    }
    exec_.instance().setDebugHooks(debugger_);
    if (cb_.log) exec_.instance().setLogHandler(cb_.log);
    debugger_->linkFired = cb_.linkFired;
    debugger_->taskStateChanged = [this](Task& t) {
        if (cb_.taskState) cb_.taskState(exec_.handleOf(&t), t.state());
    };
    exec_.setCompletionHandler([this](const Executor::Completion& c) {
        if (cb_.completed) cb_.completed(c);
    });
    exec_.setWakeHandler([this](Handle h) {
        if (cb_.taskState) cb_.taskState(h, TaskState::Running);
    });
    thread_ = std::thread([this] { run(); });
}

AsyncExecutor::~AsyncExecutor() {
    stop();
    // the debugger outlives us when it is external: do not leave callbacks that point to this object
    debugger_->linkFired = nullptr;
    debugger_->taskStateChanged = nullptr;
}

void AsyncExecutor::post(std::function<void()> cmd) {
    {
        std::lock_guard lk(mu_);
        if (stop_) return;
        idle_ = false;
        cmds_.push_back(std::move(cmd));
    }
    cv_.notify_all();
}

AsyncExecutor::Handle AsyncExecutor::postEvent(const std::string& name, std::vector<Value> args) {
    Handle h = nextHandle_++;
    post([this, h, name, args = std::move(args)]() mutable {
        std::string err;
        if (!exec_.postEvent(name, std::move(args), &err, h)) {
            Executor::Completion c;
            c.handle = h;
            c.name = name;
            c.result.state = TaskState::Failed;
            c.result.error = err;
            if (cb_.completed) cb_.completed(c);
        }
    });
    return h;
}

AsyncExecutor::Handle AsyncExecutor::postFunction(const std::string& name, std::vector<Value> args) {
    Handle h = nextHandle_++;
    post([this, h, name, args = std::move(args)]() mutable {
        std::string err;
        if (!exec_.postFunction(name, std::move(args), &err, h)) {
            Executor::Completion c;
            c.handle = h;
            c.name = name;
            c.result.state = TaskState::Failed;
            c.result.error = err;
            if (cb_.completed) cb_.completed(c);
        }
    });
    return h;
}

void AsyncExecutor::pause(Handle h) {
    post([this, h] {
        if (auto t = exec_.task(h)) debugger_->command(*t, Debugger::Command::Pause);
    });
}

void AsyncExecutor::resume(Handle h) {
    post([this, h] {
        if (auto t = exec_.task(h)) {
            debugger_->command(*t, Debugger::Command::Continue);
            exec_.resume(h);
        }
    });
}

void AsyncExecutor::step(Handle h, Debugger::Command cmd) {
    post([this, h, cmd] {
        if (auto t = exec_.task(h)) {
            debugger_->command(*t, cmd);
            exec_.resume(h);
        }
    });
}

void AsyncExecutor::abort(Handle h) {
    post([this, h] { exec_.abort(h); });         // slices are short, so the worker sees this soon
}

void AsyncExecutor::abortAll() {
    post([this] { exec_.abortAll(); });
}

void AsyncExecutor::setStepDelay(int ms) {
    delayMs_ = std::max(0, ms);
    cv_.notify_all();
}

bool AsyncExecutor::inspect(const std::function<void(Executor&)>& fn) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    {
        std::lock_guard lk(mu_);
        if (stop_) return false;
        cmds_.push_back([this, &fn, promise] {
            fn(exec_);
            promise->set_value();
        });
    }
    cv_.notify_all();
    try {
        future.get();
    } catch (const std::future_error&) {      // the worker stopped before it got to this request
        return false;
    }
    return true;
}

bool AsyncExecutor::waitIdle(int timeoutMs) {
    std::unique_lock lk(mu_);
    auto done = [this] { return idle_.load() || stop_; };
    if (timeoutMs < 0) {
        idleCv_.wait(lk, done);
        return true;
    }
    return idleCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), done);
}

void AsyncExecutor::stop() {
    {
        std::lock_guard lk(mu_);
        if (stop_) return;
        stop_ = true;
    }
    exec_.instance().requestAbort();
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    idleCv_.notify_all();
}

void AsyncExecutor::run() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    bool runnable = false;
    double nextWake = -1;
    for (;;) {
        std::deque<std::function<void()>> batch;
        {
            std::unique_lock lk(mu_);
            auto wake = [this] { return stop_ || !cmds_.empty(); };
            if (!stop_ && cmds_.empty()) {
                int delay = delayMs_.load();
                if (runnable) {
                    if (delay > 0) cv_.wait_for(lk, std::chrono::milliseconds(delay), wake);      // slow motion, interruptible
                } else if (nextWake >= 0) {
                    cv_.wait_for(lk, std::chrono::duration<double>(std::max(0.0005, nextWake)), wake);
                } else {
                    idle_ = exec_.idle();
                    if (idle_) idleCv_.notify_all();
                    cv_.wait(lk, wake);
                }
            }
            if (stop_) break;
            batch.swap(cmds_);
        }
        for (auto& c : batch) c();

        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        auto stats = exec_.pump(dt, delayMs_.load() > 0 ? 1 : 2000);
        runnable = stats.runnable || stats.ran > 0;
        nextWake = stats.nextWake;
        bool idleNow;
        {
            std::lock_guard lk(mu_);
            idleNow = cmds_.empty() && exec_.idle();
            idle_ = idleNow;
        }
        if (idleNow) idleCv_.notify_all();
    }
    {   // requests that were never started: dropping them releases callers that wait for an inspect()
        std::lock_guard lk(mu_);
        cmds_.clear();
    }
    // shutdown: end every task so that completion handlers see them
    exec_.abortAll();
    exec_.instance().clearAbort();
    exec_.pump(0, 1);
    idle_ = true;
    idleCv_.notify_all();
}

} // namespace fg
