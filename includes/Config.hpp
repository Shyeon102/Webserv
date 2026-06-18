/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25 16:04:02 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/06/19 00:59:37 by jihyeki2         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#define DEFAULT_CONFIG_PATH "./www.conf"

#include "Token.hpp"
#include "ConfigTokenizer.hpp"
#include "ServerConfig.hpp"
#include "ConfigException.hpp"
#include <fstream>
#include <sstream> // std::stringstream
#include <stdexcept>
#include <iostream>
#include <vector>

enum	ParseState
{
	STATE_GLOBAL,
	STATE_SERVER,
	STATE_LOCATION
};

/* Config 파일 전체 소유, parse state 구조 검증까지 담당 */
class	Config
{
	public:
		Config(const std::string &filePath);
		~Config(void);

		void	configParse(void);

		/* 외부에서 _servers 접근 위한 함수 (나중에 Server쪽에서 사용) */
		const std::vector<ServerConfig>&	getServers(void) const;

	private: // 논리적 + 가독성을 위해 private 2개로 분리 (C++에서 private 여러개 작성 가능)
		struct	ParserContext
		{
			ParseState		state;
			size_t			i;

			ServerConfig	currentServer;
			LocationConfig*	currentLocation;

			bool	serverOpened;
			bool	locationOpened;
			bool	expectingServerBrace; // server 이후 '{' 반드시 기다려야 하는 상태면 true, '{' 를 만나면 false (기다리지 않아도 됨)
			bool	expectingLocationBrace; // location 이후 '{' 반드시 기다려야 하는 상태면 true, '{' 를 만나면 false (기다리지 않아도 됨)

			// 생성자
			ParserContext() : state(STATE_GLOBAL), i(0), currentLocation(0), serverOpened(false),
				locationOpened(false), expectingServerBrace(false), expectingLocationBrace(false) {}
		};

		/* State handlers */
		void	handleGlobalState(ParserContext& ctx);
		void	handleServerState(ParserContext& ctx);
		void	handleLocationState(ParserContext& ctx);

		/* cross-block 검증: 모든 server block 파싱이 끝난 뒤 호출 */
		void	checkDuplicateServer(void) const;
		void	validateGlobalSettings(void) const; // const: _servers를 읽기만 하기 때문 (getIdleTimeout() 등 getter가 전부 const라 문제 없음)

		/* utils */
		std::string	openConfigFile(const std::string& filePath); // 내부 준비 작업이므로 private
		
	private:
		std::string					_content;
		std::vector<Token>			_tokens;
		std::vector<ServerConfig>	_servers;
};

#endif
