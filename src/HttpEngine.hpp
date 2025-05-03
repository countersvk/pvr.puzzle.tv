#ifndef HTTPENGINE_HPP
#define HTTPENGINE_HPP

#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include "ActionQueue.hpp"
#include "globals.hpp"

class QueueNotRunningException : public std::exception
{
public:
    const char* what() const noexcept override { return reason.c_str(); }
    const std::string reason;
    explicit QueueNotRunningException(const char* r = "") : reason(r) {}
};

class CurlErrorException : public std::exception
{
public:
    const char* what() const noexcept override { return reason.c_str(); }
    const std::string reason;
    explicit CurlErrorException(const char* r = "") : reason(r) {}
};

class HttpEngine
{
public:
    enum RequestPriority { RequestPriority_Hi, RequestPriority_Low };
    using TCookies = std::map<std::string, std::string>;

    struct Request {
        std::string Url;
        std::string PostData;
        std::vector<std::string> Headers;

        explicit Request(std::string url, 
                        std::string postData = {}, 
                        std::vector<std::string> headers = {})
            : Url(std::move(url)), 
              PostData(std::move(postData)), 
              Headers(std::move(headers)) {}

        bool IsPost() const noexcept { return !PostData.empty(); }
    };

    HttpEngine();
    ~HttpEngine();

    template <typename TParser, typename TCompletion>
    void CallApiAsync(const Request& request, 
                     TParser parser, 
                     TCompletion completion, 
                     RequestPriority priority = RequestPriority_Low)
    {
        if (!m_apiCalls->IsRunning())
            throw QueueNotRunningException("API request queue not running");

        auto shared_this = shared_from_this();
        auto request_copy = request; // Copy for lambda capture
        
        ActionQueue::TAction action = [shared_this, request_copy, parser, completion, priority]() {
            try {
                std::string response;
                std::string effectiveUrl;
                const auto requestId = shared_this->m_DebugRequestId.fetch_add(1);
                
                // Archive-specific processing
                if (request_copy.Url.find("archive") != std::string::npos) {
                    kodi::Log(ADDON_LOG_DEBUG, "Processing archive request: %s", 
                             request_copy.Url.c_str());
                }
                
                DoCurl(request_copy, shared_this->m_sessionCookie, &response, requestId, &effectiveUrl);
                
                // Archive data handling
                if (!response.empty() && priority == RequestPriority_Hi) {
                    shared_this->ProcessArchiveResponse(response);
                }
                
                shared_this->RunOnCompletion([=]() {
                    try {
                        parser(response);
                        completion(ActionQueue::ActionResult(ActionQueue::ActionStatus::Completed));
                    } catch (...) {
                        completion(ActionQueue::ActionResult(
                            ActionQueue::ActionStatus::Failed, 
                            std::current_exception()
                        ));
                    }
                }, priority);
                
            } catch (...) {
                completion(ActionQueue::ActionResult(
                    ActionQueue::ActionStatus::Failed, 
                    std::current_exception()
                ));
            }
        };

        if (priority == RequestPriority_Hi) {
            m_apiCalls->PerformHiPriority(action, [completion](const auto& result) {
                if (result.status != ActionQueue::ActionStatus::Completed) {
                    completion(result);
                }
            });
        } else {
            m_apiCalls->PerformAsync(action, [completion](const auto& result) {
                if (result.status != ActionQueue::ActionStatus::Completed) {
                    completion(result);
                }
            });
        }
    }

    void RunOnCompletion(ActionQueue::TAction action, RequestPriority priority) {
        auto handler = [action](const ActionQueue::ActionResult&) { action(); };
        
        if (priority == RequestPriority_Hi) {
            m_apiHiPriorityCallCompletions->PerformAsync(action, handler);
        } else {
            m_apiCallCompletions->PerformAsync(action, handler);
        }
    }

    void CancelAllRequests();
    static void SetCurlTimeout(long timeout);
    static bool CheckInternetConnection(long timeout = 10);
    TCookies m_sessionCookie;

private:
    void ProcessArchiveResponse(const std::string& response) {
        try {
            // Archive processing logic
            kodi::Log(ADDON_LOG_INFO, "Processing archive response (%zu bytes)", response.size());
            // Add actual archive parsing here
        } catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "Archive processing failed: %s", e.what());
        }
    }

    static size_t CurlWriteData(void* buffer, size_t size, size_t nmemb, void* userp) {
        std::string* response = static_cast<std::string*>(userp);
        response->append(static_cast<char*>(buffer), size * nmemb);
        return size * nmemb;
    }

    static void DoCurl(const Request& request, 
                      const TCookies& cookie, 
                      std::string* response, 
                      uint64_t requestId = 0, 
                      std::string* effectiveUrl = nullptr);

    std::shared_ptr<ActionQueue::CActionQueue> m_apiCalls;
    std::shared_ptr<ActionQueue::CActionQueue> m_apiCallCompletions;
    std::shared_ptr<ActionQueue::CActionQueue> m_apiHiPriorityCallCompletions;
    
    std::atomic<uint64_t> m_DebugRequestId{1};
    static std::atomic<long> c_CurlTimeout;
};

#endif // HTTPENGINE_HPP
