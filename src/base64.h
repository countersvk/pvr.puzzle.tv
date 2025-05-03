#pragma once

#include <string>
#include <span>
#include <cstddef>

namespace base64 {

    // Кодирование данных произвольного бинарного формата
    [[nodiscard]] std::string encode(std::span<const std::byte> data) noexcept;

    // Кодирование строковых данных (специализация для удобства)
    [[nodiscard]] std::string encode(std::string_view str) noexcept;

    // Декодирование base64 строки
    [[nodiscard]] std::string decode(std::string_view encoded_str);

} // namespace base64
