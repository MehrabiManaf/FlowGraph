// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
// The Bytes nodes: raw data (the content of a file, a BLOB, a network message), not text. A bytes value is one shared buffer -
// cheap to pass along, whatever its size; To Array / Make Bytes turn it into an array<byte> and back, for the array nodes.
#include <FlowGraph/Encoding.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Vm.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace fg {

namespace {

constexpr const char* kCategory = "Bytes";

NodeDef pureBytesNode(std::string id, std::string title) {
    NodeDef d;
    d.id = std::move(id);
    d.title = std::move(title);
    d.category = kCategory;
    d.pure = true;
    d.style = NodeStyle::Pure;
    return d;
}

std::string_view bytesIn(NodeCtx& c, size_t i) {
    const Value& v = c.in(i);
    return v.isBytes() ? v.bytesView() : std::string_view();
}

// From Base64 / From Hex: the bytes of a text, and whether the text was what it should be.
void decodeNode(Registry& r, const char* id, const char* title, const char* keywords, const char* doc, const char* example,
                bool (*decode)(std::string_view, std::string&)) {
    NodeDef d = pureBytesNode(id, title);
    d.keywords = keywords;
    d.doc = doc;
    d.pins = {dataIn("Text", "string", Value(example)), dataOut("Bytes", "bytes"), dataOut("Valid", "bool")};
    d.pins[2].doc = "Whether the text was valid (otherwise Bytes is empty)";
    d.run = [decode](NodeCtx& c) {
        std::string data;
        const bool ok = decode(c.inString(0), data);
        c.out(0, Value(Bytes(std::move(data))));
        c.out(1, Value(ok));
    };
    r.addNode(std::move(d));
}

} // namespace

