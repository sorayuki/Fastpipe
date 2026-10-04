#include "process_launcher.h"

#include "cli_parser.h"
#include "pipe_chain.h"
#include "stdio_injection.h"
#include "win_handle.h"

#include <windows.h>

#if defined(FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK)
#include "detours.h"
#endif

#include <cstddef>
#include <string>
#include <vector>

namespace fastpipe {
namespace {

struct child_process {
    unique_handle process;
    unique_handle thread;
};

bool duplicate_inheritable_handle(HANDLE source, unique_handle& duplicate) {
    if (source == nullptr || source == INVALID_HANDLE_VALUE) {
        return false;
    }

    HANDLE handle = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            source,
            GetCurrentProcess(),
            &handle,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS)) {
        return false;
    }

    duplicate.reset(handle);
    return true;
}

bool add_handle(std::vector<HANDLE>& handles, HANDLE handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return true;
    }
    handles.push_back(handle);
    return true;
}

bool initialize_handle_list(
    const std::vector<HANDLE>& handles,
    std::vector<std::byte>& attribute_storage,
    LPPROC_THREAD_ATTRIBUTE_LIST& attributes,
    std::wstring& error) {
    attributes = nullptr;
    if (handles.empty()) {
        return true;
    }

    SIZE_T required_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &required_size);
    if (required_size == 0) {
        error = L"InitializeProcThreadAttributeList sizing failed: " + win32_error_message();
        return false;
    }

    attribute_storage.resize(required_size);
    attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &required_size)) {
        error = L"InitializeProcThreadAttributeList failed: " + win32_error_message();
        attributes = nullptr;
        return false;
    }

    if (!UpdateProcThreadAttribute(
            attributes,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(handles.data()),
            handles.size() * sizeof(HANDLE),
            nullptr,
            nullptr)) {
        error = L"UpdateProcThreadAttribute failed: " + win32_error_message();
        DeleteProcThreadAttributeList(attributes);
        attributes = nullptr;
        return false;
    }

    return true;
}

void terminate_children(HANDLE job, const std::vector<child_process>& children) {
    if (job != nullptr) {
        TerminateJobObject(job, ERROR_PROCESS_ABORTED);
        return;
    }

    for (const child_process& child : children) {
        if (child.process) {
            TerminateProcess(child.process.get(), ERROR_PROCESS_ABORTED);
        }
    }
}

}  // namespace

