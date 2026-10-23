// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// The FileSystem plugin: paths, questions to the disk, files and folders, and the File type (read and write a file).
//
// * Paths are UTF-8 text in every language; a relative path is relative to the working directory of the process (the cwd of a
//   launch configuration). Files are read and written as UTF-8 text.
// * The nodes that work on the disk (Open File, Copy, List Folder...) do the work on a thread of their own: the script waits, the
//   thread that runs it does not, so a big file or a slow network drive never freezes the application. They go on with Then when
//   the work is done and with Failed (and the reason in Error) when it could not be done - a missing file is not a script error.
//   Quick work (a small file) is waited for a moment, so that a loop over many files does not wait for the clock at every one.
//   When the script stops waiting (Stop, a hot reload, the end of the instance) the work stops at its next chunk.
// * The quick questions (Exists, File Size...) and the paths are pure functions: they never fail, they say false / -1 / "".
#include <FlowGraph/Plugin.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;
using namespace fg;

namespace {

// ------------------------------------------------------------------ paths and text

fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size())); }

std::string textOf(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// "cannot read 'notes.txt': No such file or directory"
std::string failure(const char* what, const std::string& path, const std::error_code& ec) {
    return std::string("cannot ") + what + " '" + path + "': " + ec.message();
}

// `*` any characters, `?` one; several patterns separated by `;` ("*.txt;*.md"); letter case counts except on Windows
bool matchOne(const std::u8string& name, size_t n, const std::u8string& pattern, size_t p) {
    while (p < pattern.size()) {
        if (pattern[p] == u8'*') {
            while (p < pattern.size() && pattern[p] == u8'*') ++p;
            if (p == pattern.size()) return true;
            for (size_t k = n; k <= name.size(); ++k)
                if (matchOne(name, k, pattern, p)) return true;
            return false;
        }
        if (n == name.size()) return false;
        char8_t a = name[n], b = pattern[p];
#if defined(_WIN32)
        if (a >= u8'A' && a <= u8'Z') a = char8_t(a - u8'A' + u8'a');
        if (b >= u8'A' && b <= u8'Z') b = char8_t(b - u8'A' + u8'a');
#endif
        if (b != u8'?' && a != b) return false;
        ++n;
        ++p;
    }
    return n == name.size();
}

bool matches(const fs::path& file, const std::string& patterns) {
    if (patterns.empty() || patterns == "*") return true;
    const std::u8string name = file.filename().u8string();
    size_t start = 0;
    for (;;) {
        const size_t bar = patterns.find(';', start);
        const std::string one = patterns.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (!one.empty() && matchOne(name, 0, std::u8string(reinterpret_cast<const char8_t*>(one.data()), one.size()), 0)) return true;
        if (bar == std::string::npos) return false;
        start = bar + 1;
    }
}

// ------------------------------------------------------------------ work on a thread of its own

// The work threads run the code of this library, a little longer than their work (they end after `done`), and a Runtime unloads
// its plugins when it goes away. So once a work thread has started, the library stays loaded until the process ends.
void keepLoaded() {
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(_WIN32)
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&keepLoaded), &self);
#else
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&keepLoaded), &info) && info.dli_fname) dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
#endif
    });
}

// What the work of a node hands back. Written by the work thread until `done`, read by the node after.
struct Job {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::mutex m;                       // with `cv`: the node waits a moment for quick work (startWork)
    std::condition_variable cv;
    std::string error;                  // empty: the work was done
    std::vector<std::pair<std::string, bool>> entries;      // List Folder / For Each File: path, is a folder
    std::shared_ptr<void> file;                             // Open File: the OpenFile
    std::string text;                                       // Read Text: what was read
};

// Kept by the node while it waits (NodeCtx::native). It goes when the node is done with it, or when the task stops waiting (Stop,
// hot reload, the instance going away): then the work is cancelled.
struct JobHandle {
    std::shared_ptr<Job> job;
    size_t next = 0;                    // For Each File: the entry the loop body gets next
    ~JobHandle() {
        if (job) job->cancelled = true;
    }
};

// How long a node waits for its work before it lets the script wait: most work (a small file, a folder) is done by then, and the
// node goes on at once instead of at the next poll of the task (a tick of the application, or a sleep of the system's timer).
constexpr auto kQuickWork = std::chrono::milliseconds(2);

// Starts the work on a thread of its own (NodeCtx::native keeps its JobHandle). True when it was done quickly: the node goes on
// now; false when the script waits for it and the node runs again (resumed) when it is done.
bool startWork(NodeCtx& c, std::function<void(Job&)> work) {
    keepLoaded();
    auto job = std::make_shared<Job>();
    std::thread([job, work = std::move(work)] {
        try {
            work(*job);
        } catch (const std::exception& e) {
            if (job->error.empty()) job->error = e.what();
        }
        {
            std::lock_guard lock(job->m);
            job->done = true;
        }
        job->cv.notify_all();
    }).detach();
    auto handle = std::make_shared<JobHandle>();
    handle->job = job;
    c.native() = handle;
    {
        std::unique_lock lock(job->m);
        if (job->cv.wait_for(lock, kQuickWork, [&] { return job->done.load(); })) return true;
    }
    c.waitThenResume([w = std::weak_ptr<Job>(job)](double) {
        auto j = w.lock();
        return !j || j->done.load();
    });
    return false;
}

