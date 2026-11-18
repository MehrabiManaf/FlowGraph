// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

namespace fg::file {

inline std::filesystem::path path(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

inline void replace(const std::string& filename, const std::string& text) {
    namespace fs = std::filesystem;
    // Saving through a symlink updates its destination. Stage beside the destination.
    fs::path target = fs::absolute(path(filename));
    if (fs::is_symlink(fs::symlink_status(target)))
        target = fs::weakly_canonical(target);
    const fs::file_status original = fs::status(target);
    if (fs::exists(original) && !fs::is_regular_file(original))
        throw std::runtime_error("cannot replace '" + filename + "': not a regular file");
    std::random_device random;
    fs::path staging;
    for (int attempt = 0; attempt < 16; ++attempt) {
        staging = target.parent_path() / (".flow-save-" + std::to_string(random()));
        if (fs::create_directory(staging))
            break;
        staging.clear();
    }
    if (staging.empty())
        throw std::runtime_error("cannot stage a save for '" + filename + "'");

    const fs::path stagedFile = staging / "new";
    try {
        std::ofstream out(stagedFile, std::ios::binary);
        out << text;
        out.close();
        if (!out)
            throw std::runtime_error("error while writing '" + filename + "'");
#ifndef _WIN32
        if (fs::is_regular_file(original))
            fs::permissions(stagedFile, original.permissions());
#endif
        fs::rename(stagedFile, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove(stagedFile, ignored);
        fs::remove(staging, ignored);
        throw;
    }
    std::error_code ignored;
    fs::remove(staging, ignored);
}

} // namespace fg::file
