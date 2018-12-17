// ---------------------------------------------------------------------------
//
//  Author  : github.com/luncliff (luncliff@gmail.com)
//  License : CC BY 4.0
//
// ---------------------------------------------------------------------------
#include <messaging/concurrent.h>

bool concurrent_message_queue::is_full() const noexcept
{
    return qu.is_full();
}
bool concurrent_message_queue::empty() const noexcept
{
    return qu.empty();
}
bool concurrent_message_queue::push(const value_type msg) noexcept
{
    std::unique_lock lck{cs};
    return qu.push(msg);
}
bool concurrent_message_queue::try_pop(reference msg) noexcept
{
    std::unique_lock lck{cs};
    return qu.try_pop(msg);
}