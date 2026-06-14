#include <iostream>
#include <string>
#include <csignal>
#include "Config.hpp"
#include "Server.hpp"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN); // 추가: 클라이언트가 갑자기 끊겨도 서버가 안 죽게
    const std::string configPath = (argc > 1) ? argv[1] : DEFAULT_CONFIG_PATH;

    try {
        Config config(configPath);
        config.configParse();

        const std::vector<ServerConfig>& servers = config.getServers();
        if (servers.empty()) {
            std::cerr << "Config error: no server blocks found" << std::endl;
            return 1;
        }

        Server server(servers);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Startup error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
