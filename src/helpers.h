#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <algorithm>
#include <cctype>
#include <locale>
#include <charconv>
#include <stdexcept>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

namespace Helpers {

// Преобразование чисел в строку с безопасным буфером
template<typename T>
std::string n_to_string(T value) noexcept {
    char buffer[20]{}; // Достаточно для 64-битных чисел
    auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
    return (ec == std::errc()) ? std::string(buffer, ptr) : std::string{};
}

// Шестнадцатеричное представление с проверкой типа
template<typename T>
std::string n_to_string_hex(T value) noexcept {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
        return std::format("{:a}", value);
    } else {
        return std::format("{:x}", value);
    }
}

// Преобразование времени с обработкой ошибок
inline std::string time_t_to_string(
    const std::chrono::system_clock::time_point& tp) noexcept 
{
    try {
        return std::format("{:%d/%m/%Y %H:%M:%S}", tp);
    } catch (const std::exception& e) {
        return "Invalid time: " + std::string(e.what());
    }
}

// Тримминг строк с поддержкой локали
inline std::string_view ltrim(std::string_view s, 
                            const std::locale& loc = std::locale::classic()) 
{
    auto it = std::find_if(s.begin(), s.end(), [&loc](char c) {
        return !std::isspace(c, loc);
    });
    s.remove_prefix(it - s.begin());
    return s;
}

inline std::string_view rtrim(std::string_view s,
                            const std::locale& loc = std::locale::classic()) 
{
    auto it = std::find_if(s.rbegin(), s.rend(), [&loc](char c) {
        return !std::isspace(c, loc);
    });
    s.remove_suffix(it - s.rbegin());
    return s;
}

inline std::string_view trim(std::string_view s,
                           const std::locale& loc = std::locale::classic()) 
{
    return ltrim(rtrim(s, loc), loc);
}

// Вывод JSON с обработкой ошибок и поддержкой разных потоков
template <typename T>
void dump_json(const T& jValue, std::ostream& os = std::cout) noexcept {
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
    
    if (!jValue.Accept(writer)) {
        os << "Failed to serialize JSON\n";
        return;
    }
    
    try {
        os << sb.GetString() << '\n';
    } catch (const std::ios_base::failure& e) {
        std::cerr << "Output error: " << e.what() << '\n';
    }
}

// Явные инстанцирования для JSON типов
template void dump_json<rapidjson::Document>(const rapidjson::Document&, std::ostream&);
template void dump_json<rapidjson::Value>(const rapidjson::Value&, std::ostream&);

} // namespace Helpers

#endif // HELPERS_H
