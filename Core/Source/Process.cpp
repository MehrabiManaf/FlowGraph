// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include "Process.hpp"

#include <FlowGraph/Value.hpp>

#include <chrono>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <signal.h>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fg::detail {

namespace {

#if defined(_WIN32)
bool validUtf8(const std::string& s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len;
        if (c < 0x80) { ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        else return false;
        if (i + len > n) return false;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}
#endif

std::filesystem::path pathOf(const std::string& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

void checkOptions(const ProcessOptions& o) {
    if (o.program.empty()) throw ScriptError(o.shell ? "no command given" : "no program given");
    if (!o.workingDirectory.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(pathOf(o.workingDirectory), ec))
            throw ScriptError("the working directory '" + o.workingDirectory + "' does not exist");
    }
}

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const wchar_t* w, int len) {
    if (len <= 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    return s;
}
// What a program writes: UTF-8 when it is valid UTF-8, otherwise text of the OEM code page (cmd.exe's own commands, like dir, and
// most console programs write in that one when their output is not a console).
std::string toUtf8(std::string s) {
    if (validUtf8(s)) return s;
    const int n = MultiByteToWideChar(CP_OEMCP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return narrow(w.data(), n);
}
std::string systemMessage(DWORD err) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
                                   reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string s = n ? narrow(buf, static_cast<int>(n)) : "error " + std::to_string(err);
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s;
}
// One argument of a command line as the C runtime (CommandLineToArgvW) splits it again.
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (auto it = a.begin();; ++it) {
        size_t backslashes = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++backslashes; }
        if (it == a.end()) { r.append(backslashes * 2, L'\\'); break; }
        if (*it == L'"') r.append(backslashes * 2 + 1, L'\\');
        else r.append(backslashes, L'\\');
        r.push_back(*it);
    }
    r.push_back(L'"');
    return r;
}
struct OwnedHandle {
    HANDLE h = nullptr;
    ~OwnedHandle() { reset(); }
    void reset() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }
    HANDLE release() { HANDLE r = h; h = nullptr; return r; }
};
#else
std::string toUtf8(std::string s) { return s; }
// One argument for /bin/sh: in single quotes, a single quote as '\''.
std::string shellQuote(const std::string& a) {
    std::string r = "'";
    for (char ch : a) {
        if (ch == '\'') r += "'\\''";
        else r += ch;
    }
    return r + "'";
}
bool makePipe(int fds[2]) {
#if defined(__linux__)
    return pipe2(fds, O_CLOEXEC) == 0;
#else
    if (pipe(fds) != 0) return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}
