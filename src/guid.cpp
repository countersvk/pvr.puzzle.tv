#include "guid.h"
#include <random>
#include <functional>

namespace CUSTOM_GUID {
    namespace {
        // Потокобезопасные генераторы
        thread_local std::random_device rd;
        thread_local std::mt19937_64 gen(rd());
        thread_local std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
        
        // Шаблон для форматирования UUID согласно RFC 4122
        constexpr std::array version_4_mask{
            0xFF, 0xFF, 0x0F, 0x00,  // version 4
            0x3F, 0x00, 0x00, 0x00,  // variant 1 (RFC 4122)
            0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF
        };
    }

    std::string generate() noexcept {
        try {
            // Генерация 128-битного UUID
            std::array<uint8_t, 16> uuid;
            
            // Заполнение случайными данными
            const uint64_t part1 = dist(gen);
            const uint64_t part2 = dist(gen);
            std::memcpy(uuid.data(), &part1, 8);
            std::memcpy(uuid.data() + 8, &part2, 8);

            // Применение маски версии 4 и варианта
            for(size_t i = 0; i < uuid.size(); ++i) {
                uuid[i] &= version_4_mask[i];
                if(i == 6) uuid[i] |= 0x40;  // version 4
                if(i == 8) uuid[i] |= 0x80;  // variant 1
            }

            // Форматирование в строку
            return std::format("{:02x}{:02x}{:02x}{:02x}-"
                              "{:02x}{:02x}-"
                              "{:02x}{:02x}-"
                              "{:02x}{:02x}-"
                              "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                uuid[0], uuid[1], uuid[2], uuid[3],
                uuid[4], uuid[5],
                uuid[6], uuid[7],
                uuid[8], uuid[9],
                uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
            
        } catch(...) {
            return "00000000-0000-4000-8000-000000000000"; // Fallback
        }
    }
}
