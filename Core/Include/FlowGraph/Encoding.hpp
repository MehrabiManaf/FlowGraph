// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Text and bytes: UTF-8, Base64 and hex. Used by the Bytes nodes, by .flow files (bytes are Base64 there) and by editors (hex).
#include <FlowGraph/Export.hpp>

#include <string>
#include <string_view>

namespace fg {

// The UTF-8 text of the bytes `b`. A byte that is not part of a valid UTF-8 sequence becomes U+FFFD (one for each longest invalid
// piece, as the Unicode standard recommends); `valid` says whether there was none.
FG_CORE_EXPORT std::string bytesToText(std::string_view b, bool* valid = nullptr);

// Base64 (RFC 4648) with padding. Decoding ignores white space, takes the URL alphabet (- _) too and does not need the padding;
// false (and `out` cleared) when `text` is not Base64.
FG_CORE_EXPORT std::string base64Encode(std::string_view b);
FG_CORE_EXPORT bool base64Decode(std::string_view text, std::string& out);

// Hex: "48656C6C6F", or "48 65 6C 6C 6F" with `spaced`. Decoding takes upper and lower case and ignores white space, ':' and '-'
// between the pairs; false (and `out` cleared) for anything else or an odd number of digits.
FG_CORE_EXPORT std::string hexEncode(std::string_view b, bool spaced = false);
FG_CORE_EXPORT bool hexDecode(std::string_view text, std::string& out);

} // namespace fg