void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}
// The file a program name stands for: itself when it has a '/', otherwise the first executable of that name in PATH.
std::string findProgram(const std::string& name) {
    if (name.find('/') != std::string::npos) return name;
    const char* env = std::getenv("PATH");
    const std::string path = env && *env ? env : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    for (;;) {
        const size_t end = path.find(':', start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        const std::string candidate = dir + "/" + name;
        struct stat st {};
        if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(candidate.c_str(), X_OK) == 0) return candidate;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}
#endif

// "\r\n" -> "\n": the line breaks of a script's texts, whatever the platform.
std::string lineBreaks(std::string s) {
    if (s.find('\r') == std::string::npos) return s;
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        if (!(s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')) r += s[i];
    return r;
}

} // namespace

// ------------------------------------------------------------------ reading (both platforms)
void ChildProcess::pushLine(std::string text, bool error) {
    if (!text.empty() && text.back() == '\r') text.pop_back();
    text = toUtf8(std::move(text));
    queued_ += text.size() + 1;
    lines_.push_back({std::move(text), error});
}

void ChildProcess::feed(int stream, const char* data, size_t size) {
    std::unique_lock<std::mutex> lock(mu_);
    if (mode_ == Mode::Collect) {
        std::string& out = collected_[stream];
        const size_t room = kMaxOutput - out.size();
        if (size > room) truncated_ = true;
        out.append(data, size < room ? size : room);
        return;
    }
    Stream& st = streams_[stream];
    size_t start = 0;
    for (size_t i = 0; i < size; ++i)
        if (data[i] == '\n') {
            st.partial.append(data + start, i - start);
            pushLine(std::move(st.partial), stream == 1);
            st.partial.clear();
            start = i + 1;
        }
    st.partial.append(data + start, size - start);
    if (st.partial.size() >= kMaxOutput) {          // a line without end: handed out in pieces
        pushLine(std::move(st.partial), stream == 1);
        st.partial.clear();
    }
    // The script reads slower than the program writes: this thread waits, the pipe fills up and the program waits too.
    cv_.wait(lock, [this] { return stopping_ || queued_ <= kMaxQueued; });
}

void ChildProcess::endStream(int stream) {
    std::lock_guard<std::mutex> lock(mu_);
    Stream& st = streams_[stream];
    if (!st.partial.empty()) {
        pushLine(std::move(st.partial), stream == 1);
        st.partial.clear();
    }
    st.eof = true;
}

bool ChildProcess::nextLine(Line& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (lines_.empty()) return false;
    out = std::move(lines_.front());
    lines_.pop_front();
    queued_ -= out.text.size() + 1;
    cv_.notify_all();
    return true;
}

bool ChildProcess::hasLine() {
    std::lock_guard<std::mutex> lock(mu_);
    return !lines_.empty();
}

std::string ChildProcess::output() {
    std::lock_guard<std::mutex> lock(mu_);
    return lineBreaks(toUtf8(collected_[0]));
}

std::string ChildProcess::errorOutput() {
    std::lock_guard<std::mutex> lock(mu_);
    return lineBreaks(toUtf8(collected_[1]));
}

bool ChildProcess::truncated() {
    std::lock_guard<std::mutex> lock(mu_);
    return truncated_;
}

bool ChildProcess::exited() {
    pollExit();
    return exited_;
}

bool ChildProcess::finished() {
    pollExit();
    std::lock_guard<std::mutex> lock(mu_);
    return exited_ && streams_[0].eof && streams_[1].eof && lines_.empty();
}

#if defined(_WIN32)
// ------------------------------------------------------------------ Windows
ChildProcess::ChildProcess(const ProcessOptions& o, Mode mode) : mode_(mode) {
    checkOptions(o);
    std::wstring application, commandLine;
    if (o.shell) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) application.assign(buf, n);
        else {
            const UINT k = GetSystemDirectoryW(buf, MAX_PATH);
            application = std::wstring(buf, k < MAX_PATH ? k : 0) + L"\\cmd.exe";
        }
        std::wstring line = widen(o.program);
        for (auto& a : o.args) line += L" " + quoteArg(widen(a));
        commandLine = quoteArg(application) + L" /d /s /c \"" + line + L"\"";
    } else {
        commandLine = quoteArg(widen(o.program));
        for (auto& a : o.args) commandLine += L" " + quoteArg(widen(a));
    }

    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    OwnedHandle readEnd[2], writeEnd[2], nul;
    for (int s = 0; s < 2; ++s) {
        if (!CreatePipe(&readEnd[s].h, &writeEnd[s].h, &sa, 0)) throw ScriptError("cannot run '" + o.program + "': " + systemMessage(GetLastError()));
        SetHandleInformation(readEnd[s].h, HANDLE_FLAG_INHERIT, 0);
    }
    nul.h = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul.h == INVALID_HANDLE_VALUE) throw ScriptError("cannot run '" + o.program + "': " + systemMessage(GetLastError()));

    // Only these three handles are inherited: a program another thread starts at the same time does not get them.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) throw ScriptError("cannot run '" + o.program + "': " + systemMessage(GetLastError()));
    HANDLE inherited[3] = {nul.h, writeEnd[0].h, writeEnd[1].h};
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul.h;
    si.StartupInfo.hStdOutput = writeEnd[0].h;
    si.StartupInfo.hStdError = writeEnd[1].h;
    si.lpAttributeList = attrs;
    const std::wstring cwd = widen(o.workingDirectory);
    PROCESS_INFORMATION pi{};
    // Suspended until it is in the job: nothing it starts can escape the job.
    const BOOL ok = CreateProcessW(o.shell ? application.c_str() : nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    const DWORD err = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    nul.reset();
    writeEnd[0].reset();                // the program has its own copies: the reading ends see the end when the program's are closed
    writeEnd[1].reset();
    if (!ok) throw ScriptError("cannot run '" + o.program + "': " + systemMessage(err));

    process_ = pi.hProcess;
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof(li)) || !AssignProcessToJobObject(job_, process_)) {
            CloseHandle(job_);          // kill() falls back to the program alone
            job_ = nullptr;
        }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    pipes_[0] = readEnd[0].release();
    pipes_[1] = readEnd[1].release();
    try {
        for (int s = 0; s < 2; ++s) readers_[s] = std::thread([this, s] { readLoop(s); });
    } catch (...) {
        closeAll();
        throw;
    }
}

