#include "async.hpp"
#include "log.hpp"

#include <string>

using namespace zed::async;

int p[2];

auto r() -> Task<void> {
    while (true) {
        char buf[1024]{0};
        auto n = co_await zed::async::Read(p[0], buf, sizeof(buf), 0);
        zed::log::console.info("read {} buf: {}", n, buf);
        co_await zed::async::Sleep(1s);
    }
}

auto w() -> Task<void> {
    long long num = 1;
    while (true) {
        auto str = std::to_string(num++);
        zed::log::console.info("write {}", str);
        auto n = co_await zed::async::Write(p[1], str.c_str(), str.size(), 0);
        co_await zed::async::Sleep(1s);
    }
}

int main() {
    pipe(p);
    Scheduler s;
    s.push(r());
    s.push(w());
    s.run();
    return 0;
}