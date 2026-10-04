#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace fastpipe {

struct Config {
    DWORD buffer_size = 16u * 1024u * 1024u;
};

struct ConfigResult {
    bool ok = false;
    Config config;
    std::wstring error;
};

ConfigResult load_config();
ConfigResult parse_buffer_size(std::wstring_view value);

}