PipelineResult run_pipeline(
    const std::vector<std::vector<std::wstring>>& commands,
    DWORD buffer_size) {
    PipelineResult result;
    if (commands.empty()) {
        result.error = L"cannot run an empty pipeline";
        return result;
    }
    if (commands.size() > MAXIMUM_WAIT_OBJECTS) {
        result.error = L"pipeline cannot contain more than 64 commands";
        return result;
    }

    std::vector<pipe_pair> pipes;
    if (!create_pipe_chain(commands.size() - 1, buffer_size, pipes, result.error)) {
        return result;
    }

    unique_handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        result.error = L"CreateJobObject failed: " + win32_error_message();
        return result;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            job.get(),
            JobObjectExtendedLimitInformation,
            &job_limits,
            sizeof(job_limits))) {
        result.error = L"SetInformationJobObject failed: " + win32_error_message();
        return result;
    }

    std::vector<child_process> children;
    children.reserve(commands.size());

    for (std::size_t index = 0; index < commands.size(); ++index) {
        unique_handle inherited_input;
        unique_handle inherited_output;
        unique_handle inherited_error;

        HANDLE input = nullptr;
        HANDLE output = nullptr;
        if (index == 0) {
            if (!duplicate_inheritable_handle(GetStdHandle(STD_INPUT_HANDLE), inherited_input)) {
                result.error = L"could not duplicate standard input: " + win32_error_message();
                terminate_children(job.get(), children);
                return result;
            }
            input = inherited_input.get();
        } else {
            input = pipes[index - 1].read.get();
        }

        if (index + 1 == commands.size()) {
            if (!duplicate_inheritable_handle(GetStdHandle(STD_OUTPUT_HANDLE), inherited_output)) {
                result.error = L"could not duplicate standard output: " + win32_error_message();
                terminate_children(job.get(), children);
                return result;
            }
            output = inherited_output.get();
        } else {
            output = pipes[index].write.get();
        }

        HANDLE standard_error = GetStdHandle(STD_ERROR_HANDLE);
        if (standard_error != nullptr && standard_error != INVALID_HANDLE_VALUE &&
            !duplicate_inheritable_handle(standard_error, inherited_error)) {
            result.error = L"could not duplicate standard error: " + win32_error_message();
            terminate_children(job.get(), children);
            return result;
        }

        std::vector<HANDLE> inherited_handles;
        inherited_handles.reserve(3);
        add_handle(inherited_handles, input);
        add_handle(inherited_handles, output);
        add_handle(inherited_handles, inherited_error.get());

        std::vector<std::byte> attribute_storage;
        LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
        if (!initialize_handle_list(inherited_handles, attribute_storage, attributes, result.error)) {
            terminate_children(job.get(), children);
            return result;
        }

        STARTUPINFOEXW startup_info{};
        startup_info.StartupInfo.cb = sizeof(startup_info);
        startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup_info.StartupInfo.hStdInput = input;
        startup_info.StartupInfo.hStdOutput = output;
        startup_info.StartupInfo.hStdError = inherited_error ? inherited_error.get() : standard_error;
        startup_info.lpAttributeList = attributes;

        std::wstring command_line = build_command_line(commands[index]);
        std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
        mutable_command_line.push_back(L'\0');

        PROCESS_INFORMATION process_info{};
        const DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT |
                                     CREATE_SUSPENDED |
                                     CREATE_UNICODE_ENVIRONMENT;
        BOOL created = FALSE;
#if defined(FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK)
        const std::string hook_path = find_stdio_hook_for_command(commands[index]);
        const bool direct_injection = !hook_path.empty() &&
                                      can_direct_inject_stdio_hook(commands[index]);
        if (!hook_path.empty() && !direct_injection) {
            created = DetourCreateProcessWithDllExW(
                nullptr,
                mutable_command_line.data(),
                nullptr,
                nullptr,
                TRUE,
                creation_flags,
                nullptr,
                nullptr,
                &startup_info.StartupInfo,
                &process_info,
                hook_path.c_str(),
                nullptr);
        }
        if (direct_injection) {
            created = CreateProcessW(
                nullptr,
                mutable_command_line.data(),
                nullptr,
                nullptr,
                TRUE,
                creation_flags,
                nullptr,
                nullptr,
                &startup_info.StartupInfo,
                &process_info);
            if (created && !inject_stdio_hook(process_info.hProcess, hook_path)) {
                TerminateProcess(process_info.hProcess, ERROR_DLL_INIT_FAILED);
                CloseHandle(process_info.hProcess);
                CloseHandle(process_info.hThread);
                ZeroMemory(&process_info, sizeof(process_info));
                created = FALSE;
            }
        }
#endif
        if (!created) {
            created = CreateProcessW(
                nullptr,
                mutable_command_line.data(),
                nullptr,
                nullptr,
                TRUE,
                creation_flags,
                nullptr,
                nullptr,
                &startup_info.StartupInfo,
                &process_info);
        }

        if (attributes != nullptr) {
            DeleteProcThreadAttributeList(attributes);
        }

        if (!created) {
            result.error = L"CreateProcessW failed for command \"" + command_line + L"\": " + win32_error_message();
            terminate_children(job.get(), children);
            return result;
        }

        child_process child;
        child.process.reset(process_info.hProcess);
        child.thread.reset(process_info.hThread);

        if (!AssignProcessToJobObject(job.get(), child.process.get())) {
            result.error = L"AssignProcessToJobObject failed: " + win32_error_message();
            TerminateProcess(child.process.get(), ERROR_PROCESS_ABORTED);
            terminate_children(job.get(), children);
            return result;
        }

        if (ResumeThread(child.thread.get()) == static_cast<DWORD>(-1)) {
            result.error = L"ResumeThread failed: " + win32_error_message();
            TerminateProcess(child.process.get(), ERROR_PROCESS_ABORTED);
            terminate_children(job.get(), children);
            return result;
        }

        children.push_back(std::move(child));

        if (index > 0) {
            pipes[index - 1].read.reset();
        }
        if (index + 1 < commands.size()) {
            pipes[index].write.reset();
        }
    }

    std::vector<HANDLE> process_handles;
    process_handles.reserve(children.size());
    for (const child_process& child : children) {
        process_handles.push_back(child.process.get());
    }

    if (WaitForMultipleObjects(
            static_cast<DWORD>(process_handles.size()),
            process_handles.data(),
            TRUE,
            INFINITE) != WAIT_OBJECT_0) {
        result.error = L"WaitForMultipleObjects failed: " + win32_error_message();
        terminate_children(job.get(), children);
        return result;
    }

    for (std::size_t index = 0; index < children.size(); ++index) {
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(children[index].process.get(), &exit_code)) {
            result.error = L"GetExitCodeProcess failed: " + win32_error_message();
            return result;
        }
        if (index + 1 == children.size()) {
            result.exit_code = exit_code;
        }
    }

    result.ok = true;
    return result;
}

}