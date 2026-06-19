/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/25 16:03:47 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/06/19 02:33:50 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"
#include "LocationConfig.hpp"

Config::Config(const std::string &filePath)
{
	this->_content = openConfigFile(filePath);
	ConfigTokenizer	tokenizer;
	this->_tokens = tokenizer.tokenize(this->_content);
}

Config::~Config() {}

void	Config::configParse()
{
	ParserContext ctx;
	this->_servers.clear();

	try
	{
		while (ctx.i < this->_tokens.size())
		{
			if (ctx.state == STATE_GLOBAL)
				handleGlobalState(ctx);
			else if (ctx.state == STATE_SERVER)
				handleServerState(ctx);
			else if (ctx.state == STATE_LOCATION)
				handleLocationState(ctx);
		}
	}
	catch (...) // 어떤 타입의 예외든 다 받겠다, cleanup용(자원 정리) : currentLocation은 포인터변수 -> location은 있을수도 있고, 없을 수도 있음(생성자 만들지 않음, 그래서 pointer 변수)
	{
		if (ctx.currentLocation)
		{
			delete ctx.currentLocation;
			ctx.currentLocation = 0;
		}
		throw ;
	}
	
	checkDuplicateServer();   // 블록 간 listen 주소 중복(가상호스트 아닌 진짜 중복) 검사
	validateGlobalSettings(); // 모든 server block 파싱,기본값 적용 끝난 뒤 전역 정책 일치 검사
	// try/catch는 파싱 중 currentLocation(힙 포인터) 정리용. 루프가 정상 종료된 시점엔 정리할 게 없음 그래서 catch 밖에 두기
}

/* state handlers */
void	Config::handleGlobalState(ParserContext& ctx)
{
	const Token	&token = this->_tokens[ctx.i];

	if (token.type == TOKEN_WORD && token.value == "server")
	{
		ctx.currentServer = ServerConfig(); // 새 서버 시작 (생성)
		ctx.serverOpened = false;
		ctx.expectingServerBrace = true;
		ctx.state = STATE_SERVER;
		ctx.i++;
		return ;
	}

	if (token.type == TOKEN_EOF)
	{
		if (ctx.serverOpened)
			throw ConfigSyntaxException("Error: unclosed server block at end of file");
		ctx.i++; // EOF consumed: stop parse loop
		return ;
	}

	throw ConfigSyntaxException("Error: unexpected token outside server block");
}

void	Config::handleServerState(ParserContext& ctx)
{
	const Token	&token = this->_tokens[ctx.i];

	if (token.type == TOKEN_LBRACE) // '{'
	{
		if (!ctx.expectingServerBrace)
			throw ConfigSyntaxException("Error: unexpected '{' in server block");

		ctx.serverOpened = true;
		ctx.expectingServerBrace = false; // server 이후 '{'를 만났으니 기다리지 않아도 된다
		ctx.i++;
		return ;
	}

	if (token.type == TOKEN_RBRACE) // '}'
	{
		if (!ctx.serverOpened)
			throw ConfigSyntaxException("Error: '}' without matching '{' in server block");

		if (ctx.locationOpened)
			throw ConfigSyntaxException("Error: unclosed location block before server end");

		// server semantic 유효성 검사: 예외가 아니면 다음에 server 저장으로 간다.
		ctx.currentServer.validateServerBlock();
		this->_servers.push_back(ctx.currentServer); // 현재 server 저장

		ctx.serverOpened = false;
		ctx.state = STATE_GLOBAL; // 인스턴스 서버 1개 끝 (중첩구조)
		ctx.i++;
		return ;
	}

	if (token.type == TOKEN_WORD && token.value == "location")
	{
		if (!ctx.serverOpened)
			throw ConfigSyntaxException("Error: location outside server block");

		// 다음 token이 없거나 타입이 다른지 미리 syntax 검사
		if ((ctx.i + 1) >= this->_tokens.size() || this->_tokens[ctx.i + 1].type != TOKEN_WORD)
			throw ConfigSyntaxException("Error: location requires a path");

		ctx.currentLocation = new LocationConfig(this->_tokens[ctx.i + 1].value); // 다음 토큰이 path여야함: ex) /upload
		ctx.expectingLocationBrace = true; // loation 이후에 '{'가 와야해서 기다려야 하므로 true
		ctx.locationOpened = false;
		ctx.state = STATE_LOCATION;
		ctx.i += 2; // location + path
		return ;
	}

	if (token.type == TOKEN_WORD)
	{
		if (!ctx.serverOpened)
			throw ConfigSyntaxException("Error: missing '{' after server");

		ctx.currentServer.parseDirective(this->_tokens, ctx.i);
		return ;
	}

	throw ConfigSyntaxException("Error: invalid token in SERVER scope");
}

void	Config::handleLocationState(ParserContext& ctx)
{
	const Token	&token = this->_tokens[ctx.i];

	if (!ctx.currentLocation) // 안전 검사
		throw ConfigSyntaxException("Error: location state without object");

	if (token.type == TOKEN_LBRACE)
	{
		if (!ctx.expectingLocationBrace)
			throw ConfigSyntaxException("Error: unexpected '{' in location block");

		ctx.locationOpened = true;
		ctx.expectingLocationBrace = false; // location 이후 '{' 를 만났으니 기다리지 않아도 되어서 false
		ctx.i++;
		return ;
	}

	if (token.type == TOKEN_RBRACE)
	{
		if (!ctx.locationOpened)
			throw ConfigSyntaxException("Error: '}' without matching '{' in location block");

		ctx.currentLocation->validateLocationBlock(); // location semantic 유효성 검사
		ctx.currentServer.addLocation(*ctx.currentLocation);

		delete ctx.currentLocation;
		ctx.currentLocation = 0; // dangling pointer 방지(초기화)

		ctx.locationOpened = false;
		ctx.state = STATE_SERVER; // location 위에 server
		ctx.i++;
		return ;
	}

	if (token.type == TOKEN_WORD)
	{
		if (!ctx.locationOpened)
			throw ConfigSyntaxException("Error: missing '{' after location path");

		ctx.currentLocation->parseDirective(this->_tokens, ctx.i);
		return ;
	}

	throw ConfigSyntaxException("Error: invalid token in LOCATION scope");
}

