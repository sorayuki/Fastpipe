#pragma once

#include <string>
#include <vector>

namespace fastpipe {

struct CommandParseResult {
    bool ok = false;
    std::vector<std::vector<std::wstring>> commands;
    std::wstring error;
};

CommandParseResult parse_commands(int argc, wchar_t* argv[]);
std::wstring quote_windows_argument(const std::wstring& argument);
std::wstring build_command_line(const std::vector<std::wstring>& arguments);

}