void ChildProcess::readLoop(int stream) {
    char buf[65536];
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stopping_) break;
        }
        DWORD n = 0;
        if (!ReadFile(pipes_[stream], buf, sizeof(buf), &n, nullptr)) break;     // the end (or cancelled)
        if (n) feed(stream, buf, n);
    }
    endStream(stream);
    std::lock_guard<std::mutex> lock(mu_);
    readerDone_[stream] = true;
}

void ChildProcess::pollExit() {
    if (exited_ || !process_) return;
    if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    exitCode_ = static_cast<int>(code);
    exited_ = true;
}

void ChildProcess::kill() {
    if (job_) TerminateJobObject(job_, 1);
    else if (process_ && !exited_) TerminateProcess(process_, 1);
}

void ChildProcess::closeAll() {
    kill();
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (int s = 0; s < 2; ++s) {
        if (!readers_[s].joinable()) continue;
        // The program is gone, so the read ends; something outside the job may still hold the pipe: then the read is cancelled.
        for (int tries = 0;; ++tries) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (readerDone_[s]) break;
            }
            if (tries > 0) CancelSynchronousIo(readers_[s].native_handle());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        readers_[s].join();
    }
    for (auto& p : pipes_) {
        if (p) CloseHandle(p);
        p = nullptr;
    }
    if (job_) CloseHandle(job_);
    job_ = nullptr;
    if (process_) CloseHandle(process_);
    process_ = nullptr;
}

ChildProcess::~ChildProcess() { closeAll(); }

#else
// ------------------------------------------------------------------ POSIX
ChildProcess::ChildProcess(const ProcessOptions& o, Mode mode) : mode_(mode) {
    checkOptions(o);
    std::string path;
    std::vector<std::string> argv;
    if (o.shell) {
        path = "/bin/sh";
        std::string line = o.program;
        for (auto& a : o.args) line += " " + shellQuote(a);
        argv = {"sh", "-c", line};
    } else {
        path = findProgram(o.program);
        if (path.empty()) throw ScriptError("cannot run '" + o.program + "': not found");
        argv.push_back(o.program);
        argv.insert(argv.end(), o.args.begin(), o.args.end());
    }
    std::vector<char*> cargv;
    for (auto& a : argv) cargv.push_back(a.data());
    cargv.push_back(nullptr);
    const char* cwd = o.workingDirectory.empty() ? nullptr : o.workingDirectory.c_str();

    int out[2] = {-1, -1}, err[2] = {-1, -1}, report[2] = {-1, -1};
    auto failed = [&](const std::string& what) {
        const int e = errno;
        for (int* p : {out, err, report, wake_}) { closeFd(p[0]); closeFd(p[1]); }
        throw ScriptError("cannot run '" + o.program + "': " + what + std::strerror(e));
    };
    if (!makePipe(out) || !makePipe(err) || !makePipe(report) || !makePipe(wake_)) failed("");
    int devnull = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (devnull < 0) failed("");

    const pid_t pid = ::fork();
    if (pid == 0) {
        // The child: only async-signal-safe calls until exec. A process group of its own, so it can be killed with all it starts.
        ::setpgid(0, 0);
        struct sigaction dfl {};
        dfl.sa_handler = SIG_DFL;
        ::sigaction(SIGPIPE, &dfl, nullptr);          // a host that ignores SIGPIPE must not pass that on
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        ::dup2(devnull, 0);
        ::dup2(out[1], 1);
        ::dup2(err[1], 2);
        int e = 0;
        if (cwd && ::chdir(cwd) != 0) e = errno;
        else {
            ::execv(path.c_str(), cargv.data());
            e = errno;
        }
        ssize_t w = ::write(report[1], &e, sizeof(e));
        (void)w;
        ::_exit(127);
    }
    closeFd(devnull);
    if (pid < 0) failed("");
    ::setpgid(pid, pid);                              // also here: whichever runs first
    closeFd(out[1]);
    closeFd(err[1]);
    closeFd(report[1]);
    int childErrno = 0;
    ssize_t n;
    do n = ::read(report[0], &childErrno, sizeof(childErrno));
    while (n < 0 && errno == EINTR);
    closeFd(report[0]);
    if (n == sizeof(childErrno)) {                    // exec (or chdir) failed: the pipe was not closed by exec but written to
        int status;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        closeFd(out[0]);
        closeFd(err[0]);
        closeFd(wake_[0]);
        closeFd(wake_[1]);
        throw ScriptError("cannot run '" + o.program + "': " + std::strerror(childErrno));
    }
    pid_ = pid;
    pipes_[0] = out[0];
    pipes_[1] = err[0];
    try {
        reader_ = std::thread([this] { readLoop(); });
    } catch (...) {
        closeAll();
        throw;
    }
}

