#include "ConfigUtils.hpp"
#include "ConfigException.hpp" // 예외 타입은 함수 시그니처에 없음 (헤더: 불필요, .cpp: 필요)
#include <vector> // size()
#include <cctype> // isdigit()

bool	isNumber(const std::string &s)
{
	if (s.empty())
		return false;
	
	for (size_t i = 0; i < s.size(); i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
	}

	return true;
}

Token	directiveSyntaxCheck(const std::vector<Token>& tokens, size_t& i, const std::string& directiveName)
{
	if ((i + 1) >= tokens.size())
		throw ConfigSyntaxException("Error: " + directiveName + " requires an argument");
	
	const Token	&tokenValue = tokens[i + 1]; // token values
	
	if (tokenValue.type != TOKEN_WORD)
		throw ConfigSyntaxException("Error: Invalid argument for " + directiveName);
	
	i += 2;

	if (tokens[i].type != TOKEN_SEMICOLON)
		throw ConfigSyntaxException("Error: missing ';' after " + directiveName);
	
	i++;

	return tokenValue;
}