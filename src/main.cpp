#include "cli_parser.h"
#include "config.h"
#include "process_launcher.h"

#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    const fastpipe::CommandParseResult commands = fastpipe::parse_commands(argc - 1, argv + 1);
    if (!commands.ok) {
        std::wcerr << L"fp: " << commands.error << L"\n";
        return 2;
    }

    const fastpipe::ConfigResult config = fastpipe::load_config();
    if (!config.ok) {
        std::wcerr << L"fp: " << config.error << L"\n";
        return 2;
    }

    const fastpipe::PipelineResult pipeline = fastpipe::run_pipeline(
        commands.commands,
        config.config.buffer_size);
    if (!pipeline.ok) {
        std::wcerr << L"fp: " << pipeline.error << L"\n";
        return 3;
    }

    return static_cast<int>(pipeline.exit_code);
}
