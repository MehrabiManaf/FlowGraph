// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#include <FlowGraph/Encoding.hpp>

#include <cstdint>

namespace fg {

std::string bytesToText(std::string_view b, bool* valid) {
    static constexpr char kReplacement[] = "\xEF\xBF\xBD";      // U+FFFD
    std::string out;
    out.reserve(b.size());
    bool ok = true;
    const size_t n = b.size();
    size_t i = 0;
    while (i < n) {
        const unsigned c = static_cast<unsigned char>(b[i]);
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        size_t len = 0;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        if (len == 0) {                                          // a continuation byte or a byte UTF-8 never uses
            out += kReplacement;
            ok = false;
            ++i;
            continue;
        }
        // the continuation bytes; the second one is narrower after some leads (no overlong forms, no surrogates, nothing > U+10FFFF)
        size_t j = 1;
        for (; j < len && i + j < n; ++j) {
            const unsigned cc = static_cast<unsigned char>(b[i + j]);
            unsigned lo = 0x80, hi = 0xBF;
            if (j == 1) {
                if (c == 0xE0) lo = 0xA0;
                else if (c == 0xED) hi = 0x9F;
                else if (c == 0xF0) lo = 0x90;
                else if (c == 0xF4) hi = 0x8F;
            }
            if (cc < lo || cc > hi) break;
        }
        if (j == len) {
            out.append(b.substr(i, len));
            i += len;
        } else {                                                 // the longest valid start of a sequence: one replacement
            out += kReplacement;
            ok = false;
            i += j;
        }
    }
    if (valid) *valid = ok;
    return out;
}

namespace {

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Digit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string base64Encode(std::string_view b) {
    std::string out;
    out.reserve((b.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        const uint32_t v = (static_cast<uint32_t>(static_cast<unsigned char>(b[i])) << 16) |
                           (static_cast<uint32_t>(static_cast<unsigned char>(b[i + 1])) << 8) | static_cast<unsigned char>(b[i + 2]);
        out += kBase64[v >> 18];
        out += kBase64[(v >> 12) & 63];
        out += kBase64[(v >> 6) & 63];
        out += kBase64[v & 63];
    }
    const size_t left = b.size() - i;
    if (left > 0) {
        uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(b[i])) << 16;
        if (left == 2) v |= static_cast<uint32_t>(static_cast<unsigned char>(b[i + 1])) << 8;
        out += kBase64[v >> 18];
        out += kBase64[(v >> 12) & 63];
        out += left == 2 ? kBase64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(std::string_view text, std::string& out) {
    out.clear();
    out.reserve(text.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0, digits = 0;
    size_t padding = 0;
    for (char c : text) {
        if (isSpace(c)) continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        const int d = base64Digit(c);
        if (d < 0 || padding > 0) {                              // not Base64, or a digit after the padding
            out.clear();
            return false;
        }
        acc = (acc << 6) | static_cast<uint32_t>(d);
        bits += 6;
        ++digits;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    // a lone digit at the end carries no byte; the padding must fit what is missing
    if (digits % 4 == 1 || padding > 2 || (padding > 0 && (digits + padding) % 4 != 0)) {
        out.clear();
        return false;
    }
    return true;
}

std::string hexEncode(std::string_view b, bool spaced) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(b.size() * (spaced ? 3 : 2));
    for (size_t i = 0; i < b.size(); ++i) {
        const auto c = static_cast<unsigned char>(b[i]);
        if (spaced && i) out += ' ';
        out += kHex[c >> 4];
        out += kHex[c & 15];
    }
    return out;
}

bool hexDecode(std::string_view text, std::string& out) {
    out.clear();
    out.reserve(text.size() / 2);
    int high = -1;
    for (char c : text) {
        if (isSpace(c) || c == ':' || c == '-') {
            if (high >= 0) {                                     // a separator inside a pair
                out.clear();
                return false;
            }
            continue;
        }
        const int d = hexDigit(c);
        if (d < 0) {
            out.clear();
            return false;
        }
        if (high < 0) {
            high = d;
        } else {
            out += static_cast<char>((high << 4) | d);
            high = -1;
        }
    }
    if (high >= 0) {                                             // an odd number of digits
        out.clear();
        return false;
    }
    return true;
}

} // namespace fg
