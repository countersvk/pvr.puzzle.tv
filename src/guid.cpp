#include <string>
#include <random>
#include <format>
#include <array>

namespace CUSTOM_GUID {

namespace {
    thread_local std::mt19937 gen(std::random_device{}());
    thread_local std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
}

std::string generate() {
    // Генерация 128-битного UUID (16 bytes)
    std::array<uint32_t, 4> data{dist(gen), dist(gen), dist(gen), dist(gen)};

    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:08x}{:04x}",
        data[0],                        // 32 бита
        data[1] & 0xFFFF,               // 16 бит
        (data[1] >> 16) & 0x0FFF | 0x4000, // Версия 4
        data[2] & 0x3FFF | 0x8000,      // Вариант 1
        data[2] >> 16, data[3]);        // Остальные 48 бит
}
}
