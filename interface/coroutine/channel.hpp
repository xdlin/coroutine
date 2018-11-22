﻿// ---------------------------------------------------------------------------
//
//  Author  : github.com/luncliff (luncliff@gmail.com)
//  License : CC BY 4.0
//  Note
//      Coroutine channel. This is basesd on GoLang channel
//
// ---------------------------------------------------------------------------
#ifndef COROUTINE_CHANNEL_HPP
#define COROUTINE_CHANNEL_HPP

#include <cassert>
#include <mutex>
#include <tuple>

#include <experimental/coroutine>

namespace internal
{
static void* poison() noexcept { return reinterpret_cast<void*>(0xFADE'BCFA); }

// - Note
//      Minimal linked list without node allocation
template<typename NodeType>
class list
{
    using node_t = NodeType;

    node_t* head{};
    node_t* tail{};

  public:
    list() noexcept = default;

  public:
    bool is_empty() const noexcept { return head == nullptr; }
    void push(node_t* node) noexcept
    {
        if (tail)
        {
            tail->next = node;
            tail = node;
        }
        else
            head = tail = node;
    }
    auto pop() noexcept -> node_t*
    {
        node_t* node = head;
        if (head == tail)
            // empty or 1
            head = tail = nullptr;
        else
            // 2 or more
            head = head->next;

        // this can be nullptr
        return node;
    }
};
} // namespace internal

template<typename T, typename Lockable>
class channel;
template<typename T, typename Lockable>
class reader;
template<typename T, typename Lockable>
class writer;

// - Note
//      Awaitable reader for `channel`
template<typename T, typename Lockable>
class reader final
{
  public:
    using value_type = T;
    using pointer = T*;
    using reference = T&;
    using channel_type = channel<T, Lockable>;

  private:
    using writer = typename channel_type::writer;
    using writer_list = typename channel_type::writer_list;
    using reader_list = typename channel_type::reader_list;

    friend channel_type;
    friend writer;
    friend reader_list;

  private:
    mutable pointer ptr; // Address of value
    mutable void* frame; // Resumeable Handle
    union {
        reader* next = nullptr; // Next reader in channel
        channel_type* chan;     // Channel to push this reader
    };

  private:
    explicit reader(channel_type& ch) noexcept
        : ptr{}, frame{nullptr}, chan{std::addressof(ch)}
    {
    }
    reader(const reader&) noexcept = delete;
    reader& operator=(const reader&) noexcept = delete;

  public:
    reader(reader&& rhs) noexcept
    {
        std::swap(this->ptr, rhs.ptr);
        std::swap(this->frame, rhs.frame);
        std::swap(this->chan, rhs.chan);
    }
    reader& operator=(reader&& rhs) noexcept
    {
        std::swap(this->ptr, rhs.ptr);
        std::swap(this->frame, rhs.frame);
        std::swap(this->chan, rhs.chan);
        return *this;
    }

  public:
    bool await_ready() const noexcept;
    void await_suspend(std::experimental::coroutine_handle<> rh) noexcept;
    auto await_resume() noexcept -> std::tuple<value_type, bool>;
};

// - Note
//      Awaitable writer for `channel`
template<typename T, typename Lockable>
class writer final
{
  public:
    using value_type = T;
    using pointer = T*;
    using reference = T&;
    using channel_type = channel<T, Lockable>;

  private:
    using reader = typename channel_type::reader;
    using writer_list = typename channel_type::writer_list;
    using reader_list = typename channel_type::reader_list;

    friend channel_type;
    friend reader;
    friend writer_list;

  private:
    mutable pointer ptr; // Address of value
    mutable void* frame; // Resumeable Handle
    union {
        writer* next = nullptr; // Next writer in channel
        channel_type* chan;     // Channel to push this writer
    };

  private:
    explicit writer(channel_type& ch, pointer pv) noexcept
        : ptr{pv}, frame{nullptr}, chan{std::addressof(ch)}
    {
    }
    writer(const writer&) noexcept = delete;
    writer& operator=(const writer&) noexcept = delete;

  public:
    writer(writer&& rhs) noexcept
    {
        std::swap(this->ptr, rhs.ptr);
        std::swap(this->frame, rhs.frame);
        std::swap(this->chan, rhs.chan);
    }
    writer& operator=(writer&& rhs) noexcept
    {
        std::swap(this->ptr, rhs.ptr);
        std::swap(this->frame, rhs.frame);
        std::swap(this->chan, rhs.chan);
        return *this;
    }

  public:
    bool await_ready() const noexcept;
    void await_suspend(std::experimental::coroutine_handle<> _rh) noexcept;
    bool await_resume() noexcept;
};

// - Note
//      Coroutine Channel
//      Channel doesn't support Copy, Move
template<typename T, typename Lockable>
class channel final : private internal::list<reader<T, Lockable>>,
                      private internal::list<writer<T, Lockable>>
{
    static_assert(std::is_reference<T>::value == false,
                  "Using reference for channel is forbidden.");

  public:
    using value_type = T;
    using pointer = value_type*;
    using reference = value_type&;

    using mutex_t = Lockable;

  private:
    using reader = reader<value_type, mutex_t>;
    using reader_list = internal::list<reader>;

    using writer = writer<value_type, mutex_t>;
    using writer_list = internal::list<writer>;

    friend reader;
    friend writer;

  private:
    mutex_t mtx{};

  public:
    channel() noexcept(false) : reader_list{}, writer_list{}, mtx{} {}
    channel(const channel&) noexcept = delete;
    channel(channel&&) noexcept = delete;

    channel& operator=(const channel&) noexcept = delete;
    channel& operator=(channel&&) noexcept = delete;

    ~channel() noexcept
    {
        writer_list& writers = *this;
        reader_list& readers = *this;

        //
        // Because of thread scheduling,
        // Some coroutines can be enqueued into list just after
        // this destructor unlocks.
        //
        // But this can't be detected at once since
        // we have 2 list in the channel...
        //
        // Current implementation triggers scheduling repeatedly
        // to reduce the possibility. As repeat count becomes greater,
        // the possibility drops to zero. But notice that it is NOT zero.
        //
        size_t repeat = 1; // recommend 5'000+ repeat for hazard usage
        while (repeat--)
        {
            // Give chance to other coroutines to come into the lists
            // std::this_thread::yield();
            std::unique_lock lck{this->mtx};
            while (writers.is_empty() == false)
            {
                writer* w = writers.pop();
                auto rh = std::experimental::coroutine_handle<>::from_address(
                    w->frame);
                w->frame = internal::poison();

                rh.resume();
            }
            while (readers.is_empty() == false)
            {
                reader* r = readers.pop();
                auto rh = std::experimental::coroutine_handle<>::from_address(
                    r->frame);
                r->frame = internal::poison();

                rh.resume();
            }
        }
        return;
    }

  public:
    // - Note
    //      Awaitable write.
    //      `writer` type implements the awaitable concept
    decltype(auto) write(reference ref) noexcept
    {
        return writer{*this, std::addressof(ref)};
    }
    // - Note
    //      Awaitable read.
    //      `reader` type implements the awaitable concept
    decltype(auto) read() noexcept { return reader{*this}; }
};

template<typename T, typename M>
bool reader<T, M>::await_ready() const noexcept
{
    chan->mtx.lock();
    if (chan->writer_list::is_empty()) return false;

    writer* w = chan->writer_list::pop();
    assert(w != nullptr);
    assert(w->ptr != nullptr);
    assert(w->frame != nullptr);

    // exchange address & resumeable_handle
    std::swap(this->ptr, w->ptr);
    std::swap(this->frame, w->frame);

    chan->mtx.unlock();
    return true;
}

template<typename T, typename M>
void reader<T, M>::await_suspend(
    std::experimental::coroutine_handle<void> coro) noexcept
{
    // notice that next & chan are sharing memory
    channel_type& ch = *(this->chan);

    this->frame = coro.address(); // remember handle before push/unlock
    this->next = nullptr;         // clear to prevent confusing

    ch.reader_list::push(this); // push to channel
    ch.mtx.unlock();
}

template<typename T, typename M>
auto reader<T, M>::await_resume() noexcept -> std::tuple<value_type, bool>
{
    // frame holds poision if the channel is going to destroy
    if (this->frame == internal::poison())
        return std::make_tuple(value_type{}, false);

    // Store first. we have to do this
    // because the resume operation can destroy the writer coroutine
    value_type value = std::move(*ptr);
    if (auto rh = std::experimental::coroutine_handle<>::from_address(frame))
    {
        assert(this->frame != nullptr);
        assert(*reinterpret_cast<uint64_t*>(frame) != 0);
        rh.resume();
    }

    return std::make_tuple(std::move(value), true);
}

template<typename T, typename M>
bool writer<T, M>::await_ready() const noexcept
{
    chan->mtx.lock();
    if (chan->reader_list::is_empty()) return false;

    reader* r = chan->reader_list::pop();
    // exchange address & resumeable_handle
    std::swap(this->ptr, r->ptr);
    std::swap(this->frame, r->frame);

    chan->mtx.unlock();
    return true;
}

template<typename T, typename M>
void writer<T, M>::await_suspend(
    std::experimental::coroutine_handle<void> coro) noexcept
{
    // notice that next & chan are sharing memory
    channel_type& ch = *(this->chan);

    this->frame = coro.address(); // remember handle before push/unlock
    this->next = nullptr;         // clear to prevent confusing

    ch.writer_list::push(this); // push to channel
    ch.mtx.unlock();
}

template<typename T, typename M>
bool writer<T, M>::await_resume() noexcept
{
    // frame holds poision if the channel is going to destroy
    if (this->frame == internal::poison()) return false;

    if (auto rh = std::experimental::coroutine_handle<>::from_address(frame))
    {
        assert(this->frame != nullptr);
        assert(*reinterpret_cast<uint64_t*>(frame) != 0);
        rh.resume();
    }
    return true;
}

#endif // COROUTINE_CHANNEL_HPP
