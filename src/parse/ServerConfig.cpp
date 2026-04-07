/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ServerConfig.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27 17:31:47 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/03/01 19:25:37 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ServerConfig.hpp"

/* 임시 기본 경로/제한값 */
static const std::string	DEFAULT_SERVER_ROOT = "./www";
static const std::string	DEFAULT_ERROR_PAGE = "/errors/404.html";
static const size_t			DEFAULT_CLIENT_MAX_BODY_SIZE = 10 * 1024 * 1024; // 10MB
static const size_t			MAX_CLIENT_BODY_SIZE = 10 * 1024 * 1024; // 정책: 최대 10MB
static const int			DEFAULT_MAX_CONNECTIONS = 1024;
static const int			DEFAULT_IDLE_TIMEOUT = 15;
static const int			DEFAULT_WRITE_TIMEOUT = 10;
static const int			DEFAULT_KEEPALIVE_MAX = 100;


/* 공통 helper func */
static void	parseListenValue(const std::string& value, std::string& ip, int& port)
{
	size_t colon = value.find(':');

	if (colon == std::string::npos)
	{
		ip = "0.0.0.0";
		if (!isNumber(value))
			throw ConfigSyntaxException("Error: listen: invalid port");
		port = std::atoi(value.c_str());
	}
	else
	{
		ip = value.substr(0, colon);
		std::string portStr = value.substr(colon + 1);

		if (ip.empty() || portStr.empty() || !isNumber(portStr))
			throw ConfigSyntaxException("Error: listen: invalid ip:port");

		port = std::atoi(portStr.c_str());
	}

	if (port <= 0 || port > 65535)
		throw ConfigSemanticException("Error: listen: port out of range");
}

ServerConfig::ServerConfig() : _root(""), _errorPage(""), _hasServerNames(false), _hasMethods(false), _clientMaxBodySize(0),
	_hasClientMaxBodySize(false), _hasIndex(false), _hasRedirect(false), _hasAllowMethods(false),
	_maxConnections(0), _hasMaxConnections(false), _idleTimeout(0), _hasIdleTimeout(false),
	_writeTimeout(0), _hasWriteTimeout(false), _keepAliveMax(0), _hasKeepAliveMax(false), _autoindex(false), _hasAutoindex(false) {}

ServerConfig::~ServerConfig() {}

void	ServerConfig::handleAutoIndex(const std::vector<Token>& tokens, size_t& i)
{
    if (this->_hasAutoindex)
        throw ConfigSemanticException("Error: duplicate autoindex directive");

    const Token	&valueToken = directiveSyntaxCheck(tokens, i, "autoindex");

    if (valueToken.value == "on")
        this->_autoindex = true;
    else if (valueToken.value == "off")
        this->_autoindex = false;
    else
        throw ConfigSyntaxException("Error: autoindex must be 'on' or 'off'");

    this->_hasAutoindex = true;
}

void	ServerConfig::handleListen(const std::vector<Token>& tokens, size_t& i)
{
	const Token& value = directiveSyntaxCheck(tokens, i, "listen");

	std::string ip;
	int port;

	parseListenValue(value.value, ip, port);
	checkDuplicateListen(ip, port);

	ListenAddress	addr;
	addr.ip = ip;
	addr.port = port;

	this->_listen.push_back(addr);
}

void	ServerConfig::handleRoot(const std::vector<Token> &tokens, size_t &i)
{
	const Token	&pathToken = directiveSyntaxCheck(tokens, i, "root");
	this->_root = pathToken.value;
}

void ServerConfig::handleErrorPage(const std::vector<Token>& tokens, size_t& i)
{
    i++; // error_page

    if (i >= tokens.size() || tokens[i].type != TOKEN_WORD || !isNumber(tokens[i].value))
        throw ConfigSyntaxException("Error: error_page requires status code");

    int status = std::atoi(tokens[i].value.c_str());
    i++;

    if (i >= tokens.size() || tokens[i].type != TOKEN_WORD)
        throw ConfigSyntaxException("Error: error_page requires path");

    std::string path = tokens[i].value;
    i++;

    if (i >= tokens.size() || tokens[i].type != TOKEN_SEMICOLON)
        throw ConfigSyntaxException("Error: missing ';' after error_page");

    i++;

    if (this->_errorPages.count(status))
        throw ConfigSemanticException("Error: duplicate error_page for status");

    this->_errorPages[status] = path;
}

void	ServerConfig::handleMethods(const std::vector<Token>& tokens, size_t& i)
{
	this->_methods.clear();
	this->_hasMethods = true;

	i++;

	while (i < tokens.size())
	{
		if (tokens[i].type == TOKEN_SEMICOLON)
		{
			i++;
			return;
		}

		if (tokens[i].type != TOKEN_WORD)
			throw ConfigSyntaxException("Error: methods: invalid token");

		const std::string& met = tokens[i].value;

		if (met != "GET" && met != "POST" && met != "DELETE")
			throw ConfigSyntaxException("Error: methods: unknown method " + met);

		this->_methods.push_back(met);
		i++;
	}

	throw ConfigSyntaxException("Error: methods: missing ';'");
}

