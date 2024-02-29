#pragma once

#include "zedio/io/io.hpp"

namespace zedio::fs::detail {

class FileIO : public io::IO {
private:
    explicit FileIO(int fd)
        : IO{fd} {}

public:
    [[nodiscard]]
    auto fsync(unsigned int fsync_flags) noexcept {
        return io::Fsync{fd_, fsync_flags};
    }

    [[nodiscard]]
    auto metadata() const noexcept {
        class Statx : public io::detail::IORegistrator<Statx, decltype(io_uring_prep_statx)> {
        private:
            using Super = io::detail::IORegistrator<Statx, decltype(io_uring_prep_statx)>;

        public:
            Statx(int fd)
                : Super{io_uring_prep_statx,
                        fd,
                        "",
                        AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT,
                        STATX_ALL,
                        &statx_} {}

            auto await_resume() const noexcept -> Result<struct statx> {
                if (this->cb_.result_ >= 0) [[likely]] {
                    return statx_;
                } else {
                    return std::unexpected{make_sys_error(-this->cb_.result_)};
                }
            }

        private:
            struct statx statx_ {};
        };
        return Statx{fd_};
    }

    template <class T>
    [[nodiscard]]
    auto read_to_end(T &buf) const noexcept -> zedio::async::Task<Result<void>> {
        auto offset = buf.size();
        {
            auto ret = co_await this->metadata();
            if (!ret) {
                co_return std::unexpected{ret.error()};
            }
            buf.resize(offset + ret.value().stx_size);
        }
        auto span = std::span<char>{buf}.subspan(offset);
        auto ret = Result<std::size_t>{};
        while (true) {
            ret = co_await this->read(span);
            if (!ret) [[unlikely]] {
                co_return std::unexpected{ret.error()};
            }
            if (ret.value() == 0) {
                break;
            }
            span = span.subspan(ret.value());
            if (span.empty()) {
                break;
            }
        }
        co_return Result<void>{};
    }

public:
    template <class FileType>
    [[nodiscard]]
    static auto open(const std::string_view &path, int flags, mode_t mode) {
        class Open : public io::detail::IORegistrator<Open, decltype(io_uring_prep_openat)> {
        private:
            using Super = io::detail::IORegistrator<Open, decltype(io_uring_prep_openat)>;

        public:
            Open(int dfd, const char *path, int flags, mode_t mode)
                : Super{io_uring_prep_openat, dfd, path, flags, mode} {}

            Open(const char *path, int flags, mode_t mode)
                : Open{AT_FDCWD, path, flags, mode} {}

            auto await_resume() const noexcept -> Result<FileType> {
                if (this->cb_.result_ >= 0) [[likely]] {
                    return FileType{FileIO{this->cb_.result_}};
                } else {
                    return std::unexpected{make_sys_error(-this->cb_.result_)};
                }
            }
        };
        return Open{path.data(), flags, mode};
    }
};

} // namespace zedio::fs::detail
