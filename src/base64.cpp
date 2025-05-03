#include <array>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstddef>
#include <algorithm>
#include <bit>

namespace base64 {

    constexpr std::string_view base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    constexpr auto padding_char = '=';
    
    // Концепция для проверки типа входных данных
    template<typename T>
    concept ByteContainer = requires(T t) {
        { t.size() } -> std::convertible_to<std::size_t>;
        { t.data() } -> std::convertible_to<const std::byte*>;
    };

    // Константный поиск символа в строке
    constexpr bool is_base64(char c) noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || (c == '+') || (c == '/');
    }

    // Оптимизированное кодирование
    template<ByteContainer T>
    [[nodiscard]] std::string encode(const T& input) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
        const std::size_t len = input.size();
        
        std::string result;
        result.reserve(((len + 2) / 3) * 4);

        for(std::size_t i = 0; i < len; i += 3) {
            const std::size_t remaining = len - i;
            const std::size_t group_size = std::min(remaining, 3ul);

            // Формирование 24-битного значения
            unsigned int triple = (bytes[i] << 16) | 
                                (group_size > 1 ? (bytes[i+1] << 8) : 0) | 
                                (group_size > 2 ? bytes[i+2] : 0);

            // Извлечение 6-битных значений
            const std::array indices{
                static_cast<char>((triple >> 18) & 0x3F),
                static_cast<char>((triple >> 12) & 0x3F),
                static_cast<char>((triple >> 6)  & 0x3F),
                static_cast<char>(triple & 0x3F)
            };

            // Заполнение результата
            for(std::size_t j = 0; j < group_size + 1; ++j) {
                result += base64_chars[indices[j]];
            }

            // Добавление padding при необходимости
            for(std::size_t j = 0; j < 3 - group_size; ++j) {
                result += padding_char;
            }
        }

        return result;
    }

    // Оптимизированное декодирование
    [[nodiscard]] std::string decode(std::string_view encoded_string) {
        std::string result;
        result.reserve((encoded_string.size() * 3) / 4);

        unsigned int buffer = 0;
        int bits_collected = 0;
        
        for(const char c : encoded_string) {
            if(c == padding_char || !is_base64(c)) break;

            const auto pos = base64_chars.find(c);
            if(pos == std::string_view::npos) {
                throw std::invalid_argument("Invalid base64 character");
            }

            buffer = (buffer << 6) | static_cast<unsigned int>(pos);
            bits_collected += 6;

            if(bits_collected >= 8) {
                bits_collected -= 8;
                result += static_cast<char>((buffer >> bits_collected) & 0xFF);
            }
        }

        return result;
    }

} // namespace base64
