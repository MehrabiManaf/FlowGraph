#include <FlowGraph/StandardApp.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fg::standard {

void registerStandardApp(Registry& reg) {
    reg.beginSource("standard");
    EventDesc& ev = reg.addEvent(kMainEvent, {{"Args", "array<string>"}});
    ev.doc = "Entry point of a standard application. Args holds the command line arguments of the script.";
    NodeDef d;
    d.id = "std.exit";
    d.title = "Exit";
    d.category = "Application";
    d.keywords = "quit terminate stop end program";
    d.doc = "Ends the script immediately and reports the exit code to the program that runs it.";
    d.style = NodeStyle::Flow;
    d.pins = {execIn(), dataIn("Exit Code", "int", Value(0))};
    d.run = [](NodeCtx& c) { c.exit(static_cast<int>(c.inInt(0))); };
    reg.addNode(std::move(d));
    reg.endSource();
}

Value argsValue(const Registry& reg, const std::vector<std::string>& args) {
    Value arr = reg.makeArray(reg.tString());
    for (auto& a : args) arr.arrayPtr()->items.push_back(Value(a));
    return arr;
}

RunOutcome runMain(Instance& inst, const std::vector<std::string>& args) {
    RunOutcome out;
    if (!inst.program().events.count(kMainEvent)) {
        out.exitCode = 1;
        out.error = "the script has no Main event";
        return out;
    }
    RunResult r = inst.callEvent(kMainEvent, {argsValue(inst.registry(), args)});
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    while (r.task && !r.task->done()) {
        if (r.task->state() == TaskState::Paused) {          // only possible with a debugger attached
            out.exitCode = 1;
            out.error = "the script was paused but no debugger is attached";
            return out;
        }
        double wake = inst.nextWakeIn();
        if (wake < 0) wake = 0.001;
        std::this_thread::sleep_for(std::chrono::duration<double>(wake));
        auto now = clock::now();
        inst.tick(std::chrono::duration<double>(now - last).count());
        last = now;
    }
    RunResult fin = r.task ? r.task->result() : r;
    out.exited = fin.exited;
    out.ok = fin.state == TaskState::Finished;
    out.error = fin.error;
    out.exitCode = fin.exited ? fin.exitCode : (out.ok ? 0 : 1);
    return out;
}

// ------------------------------------------------------------------ launch configurations
LaunchConfig* LaunchFile::find(const std::string& name) {
    for (auto& c : configs)
        if (c.name == name) return &c;
    return nullptr;
}
const LaunchConfig* LaunchFile::find(const std::string& name) const {
    for (auto& c : configs)
        if (c.name == name) return &c;
    return nullptr;
}

LaunchConfig& LaunchFile::current() {
    if (configs.empty()) configs.emplace_back();
    if (auto* c = find(selected)) return *c;
    selected = configs.front().name;
    return configs.front();
}

Json LaunchFile::toJson() const {
    Json j = Json::object();
    j.set("format", "flowlaunch");
    j.set("version", 1);
    j.set("selected", selected);
    Json list = Json::array();
    for (auto& c : configs) {
        Json cj = Json::object();
        cj.set("name", c.name);
        Json args = Json::array();
        for (auto& a : c.args) args.push(a);
        cj.set("args", std::move(args));
        if (!c.cwd.empty()) cj.set("cwd", c.cwd);
        if (!c.env.empty()) {
            Json env = Json::object();
            for (auto& [k, v] : c.env) env.set(k, v);
            cj.set("env", std::move(env));
        }
        if (!c.pluginPaths.empty()) {
            Json pp = Json::array();
            for (auto& p : c.pluginPaths) pp.push(p);
            cj.set("pluginPaths", std::move(pp));
        }
        if (c.stepDelayMs) cj.set("stepDelayMs", c.stepDelayMs);
        list.push(std::move(cj));
    }
    j.set("configurations", std::move(list));
    return j;
}

LaunchFile LaunchFile::fromJson(const Json& j) {
    if (!j.isObject() || j["format"].asString() != "flowlaunch") throw JsonError("not a FlowGraph launch file (missing \"format\": \"flowlaunch\")");
    LaunchFile f;
    f.selected = j["selected"].asString();
    for (auto& cj : j["configurations"].items()) {
        LaunchConfig c;
        c.name = cj["name"].asString("Default");
        for (auto& a : cj["args"].items()) c.args.push_back(a.asString());
        c.cwd = cj["cwd"].asString();
        for (auto& [k, v] : cj["env"].members()) c.env[k] = v.asString();
        for (auto& p : cj["pluginPaths"].items()) c.pluginPaths.push_back(p.asString());
        c.stepDelayMs = static_cast<int>(cj["stepDelayMs"].asInt());
        f.configs.push_back(std::move(c));
    }
    return f;
}

LaunchFile LaunchFile::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    LaunchFile f;
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        f = fromJson(Json::parse(ss.str()));
    }
    f.current();                                       // guarantees at least one configuration
    return f;
}

void LaunchFile::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << toJson().dump(2);
}

std::string LaunchFile::pathFor(const std::string& scriptPath) {
    std::filesystem::path p(scriptPath);
    p.replace_extension(".flowlaunch");
    return p.string();
}

} // namespace fg::standard