void	ServerConfig::handleServerName(const std::vector<Token>& tokens, size_t& i)
{
	this->_serverNames.clear();
	this->_hasServerNames = true;

	i++; // server_name 패스

	bool	hasValue = false;

	while (i < tokens.size())
	{
		if (tokens[i].type == TOKEN_SEMICOLON)
		{
			if (!hasValue)
				throw ConfigSyntaxException("Error: server_name requires at least one value");
			i++;
			return;
		}
		if (tokens[i].type != TOKEN_WORD)
			throw ConfigSyntaxException("Error: server_name: invalid token");

		hasValue = true;

		const std::string&	name = tokens[i].value;

		// server_name 중복 검사
		for (size_t j = 0; j < this->_serverNames.size(); j++)
		{
			if (this->_serverNames[j] == name)
				throw ConfigSemanticException("Error: duplicate server_name: " + name);
		}

		this->_serverNames.push_back(name);
		i++;
	}
	throw ConfigSyntaxException("Error: server_name: missing ';'");
}

void	ServerConfig::handleClientMaxBodySize(const std::vector<Token>& tokens, size_t& i)
{
	if (this->_hasClientMaxBodySize)
		throw ConfigSemanticException("Error: duplicate client_max_body_size");

	const Token&	sizeValue = directiveSyntaxCheck(tokens, i, "client_max_body_size");

	if (!isNumber(sizeValue.value))
		throw ConfigSyntaxException("Error: client_max_body_size must be a number");

	long size = std::atol(sizeValue.value.c_str()); // 문자열 -> 숫자로 저장 (int는 오버플로우 위험있음(혹은 쓰레기값), long은 음수 입력을 음수 그대로 감지 가능)

	if (size <= 0)
		throw ConfigSemanticException("Error: client_max_body_size must be > 0");
	
	if (static_cast<size_t>(size) > MAX_CLIENT_BODY_SIZE)
		throw ConfigSemanticException("Error: client_max_body_size too large");

	this->_clientMaxBodySize = static_cast<size_t>(size);
	this->_hasClientMaxBodySize = true;
}

void	ServerConfig::handleIndex(const std::vector<Token>& tokens, size_t& i)
{
	this->_index.clear();
	this->_hasIndex = true;

	i++; // "index" 패스

	bool hasValue = false;

	while (i < tokens.size())
	{
		if (tokens[i].type == TOKEN_SEMICOLON)
		{
			if (!hasValue)
				throw ConfigSyntaxException("Error: index requires at least one value");
			i++;
			return;
		}

		if (tokens[i].type != TOKEN_WORD)
			throw ConfigSyntaxException("Error: index: invalid token");

		const std::string& name = tokens[i].value;

		// 중복 검사
		for (size_t j = 0; j < this->_index.size(); j++)
		{
			if (this->_index[j] == name)
				throw ConfigSemanticException("Error: duplicate index: " + name);
		}

		this->_index.push_back(name);
		hasValue = true;
		i++;
	}
	throw ConfigSyntaxException("Error: index: missing ';'");
}

/* 문법 : return <status> <target> ; */
void	ServerConfig::handleReturn(const std::vector<Token>& tokens, size_t& i)
{
	if (this->_hasRedirect)
		throw ConfigSemanticException("Error: duplicate return directive");

	i++; // "return"

	// status code
	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD || !isNumber(tokens[i].value))
		throw ConfigSyntaxException("Error: return requires status code");

	int status = std::atoi(tokens[i].value.c_str()); // status 범위 작아서 int도 괜찮음
	if (status < 300 || status > 399)
		throw ConfigSemanticException("Error: return status must be 3xx");

	i++;

	// target
	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD)
		throw ConfigSyntaxException("Error: return requires target");

	std::string target = tokens[i].value;
	i++;

	// semicolon
	if (i >= tokens.size() || tokens[i].type != TOKEN_SEMICOLON)
		throw ConfigSyntaxException("Error: missing ';' after return");

	i++;

	this->_redirect.status = status;
	this->_redirect.target = target;
	this->_hasRedirect = true;
}

