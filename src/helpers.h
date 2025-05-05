#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <algorithm>
#include <cctype>
#include <locale>
#include <stdexcept>
#include <iostream>
#include <charconv>
#include <optional>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

namespace Helpers {

// Безопасное преобразование чисел с обработкой ошибок
template<typename T>
std::optional<std::string> n_to_string(T value) noexcept {
    char buffer[64]{};
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return (ec == std::errc()) 
        ? std::make_optional(std::string(buffer, ptr)) 
        : std::nullopt;
}

// Шестнадцатеричное представление с concept-ограничениями
template<typename T>
requires std::integral<T> || std::floating_point<T>
std::string n_to_string_hex(T value) {
    if constexpr (std::floating_point<T>) {
        return std::format("{:a}", value);
    } else {
        return std::format("{:x}", value);
    }
}

// Преобразование времени с таймзонами
inline std::string time_t_to_string(
    const std::chrono::system_clock::time_point& tp,
    std::string_view fmt = "{:%d/%m/%Y %H:%M:%S}",
    const std::locale& loc = std::locale("")) 
{
    try {
        return std::vformat(loc, fmt, std::make_format_args(tp));
    } catch (const std::exception& e) {
        return std::format("Time error: {}", e.what());
    }
}

// Тримминг строк с concept-ограничениями
template<typename CharT>
requires std::integral<CharT> && (sizeof(CharT) == 1)
std::basic_string_view<CharT> trim(std::basic_string_view<CharT> sv,
                                  const std::locale& loc = std::locale::classic()) 
{
    auto is_space = [&loc](CharT c) {
        return std::isspace(static_cast<char>(c), loc);
    };
    
    auto start = std::find_if_not(sv.begin(), sv.end(), is_space);
    auto end = std::find_if_not(sv.rbegin(), sv.rend(), is_space).base();
    
    return (start < end) 
        ? std::basic_string_view<CharT>(&*start, end - start)
        : std::basic_string_view<CharT>{};
}

// Сериализация JSON с обработкой ошибок
template <typename T>
void dump_json(const T& jValue, 
              std::ostream& os = std::cout,
              bool pretty = true) 
{
    rapidjson::StringBuffer sb;
    
    if (pretty) {
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
        if (!jValue.Accept(writer)) {
            throw std::runtime_error("JSON serialization failed");
        }
    } else {
        rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
        if (!jValue.Accept(writer)) {
            throw std::runtime_error("JSON serialization failed");
        }
    }
    
    os.write(sb.GetString(), sb.GetSize());
}

// Явные инстанцирования для JSON типов
template void dump_json<rapidjson::Document>(const rapidjson::Document&, std::ostream&, bool);
template void dump_json<rapidjson::Value>(const rapidjson::Value&, std::ostream&, bool);

} // namespace Helpers

#endif // HELPERS_H
