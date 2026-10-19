// The System nodes (System.hpp): Run Command, For Each Output Line, Operating System. Not part of registerBuiltins - an application
// registers them when its scripts may run programs.
#include <FlowGraph/System.hpp>
#include <FlowGraph/Vm.hpp>

#include "Process.hpp"

#include <algorithm>
#include <chrono>

namespace fg {

namespace {

using detail::ChildProcess;
using detail::ProcessOptions;
using Clock = std::chrono::steady_clock;

enum class OperatingSystem { Windows, Linux, macOS, Other };

// Inputs 0..3 of both nodes: Program, Arguments, Working Directory, Use Shell.
ProcessOptions optionsOf(NodeCtx& c) {
    ProcessOptions o;
    o.program = c.inString(0);
    if (auto* a = c.in(1).arrayPtr())
        for (auto& v : a->items) o.args.push_back(v.isString() ? v.str() : c.registry().toString(v));
    o.workingDirectory = c.inString(2);
    o.shell = c.inBool(3);
    return o;
}

void addProgramPins(NodeDef& d) {
    d.pins.push_back(dataIn("Program", "string", Value("")));
    d.pins.push_back(reads(dataIn("Arguments", "array<string>")));
    d.pins.push_back(dataIn("Working Directory", "string", Value("")));
    d.pins.push_back(dataIn("Use Shell", "bool", Value(false)));
    d.pins[1].doc = "A program (searched for in PATH), or a command line of the system shell when Use Shell is on";
    d.pins[2].doc = "Each one reaches the program as it is: spaces and quotes need no quoting (with Use Shell they are added to the command line, quoted)";
    d.pins[3].doc = "Where the program runs; empty: the current directory";
    d.pins[4].doc = "Run Program with the system shell (cmd.exe on Windows, /bin/sh elsewhere): its own commands like dir or echo, "
                    "pipes, redirections and variables work";
}

const char* kProgramDoc =
    " The program's standard input is empty. A program that writes to a pipe instead of a terminal may hold its output back until "
    "its buffer is full. Text that is not UTF-8 is read in the OEM code page on Windows (the one of cmd.exe's own commands). A program that "
    "cannot be started (not found, no such working directory) is a script error. The program never outlives the script: stopping it, a hot "
    "reload or the end of the instance kill it and everything it started.";

// Run Command: the program and, while it runs, its deadline.
struct RunState {
    std::unique_ptr<ChildProcess> process;
    bool timed = false;
    Clock::time_point deadline;
    bool expired() const { return timed && Clock::now() >= deadline; }
    bool ready() { return process->finished() || expired(); }
};

} // namespace

void registerSystemNodes(Registry& reg) {
    const std::string previousSource = reg.source();
    reg.beginSource("system");

    reg.registerEnum<OperatingSystem>("OperatingSystem", "System")
        .value("Windows", OperatingSystem::Windows).value("Linux", OperatingSystem::Linux).value("macOS", OperatingSystem::macOS)
        .value("Other", OperatingSystem::Other);
    reg.addFunction("System.Operating System", [] {
#if defined(_WIN32)
        return OperatingSystem::Windows;
#elif defined(__APPLE__)
        return OperatingSystem::macOS;
#elif defined(__linux__)
        return OperatingSystem::Linux;
#else
        return OperatingSystem::Other;
#endif
    }).makePure().withResultName("System").withKeywords("os platform windows linux mac macos")
      .withDoc("The operating system the script runs on: a Switch on Enum picks the command for each one.");

    {
        NodeDef d;
        d.id = "system.run";
        d.title = "Run Command";
        d.category = "System";
        d.keywords = "shell command process execute program exec spawn terminal cmd bash system";
        d.doc = std::string("Runs a program to its end and hands out what it wrote and its exit code (usually 0 for success). The script "
                            "waits for it; the application does not. Error Output is what the program wrote to its standard error. Each "
                            "output keeps up to 16 MiB; its line breaks are \\n on every platform.") + kProgramDoc;
        d.waits = true;
        d.pins.push_back(execIn());
        addProgramPins(d);
        d.pins.push_back(dataIn("Timeout", "float", Value(0.0)));
        d.pins.back().doc = "Seconds the program may run; then it is killed and Timed Out is on. 0: no limit";
        d.pins.push_back(execOut());
        d.pins.push_back(dataOut("Exit Code", "int"));
        d.pins.push_back(dataOut("Output", "string"));
        d.pins.push_back(dataOut("Error Output", "string"));
        d.pins.push_back(dataOut("Timed Out", "bool"));
        d.pins[7].doc = "-1 when the program was killed because of the timeout";
        d.run = [](NodeCtx& c) {
            std::shared_ptr<RunState> s;
            if (c.resumed()) s = std::static_pointer_cast<RunState>(c.native());
            else {
                s = std::make_shared<RunState>();
                s->process = std::make_unique<ChildProcess>(optionsOf(c), ChildProcess::Mode::Collect);
                const double timeout = c.inFloat(4);
                if (timeout > 0) {
                    s->timed = true;
                    s->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(std::min(timeout, 1e9)));
                }
            }
            if (!s->ready()) {
                c.native() = s;
                c.waitThenResume([w = std::weak_ptr<RunState>(s)](double) {
                    auto p = w.lock();
                    return !p || p->ready();
                });
                return;
            }
            const bool timedOut = !s->process->finished();
            if (timedOut) s->process->kill();
            c.out(0, Value(static_cast<int64_t>(timedOut ? -1 : s->process->exitCode())));
            c.out(1, Value(s->process->output()));
            c.out(2, Value(s->process->errorOutput()));
            c.out(3, Value(timedOut));
            if (s->process->truncated()) c.log(LogLevel::Warning, "Run Command: the program wrote more than 16 MiB; the rest was left out");
        };
        reg.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "system.lines";
        d.title = "For Each Output Line";
        d.category = "System";
        d.style = NodeStyle::Flow;
        d.keywords = "shell command process execute program stream tail follow watch log line read output terminal cmd bash";
        d.doc = std::string("Runs a program and the loop body for each line it writes, as the lines come (tail -f, a log, a build): the "
                            "node waits for the next line, the application does not. Lines of the standard output and of the standard "
                            "error come in the order they were read. Completed when the program has ended and every line was handled; "
                            "a Break ends the loop and kills the program. Exit Code is -1 until the program has ended (and after a "
                            "Break).") + kProgramDoc;
        d.waits = true;
        d.breakPin = 0;
        d.pins.push_back(execIn());
        addProgramPins(d);
        d.pins.push_back(execOut("Completed"));
        d.pins.push_back(execOut("Loop Body"));
        d.pins.push_back(dataOut("Line", "string"));
        d.pins.push_back(dataOut("From Error Output", "bool"));
        d.pins.push_back(dataOut("Exit Code", "int"));
        d.pins[7].doc = "A line, without its line break";
        d.pins[8].doc = "The line came from the standard error";
        // native(): the ChildProcess. It goes when the node is done with it - Completed, a Break (the loop's continuation is dropped),
        // the task aborted or failed (Task::releaseNatives) - and its destructor kills the program.
        d.run = [](NodeCtx& c) {
            std::shared_ptr<ChildProcess> p;
            if (c.resumed()) p = std::static_pointer_cast<ChildProcess>(c.native());
            else {
                p = std::make_shared<ChildProcess>(optionsOf(c), ChildProcess::Mode::Lines);
                c.out(2, Value(int64_t(-1)));
            }
            ChildProcess::Line line;
            if (p->nextLine(line)) {
                c.out(0, Value(std::move(line.text)));
                c.out(1, Value(line.error));
                c.native() = p;
                c.call(1);
                return;
            }
            if (p->finished()) {
                c.out(2, Value(static_cast<int64_t>(p->exitCode())));
                c.next(0);
                return;
            }
            c.native() = p;
            c.waitThenResume([w = std::weak_ptr<ChildProcess>(p)](double) {
                auto q = w.lock();
                return !q || q->hasLine() || q->finished();
            });
        };
        reg.addNode(std::move(d));
    }

    reg.beginSource(previousSource);
}

} // namespace fg
