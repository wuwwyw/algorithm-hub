#include <iostream>
#include <unistd.h>
#include <memory>
#include "../net/Tcpserver.hpp"


int main(int argc, char *argv[]) {
    if (argc != 1 && argc != 3 && !(argc == 2 && std::string(argv[1]) == "--check-config")) {
        std::cerr << "Usage: " << argv[0] << " [IPv4 port | --check-config]\n";
        return 1;
    }
    try {
        const auto config = ServerConfig::load();
        if (argc == 2) {
            std::cout << "Configuration valid (database connectivity not checked)\n"
                      << "HTTP: " << config.listen_host << ':' << config.listen_port << '\n'
                      << "MySQL: " << config.db_host << ':' << config.db_port
                      << ", user=" << config.db_user << ", database=" << config.db_name
                      << ", password=[set]\n"
                      << "Redis: " << config.redis_host << ':' << config.redis_port
                      << ", authentication=" << (config.redis_password.empty() ? "disabled" : "enabled") << '\n';
            return 0;
        }
        const std::string ip = argc == 3 ? argv[1] : config.listen_host;
        const auto port = argc == 3 ? ServerConfig::parse_port(argv[2], "port") : config.listen_port;
        in_addr address{};
        if (inet_pton(AF_INET, ip.c_str(), &address) != 1) {
            throw std::runtime_error("HTTP listen address must be a valid IPv4 address");
        }
        auto server = std::make_unique<TcpServer>(static_cast<uint16_t>(port), ip, config);
        server->loop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Startup failed: " << error.what() << '\n';
        return 1;
    }
}
