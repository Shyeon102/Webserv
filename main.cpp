#include <iostream>
#include <string>
#include <csignal>
#include "Config.hpp"
#include "Server.hpp"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN); // 추가: 클라이언트가 갑자기 끊겨도 서버가 안 죽게
    const std::string configPath = (argc > 1) ? argv[1] : DEFAULT_CONFIG_PATH;
    // config path를 입력 받음.
    try {
        Config config(configPath);
        config.configParse();
        // config path의 설정파일로 설정 맞추기
        const std::vector<ServerConfig>& servers = config.getServers();
        // server config 객체 생성 - vector로 server가 여러개니까. 
        if (servers.empty()) {
            // server가 없으면 에러처리 
            std::cerr << "Config error: no server blocks found" << std::endl;
            return 1;
        }
        // 서버가 있으면 각 서버를 실행. 
        Server server(servers);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Startup error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
