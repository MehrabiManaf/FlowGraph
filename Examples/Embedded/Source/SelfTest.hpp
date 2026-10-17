#pragma once
#include "MainWindow.hpp"

namespace msgapp {

// `embedded_example --selftest`: drives the application like a user would (new table, feed messages, load a script, save it, edit, attach)
// without showing anything, and checks the tables. The messages are fed by the test instead of the timer, so that it is deterministic.
// Returns the number of failed checks.
int runSelfTest(fg::Runtime& rt, MessageStore& store);

} // namespace msgapp
