#include "HttpEngine.hpp"
#include <thread>
#include <chrono>
#include <kodi/Filesystem.h>
#include "base64.h"
#include "httplib.h"

using namespace std::chrono_literals;

long HttpEngine::c_CurlTimeout = 15;

HttpEngine::HttpEngine() 
    : m_apiCalls(std::make_unique<CActionQueue>(100000, "API Calls")),
      m_apiCallCompletions(std::make_unique<CActionQueue>(100000, "API Completion")),
      m_apiHiPriorityCallCompletions(std::make_unique<CActionQueue>(100000, "HiPri API")),
      m_DebugRequestId(1)
{
    m_apiCalls->Start();
    m_apiCallCompletions->Start();
    m_apiHiPriorityCallCompletions->Start();
}

// Метод для работы с архивом
void HttpEngine::FetchArchiveData(const std::string& archiveUrl)
{
    Request request{archiveUrl};
    auto requestId = m_DebugRequestId.fetch_add(1);
    
    m_apiCalls->PerformAsync([=]() {
        std::string response;
        std::string effectiveUrl;
        DoCurl(request, {}, &response, requestId, &effectiveUrl);
        
        // Обработка архивных данных
        if(!response.empty()) {
            ProcessArchiveResponse(response);
        }
    }, [](const ActionResult& result) {
        if(result.status != ActionStatus::Completed) {
            kodi::Log(ADDON_LOG_ERROR, "Archive fetch failed!");
        }
    });
}

void HttpEngine::ProcessArchiveResponse(const std::string& response)
{
    // Реальная логика обработки архива 
    kodi::Log(ADDON_LOG_INFO, "Processing archive data...");
    // ... парсинг XML/JSON, обработка временных меток и т.д.
}

bool HttpEngine::CheckInternetConnection(long timeoutSec)
{
    try {
        kodi::vfs::CFile file;
        return file.CURLCreate("https://www.google.com") && 
               file.CURLOpen(ADDON_READ_NO_CACHE);
    } 
    catch (...) {
        return false;
    }
}

void HttpEngine::DoCurl(const Request& request, const TCookies& cookies,
                       std::string* response, uint64_t requestId,
                       std::string* effectiveUrl)
{
    kodi::vfs::CFile curl;
    const auto startTime = std::chrono::steady_clock::now();
    
    try {
        // Базовая настройка CURL
        curl.CURLCreate(httplib::detail::encode_url(request.Url));
        curl.SetTimeout(c_CurlTimeout);

        // Заголовки и куки
        for (const auto& header : request.Headers) {
            size_t pos = header.find(':');
            if(pos != std::string::npos) {
                curl.AddHeader(header.substr(0, pos), header.substr(pos+1));
            }
        }

        std::string cookieStr;
        for (const auto& [name, value] : cookies) {
            cookieStr += name + "=" + value + "; ";
        }
        if (!cookieStr.empty()) {
            curl.AddHeader("Cookie", cookieStr);
        }

        // Выполнение запроса
        if(curl.CURLOpen(request.IsPost() ? ADDON_WRITE_NO_CACHE : ADDON_READ_NO_CACHE)) 
        {
            // Чтение ответа
            char buffer[32*1024];
            ssize_t bytesRead;
            while ((bytesRead = curl.Read(buffer, sizeof(buffer))) > 0) {
                response->append(buffer, bytesRead);
            }

            // Для архива: сохранение эффективного URL
            if(effectiveUrl) {
                *effectiveUrl = curl.GetEffectiveURL();
            }
        }

        // Логирование времени выполнения (важно для архива)
        auto duration = std::chrono::steady_clock::now() - startTime;
        kodi::Log(ADDON_LOG_DEBUG, "Archive request took %lldms", 
                 std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }
    catch (const std::exception& e) {
        kodi::Log(ADDON_LOG_ERROR, "Archive error: %s", e.what());
        throw;
    }
}

void HttpEngine::CancelAllRequests()
{
    m_apiCalls->StopThread();
    m_apiCallCompletions->StopThread();
    m_apiHiPriorityCallCompletions->StopThread();
}

HttpEngine::~HttpEngine()
{
    CancelAllRequests();
}

size_t HttpEngine::CurlWriteData(void* buffer, size_t size, size_t nmemb, void* userp)
{
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(buffer), size * nmemb);
    return size * nmemb;
}

void HttpEngine::SetCurlTimeout(long timeout)
{
    c_CurlTimeout = timeout;
}
