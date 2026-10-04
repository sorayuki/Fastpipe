#pragma once

#include "win_handle.h"

#include <string>
#include <vector>

namespace fastpipe {

struct pipe_pair {
    unique_handle read;
    unique_handle write;
};

bool create_pipe_chain(
    std::size_t pipe_count,
    DWORD buffer_size,
    std::vector<pipe_pair>& pipes,
    std::wstring& error);

}