#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace fastpipe {

std::string find_stdio_hook_for_command(const std::vector<std::wstring>& command);
bool can_direct_inject_stdio_hook(const std::vector<std::wstring>& command);
bool inject_stdio_hook(HANDLE process, const std::string& path);

}