std::shared_ptr<JobHandle> handleOf(NodeCtx& c) { return std::static_pointer_cast<JobHandle>(c.native()); }

constexpr size_t kChunk = 1 << 20;      // copies go in pieces of 1 MiB: a cancelled one stops after the piece

// One file, in pieces (it can be cancelled), with its permissions and its time of change.
bool copyFile(Job& job, const fs::path& from, const fs::path& to) {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        job.error = "cannot copy '" + textOf(from) + "' to '" + textOf(to) + "'";
        return false;
    }
    std::string chunk(kChunk, '\0');
    while (in) {
        if (job.cancelled) break;
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        out.write(chunk.data(), in.gcount());
    }
    out.close();
    std::error_code ec;
    if (job.cancelled || !out) {
        if (!job.cancelled) job.error = "cannot copy '" + textOf(from) + "' to '" + textOf(to) + "': writing failed (is the disk full?)";
        fs::remove(to, ec);
        return false;
    }
    fs::permissions(to, fs::status(from, ec).permissions(), ec);
    fs::last_write_time(to, fs::last_write_time(from, ec), ec);
    return true;
}

bool copyAny(Job& job, const fs::path& from, const fs::path& to, bool overwrite) {
    std::error_code ec;
    if (!fs::exists(from, ec)) {
        job.error = "cannot copy '" + textOf(from) + "': it does not exist";
        return false;
    }
    if (fs::exists(to, ec) && !overwrite) {
        job.error = "cannot copy to '" + textOf(to) + "': it exists (Overwrite replaces it)";
        return false;
    }
    if (!fs::is_directory(from, ec)) return copyFile(job, from, to);
    // a folder with all it holds
    fs::create_directories(to, ec);
    if (ec) {
        job.error = failure("create", textOf(to), ec);
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (job.cancelled) return false;
        const fs::path rel = fs::relative(it->path(), from, ec);
        const fs::path dest = to / rel;
        if (it->is_directory(ec)) fs::create_directories(dest, ec);
        else if (!copyFile(job, it->path(), dest)) return false;
    }
    if (ec) {
        job.error = failure("copy", textOf(from), ec);
        return false;
    }
    return true;
}

// The entries of a folder that match the patterns, folders first, each group in name order.
bool listFolder(Job& job, const std::string& folder, const std::string& patterns, bool recursive) {
    const fs::path root = pathOf(folder.empty() ? std::string(".") : folder);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        job.error = "cannot list '" + folder + "': " + (fs::exists(root, ec) ? "it is not a folder" : "it does not exist");
        return false;
    }
    std::vector<std::pair<std::string, bool>> found;
    auto add = [&](const fs::directory_entry& e) {
        const bool isFolder = e.is_directory(ec);
        if (matches(e.path(), patterns)) found.emplace_back(textOf(e.path()), isFolder);
    };
    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (job.cancelled) return false;
            add(*it);
        }
    } else {
        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (job.cancelled) return false;
            add(*it);
        }
    }
    if (ec) {
        job.error = failure("list", folder, ec);
        return false;
    }
    std::stable_sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second : a.first < b.first; });
    job.entries = std::move(found);
    return true;
}

// ------------------------------------------------------------------ open files

enum class FileMode { Read, Write, Append };

// A file that Open File opened (the File type of scripts): read a line or the rest at a time, or written a piece at a time.
// It is closed by Close, or when nothing refers to it any more - which can be long after the script has run: an Instance keeps
// the outputs of its nodes (flowstudio keeps the instance of the last run to inspect it). So every write goes to the file at once
// (a forgotten Close loses nothing), at a few microseconds a write. Reading goes through a buffer. A line, or a small write, runs on
// the thread of the script; Read Text and a big write (1 MiB or more) on a work thread (the file is busy meanwhile). What cannot be done (writing a file opened for reading, a closed file, a failing disk) goes to Failed, as Open File.
class OpenFile {
public:
    OpenFile(std::string path, FileMode mode) : path_(std::move(path)), mode_(mode) {}

