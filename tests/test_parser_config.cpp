#include "cli_parser.h"
#include "config.h"

#include <windows.h>

#include <cassert>
#include <string>
#include <vector>

namespace {

fastpipe::CommandParseResult parse(const std::vector<std::wstring>& arguments) {
    std::vector<wchar_t*> argv;
    argv.reserve(arguments.size());
    for (const std::wstring& argument : arguments) {
        argv.push_back(const_cast<wchar_t*>(argument.c_str()));
    }
    return fastpipe::parse_commands(static_cast<int>(argv.size()), argv.data());
}

void test_parser() {
    const auto result = parse({L"producer", L"arg with spaces", L"|", L"consumer"});
    assert(result.ok);
    assert(result.commands.size() == 2);
    assert(result.commands[0].size() == 2);
    assert(result.commands[0][1] == L"arg with spaces");
    assert(result.commands[1][0] == L"consumer");

    assert(!parse({L"|", L"consumer"}).ok);
    assert(!parse({L"producer", L"|"}).ok);
    assert(!parse({L"producer", L"|", L"|", L"consumer"}).ok);
    assert(!parse({}).ok);
}

void test_quoting() {
    assert(fastpipe::quote_windows_argument(L"") == L"\"\"");
    assert(fastpipe::quote_windows_argument(L"plain") == L"\"plain\"");
    assert(fastpipe::quote_windows_argument(L"a b") == L"\"a b\"");
    assert(fastpipe::quote_windows_argument(L"a\\") == L"\"a\\\\\"");
    assert(fastpipe::quote_windows_argument(L"a\"b") == L"\"a\\\"b\"");
}

void test_config() {
    assert(fastpipe::parse_buffer_size(L"4096").ok);
    assert(fastpipe::parse_buffer_size(L"67108864").ok);
    assert(fastpipe::parse_buffer_size(L"4095").ok == false);
    assert(fastpipe::parse_buffer_size(L"67108865").ok == false);
    assert(fastpipe::parse_buffer_size(L"not-a-number").ok == false);

    SetEnvironmentVariableW(L"FP_BUFFERSIZE", nullptr);
    const auto default_config = fastpipe::load_config();
    assert(default_config.ok);
    assert(default_config.config.buffer_size == 16u * 1024u * 1024u);

    SetEnvironmentVariableW(L"FP_BUFFERSIZE", L"8192");
    const auto configured = fastpipe::load_config();
    assert(configured.ok);
    assert(configured.config.buffer_size == 8192);

    SetEnvironmentVariableW(L"FP_BUFFERSIZE", L"4095");
    assert(!fastpipe::load_config().ok);
    SetEnvironmentVariableW(L"FP_BUFFERSIZE", nullptr);
}

}  // namespace

int wmain() {
    test_parser();
    test_quoting();
    test_config();
    return 0;
}
