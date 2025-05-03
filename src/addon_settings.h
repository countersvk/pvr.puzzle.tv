#ifndef ADDON_SETTINGS_H
#define ADDON_SETTINGS_H

#include <map>
#include <string>
#include <variant>
#include <functional>
#include <optional>
#include <fmt/format.h>
#include <kodi/AddonBase.h>

namespace PvrClient {

using SettingValue = std::variant<std::string, int, bool, float>;
using ValueHandler = std::function<void(const SettingValue&)>;
using ValuePredicate = std::function<bool(const SettingValue&)>;

class AddonSettings {
public:
    struct SettingDescriptor {
        std::string name;
        SettingValue defaultValue;
        ADDON_STATUS changeStatus;
        ValueHandler propagator;
        ValueHandler onSet;
    };

    AddonSettings() = default;

    template<typename T>
    void Add(std::string name, T defaultValue, 
             ADDON_STATUS onChanged = ADDON_STATUS_OK,
             ValueHandler propagator = {},
             ValueHandler onSet = {})
    {
        settings_.emplace(std::move(name), SettingDescriptor{
            std::move(name), 
            SettingValue(defaultValue),
            onChanged,
            propagator ? propagator : [](auto&&){},
            onSet ? onSet : [](auto&&){}
        });
    }

    void Init()
    {
        for (auto& [name, desc] : settings_) {
            std::visit([this, &desc](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                auto current = GetSetting<T>(desc.name);
                desc.propagator(current.value_or(value));
            }, desc.defaultValue);
        }
    }

    ADDON_STATUS Set(const std::string& name, const kodi::CSettingValue& value)
    {
        auto it = settings_.find(name);
        if (it == settings_.end()) {
            kodi::Log(ADDON_LOG_ERROR, "Unknown setting: %s", name.c_str());
            return ADDON_STATUS_UNKNOWN;
        }

        return std::visit([&](auto&& storedValue) {
            using T = std::decay_t<decltype(storedValue)>;
            return HandleSet<T>(it->second, value);
        }, it->second.defaultValue);
    }

    template<typename T>
    std::optional<T> Get(const std::string& name) const
    {
        if (auto it = settings_.find(name); it != settings_.end()) {
            if (const T* value = std::get_if<T>(&it->second.defaultValue)) {
                return *value;
            }
        }
        return std::nullopt;
    }

    void Print() const
    {
        for (const auto& [name, desc] : settings_) {
            std::visit([&](auto&& value) {
                if (name.find("password") == std::string::npos && 
                    name.find("login") == std::string::npos) {
                    kodi::Log(ADDON_LOG_DEBUG, "Setting %s = %s", 
                              name.c_str(), fmt::format("{}", value).c_str());
                } else {
                    kodi::Log(ADDON_LOG_DEBUG, "Setting %s = *****", name.c_str());
                }
            }, desc.defaultValue);
        }
    }

private:
    template<typename T>
    ADDON_STATUS HandleSet(SettingDescriptor& desc, const kodi::CSettingValue& value)
    {
        try {
            T newValue = GetValue<T>(value);
            T current = std::get<T>(desc.defaultValue);
            
            if (newValue != current) {
                desc.defaultValue = newValue;
                desc.propagator(newValue);
                desc.onSet(newValue);
                return desc.changeStatus;
            }
            return ADDON_STATUS_OK;
        }
        catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "Error setting %s: %s", 
                      desc.name.c_str(), e.what());
            return ADDON_STATUS_INVALID_ARGUMENT;
        }
    }

    template<typename T>
    T GetValue(const kodi::CSettingValue& value) const
    {
        if constexpr (std::is_same_v<T, std::string>) {
            return value.GetString();
        } else if constexpr (std::is_same_v<T, int>) {
            return value.GetInt();
        } else if constexpr (std::is_same_v<T, bool>) {
            return value.GetBoolean();
        } else if constexpr (std::is_same_v<T, float>) {
            return value.GetFloat();
        }
    }

    template<typename T>
    std::optional<T> GetSetting(const std::string& name) const
    {
        if constexpr (std::is_same_v<T, std::string>) {
            return kodi::GetSettingString(name);
        } else if constexpr (std::is_same_v<T, int>) {
            return kodi::GetSettingInt(name);
        } else if constexpr (std::is_same_v<T, bool>) {
            return kodi::GetSettingBoolean(name);
        } else if constexpr (std::is_same_v<T, float>) {
            return kodi::GetSettingFloat(name);
        }
    }

    std::map<std::string, SettingDescriptor> settings_;
};

} // namespace PvrClient

#endif // ADDON_SETTINGS_H