/* 외부에서 _servers 접근 위한 함수 (나중에 Server쪽에서 사용) */
const std::vector<ServerConfig>&	Config::getServers(void) const { return this->_servers; }

/*	openConfigFile()은 파일 열기(열수 있는지 검사), 파일 내부의 버퍼를 내보내는 함수로만 진행 (내부 검사 x)
	중첩구조 파싱을 더 편하게 하기 위해 파일 내부에 문자열 버퍼를 다 꺼내서 문자배열(일자형태)로 리턴이 목표 */
std::string	Config::openConfigFile(const std::string &filePath)
{
	std::ifstream	configFile(filePath.c_str());

	if (!configFile.is_open())
		throw ConfigFileException("Error: Config file open failed: " + filePath); // main에서 try,catch문으로 예외처리 예정
	
	std::stringstream	buffer;
	buffer << configFile.rdbuf(); // stream의 내부 버퍼를 꺼내는 함수: configFile 버퍼 전체 복사후 buffer에 넣기
	configFile.close(); // ifstream은 scope 종료시 자동 close (but 명시적으로 닫는 습관 만들기)
	
	return buffer.str();
}

/*	validateGlobalSettings(): idle_timeout, write_timeout, keepalive_max,
	max_connections는 커넥션 런타임 정책이라 프로세스 전역으로 하나만 적용됨
	실제 런타임은 _configs[0](첫 블록)만 사용하므로, 블록마다 값이 다르면
	conf에 적힌 값과 실제 동작이 어긋난다(silent mismatch). 시작 시점에 막는다. */
void	Config::validateGlobalSettings(void) const
{
	if (this->_servers.size() < 2) // 0개거나 1개면 비교 대상 없음 (_servers[0] 접근 방지)
		return ;

	const ServerConfig&	first = this->_servers[0];
	for (size_t i = 1; i < this->_servers.size(); ++i)
	{
		if (this->_servers[i].getIdleTimeout() != first.getIdleTimeout())
			throw ConfigSemanticException("Error: idle_timeout must be identical across all server blocks");
		if (this->_servers[i].getWriteTimeout() != first.getWriteTimeout())
			throw ConfigSemanticException("Error: write_timeout must be identical across all server blocks");
		if (this->_servers[i].getKeepAliveMax() != first.getKeepAliveMax())
			throw ConfigSemanticException("Error: keepalive_max must be identical across all server blocks");
		if (this->_servers[i].getMaxConnections() != first.getMaxConnections())
			throw ConfigSemanticException("Error: max_connections must be identical across all server blocks");
	}
}

/* 두 server 블록이 같은 ip:port를 하나라도 공유하는지 */
static bool	sharesListenAddress(const ServerConfig& a, const ServerConfig& b)
{
	const std::vector<ServerConfig::ListenAddress>& la = a.getListenAddresses();
	const std::vector<ServerConfig::ListenAddress>& lb = b.getListenAddresses();
	for (size_t i = 0; i < la.size(); ++i)
		for (size_t j = 0; j < lb.size(); ++j)
			if (la[i].ip == lb[j].ip && la[i].port == lb[j].port) // ip:port 쌍 전체 비교
				return true;
	return false;
}

/* 두 server 블록의 server_name이 하나라도 겹치는지 */
static bool	serverNamesOverlap(const ServerConfig& a, const ServerConfig& b)
{
	if (!a.hasServerNames() || !b.hasServerNames())
		return false;
	const std::vector<std::string>& na = a.getServerNames();
	const std::vector<std::string>& nb = b.getServerNames();
	for (size_t i = 0; i < na.size(); ++i)
		for (size_t j = 0; j < nb.size(); ++j)
			if (na[i] == nb[j])
				return true;
	return false;
}

/*	checkDuplicateServer(): 서로 다른 server 블록이 같은 ip:port를 공유할 때,
	server_name으로 구분이 안 되면 모호한 중복이라 거부한다.
	- 같은 ip:port + server_name 다름        -> 가상호스트(정상)
	- 같은 ip:port + 둘 다 server_name 없음  -> 거부
	- 같은 ip:port + server_name 겹침         -> 거부
	- 다른 ip:port (포트만 같고 ip 다른 것 포함) -> 무관(정상) */
void	Config::checkDuplicateServer(void) const
{
	for (size_t i = 0; i < this->_servers.size(); ++i)
	{
		for (size_t j = i + 1; j < this->_servers.size(); ++j)
		{
			if (!sharesListenAddress(this->_servers[i], this->_servers[j]))
				continue ;

			/*if (!this->_servers[i].hasServerNames() && !this->_servers[j].hasServerNames())
				throw ConfigSemanticException("Error: duplicate server: same listen address without server_name");*/

			if (serverNamesOverlap(this->_servers[i], this->_servers[j]))
				throw ConfigSemanticException("Error: duplicate server: same listen address and server_name");
		}
	}
}