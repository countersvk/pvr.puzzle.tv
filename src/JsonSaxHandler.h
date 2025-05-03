#ifndef JSON_SAX_HANDLER_H
#define JSON_SAX_HANDLER_H

#include <kodi/AddonBase.h>
#include <rapidjson/reader.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>
#include <memory>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <functional>

namespace Helpers {
namespace Json {

using rapidjson::BaseReaderHandler;
using rapidjson::UTF8;
using rapidjson::Reader;
using rapidjson::StringStream;
using rapidjson::FileReadStream;
using rapidjson::ParseErrorCode;
using rapidjson::GetParseError_En;

template<class TDerived>
struct ParserForBase : public BaseReaderHandler<UTF8<>, TDerived> {
public:
    bool HasError() const noexcept { return m_isError; }
    const std::string& GetParseError() const noexcept { return m_error; }

protected:
    bool error(const std::string& reason) {
        m_isError = true;
        m_error = reason;
        kodi::Log(ADDON_LOG_ERROR, "JSON Parser Error: %s", reason.c_str());
        return false;
    }

private:
    bool m_isError = false;
    std::string m_error;
};

template<class T>
struct ObjectDeliverer {
    using TObjectDelegate = std::function<bool(const T&)>;
    
    bool SendObject(const T& obj) { return m_delegate ? m_delegate(obj) : false; }
    void SetDelegate(TObjectDelegate delegate) { m_delegate = std::move(delegate); }

private:
    TObjectDelegate m_delegate;
};

template<class T>
struct ParserForObject : public ParserForBase<ParserForObject<T>> {
    template<typename TValue>
    struct Typer {
        using Field = TValue T::*;
        using FieldsMap = std::map<std::string, Field>;
        using ArrayField = std::vector<TValue> T::*;
        using ArrayFieldsMap = std::map<std::string, ArrayField>;
    };

public:
    using TObjectDelegate = typename ObjectDeliverer<T>::TObjectDelegate;

    ParserForObject& WithField(const std::string& name, 
                              typename Typer<std::string>::Field field, 
                              bool isMandatory = true) {
        return RegisterField(m_stringFields, name, field, isMandatory);
    }

    // Аналогичные методы для других типов данных...

    bool StartObject() {
        if (m_state != State::ExpectObjectStart) {
            return this->error("Unexpected object start");
        }
        m_object = std::make_unique<T>();
        m_jsonFields.clear();
        m_state = State::ExpectNameOrObjectEnd;
        return true;
    }

    bool Key(const char* str, rapidjson::SizeType length, bool) {
        if (m_state != State::ExpectNameOrObjectEnd) {
            return this->error("Unexpected key");
        }
        m_currentKey.assign(str, length);
        m_state = State::ExpectValue;
        return true;
    }

    bool String(const char* str, rapidjson::SizeType length, bool) {
        if (m_state == State::ExpectValue) {
            if (auto it = m_stringFields.find(m_currentKey); it != m_stringFields.end()) {
                m_object->*(it->second) = std::string(str, length);
            }
            m_state = State::ExpectNameOrObjectEnd;
        }
        return true;
    }

    bool EndObject(rapidjson::SizeType) {
        // Валидация обязательных полей
        for (const auto& field : m_mandatoryFields) {
            if (m_stringFields.count(field) && (m_object->*(m_stringFields[field])).empty()) {
                return this->error("Missing mandatory field: " + field);
            }
        }

        if (!m_deliverer.SendObject(*m_object)) {
            return this->error("Object delivery failed");
        }
        
        m_state = State::ExpectObjectStart;
        return true;
    }

private:
    template<typename TValue>
    ParserForObject& RegisterField(typename Typer<TValue>::FieldsMap& fields,
                                  const std::string& name,
                                  typename Typer<TValue>::Field field,
                                  bool isMandatory) {
        fields.emplace(name, field);
        if (isMandatory) {
            m_mandatoryFields.insert(name);
        }
        return *this;
    }

    enum class State {
        ExpectObjectStart,
        ExpectNameOrObjectEnd,
        ExpectValue
    } m_state = State::ExpectObjectStart;

    std::unique_ptr<T> m_object;
    std::string m_currentKey;
    std::set<std::string> m_mandatoryFields;
    typename Typer<std::string>::FieldsMap m_stringFields;
    ObjectDeliverer<T> m_deliverer;
};

template<class T>
bool ParseJsonStream(const char* json, 
                    ParserForObject<T>& handler,
                    typename ParserForObject<T>::TObjectDelegate onObjectReady,
                    std::string* errorMessage) {
    handler.SetDeliverer(onObjectReady);
    
    Reader reader;
    StringStream ss(json);
    
    if (!reader.Parse(ss, handler)) {
        if (errorMessage) {
            *errorMessage = rapidjson::GetParseError_En(reader.GetParseErrorCode());
            *errorMessage += " at offset " + std::to_string(reader.GetErrorOffset());
        }
        return false;
    }
    return true;
}

}} // namespace Helpers::Json

#endif // JSON_SAX_HANDLER_H
