// A program for the tests of the System nodes (Run Command, For Each Output Line): the same on every platform.
//   args A B ...        prints each argument in brackets, one per line
//   exit N              ends with exit code N
//   err TEXT            writes TEXT to the standard error, "out" to the standard output
//   cwd                 prints the current directory
//   sleep SECONDS       waits, then prints "slept"
//   lines N MS          prints "line 1" .. "line N", MS milliseconds apart (flushed: they come one by one)
//   forever PIDFILE     writes its process id to PIDFILE, then prints "tick" every 20 ms until it is killed
//   big BYTES           prints BYTES x's (no line break)
//   utf8                prints a text that is not ASCII
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int run(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string cmd = argv[1];
    auto ms = [](long n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); };
    if (cmd == "args") {
        for (int i = 2; i < argc; ++i) std::printf("[%s]\n", argv[i]);
    } else if (cmd == "exit" && argc > 2) {
        return std::atoi(argv[2]);
    } else if (cmd == "err" && argc > 2) {
        std::fprintf(stderr, "%s\n", argv[2]);
        std::printf("out\n");
    } else if (cmd == "cwd") {
        std::printf("%s\n", std::filesystem::current_path().string().c_str());
    } else if (cmd == "sleep" && argc > 2) {
        ms(static_cast<long>(std::atof(argv[2]) * 1000));
        std::printf("slept\n");
    } else if (cmd == "lines" && argc > 3) {
        const int n = std::atoi(argv[2]);
        for (int i = 1; i <= n; ++i) {
            std::printf("line %d\n", i);
            std::fflush(stdout);
            if (i < n) ms(std::atol(argv[3]));
        }
    } else if (cmd == "forever" && argc > 2) {
        if (FILE* f = std::fopen(argv[2], "w")) {
            std::fprintf(f, "%ld", static_cast<long>(getpid()));
            std::fclose(f);
        }
        for (;;) {
            std::printf("tick\n");
            std::fflush(stdout);
            ms(20);
        }
    } else if (cmd == "big" && argc > 2) {
        const long n = std::atol(argv[2]);
        std::string chunk(65536, 'x');
        for (long left = n; left > 0; left -= static_cast<long>(chunk.size()))
            std::fwrite(chunk.data(), 1, left < static_cast<long>(chunk.size()) ? static_cast<size_t>(left) : chunk.size(), stdout);
    } else if (cmd == "utf8") {
        std::printf("h\xC3\xA9llo \xE2\x9C\x93\n");
    } else {
        return 2;
    }
    return 0;
}

#if defined(_WIN32)
// The arguments as they were passed (UTF-16), in UTF-8: a narrow main would get them in the ANSI code page.
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? static_cast<size_t>(n - 1) : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    return run(argc, argv.data());
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
