#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t* argv[]) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    if (argc < 2) {
        return 2;
    }

    const std::wstring mode = argv[1];
    if (mode == L"generate") {
        if (argc != 3) {
            return 2;
        }
        const std::uint64_t size = std::stoull(argv[2]);
        for (std::uint64_t index = 0; index < size; ++index) {
            const std::uint8_t value = static_cast<std::uint8_t>((index * 131u + 17u) & 0xffu);
            std::cout.put(static_cast<char>(value));
            if (!std::cout) {
                return 3;
            }
        }
        return 0;
    }

    if (mode == L"copy") {
        std::uint64_t count = 0;
        char buffer[64 * 1024];
        while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount() != 0) {
            std::cout.write(buffer, std::cin.gcount());
            count += static_cast<std::uint64_t>(std::cin.gcount());
            if (!std::cout) {
                return 3;
            }
        }
        return std::cin.eof() ? 0 : 4;
    }

    if (mode == L"verify") {
        if (argc != 3) {
            return 2;
        }
        const std::uint64_t expected_size = std::stoull(argv[2]);
        std::uint64_t count = 0;
        char value = 0;
        while (std::cin.get(value)) {
            const std::uint8_t expected = static_cast<std::uint8_t>((count * 131u + 17u) & 0xffu);
            if (static_cast<std::uint8_t>(static_cast<unsigned char>(value)) != expected) {
                return 5;
            }
            std::cout.put(value);
            if (!std::cout) {
                return 3;
            }
            ++count;
        }
        return std::cin.eof() && count == expected_size ? 0 : 6;
    }

    if (mode == L"fail") {
        return argc == 3 ? std::stoi(argv[2]) : 7;
    }

    return 2;
}