    // On the work thread of Open File. False and the reason when the file cannot be opened.
    bool open(std::string& error) {
        const fs::path p = pathOf(path_);
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            error = "cannot open '" + path_ + "': it is a folder";
            return false;
        }
        const auto how = mode_ == FileMode::Read    ? std::ios::in
                         : mode_ == FileMode::Write ? std::ios::out | std::ios::trunc
                                                    : std::ios::out | std::ios::app;
        stream_.open(p, how | std::ios::binary);
        if (!stream_) {
            const fs::path folder = p.parent_path();
            if (mode_ == FileMode::Read)
                error = fs::exists(p, ec) ? failure("open", path_, std::make_error_code(std::errc::permission_denied))
                                          : "cannot open '" + path_ + "': it does not exist";
            else if (!folder.empty() && !fs::is_directory(folder, ec))
                error = "cannot open '" + path_ + "': the folder '" + textOf(folder) + "' does not exist";
            else
                error = failure("open", path_, std::make_error_code(std::errc::permission_denied));
            return false;
        }
        if (mode_ == FileMode::Read) {          // a UTF-8 byte order mark is not part of the text
            char mark[3] = {};
            stream_.read(mark, 3);
            if (!(stream_.gcount() == 3 && mark[0] == '\xEF' && mark[1] == '\xBB' && mark[2] == '\xBF')) {
                stream_.clear();
                stream_.seekg(0);
            }
        }
        open_ = true;
        return true;
    }

    // Each of these is false, with the reason in `error`, when it cannot be done: the file is closed, it is open the other way,
    // or the disk fails. The end of the file is not a failure: Read Line gives "" there (At End says it).
    bool readLine(std::string& line, std::string& error) {
        line.clear();
        if (!can(true, "read", error)) return false;
        if (!std::getline(stream_, line)) {
            line.clear();                       // at the end
            return worked("read", error);
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }
    // Read Text reads the rest on a work thread: beginReadRest (on the thread of the script), readRest (on the work thread), then
    // endReadRest. Meanwhile the file is busy: its other nodes go to Failed, At End says false.
    bool beginReadRest(std::string& error) {
        if (!can(true, "read", error)) return false;
        busy_ = "Read Text is reading it";
        return true;
    }
    // The rest of the file in pieces (`cancelled` stops it after one). The memory for all of it is taken first: a file too big
    // for it fails at once, with the reason, instead of after it has filled the memory.
    bool readRest(std::string& text, std::string& error, const std::atomic<bool>& cancelled) {
        text.clear();
        std::error_code ec;
        const auto at = stream_.tellg();
        const auto size = fs::file_size(pathOf(path_), ec);
        if (!ec && at >= 0 && size > static_cast<uintmax_t>(at)) {
            const uintmax_t left = size - static_cast<uintmax_t>(at);
            try {
                if (left > text.max_size()) throw std::bad_alloc();
                text.reserve(static_cast<size_t>(left));
            } catch (const std::bad_alloc&) {
                error = tooBig(left);
                return false;
            }
        }
        std::string chunk(kChunk, '\0');
        try {
            while (stream_ && !cancelled) {
                stream_.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                text.append(chunk.data(), static_cast<size_t>(stream_.gcount()));
            }
        } catch (const std::bad_alloc&) {          // the file grew while it was read
            const uintmax_t read = text.size();
            std::string().swap(text);
            error = tooBig(read);
            return false;
        }
        return worked("read", error);
    }
    void endReadRest() { busy_ = nullptr; }
    bool atEnd() const {
        return !open_ || mode_ != FileMode::Read || (!busy_ && stream_.peek() == std::char_traits<char>::eof());
    }
    // Write Text / Write Line: `text`, and a line break after it with `line`. A big text is written on a work thread, like Read
    // Text: beginWrite, write (`cancelled` stops it after a piece; what was written stays), endWrite.
    bool canWrite(std::string& error) const { return can(false, "write", error); }
    bool beginWrite(std::string& error) {
        if (!can(false, "write", error)) return false;
        busy_ = "Write Text is writing it";
        return true;
    }
    bool write(const std::string& text, bool line, std::string& error, const std::atomic<bool>* cancelled = nullptr) {
        for (size_t at = 0; at < text.size(); at += kChunk) {
            if (cancelled && *cancelled) break;
            stream_.write(text.data() + at, static_cast<std::streamsize>(std::min(kChunk, text.size() - at)));
        }
        if (line && !(cancelled && *cancelled)) stream_.put('\n');
        stream_.flush();                         // in the file now, not when it is closed
        return worked("write", error);
    }
    void endWrite() { busy_ = nullptr; }
    bool close(std::string& error) {             // closing a closed file is fine
        if (busy_) return can(true, "close", error);
        if (!open_) return true;
        open_ = false;
        stream_.close();
        if (stream_ || mode_ == FileMode::Read) return true;
        error = "cannot write '" + path_ + "': writing failed (is the disk full?)";
        return false;
    }
    const std::string& path() const { return path_; }
    bool isOpen() const { return open_; }

private:
    bool can(bool reading, const char* what, std::string& error) const {
        if (const char* busy = busy_) error = std::string("cannot ") + what + " '" + path_ + "': " + busy;
        else if (!open_) error = std::string("cannot ") + what + " '" + path_ + "': the file is closed";
        else if (reading != (mode_ == FileMode::Read))
            error = std::string("cannot ") + what + " '" + path_ + "': it is open for " + (reading ? "writing" : "reading");
        else return true;
        return false;
    }
    // After a read or write: did the disk fail? (Reaching the end of the file is no failure.)
    bool worked(const char* what, std::string& error) {
        const bool failed = mode_ == FileMode::Read ? stream_.bad() : !stream_;
        if (failed) {
            error = std::string("cannot ") + what + " '" + path_ + "': " +
                    (mode_ == FileMode::Read ? "reading failed" : "writing failed (is the disk full?)");
            return false;
        }
        if (mode_ == FileMode::Read) stream_.clear(stream_.rdstate() & ~std::ios::failbit);
        return true;
    }

    std::string tooBig(uintmax_t bytes) const {
        return "cannot read '" + path_ + "': " + std::to_string(bytes / (1 << 20)) +
               " MiB is too much to read at once; read it a line at a time (Read Line)";
    }

    std::string path_;
    FileMode mode_;
    mutable std::fstream stream_;               // mutable: At End peeks
    bool open_ = false;
    std::atomic<const char*> busy_{nullptr};    // what works on it on a work thread ("Read Text is reading it"), or null
};