void ServerConfig::handleAllowMethods(const std::vector<Token>& tokens, size_t& i)
{
	this->_allowMethods.clear();
	this->_hasAllowMethods = true;

	i++; // "allow_methods"

	bool hasValue = false;

	while (i < tokens.size())
	{
		if (tokens[i].type == TOKEN_SEMICOLON)
		{
			if (!hasValue)
				throw ConfigSyntaxException("Error: allow_methods requires at least one value");
			i++;
			return;
		}

		if (tokens[i].type != TOKEN_WORD)
			throw ConfigSyntaxException("Error: allow_methods: invalid token");

		const std::string& met = tokens[i].value;
		if (met != "GET" && met != "POST" && met != "DELETE")
			throw ConfigSyntaxException("Error: allow_methods: unknown method " + met);

		for (size_t j = 0; j < this->_allowMethods.size(); j++)
			if (this->_allowMethods[j] == met)
				throw ConfigSemanticException("Error: duplicate allow_methods: " + met);

		this->_allowMethods.push_back(met);
		hasValue = true;
		i++;
	}
	throw ConfigSyntaxException("Error: allow_methods: missing ';'");
}

static int parsePositiveIntDirective(const std::vector<Token>& tokens, size_t& i,
	const std::string& name, int minVal, int maxVal)
{
	const Token& valueToken = directiveSyntaxCheck(tokens, i, name);
	if (!isNumber(valueToken.value))
		throw ConfigSyntaxException("Error: " + name + " must be a number");

	long value = std::atol(valueToken.value.c_str());
	if (value < minVal || value > maxVal)
		throw ConfigSemanticException("Error: " + name + " out of range");
	return static_cast<int>(value);
}

void ServerConfig::handleMaxConnections(const std::vector<Token>& tokens, size_t& i)
{
	if (_hasMaxConnections)
		throw ConfigSemanticException("Error: duplicate max_connections");
	_maxConnections = parsePositiveIntDirective(tokens, i, "max_connections", 1, 1000000);
	_hasMaxConnections = true;
}

void ServerConfig::handleIdleTimeout(const std::vector<Token>& tokens, size_t& i)
{
	if (_hasIdleTimeout)
		throw ConfigSemanticException("Error: duplicate idle_timeout");
	_idleTimeout = parsePositiveIntDirective(tokens, i, "idle_timeout", 1, 86400);
	_hasIdleTimeout = true;
}

void ServerConfig::handleWriteTimeout(const std::vector<Token>& tokens, size_t& i)
{
	if (_hasWriteTimeout)
		throw ConfigSemanticException("Error: duplicate write_timeout");
	_writeTimeout = parsePositiveIntDirective(tokens, i, "write_timeout", 1, 86400);
	_hasWriteTimeout = true;
}

void ServerConfig::handleKeepAliveMax(const std::vector<Token>& tokens, size_t& i)
{
	if (_hasKeepAliveMax)
		throw ConfigSemanticException("Error: duplicate keepalive_max");
	_keepAliveMax = parsePositiveIntDirective(tokens, i, "keepalive_max", 1, 1000000);
	_hasKeepAliveMax = true;
}

void	ServerConfig::parseDirective(const std::vector<Token> &tokens, size_t &i)
{
	const std::string	&field = tokens[i].value;

	if (field == "listen")
		handleListen(tokens, i);
	else if (field == "root")
		handleRoot(tokens, i);
	else if (field == "error_page")
		handleErrorPage(tokens, i);
	else if (field == "server_name")
		handleServerName(tokens, i);
	else if (field == "client_max_body_size")
		handleClientMaxBodySize(tokens, i);
	else if (field == "index")
		handleIndex(tokens, i);
	else if (field == "return")
		handleReturn(tokens, i);
	else if (field == "methods")
		handleMethods(tokens, i);
	else if (field == "allow_methods")
		handleAllowMethods(tokens, i);
	else if (field == "max_connections")
		handleMaxConnections(tokens, i);
	else if (field == "idle_timeout")
		handleIdleTimeout(tokens, i);
	else if (field == "write_timeout")
		handleWriteTimeout(tokens, i);
	else if (field == "keepalive_max")
		handleKeepAliveMax(tokens, i);
	else if (field == "autoindex")
    	handleAutoIndex(tokens, i);
	else
		throw ConfigSyntaxException("Error: unknown server directive: " + field);
}

/* 파싱 끝난 location 블록을 server 내부에 넣기 */
void	ServerConfig::addLocation(const LocationConfig &location)
{
	this->_locations.push_back(location);
}

void	ServerConfig::checkDuplicateListen(const std::string& ip, int port) const
{
	for (size_t i = 0; i < _listen.size(); i++)
	{
		if (this->_listen[i].ip == ip && this->_listen[i].port == port)
			throw ConfigSemanticException("Error: duplicate listen directive" );
	}
}

void	ServerConfig::validateListenDirective() const
{
	if (this->_listen.empty())
		throw ConfigSemanticException("Error: Server block must contain at least 1 listen directive");
}

void	ServerConfig::applyDefaultRoot()
{
	if (this->_root.empty())
		this->_root = DEFAULT_SERVER_ROOT;
}

