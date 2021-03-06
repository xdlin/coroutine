// ---------------------------------------------------------------------------
//
//  Author  : github.com/luncliff (luncliff@gmail.com)
//  License : CC BY 4.0
//
// ---------------------------------------------------------------------------
#include <coroutine/net.h>

auto wait_io_tasks(std::chrono::nanoseconds) noexcept(false)
    -> enumerable<coroutine_task_t>
{
    // windows implementation rely on callback.
    // So this function will always yield nothing
    co_return;
}

GSL_SUPPRESS(type .1)
GSL_SUPPRESS(f .6)
void CALLBACK onWorkDone(DWORD errc, DWORD sz, LPWSAOVERLAPPED pover,
                         DWORD flags) noexcept
{
    UNREFERENCED_PARAMETER(flags);

    io_work_t* work = reinterpret_cast<io_work_t*>(pover);

    // mostly Internal and InternalHigh holds same value.
    // so this assignment is redundant, but make it sure.
    work->Internal = errc;   // -> return of `await_resume()`
    work->InternalHigh = sz; // -> return of `work.error()`

    work->task.resume();
}

bool io_work_t::ready() const noexcept
{
    return false; // trigger `await_suspend`
}

uint32_t io_work_t::error() const noexcept
{
    static_assert(sizeof(DWORD) == sizeof(uint32_t));
    return gsl::narrow_cast<uint32_t>(Internal);
}

auto zero_overlapped(gsl::not_null<io_work_t*> work) noexcept
    -> gsl::not_null<OVERLAPPED*>
{
    OVERLAPPED* pover = work;
    *pover = OVERLAPPED{};
    return pover;
}

GSL_SUPPRESS(type .1)
auto make_wsa_buf(buffer_view_t v) noexcept -> WSABUF
{
    WSABUF buf{}; // expect NRVO
    buf.buf = reinterpret_cast<char*>(v.data());
    buf.len = gsl::narrow_cast<ULONG>(v.size_bytes());
    return buf;
}

GSL_SUPPRESS(type .1)
auto send_to(uint64_t sd, const sockaddr_in6& remote, buffer_view_t buffer,
             io_work_t& work) noexcept(false) -> io_send_to&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.to6 = std::addressof(remote);
    work.Internal = sd;
    work.InternalHigh = sizeof(remote);

    // lead to co_await operations with `io_send_to` type
    return *reinterpret_cast<io_send_to*>(std::addressof(work));
}

GSL_SUPPRESS(type .1)
auto send_to(uint64_t sd, const sockaddr_in& remote, buffer_view_t buffer,
             io_work_t& work) noexcept(false) -> io_send_to&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.to = std::addressof(remote);
    work.Internal = sd;
    work.InternalHigh = sizeof(remote);
    // lead to co_await operations with `io_send_to` type
    return *reinterpret_cast<io_send_to*>(std::addressof(work));
}

void io_send_to::suspend(coroutine_task_t rh) noexcept(false)
{
    const auto addrlen = gsl::narrow_cast<socklen_t>(InternalHigh);
    const auto flag = DWORD{0};
    const auto sd = gsl::narrow_cast<SOCKET>(Internal);
    auto bufs = make_wsa_buf(buffer);

    task = rh;                                // coroutine for the i/o callback
    ::WSASendTo(sd, &bufs, 1,                 //
                nullptr, flag, addr, addrlen, //
                zero_overlapped(this), onWorkDone);

    const auto ec = WSAGetLastError();
    if (ec == NO_ERROR || ec == ERROR_IO_PENDING)
        return; // ok. expected for async i/o

    throw std::system_error{ec, std::system_category(), "WSASendTo"};
}

int64_t io_send_to::resume() noexcept
{
    return gsl::narrow_cast<int64_t>(InternalHigh);
}

GSL_SUPPRESS(type .1)
auto recv_from(uint64_t sd, sockaddr_in6& remote, buffer_view_t buffer,
               io_work_t& work) noexcept(false) -> io_recv_from&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.from6 = std::addressof(remote);
    work.Internal = sd;
    work.InternalHigh = sizeof(remote);

    // lead to co_await operations with `io_recv_from` type
    return *reinterpret_cast<io_recv_from*>(std::addressof(work));
}

