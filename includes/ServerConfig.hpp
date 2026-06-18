/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ServerConfig.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/27 17:31:12 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/06/17 20:43:16 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include "Token.hpp" // <string>은 Token.hpp에 있음
#include "LocationConfig.hpp"
#include "ConfigException.hpp"
#include "ConfigTypes.hpp"
#include "ConfigUtils.hpp"
#include <vector>
#include <cstdlib> // std::atol

class	ServerConfig
{
	public:
		ServerConfig(void);
		~ServerConfig(void);

		/* listen = server / location 전용 flag (serverConfig 전용) */
		struct	ListenAddress
		{
			std::string	ip;
			int			port;
		};

    	/* getter */
		std::vector<int> 					getListenPorts(void) const;
		const std::vector<ListenAddress>&	getListenAddresses(void) const; // ip+port 쌍 전체 노출
		const std::vector<LocationConfig>&	getLocations(void) const;
		const std::string&					getRoot(void) const;
		const std::string&					getErrorPage(void) const;
		const std::map<int, std::string>&	getErrorPages(void) const;
		bool								hasServerNames(void) const;
		const std::vector<std::string>&		getServerNames(void) const;
		bool								hasMethods(void) const;
		const std::vector<std::string>&		getMethods(void) const;
	
		/* getter */
		bool							hasClientMaxBodySize(void) const;
		size_t							getClientMaxBodySize(void) const;
		bool							hasIndex(void) const;
		const std::vector<std::string>&	getIndex(void) const;
		bool							hasRedirect(void) const;
		const Redirect&					getRedirect(void) const;
		bool 							hasAllowMethods(void) const;
		const std::vector<std::string>&	getAllowMethods(void) const;
		bool							hasMaxConnections(void) const;
		int								getMaxConnections(void) const;
		bool							hasIdleTimeout(void) const;
		int								getIdleTimeout(void) const;
		bool							hasWriteTimeout(void) const;
		int								getWriteTimeout(void) const;
		bool							hasKeepAliveMax(void) const;
		int								getKeepAliveMax(void) const;
		bool							hasAutoindex(void) const;
		bool							getAutoindex(void) const;



		void							parseDirective(const std::vector<Token> &tokens, size_t &i);
		void							addLocation(const LocationConfig &location); // location은 server 내부에 종속: ServerConfig가 관리 및 내부에서 통제 가능(캡슐화)
		void							validateServerBlock(void); // server block 전체 보고 판단: 의미적으로 완성되었는가, 기본값을 채워야 하는가

	
	private:
		/* semantic handlersfuncs */
		void	handleListen(const std::vector<Token> &tokens, size_t &i);
		void	handleRoot(const std::vector<Token> &tokens, size_t &i);
		void	handleErrorPage(const std::vector<Token> &tokens, size_t &i);
		void	handleServerName(const std::vector<Token>& tokens, size_t& i);
		void	handleMethods(const std::vector<Token>& tokens, size_t& i);
		void	handleClientMaxBodySize(const std::vector<Token>& tokens, size_t& i);
		void	handleIndex(const std::vector<Token>& tokens, size_t& i);
		void	handleReturn(const std::vector<Token>& tokens, size_t& i);
		void	handleAllowMethods(const std::vector<Token>& tokens, size_t& i);
		void	handleMaxConnections(const std::vector<Token>& tokens, size_t& i);
		void	handleIdleTimeout(const std::vector<Token>& tokens, size_t& i);
		void	handleWriteTimeout(const std::vector<Token>& tokens, size_t& i);
		void	handleKeepAliveMax(const std::vector<Token>& tokens, size_t& i);
		void	handleAutoIndex(const std::vector<Token>& tokens, size_t& i);
		
		void	duplicateLocationPathCheck(void) const;
		void	applyDefaultErrorPage(void);
		void	applyDefaultRoot(void);
		void	validateListenDirective(void) const;
		void	checkDuplicateListen(const std::string& ip, int port) const;
	
		std::vector<ListenAddress>	_listen;
		std::string					_root;
		std::string					_errorPage; // error page path
		std::vector<LocationConfig>	_locations; // location 중첩 구조: location은 항상 server에 속함, server가 vector로 관리

		/* virtual host */
		std::vector<std::string>	_serverNames;
		bool						_hasServerNames;

		/* methods */
		std::vector<std::string>	_methods;
		bool						_hasMethods;
		
		/* server 그외 필드 */
		size_t						_clientMaxBodySize;
		bool						_hasClientMaxBodySize;
		std::vector<std::string>	_index;
		bool						_hasIndex;
		Redirect					_redirect;
		bool						_hasRedirect;

		/* allow_methods (methods와 완전히 분리된 필드) */
		std::vector<std::string>	_allowMethods;
		bool						_hasAllowMethods;

		int							_maxConnections;
		bool						_hasMaxConnections;
		int							_idleTimeout;
		bool						_hasIdleTimeout;
		int							_writeTimeout;
		bool						_hasWriteTimeout;
		int							_keepAliveMax;
		bool						_hasKeepAliveMax;

		bool						_autoindex;
		bool						_hasAutoindex;
		std::map<int, std::string>	_errorPages;
};

#endif