void	ServerConfig::applyDefaultErrorPage()
{
	if (this->_errorPage.empty())
		this->_errorPage = DEFAULT_ERROR_PAGE;
}

void	ServerConfig::duplicateLocationPathCheck() const
{
	for (size_t i = 0; i < this->_locations.size(); ++i)
	{
		for (size_t j = (i + 1); j < this->_locations.size(); ++j)
		{
			if (this->_locations[i].getPath() == this->_locations[j].getPath())
				throw ConfigSemanticException("Error: Duplicate location path: " + this->_locations[i].getPath() + "\n" + this->_locations[j].getPath());
		}
	}
}


/* Getters */
std::vector<int> ServerConfig::getListenPorts() const
{
	std::vector<int> ports;
	ports.reserve(_listen.size());
	for (size_t i = 0; i < _listen.size(); i++)
		ports.push_back(_listen[i].port);
	return ports;
}

const std::string& ServerConfig::getRoot() const
{
	return this->_root;
}

const std::string& ServerConfig::getErrorPage() const
{
	return this->_errorPage;
}

const std::vector<LocationConfig>& ServerConfig::getLocations() const
{
	return this->_locations;
}

/* validateServerBlock(): 필수인데 빠지면 서버가 동작 불가능한 것들 조건 검사
	(server_name은 필수가 아니라서(server_name은 기본값 없음) 넣지 않음: parseDirective에서 field로 있으면 넣기) 
	필수 검사만 수행하는 함수 */
void	ServerConfig::validateServerBlock()
{
	validateListenDirective(); // 1) listen 필수 검사
	applyDefaultRoot(); // 2) root 기본값
	applyDefaultErrorPage(); // 3) error_page 기본값
	if (!this->_hasClientMaxBodySize) // 필수는 아니지만, 런타임에서 반드시 필요
		this->_clientMaxBodySize = DEFAULT_CLIENT_MAX_BODY_SIZE;
	if (!this->_hasMaxConnections)
		this->_maxConnections = DEFAULT_MAX_CONNECTIONS;
	if (!this->_hasIdleTimeout)
		this->_idleTimeout = DEFAULT_IDLE_TIMEOUT;
	if (!this->_hasWriteTimeout)
		this->_writeTimeout = DEFAULT_WRITE_TIMEOUT;
	if (!this->_hasKeepAliveMax)
		this->_keepAliveMax = DEFAULT_KEEPALIVE_MAX;
	for (size_t i = 0; i < this->_locations.size(); ++i)
	{
		this->_locations[i].inheritRootIfUnset(this->_root);
		if (this->_hasIndex)
			this->_locations[i].inheritIndexIfUnset(this->_index);
		if (this->_hasAutoindex)
			this->_locations[i].inheritAutoindexIfUnset(this->_autoindex);
	}
	duplicateLocationPathCheck(); // 4) location path 중복 검사
}

/* getter */
bool	ServerConfig::hasMethods(void) const { return this->_hasMethods; }

const std::vector<std::string>&	ServerConfig::getMethods(void) const { return this->_methods; }

bool	ServerConfig::hasServerNames(void) const { return this->_hasServerNames; }

const std::vector<std::string>&	ServerConfig::getServerNames(void) const { return this->_serverNames; }

bool	ServerConfig::hasClientMaxBodySize(void) const { return this->_hasClientMaxBodySize; }

size_t	ServerConfig::getClientMaxBodySize(void) const { return this->_clientMaxBodySize; }

bool	ServerConfig::hasRedirect(void) const { return this->_hasRedirect; }

const Redirect&	ServerConfig::getRedirect(void) const { return this->_redirect; }

bool	ServerConfig::hasAllowMethods(void) const { return this->_hasAllowMethods; }

const std::vector<std::string>&	ServerConfig::getAllowMethods(void) const { return this->_allowMethods; }

bool	ServerConfig::hasMaxConnections(void) const { return this->_hasMaxConnections; }

int		ServerConfig::getMaxConnections(void) const { return this->_maxConnections; }

bool	ServerConfig::hasIdleTimeout(void) const { return this->_hasIdleTimeout; }

int		ServerConfig::getIdleTimeout(void) const { return this->_idleTimeout; }

bool	ServerConfig::hasWriteTimeout(void) const { return this->_hasWriteTimeout; }

int		ServerConfig::getWriteTimeout(void) const { return this->_writeTimeout; }

bool	ServerConfig::hasKeepAliveMax(void) const { return this->_hasKeepAliveMax; }

int		ServerConfig::getKeepAliveMax(void) const { return this->_keepAliveMax; }

bool	ServerConfig::hasAutoindex(void) const { return this->_hasAutoindex; }

bool	ServerConfig::getAutoindex(void) const { return this->_autoindex; }
