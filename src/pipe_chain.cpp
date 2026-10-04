#include "pipe_chain.h"

namespace fastpipe {

bool create_pipe_chain(
    std::size_t pipe_count,
    DWORD buffer_size,
    std::vector<pipe_pair>& pipes,
    std::wstring& error) {
    pipes.clear();
    pipes.reserve(pipe_count);

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    for (std::size_t index = 0; index < pipe_count; ++index) {
        HANDLE read_handle = nullptr;
        HANDLE write_handle = nullptr;
        if (!CreatePipe(&read_handle, &write_handle, &attributes, buffer_size)) {
            error = L"CreatePipe failed: " + win32_error_message();
            return false;
        }

        pipe_pair pair;
        pair.read.reset(read_handle);
        pair.write.reset(write_handle);
        pipes.push_back(std::move(pair));
    }

    return true;
}

}