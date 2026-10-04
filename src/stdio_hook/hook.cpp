#include <windows.h>
#include <winternl.h>

#include <array>
#include <cstddef>
#include <cwchar>
#include <cwctype>
#include <fcntl.h>
#include <limits>
#include <vector>

#include "detours.h"

namespace {

constexpr std::size_t kDefaultBufferSize = 16u * 1024u * 1024u;
constexpr std::size_t kMinimumBufferSize = 4u * 1024u;
constexpr std::size_t kMaximumBufferSize = 64u * 1024u * 1024u;
constexpr std::size_t kMaxCrtModules = 10;

std::size_t g_buffer_size = kDefaultBufferSize;

using setmode_fn = int(__cdecl*)(int, int);
using iob_fn = void*(__cdecl*)(unsigned int);
using fileno_fn = int(__cdecl*)(void*);
using setvbuf_fn = int(__cdecl*)(void*, char*, int, std::size_t);
using hook_setmode_fn = int(__cdecl*)(int, int);

#ifndef LDR_DLL_NOTIFICATION_REASON_LOADED
#define LDR_DLL_NOTIFICATION_REASON_LOADED 1
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED 2

typedef struct _FASTPIPE_LDR_DLL_NOTIFICATION_DATA {
    ULONG Flags;
    const UNICODE_STRING* FullDllName;
    const UNICODE_STRING* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
} FASTPIPE_LDR_DLL_NOTIFICATION_DATA, *PFASTPIPE_LDR_DLL_NOTIFICATION_DATA;

typedef struct _FASTPIPE_LDR_DLL_NOTIFICATION_DATA_WRAPPER {
    union {
        FASTPIPE_LDR_DLL_NOTIFICATION_DATA Loaded;
        FASTPIPE_LDR_DLL_NOTIFICATION_DATA Unloaded;
    };
} FASTPIPE_LDR_DLL_NOTIFICATION_DATA_WRAPPER, *PFASTPIPE_LDR_DLL_NOTIFICATION_DATA_WRAPPER;

typedef VOID(CALLBACK* FASTPIPE_LDR_DLL_NOTIFICATION_FUNCTION)(
    ULONG,
    const FASTPIPE_LDR_DLL_NOTIFICATION_DATA_WRAPPER*,
    PVOID);
#endif

using ldr_register_dll_notification_fn = NTSTATUS(NTAPI*)(
    ULONG,
    FASTPIPE_LDR_DLL_NOTIFICATION_FUNCTION,
    PVOID,
    PVOID*);
using ldr_unregister_dll_notification_fn = NTSTATUS(NTAPI*)(PVOID);

struct CrtHook {
    const wchar_t* name = nullptr;
    HMODULE module = nullptr;
    setmode_fn original_setmode = nullptr;
    iob_fn iob = nullptr;
    fileno_fn fileno = nullptr;
    setvbuf_fn setvbuf = nullptr;
    char* stdin_buffer = nullptr;
    char* stdout_buffer = nullptr;
    hook_setmode_fn detour = nullptr;
    bool hooked = false;
};

CRITICAL_SECTION g_lock;
std::array<CrtHook, kMaxCrtModules> g_hooks = {{
    {L"msvcrt.dll"},
    {L"msvcr70.dll"},
    {L"msvcr71.dll"},
    {L"msvcr80.dll"},
    {L"msvcr90.dll"},
    {L"msvcr100.dll"},
    {L"msvcr110.dll"},
    {L"msvcr120.dll"},
    {L"vcruntime140.dll"},
    {L"ucrtbase.dll"},
}};

PVOID g_dll_notification_cookie = nullptr;
ldr_unregister_dll_notification_fn g_unregister_dll_notification = nullptr;

int __cdecl hook_setmode(std::size_t index, int file_descriptor, int mode);

#define DEFINE_SETMODE_HOOK(index) \
    int __cdecl hook_setmode_##index(int file_descriptor, int mode) { \
        return hook_setmode(index, file_descriptor, mode); \
    }