void ChildProcess::readLoop() {
    char buf[65536];
    bool open[2] = {true, true};
    while (open[0] || open[1]) {
        pollfd fds[3];
        int streamOf[3] = {-1, -1, -1};
        nfds_t count = 0;
        for (int s = 0; s < 2; ++s)
            if (open[s]) {
                fds[count] = {pipes_[s], POLLIN, 0};
                streamOf[count++] = s;
            }
        fds[count++] = {wake_[0], POLLIN, 0};
        if (::poll(fds, count, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[count - 1].revents) break;            // the destructor
        for (nfds_t i = 0; i + 1 < count; ++i) {
            if (!fds[i].revents) continue;
            const int s = streamOf[i];
            const ssize_t k = ::read(pipes_[s], buf, sizeof(buf));
            if (k > 0) feed(s, buf, static_cast<size_t>(k));
            else if (k == 0 || (errno != EINTR && errno != EAGAIN)) {
                open[s] = false;
                endStream(s);
            }
        }
    }
    for (int s = 0; s < 2; ++s)
        if (open[s]) endStream(s);
}

void ChildProcess::pollExit() {
    if (exited_ || pid_ <= 0) return;
    // WNOWAIT: the program is not reaped yet, so its process group id stays reserved and the group can be killed safely later.
    siginfo_t si;
    std::memset(&si, 0, sizeof(si));
    if (::waitid(P_PID, static_cast<id_t>(pid_), &si, WEXITED | WNOHANG | WNOWAIT) == 0) {
        if (si.si_pid != pid_) return;
        exitCode_ = si.si_code == CLD_EXITED ? si.si_status : -si.si_status;
        exited_ = true;
    } else if (errno == ECHILD) {                     // reaped by someone else (SIGCHLD ignored by the host)
        exitCode_ = -1;
        exited_ = true;
        reaped_ = true;
    }
}

void ChildProcess::kill() {
    if (pid_ > 0 && !reaped_) ::kill(-pid_, SIGKILL);
}

void ChildProcess::closeAll() {
    kill();
    {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (wake_[1] >= 0) {
        const char c = 1;
        ssize_t w = ::write(wake_[1], &c, 1);
        (void)w;
    }
    if (reader_.joinable()) reader_.join();
    if (pid_ > 0 && !reaped_) {
        int status;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        reaped_ = true;
    }
    closeFd(pipes_[0]);
    closeFd(pipes_[1]);
    closeFd(wake_[0]);
    closeFd(wake_[1]);
}

ChildProcess::~ChildProcess() { closeAll(); }
#endif

} // namespace fg::detail
