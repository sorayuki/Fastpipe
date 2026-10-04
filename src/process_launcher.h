#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace fastpipe {

struct PipelineResult {
    bool ok = false;
    DWORD exit_code = 0;
    std::wstring error;
};

PipelineResult run_pipeline(
    const std::vector<std::vector<std::wstring>>& commands,
    DWORD buffer_size);

}