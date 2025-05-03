#ifndef ACTION_QUEUE_TYPES_H
#define ACTION_QUEUE_TYPES_H

#include <functional>
#include <exception>
#include <memory>

namespace ActionQueue
{
    enum class ActionStatus 
    {
        Completed,
        Cancelled,
        Failed
    };

    struct ActionResult
    {
        ActionStatus status;
        std::exception_ptr exception;
        
        explicit ActionResult(ActionStatus s, 
                             std::exception_ptr e = nullptr) noexcept
            : status(s), exception(std::move(e)) 
        {}
    };

    class IActionQueueItem
    {
    public:
        virtual void Perform() = 0;
        virtual void Cancel() = 0;
        virtual ~IActionQueueItem() = default;
    };

    // Modern C++ type aliases
    using TAction = std::function<void()>;
    using TCompletion = std::function<void(const ActionResult&)>;
}

#endif // ACTION_QUEUE_TYPES_H