// ------------------------------------------------------------------ nodes that wait

constexpr const char* kCategory = "File System";

// A node that waits for its work: In, its inputs; Then, Failed, its outputs and Error (the last data output).
NodeDef waitingNode(const char* id, const char* title, const char* category, const char* keywords, std::string doc,
                    std::vector<PinDef> inputs, std::vector<PinDef> outputs) {
    NodeDef d;
    d.id = id;
    d.title = title;
    d.category = std::string(kCategory) + "|" + category;
    d.keywords = keywords;
    d.doc = std::move(doc) + " The script waits for it; the application does not. Failed, and the reason in Error, when it cannot be done.";
    d.waits = true;
    d.pins.push_back(execIn());
    for (auto& p : inputs) d.pins.push_back(std::move(p));
    d.pins.push_back(execOut());
    d.pins.push_back(execOut("Failed"));
    d.pins.back().doc = "When it could not be done: Error says why";
    for (auto& p : outputs) d.pins.push_back(std::move(p));
    d.pins.push_back(dataOut("Error", "string"));
    d.pins.back().doc = "Why it failed (empty when it worked)";
    return d;
}

PinDef doc(PinDef p, const char* text) {
    p.doc = text;
    return p;
}

// Runs `work` (on a thread of its own), then hands out the results with `done` and goes on with Then - or with Failed.
void runJob(NodeCtx& c, size_t errorOut, const std::function<void(Job&)>& work, const std::function<void(NodeCtx&, Job&)>& done) {
    if (!c.resumed() && !startWork(c, work)) return;
    auto h = handleOf(c);
    Job& job = *h->job;
    if (!job.error.empty()) {
        c.out(errorOut, Value(job.error));
        c.next(1);
        return;
    }
    if (done) done(c, job);
    c.out(errorOut, Value(std::string()));
    c.next(0);
}

Value stringArray(const Registry& reg, std::vector<std::string> items) {
    Value a = reg.makeArray(reg.findType("string"));
    for (auto& s : items) a.arrayPtr()->items.emplace_back(std::move(s));
    return a;
}

