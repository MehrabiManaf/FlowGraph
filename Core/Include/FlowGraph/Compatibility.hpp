#pragma once
// Can this application run that script? A script is produced for some application: it refers to the events
// that application raises (Main, Tick, OnHit...), to its classes and functions and to the plugins it needs.
// An application that does not provide them must refuse the script ("I cannot load it") instead of running
// half of it. checkCompatibility() answers that question without compiling anything.
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>
#include <FlowGraph/Script.hpp>

#include <string>
#include <vector>

namespace fg {

struct CompatibilityIssue {
    enum class Kind {
        MissingEvent,           // the script handles an event this application never raises
        EventSignatureChanged,  // ... or the event now has other arguments than when the script was made
        MissingNode,            // a function / class / enum node that is not registered (plugin missing?)
        UnknownType,            // a variable or parameter of a type that is not registered
    };
    Kind kind = Kind::MissingNode;
    std::string graph;
    NodeId node = 0;            // 0 when the issue is not tied to a node
    std::string subject;        // event / node / type name
    std::string message;
};

struct FG_CORE_EXPORT CompatibilityReport {
    std::vector<CompatibilityIssue> issues;
    bool ok() const { return issues.empty(); }
    // One line per distinct problem ("the application has no event 'Tick' (used 2 times)").
    std::string toText() const;
    std::vector<std::string> missingEvents() const;
};

FG_CORE_EXPORT CompatibilityReport checkCompatibility(const Script& script, const Registry& reg);

// "(Count: int, Names: array<string>)"
FG_CORE_EXPORT std::string describeParams(const std::vector<ParamDecl>& params);
FG_CORE_EXPORT std::string describeParams(const EventDesc& ev);

// True when the signature recorded in a host event node (props["sig"]) still matches the event. Nodes without
// a recorded signature (old scripts) always match.
FG_CORE_EXPORT bool hostEventSignatureMatches(const Node& node, const EventDesc& event, const Registry& reg, std::string* why = nullptr);

// Records the signature of every host event node that has none yet, so that a later change of the event is
// detected. Editors call this on every edit; scripts that were saved without signatures keep working.
FG_CORE_EXPORT void stampHostEvents(Script& script, const Registry& reg);

} // namespace fg
