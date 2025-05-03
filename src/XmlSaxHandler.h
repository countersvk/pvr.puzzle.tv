#ifndef XML_SAX_HANDLER_H
#define XML_SAX_HANDLER_H

#include <memory>
#include <type_traits>
#include <kodi/Log.h>

#define XML_STATIC 1
#include "expat.h"

namespace XMLTV {

template<typename Derived = void>
class XmlSaxHandler
{
public:
    using Override = std::conditional_t<
        std::is_same_v<Derived, void>, 
        XmlSaxHandler, 
        Derived
    >;

    XmlSaxHandler()
        : m_parser(XML_ParserCreate(nullptr), &XML_ParserFree)
    {
        XML_SetElementHandler(m_parser.get(), StartElement, EndElement);
        XML_SetCharacterDataHandler(m_parser.get(), CharacterDataHandler);
        XML_SetUserData(m_parser.get(), this);
    }

    [[nodiscard]] bool Parse(const char* buffer, size_t size, bool isFinal)
    {
        if (XML_Parse(m_parser.get(), buffer, static_cast<int>(size), isFinal ? XML_TRUE : XML_FALSE) 
            == XML_STATUS_ERROR)
        {
            LogError(XML_GetErrorCode(m_parser.get()));
            return false;
        }
        return true;
    }

protected:
    virtual bool Element(const XML_Char* /*name*/, const XML_Char** /*attrs*/) { return true; }
    virtual bool ElementEnd(const XML_Char* /*name*/) { return true; }
    virtual bool ElementData(const XML_Char* /*data*/, int /*length*/) { return true; }

private:
    struct ParserDeleter {
        void operator()(XML_Parser parser) const noexcept { XML_ParserFree(parser); }
    };
    std::unique_ptr<XML_ParserStruct, ParserDeleter> m_parser;

    void LogError(XML_Error errorCode) const noexcept
    {
        kodi::Log(ADDON_LOG_ERROR, "XML parsing error [%d]: %s at line %lu",
                  static_cast<int>(errorCode),
                  XML_ErrorString(errorCode),
                  XML_GetCurrentLineNumber(m_parser.get()));
    }

    static void XMLCALL StartElement(void* userData, const XML_Char* name, const XML_Char** attrs) noexcept
    {
        auto* self = static_cast<Override*>(userData);
        if (!self->Element(name, attrs)) {
            XML_StopParser(self->m_parser.get(), XML_FALSE);
        }
    }

    static void XMLCALL EndElement(void* userData, const XML_Char* name) noexcept
    {
        auto* self = static_cast<Override*>(userData);
        if (!self->ElementEnd(name)) {
            XML_StopParser(self->m_parser.get(), XML_FALSE);
        }
    }

    static void XMLCALL CharacterDataHandler(void* userData, const XML_Char* data, int len) noexcept
    {
        auto* self = static_cast<Override*>(userData);
        if (!self->ElementData(data, len)) {
            XML_StopParser(self->m_parser.get(), XML_FALSE);
        }
    }
};

} // namespace XMLTV

#endif // XML_SAX_HANDLER_H
