#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <gsl/gsl_util> // Для gsl::narrow_cast

namespace Helpers {

// Универсальное преобразование чисел в строку
template<typename T>
std::string n_to_string(T value) noexcept {
    char buffer[64]{};
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc()) return {buffer, ptr};
    return {};
}

// Специализация для шестнадцатеричного представления
template<typename T>
std::string n_to_string_hex(T value) noexcept {
    return std::format("{:x}", value);
}

// Преобразование времени с использованием <chrono>
inline std::string time_t_to_string(
    const std::chrono::system_clock::time_point& tp) noexcept 
{
    try {
        return std::format("{:%d/%m/%y %H:%M}", tp);
    } catch (...) {
        return "Invalid time";
    }
}

// Шаблонная функция для дебага JSON
template <typename T>
void dump_json(const T& jValue) noexcept {
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
    if (jValue.Accept(writer)) {
        std::cout << sb.GetString() << '\n';
    }
}

// Тримминг строк с использованием string_view
inline std::string_view ltrim(std::string_view s) noexcept {
    auto it = std::find_if(s.begin(), s.end(), [](char c) {
        return !std::isspace(static_cast<unsigned char>(c));
    });
    return {it, s.end()};
}

inline std::string_view rtrim(std::string_view s) noexcept {
    auto it = std::find_if(s.rbegin(), s.rend(), [](char c) {
        return !std::isspace(static_cast<unsigned char>(c));
    }).base();
    return {s.begin(), it};
}

inline std::string_view trim(std::string_view s) noexcept {
    return ltrim(rtrim(s));
}

// Явные инстанцирования для JSON типов
template void dump_json<rapidjson::Document>(const rapidjson::Document&);
template void dump_json<rapidjson::Value>(const rapidjson::Value&);

} // namespace Helpers

#endif // HELPERS_H