void registerWaitingNodes(Registry& reg) {
    {
        NodeDef d = waitingNode("filesystem.copy", "Copy", "Files and Folders", "file folder directory copy duplicate backup",
                                "Copies a file, or a folder with all it holds, to the path To (the new file or folder, not the "
                                "folder to put it in).",
                                {dataIn("From", "string", Value("")), dataIn("To", "string", Value("")),
                                 doc(dataIn("Overwrite", "bool", Value(false)), "Replace what is at To (otherwise Failed when it exists)")},
                                {});
        d.run = [](NodeCtx& c) {
            std::string from, to;
            bool overwrite = false;
            if (!c.resumed()) {
                from = c.inString(0);
                to = c.inString(1);
                overwrite = c.inBool(2);
            }
            runJob(c, 0, [from, to, overwrite](Job& job) { copyAny(job, pathOf(from), pathOf(to), overwrite); }, nullptr);
        };
        reg.addNode(std::move(d));
    }
    {
        NodeDef d = waitingNode("filesystem.move", "Move", "Files and Folders", "file folder directory move rename",
                                "Moves or renames a file or a folder to the path To (also to another drive).",
                                {dataIn("From", "string", Value("")), dataIn("To", "string", Value("")),
                                 doc(dataIn("Overwrite", "bool", Value(false)), "Replace a file at To (otherwise Failed when it exists)")},
                                {});
        d.run = [](NodeCtx& c) {
            std::string from, to;
            bool overwrite = false;
            if (!c.resumed()) {
                from = c.inString(0);
                to = c.inString(1);
                overwrite = c.inBool(2);
            }
            runJob(c, 0, [from, to, overwrite](Job& job) {
                const fs::path a = pathOf(from), b = pathOf(to);
                std::error_code ec;
                if (!fs::exists(a, ec)) { job.error = "cannot move '" + from + "': it does not exist"; return; }
                if (fs::exists(b, ec)) {
                    if (!overwrite) { job.error = "cannot move to '" + to + "': it exists (Overwrite replaces a file)"; return; }
                    if (fs::is_directory(b, ec)) { job.error = "cannot move to '" + to + "': a folder is there"; return; }
                    fs::remove(b, ec);
                }
                fs::rename(a, b, ec);
                if (!ec) return;
                // another drive: copy, then remove the original
                if (!copyAny(job, a, b, overwrite)) return;
                fs::remove_all(a, ec);
                if (ec) job.error = "moved '" + from + "' as a copy, but cannot remove it: " + ec.message();
            }, nullptr);
        };
        reg.addNode(std::move(d));
    }
    {
        NodeDef d = waitingNode("filesystem.delete", "Delete", "Files and Folders", "file folder directory delete remove erase rm",
                                "Deletes a file, or a folder: an empty one, or with all it holds when Recursive is on.",
                                {dataIn("Path", "string", Value("")),
                                 doc(dataIn("Recursive", "bool", Value(false)), "Delete a folder with all it holds")},
                                {});
        d.run = [](NodeCtx& c) {
            std::string path;
            bool recursive = false;
            if (!c.resumed()) {
                path = c.inString(0);
                recursive = c.inBool(1);
            }
            runJob(c, 0, [path, recursive](Job& job) {
                const fs::path p = pathOf(path);
                std::error_code ec;
                if (!fs::exists(fs::symlink_status(p, ec))) { job.error = "cannot delete '" + path + "': it does not exist"; return; }
                if (fs::is_directory(p, ec) && !fs::is_empty(p, ec) && !recursive) {
                    job.error = "cannot delete '" + path + "': the folder is not empty (Recursive deletes it with all it holds)";
                    return;
                }
                if (recursive) fs::remove_all(p, ec);
                else fs::remove(p, ec);
                if (ec) job.error = failure("delete", path, ec);
            }, nullptr);
        };
        reg.addNode(std::move(d));
    }
    {
        NodeDef d = waitingNode("filesystem.mkdir", "Create Folder", "Files and Folders", "folder directory create make mkdir new",
                                "Creates a folder, and the folders above it that do not exist yet. A folder that is already "
                                "there is fine.",
                                {dataIn("Path", "string", Value(""))}, {});
        d.run = [](NodeCtx& c) {
            const std::string path = c.resumed() ? std::string() : c.inString(0);
            runJob(c, 0, [path](Job& job) {
                const fs::path p = pathOf(path);
                std::error_code ec;
                if (fs::is_directory(p, ec)) return;
                if (fs::exists(p, ec)) { job.error = "cannot create '" + path + "': a file is there"; return; }
                fs::create_directories(p, ec);
                if (ec) job.error = failure("create", path, ec);
            }, nullptr);
        };
        reg.addNode(std::move(d));
    }
    const char* kPatternDoc = "Which names: * any characters, ? one character, several separated by ; (*.txt;*.md)";
    {
        NodeDef d = waitingNode("filesystem.list", "List Folder", "Files and Folders", "folder directory list files dir ls find search glob",
                                "The files and the folders in a folder (with Recursive, in the folders inside it too), as paths, "
                                "each list in name order.",
                                {dataIn("Folder", "string", Value("")), doc(dataIn("Pattern", "string", Value("*")), kPatternDoc),
                                 doc(dataIn("Recursive", "bool", Value(false)), "Also what the folders inside it hold")},
                                {doc(dataOut("Files", "array<string>"), "The paths of the files"),
                                 doc(dataOut("Folders", "array<string>"), "The paths of the folders")});
        d.run = [](NodeCtx& c) {
            std::string folder, pattern;
            bool recursive = false;
            if (!c.resumed()) {
                folder = c.inString(0);
                pattern = c.inString(1);
                recursive = c.inBool(2);
            }
            runJob(c, 2, [folder, pattern, recursive](Job& job) { listFolder(job, folder, pattern, recursive); },
                   [](NodeCtx& c, Job& job) {
                       std::vector<std::string> files, folders;
                       for (auto& [path, isFolder] : job.entries) (isFolder ? folders : files).push_back(path);
                       c.out(0, stringArray(c.registry(), std::move(files)));
                       c.out(1, stringArray(c.registry(), std::move(folders)));
                   });
        };
        reg.addNode(std::move(d));
    }
    {
        NodeDef d;
        d.id = "filesystem.foreach";
        d.title = "For Each File";
        d.category = std::string(kCategory) + "|Files and Folders";
        d.style = NodeStyle::Flow;
        d.keywords = "folder directory list files loop each iterate walk find search glob";
        d.doc = "Runs the loop body for each file and folder in a folder (with Recursive, in the folders inside it too), folders "
                "first, in name order. The folder is read when the loop starts: the script waits for it, the application does not. "
                "Failed, and the reason in Error, when it cannot be read.";
        d.waits = true;
        d.breakPin = 0;
        d.pins.push_back(execIn());
        d.pins.push_back(dataIn("Folder", "string", Value("")));
        d.pins.push_back(doc(dataIn("Pattern", "string", Value("*")), kPatternDoc));
        d.pins.push_back(doc(dataIn("Recursive", "bool", Value(false)), "Also what the folders inside it hold"));
        d.pins.push_back(execOut("Completed"));
        d.pins.push_back(execOut("Loop Body"));
        d.pins.push_back(doc(execOut("Failed"), "When the folder could not be read: Error says why"));
        d.pins.push_back(doc(dataOut("Path", "string"), "The path of the file or folder"));
        d.pins.push_back(doc(dataOut("Is Folder", "bool"), "It is a folder"));
        d.pins.push_back(doc(dataOut("Error", "string"), "Why it failed (empty when it worked)"));
        // native(): the JobHandle - the listing, then where the loop is in it
        d.run = [](NodeCtx& c) {
            if (!c.resumed()) {
                const std::string folder = c.inString(0), pattern = c.inString(1);
                const bool recursive = c.inBool(2);
                if (!startWork(c, [folder, pattern, recursive](Job& job) { listFolder(job, folder, pattern, recursive); })) return;
            }
            auto h = handleOf(c);
            Job& job = *h->job;
            if (!job.error.empty()) {
                c.out(2, Value(job.error));
                c.next(2);
                return;
            }
            if (h->next < job.entries.size()) {
                auto& [path, isFolder] = job.entries[h->next++];
                c.out(0, Value(path));
                c.out(1, Value(isFolder));
                c.out(2, Value(std::string()));
                c.call(1);
                return;
            }
            c.next(0);
        };
        reg.addNode(std::move(d));
    }
}

