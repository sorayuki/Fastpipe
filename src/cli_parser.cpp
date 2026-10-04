#include "cli_parser.h"

#include <cwctype>

namespace fastpipe {

CommandParseResult parse_commands(int argc, wchar_t* argv[]) {
    CommandParseResult result;
    std::vector<std::wstring> current;

    if (argc <= 0 || argv == nullptr) {
        result.error = L"no command was specified";
        return result;
    }

    for (int index = 0; index < argc; ++index) {
        if (argv[index] == nullptr) {
            result.error = L"command arguments cannot be null";
            return result;
        }

        if (std::wstring(argv[index]) == L"|") {
            if (current.empty()) {
                result.error = L"pipe separators cannot appear at the beginning or next to another separator";
                return result;
            }
            result.commands.push_back(std::move(current));
            current.clear();
            continue;
        }

        current.emplace_back(argv[index]);
    }

    if (current.empty()) {
        result.error = L"pipe separators cannot appear at the end";
        return result;
    }

    result.commands.push_back(std::move(current));
    result.ok = true;
    return result;
}

std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back(L'"');

    std::size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }

        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, L'\\');
        quoted.push_back(character);
        backslashes = 0;
    }

    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring build_command_line(const std::vector<std::wstring>& arguments) {
    std::wstring command_line;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(arguments[index]);
    }
    return command_line;
}

}