DEFINE_SETMODE_HOOK(0)
DEFINE_SETMODE_HOOK(1)
DEFINE_SETMODE_HOOK(2)
DEFINE_SETMODE_HOOK(3)
DEFINE_SETMODE_HOOK(4)
DEFINE_SETMODE_HOOK(5)
DEFINE_SETMODE_HOOK(6)
DEFINE_SETMODE_HOOK(7)
DEFINE_SETMODE_HOOK(8)
DEFINE_SETMODE_HOOK(9)

constexpr std::array<hook_setmode_fn, kMaxCrtModules> kHookFunctions = {
    hook_setmode_0,
    hook_setmode_1,
    hook_setmode_2,
    hook_setmode_3,
    hook_setmode_4,
    hook_setmode_5,
    hook_setmode_6,
    hook_setmode_7,
    hook_setmode_8,
    hook_setmode_9,
};

bool equal_name(const wchar_t* left, const wchar_t* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    while (*left != L'\0' && *right != L'\0') {
        if (std::towlower(*left) != std::towlower(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == L'\0' && *right == L'\0';
}

bool is_crt_name(const UNICODE_STRING& name) {
    if (name.Buffer == nullptr || (name.Length % sizeof(wchar_t)) != 0) {
        return false;
    }

    for (const CrtHook& hook : g_hooks) {
        const std::size_t expected_length =
            std::wcslen(hook.name) * sizeof(wchar_t);
        if (name.Length != expected_length) {
            continue;
        }

        bool matches = true;
        for (std::size_t index = 0; index < name.Length / sizeof(wchar_t); ++index) {
            if (std::towlower(name.Buffer[index]) != std::towlower(hook.name[index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

void* find_export(HMODULE module, const char* name) {
    return module == nullptr ? nullptr : reinterpret_cast<void*>(GetProcAddress(module, name));
}

void load_buffer_size() {
    g_buffer_size = kDefaultBufferSize;

    const DWORD required = GetEnvironmentVariableW(L"FP_BUFFERSIZE", nullptr, 0);
    if (required == 0) {
        return;
    }

    std::vector<wchar_t> value(required + 1, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"FP_BUFFERSIZE",
        value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) {
        return;
    }

    std::size_t parsed = 0;
    for (DWORD index = 0; index < length; ++index) {
        if (value[index] < L'0' || value[index] > L'9') {
            return;
        }
        const std::size_t digit = static_cast<std::size_t>(value[index] - L'0');
        if (parsed > ((std::numeric_limits<std::size_t>::max)() - digit) / 10u) {
            return;
        }
        parsed = parsed * 10u + digit;
    }

    if (parsed >= kMinimumBufferSize && parsed <= kMaximumBufferSize) {
        g_buffer_size = parsed;
    }
}

bool is_standard_file_descriptor(const CrtHook& hook, int file_descriptor) {
    if (file_descriptor != 0 && file_descriptor != 1) {
        return false;
    }

    if (hook.iob == nullptr || hook.fileno == nullptr) {
        return true;
    }

    void* stream = hook.iob(static_cast<unsigned int>(file_descriptor));
    return stream != nullptr && hook.fileno(stream) == file_descriptor;
}

void configure_buffer(CrtHook& hook, int file_descriptor) {
    if (hook.iob == nullptr || hook.setvbuf == nullptr) {
        return;
    }

    void* stream = hook.iob(static_cast<unsigned int>(file_descriptor));
    if (stream == nullptr) {
        return;
    }

    char*& buffer = file_descriptor == 0 ? hook.stdin_buffer : hook.stdout_buffer;
    if (buffer == nullptr) {
        buffer = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, g_buffer_size));
    }
    if (buffer != nullptr) {
        if (hook.setvbuf(stream, buffer, 0, g_buffer_size) != 0) {
            HeapFree(GetProcessHeap(), 0, buffer);
            buffer = nullptr;
        }
    }
}

bool attach_hook(std::size_t index, HMODULE module) {
    CrtHook& hook = g_hooks[index];
    if (hook.hooked || module == nullptr) {
        return hook.hooked;
    }

    auto* setmode = reinterpret_cast<setmode_fn>(find_export(module, "_setmode"));
    if (setmode == nullptr) {
        return false;
    }

    hook.module = module;
    hook.original_setmode = setmode;
    hook.detour = kHookFunctions[index];
    hook.iob = reinterpret_cast<iob_fn>(find_export(module, "__acrt_iob_func"));
    hook.fileno = reinterpret_cast<fileno_fn>(find_export(module, "_fileno"));
    hook.setvbuf = reinterpret_cast<setvbuf_fn>(find_export(module, "setvbuf"));

    if (DetourTransactionBegin() != NO_ERROR ||
        DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&hook.original_setmode),
                     reinterpret_cast<PVOID>(kHookFunctions[index])) != NO_ERROR) {
        DetourTransactionAbort();
        hook.module = nullptr;
        hook.original_setmode = nullptr;
        hook.detour = nullptr;
        hook.iob = nullptr;
        hook.fileno = nullptr;
        hook.setvbuf = nullptr;
        return false;
    }

    if (DetourTransactionCommit() != NO_ERROR) {
        hook.module = nullptr;
        hook.original_setmode = nullptr;
        hook.detour = nullptr;
        hook.iob = nullptr;
        hook.fileno = nullptr;
        hook.setvbuf = nullptr;
        return false;
    }

    hook.hooked = true;
    return true;
}

void attach_loaded_crt_hooks() {
    EnterCriticalSection(&g_lock);
    for (std::size_t index = 0; index < g_hooks.size(); ++index) {
        if (g_hooks[index].hooked) {
            continue;
        }
        HMODULE module = GetModuleHandleW(g_hooks[index].name);
        if (module != nullptr) {
            attach_hook(index, module);
        }
    }
    LeaveCriticalSection(&g_lock);
}

int __cdecl hook_setmode(std::size_t index, int file_descriptor, int mode) {
    CrtHook& hook = g_hooks[index];
    setmode_fn original = hook.original_setmode;
    if (original == nullptr) {
        return -1;
    }

    const int result = original(file_descriptor, mode);
    if (result != -1 && mode == _O_BINARY && is_standard_file_descriptor(hook, file_descriptor)) {
        EnterCriticalSection(&g_lock);
        configure_buffer(hook, file_descriptor);
        LeaveCriticalSection(&g_lock);
    }
    return result;
}

VOID CALLBACK dll_notification(
    ULONG reason,
    const FASTPIPE_LDR_DLL_NOTIFICATION_DATA_WRAPPER* data,
    PVOID) {
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || data == nullptr ||
        data->Loaded.BaseDllName == nullptr || data->Loaded.BaseDllName->Buffer == nullptr) {
        return;
    }

    if (is_crt_name(*data->Loaded.BaseDllName)) {
        attach_loaded_crt_hooks();
    }
}

void register_dll_notification() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return;
    }

    auto register_notification = reinterpret_cast<ldr_register_dll_notification_fn>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    g_unregister_dll_notification = reinterpret_cast<ldr_unregister_dll_notification_fn>(
        GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
    if (register_notification == nullptr || g_unregister_dll_notification == nullptr) {
        return;
    }

    register_notification(0, dll_notification, nullptr, &g_dll_notification_cookie);
}

void unregister_dll_notification() {
    if (g_unregister_dll_notification != nullptr && g_dll_notification_cookie != nullptr) {
        g_unregister_dll_notification(g_dll_notification_cookie);
        g_dll_notification_cookie = nullptr;
    }
}

void detach_hooks() {
    EnterCriticalSection(&g_lock);
    for (CrtHook& hook : g_hooks) {
        if (hook.hooked && hook.original_setmode != nullptr) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(
                reinterpret_cast<PVOID*>(&hook.original_setmode),
                reinterpret_cast<PVOID>(hook.detour));
            DetourTransactionCommit();
            hook.hooked = false;
            hook.detour = nullptr;
        }
        if (hook.stdin_buffer != nullptr) {
            HeapFree(GetProcessHeap(), 0, hook.stdin_buffer);
            hook.stdin_buffer = nullptr;
        }
        if (hook.stdout_buffer != nullptr) {
            HeapFree(GetProcessHeap(), 0, hook.stdout_buffer);
            hook.stdout_buffer = nullptr;
        }
    }
    LeaveCriticalSection(&g_lock);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        load_buffer_size();
        register_dll_notification();
        attach_loaded_crt_hooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        unregister_dll_notification();
        if (reserved == nullptr) {
            detach_hooks();
        }
        DeleteCriticalSection(&g_lock);
    }
    return TRUE;
}