// ------------------------------------------------------------------ pure functions

std::string homeFolder() {
#if defined(_WIN32)
    // the wide variable: the name of the user can have any letters
    wchar_t* home = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&home, &length, L"USERPROFILE") != 0 || !home) return std::string();
    const std::string folder = textOf(fs::path(home));
    std::free(home);
    return folder;
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : std::string();
#endif
}

void registerFunctions(Registry& reg) {
    const std::string paths = std::string(kCategory) + "|Paths", info = std::string(kCategory) + "|Information";
    reg.addFunction("FileSystem.Join Path", [](const std::string& a, const std::string& b) {
        if (a.empty()) return b;
        return textOf(pathOf(a) / pathOf(b));
    }, {"A", "B"}).makePure().withCategory(paths).withResultName("Path").withKeywords("path combine concat folder file")
      .withDoc("B inside A: Join Path(\"notes\", \"today.txt\") is notes/today.txt (with \\ on Windows). An absolute B is B itself.");
    reg.addFunction("FileSystem.Parent Folder", [](const std::string& p) { return textOf(pathOf(p).parent_path()); }, {"Path"})
        .makePure().withCategory(paths).withResultName("Folder").withKeywords("path parent directory dirname up")
        .withDoc("The folder a path is in: notes/today.txt -> notes. Empty when the path has no folder.");
    reg.addFunction("FileSystem.File Name", [](const std::string& p) { return textOf(pathOf(p).filename()); }, {"Path"})
        .makePure().withCategory(paths).withResultName("Name").withKeywords("path name basename file")
        .withDoc("The last part of a path, with its extension: notes/today.txt -> today.txt.");
    reg.addFunction("FileSystem.Extension", [](const std::string& p) { return textOf(pathOf(p).extension()); }, {"Path"})
        .makePure().withCategory(paths).withResultName("Extension").withKeywords("path extension suffix type")
        .withDoc("The extension of a path with its dot: today.txt -> .txt. Empty when there is none.");
    reg.addFunction("FileSystem.Change Extension", [](const std::string& p, const std::string& ext) {
        return textOf(pathOf(p).replace_extension(pathOf(ext)));
    }, {"Path", "Extension"}).makePure().withCategory(paths).withResultName("Path").withKeywords("path extension replace rename suffix")
      .withDoc("The path with another extension: today.txt and .md -> today.md. An empty Extension removes it.");
    reg.addFunction("FileSystem.Absolute Path", [](const std::string& p) {
        std::error_code ec;
        const fs::path a = fs::absolute(pathOf(p), ec);
        return ec ? p : textOf(a.lexically_normal());
    }, {"Path"}).makePure().withCategory(paths).withResultName("Path").withKeywords("path absolute full resolve normalize")
      .withDoc("The full path, from the root: a relative path is taken from the working directory; . and .. are resolved.");
    reg.addFunction("FileSystem.Current Folder", [] {
        std::error_code ec;
        const fs::path p = fs::current_path(ec);
        return ec ? std::string() : textOf(p);
    }).makePure().withCategory(paths).withResultName("Folder").withKeywords("working directory current cwd pwd")
      .withDoc("The working directory: where relative paths start.");
    reg.addFunction("FileSystem.Temp Folder", [] {
        std::error_code ec;
        const fs::path p = fs::temp_directory_path(ec);
        return ec ? std::string() : textOf(p);
    }).makePure().withCategory(paths).withResultName("Folder").withKeywords("temporary temp tmp directory")
      .withDoc("The folder of the system for temporary files.");
    reg.addFunction("FileSystem.Home Folder", [] { return homeFolder(); }).makePure().withCategory(paths).withResultName("Folder")
        .withKeywords("home user profile directory").withDoc("The home folder of the user.");

    reg.addFunction("FileSystem.Exists", [](const std::string& p) {
        std::error_code ec;
        return !p.empty() && fs::exists(pathOf(p), ec);
    }, {"Path"}).makePure().withCategory(info).withResultName("Exists").withKeywords("file folder exists there test check")
      .withDoc("Whether there is a file or a folder at the path.");
    reg.addFunction("FileSystem.Is File", [](const std::string& p) {
        std::error_code ec;
        return !p.empty() && fs::is_regular_file(pathOf(p), ec);
    }, {"Path"}).makePure().withCategory(info).withResultName("Is File").withKeywords("file test check")
      .withDoc("Whether there is a file at the path.");
    reg.addFunction("FileSystem.Is Folder", [](const std::string& p) {
        std::error_code ec;
        return !p.empty() && fs::is_directory(pathOf(p), ec);
    }, {"Path"}).makePure().withCategory(info).withResultName("Is Folder").withKeywords("folder directory test check")
      .withDoc("Whether there is a folder at the path.");
    reg.addFunction("FileSystem.File Size", [](const std::string& p) {
        std::error_code ec;
        const fs::path file = pathOf(p);
        if (!fs::is_regular_file(file, ec)) return int64_t(-1);
        const auto size = fs::file_size(file, ec);
        return ec ? int64_t(-1) : static_cast<int64_t>(size);
    }, {"Path"}).makePure().withCategory(info).withResultName("Bytes").withKeywords("file size length bytes")
      .withDoc("The size of a file in bytes; -1 when there is no file at the path.");
    reg.addFunction("FileSystem.Last Modified", [](const std::string& p) {
        std::error_code ec;
        const auto t = fs::last_write_time(pathOf(p), ec);
        if (ec) return -1.0;
#if defined(__GLIBCXX__)
        const auto sys = std::chrono::file_clock::to_sys(t);        // MSVC's file_clock has no to_sys, older libstdc++ no clock_cast
#else
        const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
#endif
        return std::chrono::duration<double>(sys.time_since_epoch()).count();
    }, {"Path"}).makePure().withCategory(info).withResultName("Timestamp").withKeywords("file time date modified changed")
      .withDoc("When a file or folder was last changed, as a timestamp (seconds since 1970-01-01 00:00 UTC; Format Date Time "
               "writes it); -1 when there is nothing at the path.");
}

