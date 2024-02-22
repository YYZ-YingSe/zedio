#pragma once

#include "zedio/net/addr.hpp"
#include "zedio/net/datagram.hpp"

namespace zedio::net {

class UnixDatagram : public detail::BaseDatagram<UnixDatagram, UnixSocketAddr> {
    friend class BaseDatagram;

private:
    UnixDatagram(IO &&io)
        : BaseDatagram{std::move(io)} {}

public:
    [[nodiscard]]
    auto set_mark(uint32_t mark) const noexcept {
        return io_.set_mark(mark);
    }

    [[nodiscard]]
    auto set_passcred(bool on)const noexcept {
        return io_.set_passcred(on);
    }

    [[nodiscard]]
    auto passcred() const noexcept {
        return io_.passcred();
    }
};

} // namespace zedio::net