void registerBytesNodes(Registry& r) {
    r.addFunction("Bytes.Length", [](const Bytes& b) { return static_cast<int64_t>(b.size()); }, {"Bytes"})
        .makePure().withResultName("Length").withKeywords("size count bytes")
        .withDoc("The number of bytes.");
    r.addFunction("Bytes.Byte At", [](const Bytes& b, int64_t index) {
        if (index < 0 || static_cast<uint64_t>(index) >= b.size())
            throw ScriptError("index " + std::to_string(index) + " is out of range (size " + std::to_string(b.size()) + ")");
        return static_cast<std::byte>(b[static_cast<size_t>(index)]);
    }, {"Bytes", "Index"}).makePure().withResultName("Byte").withKeywords("get index element")
        .withDoc("The byte at Index (from 0). To go through every byte, To Array and For Each are faster than Byte At in a loop.");
    r.addFunction("Bytes.Slice", [](const Bytes& b, int64_t start, int64_t length) {
        const size_t from = static_cast<size_t>(std::clamp<int64_t>(start, 0, static_cast<int64_t>(b.size())));
        const size_t count = length < 0 ? b.size() - from : std::min(static_cast<size_t>(length), b.size() - from);
        if (from == 0 && count == b.size()) return b;                   // all of it: the same buffer
        return Bytes(b.data() + from, count);
    }, {"Bytes", "Start", "Length"}).makePure().withDefault(2, Value(-1)).withKeywords("sub range part cut copy")
        .withDoc("Length bytes from byte Start (from 0); Length -1: to the end.");
    r.addFunction("Bytes.Concat", [](const Bytes& a, const Bytes& b) {
        if (b.empty()) return a;
        if (a.empty()) return b;
        std::string s;
        s.reserve(a.size() + b.size());
        s.append(a.view()).append(b.view());
        return Bytes(std::move(s));
    }, {"A", "B"}).makePure().withKeywords("append join +").withDoc("The bytes of A, then the bytes of B.");
    r.addFunction("Bytes.Find", [](const Bytes& b, const Bytes& what, int64_t start) {
        if (start < 0) start = 0;
        if (static_cast<uint64_t>(start) > b.size()) return int64_t(-1);
        const size_t p = b.view().find(what.view(), static_cast<size_t>(start));
        return p == std::string_view::npos ? int64_t(-1) : static_cast<int64_t>(p);
    }, {"Bytes", "What", "Start"}).makePure().withResultName("Index").withKeywords("index of search position")
        .withDoc("Position of the first What at or after byte Start, -1 when there is none.");
    r.addFunction("Bytes.Starts With", [](const Bytes& b, const Bytes& p) { return b.view().substr(0, p.size()) == p.view(); },
                  {"Bytes", "Prefix"}).makePure().withKeywords("begins prefix header magic");
    r.addFunction("Bytes.Ends With", [](const Bytes& b, const Bytes& s) {
        return b.size() >= s.size() && b.view().substr(b.size() - s.size()) == s.view();
    }, {"Bytes", "Suffix"}).makePure().withKeywords("suffix");

    // text
    r.addFunction("Bytes.From Text", [](const std::string& text) { return Bytes(text); }, {"Text"})
        .makePure().withResultName("Bytes").withKeywords("utf8 utf-8 encode string convert")
        .withDoc("The bytes of a text (UTF-8).");
    {
        NodeDef d = pureBytesNode("bytes.totext", "To Text");
        d.keywords = "utf8 utf-8 decode string convert";
        d.doc = "The text of bytes (UTF-8). Bytes that are not UTF-8 become \xEF\xBF\xBD (U+FFFD), and Valid is off.";
        d.pins = {dataIn("Bytes", "bytes"), dataOut("Text", "string"), dataOut("Valid", "bool")};
        d.pins[2].doc = "Whether all the bytes were UTF-8";
        d.run = [](NodeCtx& c) {
            bool valid = true;
            c.out(0, Value(bytesToText(bytesIn(c, 0), &valid)));
            c.out(1, Value(valid));
        };
        r.addNode(std::move(d));
    }

    // Base64 and hex
    r.addFunction("Bytes.To Base64", [](const Bytes& b) { return base64Encode(b.view()); }, {"Bytes"})
        .makePure().withResultName("Text").withKeywords("encode base64 text")
        .withDoc("The bytes as Base64 text (RFC 4648, with padding).");
    decodeNode(r, "bytes.frombase64", "From Base64", "decode base64 text",
               "The bytes of a Base64 text (white space is ignored; the URL alphabet - _ and a missing padding are fine).",
               "SGVsbG8=", base64Decode);
    r.addFunction("Bytes.To Hex", [](const Bytes& b, bool spaced) { return hexEncode(b.view(), spaced); }, {"Bytes", "Spaced"})
        .makePure().withDefault(1, Value(false)).withResultName("Text").withKeywords("encode hex hexadecimal text dump")
        .withDoc("The bytes as hex text: 48656C6C6F, or 48 65 6C 6C 6F with Spaced.");
    decodeNode(r, "bytes.fromhex", "From Hex", "decode hex hexadecimal text",
               "The bytes of a hex text: two digits a byte, upper or lower case; spaces, : and - between the bytes are fine.",
               "48 65 6C 6C 6F", hexDecode);

    // array<byte>
    r.addFunction("Bytes.To Array", [](const Bytes& b) {
        std::vector<std::byte> out(b.size());
        std::copy(b.data(), b.data() + b.size(), reinterpret_cast<uint8_t*>(out.data()));
        return out;
    }, {"Bytes"}).makePure().withResultName("Array").withKeywords("array list convert byte")
        .withDoc("The bytes as an array<byte>, for the array nodes (For Each, Set, Add...). Make Bytes turns it back.");
    r.addFunction("Bytes.Make Bytes", [](const std::vector<std::byte>& a) {
        return Bytes(a.data(), a.size());
    }, {"Array"}).makePure().withResultName("Bytes").withKeywords("array list convert byte build")
        .withDoc("Bytes from an array<byte> (To Array turns bytes into one).");
}

} // namespace fg
