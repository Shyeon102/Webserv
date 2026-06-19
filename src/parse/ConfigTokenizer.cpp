/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigTokenizer.cpp                                :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/01/26 22:35:52 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/06/19 02:30:07 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ConfigTokenizer.hpp"

std::vector<Token>	ConfigTokenizer::tokenize(const std::string &content)
{
	std::vector<Token>	tokens;
	size_t	i = 0;

	while (i < content.size())
	{
		// 1) 공백 무시
		if (std::isspace(static_cast<unsigned char>(content[i])))
		{
			i++;
			continue ;
		}
		// 2) 주석처리: '#'부터 줄 끝(\n)까지 무시 (어디서 시작하든)
		else if (content[i] == '#')
		{
			while (i < content.size() && content[i] != '\n')
				i++;
			continue ;
		}
		// 3) 기호 토큰
		else if (content[i] == '{')
		{
			tokens.push_back(Token()); // tokens은 Token 타입 vector이기 때문에 Token을 넣어줘야함
			tokens.back().type = TOKEN_LBRACE; // 마지막에 넣은 토큰의 타입을 명시 (윗줄 토큰)
			tokens.back().value = "{";
			i++;
		}
		else if (content[i] == '}')
		{
			tokens.push_back(Token());
			tokens.back().type = TOKEN_RBRACE;
			tokens.back().value = "}";
			i++;
		}
		else if (content[i] == ';')
		{
			tokens.push_back(Token());
			tokens.back().type = TOKEN_SEMICOLON;
			tokens.back().value = ";";
			i++;
		}
		// 4) WORD 토큰 ('#'단어도 경계: 단어 중간 주석도 처리)
		else
		{
			size_t	start = i;
			
			while (i < content.size() && !std::isspace(static_cast<unsigned char>(content[i]))
				&& content[i] != '{' && content[i] != '}'
				&& content[i] != ';' && content[i] != '#')
				i++;
			
			tokens.push_back(Token());
			tokens.back().type = TOKEN_WORD;
			tokens.back().value = content.substr(start, (i - start));
		}
	}
	// 5) EOF
	tokens.push_back(Token());
	tokens.back().type = TOKEN_EOF;
	tokens.back().value = "";

	return tokens;
}
