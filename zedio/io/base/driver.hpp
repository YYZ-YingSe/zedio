#pragma once

#include "zedio/common/debug.hpp"
#include "zedio/io/base/callback.hpp"
#include "zedio/io/base/config.hpp"
#include "zedio/runtime/queue.hpp"
// C
#include <cstring>
// C++
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <numeric>
#include <queue>
// Linux
#include <liburing.h>
#include <sys/eventfd.h>

namespace zedio::io::detail {

class Driver;

thread_local Driver *t_driver{nullptr};

class Driver : util::Noncopyable {
public:
    Driver(const Config &config)
        : config_{config}
        , waker_fd_{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)} {
        if (waker_fd_ < 0) [[unlikely]] {
            throw std::runtime_error(std::format("call eventfd failed, errorno: {} message: {}",
                                                 waker_fd_,
                                                 strerror(errno)));
        }

        if (auto ret = io_uring_queue_init(config.ring_entries_, &ring_, config.io_uring_flags_);
            ret < 0) [[unlikely]] {
            throw std::runtime_error(
                std::format("Call io_uring_queue_init failed, error: {}.", strerror(-ret)));
        }
        assert(t_driver == nullptr);
        t_driver = this;
    }

    ~Driver() {
        ::close(waker_fd_);
        io_uring_queue_exit(&ring_);
        t_driver = nullptr;
    }

    // Current worker thread will be blocked on io_uring_wait_cqe
    // until other worker wakes up it or a I/O event completes
    void wait(std::optional<std::coroutine_handle<>> &run_next) {
        assert(!run_next.has_value());

        io_uring_cqe *cqe{nullptr};
        if (auto ret = io_uring_wait_cqe(&ring_, &cqe); ret != 0) [[unlikely]] {
            LOG_DEBUG("io_uring_wait_cqe failed {}", strerror(-ret));
            return;
        }
        if (cqe == nullptr) {
            return;
        }
        auto cb = reinterpret_cast<Callback *>(cqe->user_data);
        if (cb != nullptr) [[likely]] {
            cb->result_ = cqe->res;
            run_next = cb->handle_;
        }
        io_uring_cqe_seen(&ring_, cqe);
    }

    [[nodiscard]]
    auto poll(runtime::detail::LocalQueue &local_queue, runtime::detail::GlobalQueue &global_queue)
        -> bool {
        constexpr const auto                      SIZE = Config::LOCAL_QUEUE_CAPACITY;
        std::array<io_uring_cqe *, SIZE * 2>      cqes;
        std::size_t                               num_ex{0};

        std::size_t cnt = io_uring_peek_batch_cqe(&ring_, cqes.data(), sizeof(cqes.data()));
        if (cnt == 0) {
            wait_before();
            return false;
        }
        for (auto i = 0uz; i < cnt;i+=1) {
            auto cb = reinterpret_cast<Callback *>(cqes[i]->user_data);
            if (cb != nullptr) [[likely]] {
                cb->result_ = cqes[i]->res;
                if (cb->is_exclusive_) {
                    cqes[num_ex++] = reinterpret_cast<io_uring_cqe *>(std::addressof(cb->handle_));
                } else {
                    local_queue.push_back_or_overflow(std::move(cb->handle_), global_queue);
                }
            }
        }
        io_uring_cq_advance(&ring_, cnt);
        LOG_TRACE("poll shared: {}, private: {}", cnt - num_ex, num_ex);
        for (auto i = 0uz; i < num_ex; i += 1) {
            reinterpret_cast<std::coroutine_handle<> *>(cqes[i])->resume();
        }
        this->force_submit();
        return true;
    }

    [[nodiscard]]
    auto ring() -> io_uring * {
        return &ring_;
    }

    [[nodiscard]]
    auto get_sqe() -> io_uring_sqe * {
        return io_uring_get_sqe(&ring_);
    }

    void submit() {
        num_submissions_ += 1;
        if (num_submissions_ == config_.num_weak_submissions_) {
            force_submit();
        }
    }

    void force_submit() {
        num_submissions_ = 0;
        if (auto ret = io_uring_submit(&ring_); ret < 0) {
            LOG_ERROR("submit sqes failed, {}", strerror(-ret));
        }
    }

    void push_waiting_coro(std::function<void(io_uring_sqe *sqe)> &&cb) {
        LOG_DEBUG("push a waiting coros");
        waiting_coros_.push_back(std::move(cb));
    }

    void wake_up() {
        static constexpr uint64_t buf{1};
        if (auto ret = ::write(this->waker_fd_, &buf, sizeof(buf)); ret != sizeof(buf))
            [[unlikely]] {
            LOG_ERROR("Waker write failed, error: {}.", strerror(errno));
        }
    }

private:
    void wait_before() {
        decltype(waiting_coros_)::value_type cb{nullptr};
        io_uring_sqe                        *sqe{nullptr};
        if (waker_buf_ != 0) {
            waker_buf_ = 0;
            sqe = this->get_sqe();
            assert(sqe != nullptr);
            io_uring_prep_read(sqe, waker_fd_, &waker_buf_, sizeof(waker_buf_), 0);
            io_uring_sqe_set_data(sqe, nullptr);
        }

        while (!waiting_coros_.empty()) {
            sqe = this->get_sqe();
            if (sqe == nullptr) {
                break;
            }
            cb = std::move(waiting_coros_.front());
            waiting_coros_.pop_front();
        }
        force_submit();
    }

private:
    const Config                                     &config_;
    io_uring                                          ring_{};
    int                                               waker_fd_;
    uint32_t                                          num_submissions_{0};
    uint64_t                                          waker_buf_{1};
    std::list<std::function<void(io_uring_sqe *sqe)>> waiting_coros_{};
};

} // namespace zedio::io::detail
