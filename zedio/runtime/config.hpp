#pragma once

// C++
#include <format>
#include <thread>

namespace zedio::runtime::detail {

/// Timer
// Max wheel level
static inline constexpr std::size_t MAX_LEVEL = 6;
// size of slot per wheel
static inline constexpr std::size_t SLOT_SIZE{64};

/// Queue
static inline constexpr std::size_t LOCAL_QUEUE_CAPACITY{256};

struct Config {
    // size of io_uring_queue entries
    std::size_t ring_entries_{1024};
    // io_uring flags
    uint32_t ring_flags_{0};
    // 0: Merge all submission
    uint32_t submit_interval_{4};
    // num of worker
    std::size_t num_workers_{std::thread::hardware_concurrency()};
    // How many ticks worker to poll finished I/O operation
    uint32_t check_io_interval_{61};
    // How many ticks worker to pop global queue
    uint32_t check_global_interval_{61};
};

} // namespace zedio::runtime::detail

namespace std {

template <>
class formatter<zedio::runtime::detail::Config> {
public:
    constexpr auto parse(format_parse_context &context) {
        auto it{context.begin()};
        auto end{context.end()};
        if (it == end || *it == '}') {
            return it;
        }
        ++it;
        if (it != end && *it != '}') {
            throw format_error("Invalid format specifier for Config");
        }
        return it;
    }

    auto format(const zedio::runtime::detail::Config &config, auto &context) const noexcept {
        return format_to(context.out(),
                         "I/ODriver [ entries: {}, flags: {}, submit_interval: {} ], Worker [ num: "
                         "{}, check_io_interval: {}, check_global_interval: {} ]",
                         config.ring_entries_,
                         config.ring_flags_,
                         config.submit_interval_,
                         config.num_workers_,
                         config.check_io_interval_,
                         config.check_global_interval_);
    }
};

} // namespace std