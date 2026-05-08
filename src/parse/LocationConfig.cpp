/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   LocationConfig.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27 17:31:37 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/02/10 18:19:08 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "LocationConfig.hpp"

LocationConfig::LocationConfig(const std::string &path) : _path(path), _root(""), _rootSet(false), _autoindex(false), _autoindexSet(false),
	_hasMethods(false), _hasIndex(false), _hasRedirect(false), _hasAllowMethods(false), _uploadStore(""), _hasUploadStore(false), _hasCgiPass(false) {}

LocationConfig::~LocationConfig() {}

void	LocationConfig::handleRoot(const std::vector<Token> &tokens, size_t &i)
{
	const Token	&valueToken = directiveSyntaxCheck(tokens, i, "root");
	this->_root = valueToken.value;
	this->_rootSet = true;
}

void	LocationConfig::handleAutoindex(const std::vector<Token> &tokens, size_t &i)
{
	const Token	&valueToken = directiveSyntaxCheck(tokens, i, "autoindex");
	
	if (valueToken.value == "on")
		this->_autoindex = true;
	else if (valueToken.value == "off")
		this->_autoindex = false;
	else
		throw ConfigSyntaxException("Error: autoindex must be 'on' or 'off'");
	
	this->_autoindexSet = true;
}


void	LocationConfig::handleMethods(const std::vector<Token>& tokens, size_t& i)
{
	this->_methods.clear(); // 초기화
	this->_hasMethods = true;

	i++; // "methods" (str) 토큰 패스. 다음 토큰 (ex) GET POST)

	while (i < tokens.size())
	{
		if (tokens[i].type == TOKEN_SEMICOLON)
		{
			i++; // ex) methods GET POST ";"
			return;
		}

		if (tokens[i].type != TOKEN_WORD) // methods 뒤에 반드시 단어가 와야함 (GET, POST, DELETE). ex) 중괄호 {} 있으면 문법 에러
			throw ConfigSyntaxException("Error: methods: invalid token");

		const std::string&	met = tokens[i].value;

		if (met != "GET" && met != "POST" && met != "DELETE") // 이 중에 없는 글자(다른 단어) 에러
			throw ConfigSyntaxException("Error: methods: unknown method " + met);

		this->_methods.push_back(met);
		i++;
	}

	throw ConfigSyntaxException("Error: methods: missing ';'");
}

void	LocationConfig::handleIndex(const std::vector<Token>& tokens, size_t& i)
{
	this->_index.clear();
	this->_hasIndex = true;

	i++;

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

		const std::string&	name = tokens[i].value;

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

void	LocationConfig::handleReturn(const std::vector<Token>& tokens, size_t& i)
{
	if (this->_hasRedirect)
		throw ConfigSemanticException("Error: duplicate return directive");

	i++; // "return"

	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD || !isNumber(tokens[i].value))
		throw ConfigSyntaxException("Error: return requires status code");

	int	status = std::atoi(tokens[i].value.c_str());
	if (status < 300 || status > 399)
		throw ConfigSemanticException("Error: return status must be 3xx");

	i++;

	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD)
		throw ConfigSyntaxException("Error: return requires target");

	std::string target = tokens[i].value;
	i++;

	if (i >= tokens.size() || tokens[i].type != TOKEN_SEMICOLON)
		throw ConfigSyntaxException("Error: missing ';' after return");

	i++;

	this->_redirect.status = status;
	this->_redirect.target = target;
	this->_hasRedirect = true;
}

