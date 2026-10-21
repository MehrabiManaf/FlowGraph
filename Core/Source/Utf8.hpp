// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Characters of the UTF-8 strings of scripts: the string nodes count and cut characters (Unicode code points), not bytes, so a
// character is a string of length 1 in every language ("سلام" has 4, not the 8 bytes it takes). Internal to the core library.
//
// A character starts at every byte that is not a continuation byte (10xxxxxx); the continuation bytes after it belong to it. For
// valid UTF-8 that is exactly a code point. Text that is not valid UTF-8 is cut the same way by every node, and every byte belongs to
// exactly one character (continuation bytes at the very start make one of their own), so no node can lose or duplicate bytes.
// The counting goes 8 bytes at a time (the continuation bytes of a 64 bit word are marked with a bit trick and counted with a
// popcount): a 100 byte string takes a few nanoseconds.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace fg::utf8 {

inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
// Does a character start at byte i?
inline bool starts(std::string_view s, size_t i) { return i == 0 || !isContinuation(s[i]); }

// The number of continuation bytes among the 8 bytes at p: a byte 10xxxxxx has its top bit set and the next one clear.
inline int continuationsIn8(const char* p) {
    uint64_t w;
    std::memcpy(&w, p, 8);
    return std::popcount(w & ~(w << 1) & 0x8080808080808080ull);
}

// Number of characters that start in bytes [0, end) of s (end <= s.size()).
inline size_t startsBefore(std::string_view s, size_t end) {
    if (end == 0) return 0;
    size_t continuations = 0, i = 0;
    for (; i + 8 <= end; i += 8) continuations += static_cast<size_t>(continuationsIn8(s.data() + i));
    for (; i < end; ++i) continuations += isContinuation(s[i]);
    return end - continuations + (isContinuation(s[0]) ? 1 : 0);      // a broken start is a character of its own
}

// The byte offset of the character after the one that starts at `i` (i < s.size()).
inline size_t next(std::string_view s, size_t i) {
    ++i;
    while (i < s.size() && isContinuation(s[i])) ++i;
    return i;
}

// Number of characters.
inline size_t length(std::string_view s) { return startsBefore(s, s.size()); }

// Byte offset of character `index`; s.size() when the string has fewer characters.
inline size_t offset(std::string_view s, size_t index) {
    if (index == 0) return 0;
    size_t n = 0, i = 0;                                // n: characters that start before byte i
    // whole words while the character is not in them
    for (; i + 8 <= s.size(); i += 8) {
        const size_t in = 8 - static_cast<size_t>(continuationsIn8(s.data() + i)) + (i == 0 && isContinuation(s[0]) ? 1 : 0);
        if (n + in > index) break;
        n += in;
    }
    for (; i < s.size(); ++i)
        if (starts(s, i) && n++ == index) return i;
    return s.size();
}

// Character index of the character that starts at byte offset `byteOffset` (a position a byte search found).
inline size_t index(std::string_view s, size_t byteOffset) { return startsBefore(s, byteOffset < s.size() ? byteOffset : s.size()); }

} // namespace fg::utf8
