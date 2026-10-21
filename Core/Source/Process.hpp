// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// A program started by the System nodes (System.hpp), with its output read by threads of its own so the script never blocks.
// Internal to the core library.
//
// The program never outlives its ChildProcess: the destructor kills it and everything it started (Windows: a job object that kills
// on close; elsewhere: a process group of its own, killed as a whole). Its standard input is empty (NUL, /dev/null).
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fg::detail {

struct ProcessOptions {
    std::string program;                // a program (searched for in PATH) or, with `shell`, a command line of the system shell
    std::vector<std::string> args;      // appended, each quoted for the program / the shell
    std::string workingDirectory;       // empty: the current one
    bool shell = false;                 // cmd.exe /d /s /c on Windows, /bin/sh -c elsewhere
};

class ChildProcess {
public:
    enum class Mode : uint8_t {
        Collect,        // the output is gathered (Output, Error Output), up to kMaxOutput bytes each
        Lines,          // the output is split into lines as it comes; the program is held back while unread lines pile up
    };
    struct Line {
        std::string text;               // without the line break
        bool error = false;             // from the error output
    };
    static constexpr size_t kMaxOutput = 16u << 20;        // Collect: bytes kept of each output; Lines: the longest line
    static constexpr size_t kMaxQueued = 1u << 20;         // Lines: bytes of unread lines before the program has to wait

    // Starts the program. Throws ScriptError when it cannot be started (not found, no such working directory...).
    ChildProcess(const ProcessOptions& options, Mode mode);
    ~ChildProcess();                    // kills what still runs and waits for the reading threads
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // The program has ended and both of its outputs are read to their end (what it started may hold them open: then not yet).
    bool finished();
    bool exited();                      // the program itself has ended
    int exitCode() const { return exitCode_; }      // valid when exited(); killed by a signal: minus the signal number
    void kill();                        // ends the program and what it started now

    // Lines: the next unread line, false when there is none (yet).
    bool nextLine(Line& out);
    bool hasLine();
    // Collect: everything read so far; `truncated` tells whether an output was longer than kMaxOutput.
    std::string output();
    std::string errorOutput();
    bool truncated();

private:
    struct Stream {
        std::string partial;            // Lines: the start of a line whose break has not come yet
        bool eof = false;
    };
    void feed(int stream, const char* data, size_t size);       // reading threads
    void pushLine(std::string text, bool error);                 // with mu_ held
    void endStream(int stream);
    void pollExit();
    void closeAll();

    Mode mode_;
    std::mutex mu_;
    std::condition_variable cv_;        // Lines: a reading thread waits here while too much is unread
    bool stopping_ = false;
    Stream streams_[2];
    std::string collected_[2];
    bool truncated_ = false;
    std::deque<Line> lines_;
    size_t queued_ = 0;
    bool exited_ = false;
    int exitCode_ = -1;

#if defined(_WIN32)
    void* process_ = nullptr;           // HANDLEs
    void* job_ = nullptr;
    void* pipes_[2] = {nullptr, nullptr};
    std::thread readers_[2];
    bool readerDone_[2] = {false, false};      // with mu_ held
    void readLoop(int stream);
#else
    int pid_ = -1;
    bool reaped_ = false;
    int pipes_[2] = {-1, -1};
    int wake_[2] = {-1, -1};            // the destructor writes to wake_[1] to end the reading thread
    std::thread reader_;
    void readLoop();
#endif
};

} // namespace fg::detail
