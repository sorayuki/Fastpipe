#include "stdio_injection.h"

#include "win_handle.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fastpipe {
namespace {

enum class target_architecture {
    unknown,
    x86,
    x64,
    arm64,
    arm64ec,
};

#ifndef IMAGE_FILE_MACHINE_ARM64EC
#define IMAGE_FILE_MACHINE_ARM64EC 0xA641
#endif

std::wstring executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring();
        }
        buffer.resize(buffer.size() * 2);
    }
}

target_architecture machine_to_architecture(USHORT machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
        return target_architecture::x86;
    case IMAGE_FILE_MACHINE_AMD64:
        return target_architecture::x64;
    case IMAGE_FILE_MACHINE_ARM64:
        return target_architecture::arm64;
    case IMAGE_FILE_MACHINE_ARM64EC:
        return target_architecture::arm64ec;
    default:
        return target_architecture::unknown;
    }
}

std::filesystem::path resolve_executable(const std::wstring& name) {
    DWORD required = SearchPathW(nullptr, name.c_str(), L".exe", 0, nullptr, nullptr);
    if (required == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(required + 1, L'\0');
    const DWORD length = SearchPathW(
        nullptr,
        name.c_str(),
        L".exe",
        static_cast<DWORD>(buffer.size()),
        buffer.data(),
        nullptr);
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

target_architecture current_architecture() {
#if defined(_M_ARM64EC)
    return target_architecture::arm64ec;
#elif defined(_M_ARM64)
    return target_architecture::arm64;
#elif defined(_M_X64)
    return target_architecture::x64;
#elif defined(_M_IX86)
    return target_architecture::x86;
#else
    return target_architecture::unknown;
#endif
}

target_architecture read_executable_architecture(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return target_architecture::unknown;
    }

    IMAGE_DOS_HEADER dos_header{};
    DWORD bytes_read = 0;
    bool valid = ReadFile(
                     file,
                     &dos_header,
                     static_cast<DWORD>(sizeof(dos_header)),
                     &bytes_read,
                     nullptr) != FALSE &&
                 bytes_read == sizeof(dos_header) &&
                 dos_header.e_magic == IMAGE_DOS_SIGNATURE;
    if (!valid) {
        CloseHandle(file);
        return target_architecture::unknown;
    }

    LARGE_INTEGER offset{};
    offset.QuadPart = dos_header.e_lfanew;
    valid = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != FALSE;

    DWORD signature = 0;
    IMAGE_FILE_HEADER file_header{};
    if (valid) {
        valid = ReadFile(
                    file,
                    &signature,
                    static_cast<DWORD>(sizeof(signature)),
                    &bytes_read,
                    nullptr) != FALSE &&
                bytes_read == sizeof(signature) &&
                signature == IMAGE_NT_SIGNATURE;
    }
    if (valid) {
        valid = ReadFile(
                    file,
                    &file_header,
                    static_cast<DWORD>(sizeof(file_header)),
                    &bytes_read,
                    nullptr) != FALSE &&
                bytes_read == sizeof(file_header);
    }

    CloseHandle(file);
    return valid ? machine_to_architecture(file_header.Machine) : target_architecture::unknown;
}

const wchar_t* architecture_name(target_architecture architecture) {
    switch (architecture) {
    case target_architecture::x86:
        return L"x86";
    case target_architecture::x64:
        return L"x64";
    case target_architecture::arm64:
        return L"arm64";
    case target_architecture::arm64ec:
        return L"arm64ec";
    default:
        return nullptr;
    }
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(required + 1, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        name,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::filesystem::path find_hook_path(target_architecture architecture) {
    const wchar_t* name = architecture_name(architecture);
    if (name == nullptr) {
        return {};
    }

    const std::filesystem::path dll_name(
        std::wstring(L"fp_stdio_hook_") + name + L".dll");
    const std::filesystem::path executable_dir = executable_directory();
    if (executable_dir.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path explicit_directory = environment_value(L"FASTPIPE_STDIO_HOOK_DIR");
    if (!explicit_directory.empty()) {
        candidates.push_back(explicit_directory / dll_name);
    }

    candidates.push_back(executable_dir / dll_name);

    for (const wchar_t* configuration : {L"Release", L"Debug"}) {
        candidates.push_back(
            executable_dir / L".." / L"injection-artifacts" / name / configuration / dll_name);
        candidates.push_back(
            executable_dir / L"injection-artifacts" / name / configuration / dll_name);
    }
    candidates.push_back(executable_dir / dll_name);

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return std::filesystem::absolute(candidate, error);
        }
    }
    return {};
}

std::wstring widen_path(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_ACP,
        MB_ERR_INVALID_CHARS,
        path.c_str(),
        -1,
        nullptr,
        0);
    if (required <= 1) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_ACP,
            MB_ERR_INVALID_CHARS,
            path.c_str(),
            -1,
            result.data(),
            required) == 0) {
        return {};
    }
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

std::string narrow_path(const std::filesystem::path& path) {
    const std::wstring wide_path = path.wstring();
    const int required = WideCharToMultiByte(
        CP_ACP,
        WC_NO_BEST_FIT_CHARS,
        wide_path.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            wide_path.c_str(),
            -1,
            result.data(),
            required,
            nullptr,
            nullptr) == 0) {
        return {};
    }
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

bool inject_library(HANDLE process, const std::string& path) {
    const std::wstring wide_path = widen_path(path);
    if (wide_path.empty()) {
        return false;
    }

    const SIZE_T allocation_size = (wide_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(
        process,
        nullptr,
        allocation_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (remote_path == nullptr) {
        return false;
    }

    SIZE_T written = 0;
    const bool written_ok = WriteProcessMemory(
        process,
        remote_path,
        wide_path.c_str(),
        allocation_size,
        &written) != FALSE && written == allocation_size;
    if (!written_ok) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto load_library = kernel32 == nullptr
        ? nullptr
        : reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (load_library == nullptr) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    unique_handle thread(CreateRemoteThread(
        process,
        nullptr,
        0,
        load_library,
        remote_path,
        0,
        nullptr));
    if (!thread || WaitForSingleObject(thread.get(), INFINITE) != WAIT_OBJECT_0) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        return false;
    }

    DWORD module_result = 0;
    const bool loaded = GetExitCodeThread(thread.get(), &module_result) != FALSE && module_result != 0;
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    return loaded;
}

}  // namespace

std::string find_stdio_hook_for_command(const std::vector<std::wstring>& command) {
    if (command.empty()) {
        return {};
    }

    const std::filesystem::path executable = resolve_executable(command.front());
    if (executable.empty()) {
        return {};
    }

    const target_architecture architecture = read_executable_architecture(executable);
    return narrow_path(find_hook_path(architecture));
}

bool can_direct_inject_stdio_hook(const std::vector<std::wstring>& command) {
    if (command.empty()) {
        return false;
    }

    const std::filesystem::path executable = resolve_executable(command.front());
    if (executable.empty()) {
        return false;
    }

    return read_executable_architecture(executable) == current_architecture();
}

bool inject_stdio_hook(HANDLE process, const std::string& path) {
    return process != nullptr && process != INVALID_HANDLE_VALUE && inject_library(process, path);
}

}