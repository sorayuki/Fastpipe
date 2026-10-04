#include "config.h"

#include <limits>
#include <vector>

namespace fastpipe {
namespace {

constexpr unsigned long long kDefaultBufferSize = 16ull * 1024ull * 1024ull;
constexpr unsigned long long kMinimumBufferSize = 4ull * 1024ull;
constexpr unsigned long long kMaximumBufferSize = 64ull * 1024ull * 1024ull;

ConfigResult make_error(const std::wstring& error) {
    ConfigResult result;
    result.error = error;
    return result;
}

}  // namespace

ConfigResult parse_buffer_size(std::wstring_view value) {
    if (value.empty()) {
        return make_error(L"FP_BUFFERSIZE cannot be empty");
    }

    unsigned long long parsed = 0;
    for (wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            return make_error(L"FP_BUFFERSIZE must contain only decimal digits");
        }

        const unsigned long long digit = static_cast<unsigned long long>(character - L'0');
        if (parsed > (std::numeric_limits<unsigned long long>::max() - digit) / 10ull) {
            return make_error(L"FP_BUFFERSIZE is too large");
        }
        parsed = parsed * 10ull + digit;
    }

    if (parsed < kMinimumBufferSize || parsed > kMaximumBufferSize) {
        return make_error(L"FP_BUFFERSIZE must be between 4096 and 67108864 bytes");
    }

    ConfigResult result;
    result.ok = true;
    result.config.buffer_size = static_cast<DWORD>(parsed);
    return result;
}

ConfigResult load_config() {
    DWORD required_length = GetEnvironmentVariableW(L"FP_BUFFERSIZE", nullptr, 0);
    if (required_length == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            ConfigResult result;
            result.ok = true;
            result.config.buffer_size = static_cast<DWORD>(kDefaultBufferSize);
            return result;
        }
        return make_error(L"could not read FP_BUFFERSIZE");
    }

    std::vector<wchar_t> buffer(required_length + 1, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"FP_BUFFERSIZE",
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return make_error(L"could not read FP_BUFFERSIZE");
    }
    return parse_buffer_size(std::wstring_view(buffer.data(), length));
}

}