// The File type, FileMode and Open File: a file read a line at a time or written a piece at a time.
void registerOpenFiles(Registry& reg) {
    reg.registerEnum<FileMode>("FileMode", kCategory)
        .doc("How Open File opens a file.")
        .value("Read", FileMode::Read, "To read it")
        .value("Write", FileMode::Write, "To write it: it starts empty")
        .value("Append", FileMode::Append, "To write after what it has (a new file when there is none)");

    auto file = reg.registerClass<OpenFile>("File", kCategory);
    file.doc("A file Open File opened: read a line at a time, or written a piece at a time (each write is in the file at once). "
             "Close it when it is done: until then other programs may not be able to delete or rename it.")
        .method("Is At End", &OpenFile::atEnd).keywords("end eof finished done")
        .describe("Whether everything has been read (always for a file opened for writing, or a closed one).")
        .readonly("Path", [](const OpenFile& f) { return f.path(); })
        .readonly("Is Open", [](const OpenFile& f) { return f.isOpen(); });
    file.type()->properties[0].doc = "The path it was opened with";
    file.type()->properties[1].doc = "Whether it is open (not closed yet)";
    reg.mutableFunction("File.Is At End")->retName = "At End";

    // Reading, writing and closing: nodes of their own (not methods), for their Failed output. They are quick (a buffer, one
    // write), so they run at once on the thread of the script. `act` does the work: false and the reason when it cannot.
    using Act = std::function<bool(NodeCtx&, OpenFile&, std::string& error)>;
    auto fileNode = [&reg](const char* id, const char* title, const char* keywords, std::string text, std::vector<PinDef> inputs,
                           std::vector<PinDef> outputs, Act act) {
        NodeDef d;
        d.id = id;
        d.title = title;
        d.category = std::string(kCategory) + "|File";
        d.keywords = keywords;
        d.doc = std::move(text) + " Failed, and the reason in Error, when it cannot be done (a closed file, a file opened the "
                                 "other way, a failing disk).";
        d.pins.push_back(execIn());
        d.pins.push_back(doc(dataIn("Target", "File"), "The file Open File opened"));
        for (auto& p : inputs) d.pins.push_back(std::move(p));
        d.pins.push_back(execOut());
        d.pins.push_back(doc(execOut("Failed"), "When it could not be done: Error says why"));
        for (auto& p : outputs) d.pins.push_back(std::move(p));
        d.pins.push_back(doc(dataOut("Error", "string"), "Why it failed (empty when it worked)"));
        const size_t errorOut = outputs.size();
        d.run = [act, errorOut](NodeCtx& c) {
            std::string error;
            auto f = c.registry().unwrap<OpenFile>(c.in(0));
            if (!f) error = "no file: connect the File of Open File to Target";
            else if (act(c, *f, error)) {
                c.out(errorOut, Value(std::string()));
                c.next(0);
                return;
            }
            c.out(errorOut, Value(std::move(error)));
            c.next(1);
        };
        reg.addNode(std::move(d));
    };
    fileNode("filesystem.file.readline", "Read Line", "file read line next text",
             "Reads the next line of the file, without its line break; empty at the end (Is At End says when).", {},
             {doc(dataOut("Line", "string"), "The line (empty when it failed)")}, [](NodeCtx& c, OpenFile& f, std::string& error) {
                 std::string line;
                 const bool ok = f.readLine(line, error);
                 c.out(0, Value(std::move(line)));
                 return ok;
             });
    {   // Read Text: the rest can be big - on a work thread, as Copy
        NodeDef d = waitingNode("filesystem.file.readtext", "Read Text", "File", "file read rest all text content",
                                "Reads the rest of the file, from where reading is. A big file is read in the background (Stop "
                                "stops it); when it is too big for the memory, Read Line reads it a line at a time.",
                                {doc(dataIn("Target", "File"), "The file Open File opened")},
                                {doc(dataOut("Text", "string"), "The text (empty when it failed)")});
        d.run = [](NodeCtx& c) {
            auto fail = [&](std::string error) {
                c.out(0, Value(std::string()));
                c.out(1, Value(std::move(error)));
                c.next(1);
            };
            if (!c.resumed()) {
                std::string error;
                auto f = c.registry().unwrap<OpenFile>(c.in(0));
                if (!f) return fail("no file: connect the File of Open File to Target");
                if (!f->beginReadRest(error)) return fail(std::move(error));
                if (!startWork(c, [f](Job& job) {
                        f->readRest(job.text, job.error, job.cancelled);
                        f->endReadRest();
                    }))
                    return;
            }
            Job& job = *handleOf(c)->job;
            if (!job.error.empty()) return fail(job.error);
            c.out(0, Value(std::move(job.text)));
            c.out(1, Value(std::string()));
            c.next(0);
        };
        reg.addNode(std::move(d));
    }
    // Write Text / Write Line: written from the input's own string (no copy of it). A text of a piece (1 MiB) or more is written on a
    // work thread, as Copy; a smaller one at once (a thread for each line of a loop would cost more than the line).
    auto writeNode = [&reg](const char* id, const char* title, const char* keywords, std::string text, bool line) {
        NodeDef d = waitingNode(id, title, "File", keywords, std::move(text),
                                {doc(dataIn("Target", "File"), "The file Open File opened"), dataIn("Text", "string", Value(""))}, {});
        d.run = [line](NodeCtx& c) {
            auto fail = [&](std::string error) {
                c.out(0, Value(std::move(error)));
                c.next(1);
            };
            if (!c.resumed()) {
                std::string error;
                auto f = c.registry().unwrap<OpenFile>(c.in(0));
                if (!f) return fail("no file: connect the File of Open File to Target");
                const Value text = c.in(1).isString() ? c.in(1) : Value(c.inString(1));   // shares the string
                if (text.str().size() < kChunk) {
                    if (!f->canWrite(error) || !f->write(text.str(), line, error)) return fail(std::move(error));
                    c.out(0, Value(std::string()));
                    c.next(0);
                    return;
                }
                if (!f->beginWrite(error)) return fail(std::move(error));
                if (!startWork(c, [f, text, line](Job& job) {
                        f->write(text.str(), line, job.error, &job.cancelled);
                        f->endWrite();
                    }))
                    return;
            }
            Job& job = *handleOf(c)->job;
            if (!job.error.empty()) return fail(job.error);
            c.out(0, Value(std::string()));
            c.next(0);
        };
        reg.addNode(std::move(d));
    };
    writeNode("filesystem.file.writeline", "Write Line", "file write line text append add",
              "Writes the text (UTF-8) and a line break (\\n). A big text is written in the background (Stop stops it).", true);
    writeNode("filesystem.file.writetext", "Write Text", "file write text append add",
              "Writes the text (UTF-8) at the end of what was written. A big text is written in the background (Stop stops it).",
              false);
    fileNode("filesystem.file.close", "Close", "file close end finish release",
             "Closes the file, so that other programs can delete or rename it. Closing a closed file does nothing.", {}, {},
             [](NodeCtx&, OpenFile& f, std::string& error) { return f.close(error); });

    NodeDef d = waitingNode("filesystem.open", "Open File", "File", "file open read write append stream handle create",
                            "Opens a file to read it a line at a time (Read), or to write it a piece at a time: Write starts it "
                            "empty, Append writes after what it has. File reads and writes it; close it with Close when "
                            "it is done.",
                            {dataIn("Path", "string", Value("")),
                             doc(dataIn("Mode", "FileMode", Value(int64_t(0))), "Read, Write (it starts empty) or Append")},
                            {doc(dataOut("File", "File"), "The open file")});
    d.run = [](NodeCtx& c) {
        std::string path;
        FileMode mode = FileMode::Read;
        if (!c.resumed()) {
            path = c.inString(0);
            const int64_t m = c.inInt(1);
            mode = m == 1 ? FileMode::Write : m == 2 ? FileMode::Append : FileMode::Read;
        }
        runJob(c, 1, [path, mode](Job& job) {
            auto f = std::make_shared<OpenFile>(path, mode);
            if (f->open(job.error)) job.file = f;
        }, [](NodeCtx& c, Job& job) { c.out(0, c.registry().wrap(std::static_pointer_cast<OpenFile>(job.file))); });
    };
    reg.addNode(std::move(d));
}

} // namespace

FG_PLUGIN(reg) {
    registerOpenFiles(reg);
    registerFunctions(reg);
    registerWaitingNodes(reg);
}