void LocationConfig::handleAllowMethods(const std::vector<Token>& tokens, size_t& i)
{
	this->_allowMethods.clear();
	this->_hasAllowMethods = true;

	i++; // "allow_methods"

	bool	hasValue = false;

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

/* 문법: upload_store /var/www/uploads; */
void	LocationConfig::handleUploadStore(const std::vector<Token>& tokens, size_t& i)
{
	if (this->_hasUploadStore)
		throw ConfigSemanticException("Error: duplicate upload_store directive");

	const Token&	uploadValue = directiveSyntaxCheck(tokens, i, "upload_store");

	if (uploadValue.value.empty())
		throw ConfigSemanticException("Error: upload_store path is empty");

	this->_uploadStore = uploadValue.value;
	this->_hasUploadStore = true;
}

/* 문법: cgi_pass .php /usr/bin/php-cgi;
		cgi_pass .py  /usr/bin/python3;
		
	key: .php | value: /usr/bin/php-cgi
*/
void	LocationConfig::handleCgiPass(const std::vector<Token>& tokens, size_t& i)
{
	i++; // "cgi_pass"

	// extension
	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD)
		throw ConfigSyntaxException("Error: cgi_pass requires extension");

	std::string	ext = tokens[i].value;
	if (ext.empty() || ext[0] != '.')
		throw ConfigSemanticException("Error: cgi_pass extension must start with '.'");

	i++;

	// executable
	if (i >= tokens.size() || tokens[i].type != TOKEN_WORD)
		throw ConfigSyntaxException("Error: cgi_pass requires executable path");

	std::string exec = tokens[i].value;
	i++;

	// semicolon
	if (i >= tokens.size() || tokens[i].type != TOKEN_SEMICOLON)
		throw ConfigSyntaxException("Error: missing ';' after cgi_pass");

	i++;

	// duplicate extension check
	if (this->_cgiPass.count(ext))
		throw ConfigSemanticException("Error: duplicate cgi_pass for extension: " + ext);

	this->_cgiPass[ext] = exec;
	this->_hasCgiPass = true;
}

void	LocationConfig::parseDirective(const std::vector<Token> &tokens, size_t &i)
{
	const std::string	&field = tokens[i].value;

	if (field == "root")
		handleRoot(tokens, i);
	else if (field == "autoindex")
		handleAutoindex(tokens, i);
	else if (field == "index")
		handleIndex(tokens, i);
	else if (field == "return")
		handleReturn(tokens, i);
	else if (field == "methods")
		handleMethods(tokens, i);
	else if (field == "allow_methods")
		handleAllowMethods(tokens, i);
	else if (field == "upload_store")
		handleUploadStore(tokens, i);
	else if (field == "cgi_pass")
		handleCgiPass(tokens, i);
	else
		throw ConfigSemanticException("Error: Unknown location directive: " + field);
}

void	LocationConfig::validatePath() const
{
	// path 유효성 검사
	if (this->_path.empty())
		throw ConfigSemanticException("Error: Location path is empty");

	if (this->_path[0] != '/') // ex) location '/'upload
		throw ConfigSemanticException("Error: Location path must start whit '/'");
}

/* 필수 검사만 수행하는 함수 */
void	LocationConfig::validateLocationBlock()
{
	validatePath();
}

/* getters */
const std::string&	LocationConfig::getPath(void) const { return this->_path; }

bool	LocationConfig::hasRoot(void) const { return this->_rootSet; }

const std::string&	LocationConfig::getRoot(void) const { return this->_root; }

bool	LocationConfig::hasAutoindex(void) const { return this->_autoindexSet; }

bool	LocationConfig::getAutoindex(void) const { return this->_autoindex; }

bool	LocationConfig::hasMethods(void) const { return this->_hasMethods; }

const std::vector<std::string>&	LocationConfig::getMethods(void) const { return this->_methods; }

bool	LocationConfig::hasIndex(void) const { return this->_hasIndex; }

const std::vector<std::string>&	LocationConfig::getIndex(void) const { return this->_index; }

bool	LocationConfig::hasRedirect(void) const { return this->_hasRedirect; }

const Redirect&	LocationConfig::getRedirect(void) const { return this->_redirect; }

bool	LocationConfig::hasAllowMethods(void) const { return this->_hasAllowMethods; }

const std::vector<std::string>& LocationConfig::getAllowMethods(void) const { return this->_allowMethods; }

bool	LocationConfig::hasUploadStore(void) const { return this->_hasUploadStore; } // true or false

const std::string&	LocationConfig::getUploadStore(void) const { return this->_uploadStore; } // "/tmp/uploads"

bool	LocationConfig::hasCgiPass(void) const { return this->_hasCgiPass; }

const std::map<std::string, std::string>&	LocationConfig::getCgiPass(void) const { return this->_cgiPass; }

void	LocationConfig::inheritRootIfUnset(const std::string& serverRoot)
{
	if (!this->_rootSet)
	{
		this->_root = serverRoot;
		this->_rootSet = true;
	}
}

void	LocationConfig::inheritIndexIfUnset(const std::vector<std::string>& serverIndex)
{
	if (!this->_hasIndex)
	{
		this->_index = serverIndex;
		this->_hasIndex = true;
	}
}

void	LocationConfig::inheritAutoindexIfUnset(bool serverAutoindex)
{
	if (!this->_autoindexSet)
	{
		this->_autoindex = serverAutoindex;
		this->_autoindexSet = true;
	}
}
