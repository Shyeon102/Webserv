/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Router.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/23 16:28:47 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/16 17:49:54 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Router.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: jaoh <jaoh@student.42.fr>                  +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/12/23 16:28:47 by jaoh              #+#    #+#             */
/*   Updated: 2026/02/16 17:46:01 by jaoh             ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Router.hpp"
#include <algorithm>

// Router
// - 여러 LocationConfig를 담고 있고
// - URI에 가장 잘 매칭되는(location.path가 가장 긴 prefix) location을 찾아주는 역할
Router::Router() {}

// 설정 파일 등에서 읽어온 location 설정을 내부 벡터에 추가
void Router::addLocation(const LocationConfig& loc) {
    locations.push_back(loc);
}

/*
** Prefix matching
**   예) /images/cat.png → /images
** 여러 location 중에서 uri의 prefix로 가장 길게 매칭되는 location을 선택한다.
*/
const LocationConfig* Router::match(const std::string& uri) const {
    const LocationConfig* best = NULL;
    size_t bestLen = 0;

    // 모든 location을 순회하면서 prefix 매칭 확인
    for (size_t i = 0; i < locations.size(); i++) {
        const std::string& path = locations[i].getPath();

        // uri가 path로 시작하면(prefix) 후보로 본다.
        if (uri.compare(0, path.size(), path) == 0) {
            // 가장 긴 path를 가진 location을 선택 (more specific)
            if (path.size() > bestLen) {
                best = &locations[i];
                bestLen = path.size();
            }
        }
    }
    return best;
}

/*
** 메서드 허용 규칙 (4가지 요구사항):
** 
** 1. GET은 항상 허용 (config 없어도 OK)
** 2. POST, DELETE는 config에 명시되어야만 허용
** 3. allow_methods와 methods가 둘 다 없으면 → GET만 허용
** 4. 구현은 GET, POST, DELETE 모두 되어있음 (config는 허용 여부만 결정)
*/