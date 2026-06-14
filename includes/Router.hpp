/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Router.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/23 16:28:35 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/16 11:37:35 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "LocationConfig.hpp"
#include <string>
#include <vector>

// Router
// - 여러 LocationConfig를 관리하고
// - 들어온 요청의 URI에 대해 어떤 location을 사용할지 결정하는 책임을 가진다.
class Router {
public:
    Router();

    void addLocation(const LocationConfig& loc);

    // URI에 가장 잘 매칭되는 location 반환 (가장 긴 prefix)
    const LocationConfig* match(const std::string& uri) const;
    
    // 해당 location에서 HTTP method가 허용되는지 확인
    // 우선순위: allow_methods > methods (legacy fallback)
    // 허용 리스트에 명시된 method만 허용
private:
    std::vector<LocationConfig> locations;
};

#endif