GSL_SUPPRESS(type .1)
auto recv_from(uint64_t sd, sockaddr_in& remote, buffer_view_t buffer,
               io_work_t& work) noexcept(false) -> io_recv_from&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.from = std::addressof(remote);
    work.Internal = sd;
    work.InternalHigh = sizeof(remote);

    // lead to co_await operations with `io_recv_from` type
    return *reinterpret_cast<io_recv_from*>(std::addressof(work));
}

void io_recv_from::suspend(coroutine_task_t rh) noexcept(false)
{
    const auto sd = gsl::narrow_cast<SOCKET>(Internal);
    auto addrlen = gsl::narrow_cast<socklen_t>(InternalHigh);
    auto flag = DWORD{0};
    auto buf = make_wsa_buf(buffer);

    task = rh;                 // coroutine for the i/o callback
    ::WSARecvFrom(sd, &buf, 1, //
                  nullptr, &flag, addr, &addrlen, //
                  zero_overlapped(this), onWorkDone);

    const auto ec = WSAGetLastError();
    if (ec == NO_ERROR || ec == ERROR_IO_PENDING)
        return; // ok. expected for async i/o

    throw std::system_error{ec, std::system_category(), "WSARecvFrom"};
}

int64_t io_recv_from::resume() noexcept
{
    return gsl::narrow_cast<int64_t>(InternalHigh);
}

GSL_SUPPRESS(type .1)
auto send_stream(uint64_t sd, buffer_view_t buffer, uint32_t flag,
                 io_work_t& work) noexcept(false) -> io_send&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.Internal = sd;
    work.InternalHigh = flag;
    // trigger co_await operations with `io_send` type
    return *reinterpret_cast<io_send*>(std::addressof(work));
}

void io_send::suspend(coroutine_task_t rh) noexcept(false)
{
    const auto sd = gsl::narrow_cast<SOCKET>(Internal);
    const auto flag = gsl::narrow_cast<DWORD>(InternalHigh);
    auto buf = make_wsa_buf(buffer);

    task = rh;             // coroutine frame for
    ::WSASend(sd, &buf, 1, //   overlapped callback
              nullptr, flag, zero_overlapped(this), onWorkDone);

    const auto ec = WSAGetLastError();
    if (ec == NO_ERROR || ec == ERROR_IO_PENDING)
        return; // ok. expected for async i/o

    throw std::system_error{ec, std::system_category(), "WSASend"};
}

int64_t io_send::resume() noexcept
{
    return gsl::narrow_cast<int64_t>(InternalHigh);
}

GSL_SUPPRESS(type .1)
auto recv_stream(uint64_t sd, buffer_view_t buffer, uint32_t flag,
                 io_work_t& work) noexcept(false) -> io_recv&
{
    static_assert(sizeof(SOCKET) == sizeof(uint64_t));
    static_assert(sizeof(HANDLE) == sizeof(SOCKET));

    work.buffer = buffer;
    work.Internal = sd;
    work.InternalHigh = flag;
    // lead to co_await operations with `io_recv` type
    return *reinterpret_cast<io_recv*>(std::addressof(work));
}

void io_recv::suspend(coroutine_task_t rh) noexcept(false)
{
    const auto sd = gsl::narrow_cast<SOCKET>(Internal);
    auto flag = gsl::narrow_cast<DWORD>(InternalHigh);
    auto buf = make_wsa_buf(buffer);

    task = rh;             // coroutine frame for
    ::WSARecv(sd, &buf, 1, //   overlapped callback
              nullptr, &flag, zero_overlapped(this), onWorkDone);

    const auto ec = WSAGetLastError();
    if (ec == NO_ERROR || ec == ERROR_IO_PENDING)
        return; // ok. expected for async i/o

    throw std::system_error{ec, std::system_category(), "WSARecv"};
}

int64_t io_recv::resume() noexcept
{
    return gsl::narrow_cast<int64_t>(InternalHigh);
}
