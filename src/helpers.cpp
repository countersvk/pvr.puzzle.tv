#include <chrono>
#include <format>
#include <string>
#include <iostream>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "globals.hpp"

namespace Helpers {

// Универсальная конвертация чисел в строку
template<typename T>
std::string n_to_string(T value) {
    return std::to_string(value);
}

// Специализация для шестнадцатеричного представления
template<typename T>
std::string n_to_string_hex(T value) {
    return std::format("{:x}", value);
}

// Преобразование времени с использованием <chrono>
std::string time_t_to_string(const std::time_t& time) {
    try {
        const auto tp = std::chrono::system_clock::from_time_t(time);
        return std::format("{:%d/%m/%y %H:%M}", tp);
    } catch (const std::exception& e) {
        Globals::LogError("Time conversion error: {}", e.what());
        return "Invalid time";
    }
}

// Шаблонная функция для дебага JSON
template <typename T>
void dump_json(const T& jValue) {
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
    
    if (!jValue.Accept(writer)) {
        Globals::LogError("Invalid JSON value");
        return;
    }
    
    std::cout << sb.GetString() << '\n';
}

// Явное инстанцирование шаблонов
template void dump_json<rapidjson::Document>(const rapidjson::Document&);
template void dump_json<rapidjson::Value>(const rapidjson::Value&);

} // namespace Helpers
