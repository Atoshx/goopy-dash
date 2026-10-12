#pragma once
#include <string_view>

// Melty's test runs: while %LOCALAPPDATA%\Melty\test\goopy-dash.json exists, the mod
// opens a level by itself, keeps the player alive, and reports what Goopy did.
namespace goopy::test {
bool active();
void report(std::string_view check, bool pass, std::string_view why = {});
}
