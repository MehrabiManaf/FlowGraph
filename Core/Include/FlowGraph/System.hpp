// SPDX-FileCopyrightText: 2026 Abdolmanaf Mehrabi Sisakht
// SPDX-License-Identifier: MIT
#pragma once
// Nodes that reach the operating system: running programs and commands. OPT-IN - a script with these nodes can do anything the user
// running it can, so they are not built in: an application registers them when its scripts may use them. The standard applications
// (`flow`, `flowstudio`: standard::registerStandardApp) do; an application that embeds FlowGraph decides for itself, and
// CompileOptions::allowNode can still refuse them per script.
//
//   Run Command            runs a program (or a shell command) to its end; Exit Code, Output, Error Output, Timed Out. The task
//                          waits, the thread does not.
//   For Each Output Line   runs a program and goes through the lines it writes AS THEY COME (`tail -f`, a log, a build): the loop
//                          body runs for each line, the node waits between lines. Completed when the program has ended; a Break
//                          ends the loop and the program.
//   Operating System       Windows, Linux, macOS or Other (an enum: a Switch on Enum picks the command per platform).
//
// A program started by these nodes never outlives what started it: Stop, Abort, a Break, a hot reload or the end of the instance kill
// it, and with it everything it started (a shell's commands: a job object on Windows, a process group elsewhere).
#include <FlowGraph/Export.hpp>
#include <FlowGraph/Registry.hpp>

namespace fg {

FG_CORE_EXPORT void registerSystemNodes(Registry& reg);

} // namespace fg
