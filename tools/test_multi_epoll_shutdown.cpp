#include <unistd.h>
#include "../net/Tcpserver.hpp"
#include <filesystem>

static int descriptor_count() {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        (void)entry;
        ++count;
    }
    return count;
}

int main() {
    const int before = descriptor_count();
    const auto start = std::chrono::steady_clock::now();
    {
        // Port zero asks the kernel to choose a free local port.
        TcpServer server(0, "127.0.0.1");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    const int after = descriptor_count();
    std::cout << "construct_and_destroy_ms=" << elapsed
              << " descriptors_before=" << before
              << " descriptors_after=" << after << '\n';
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code error;
        auto target = std::filesystem::read_symlink(entry.path(), error);
        if (!error) std::cout << entry.path().filename() << " -> " << target << '\n';
    }
    return before == after ? 0 : 1;
}
