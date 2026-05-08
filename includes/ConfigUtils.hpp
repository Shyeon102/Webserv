#ifndef CONFIGUTILS_HPP
#define CONFIGUTILS_HPP

#include "Token.hpp"
#include <vector>

bool			isNumber(const std::string &s);
Token			directiveSyntaxCheck(const std::vector<Token>& tokens, size_t& i, const std::string& directiveName);

#endif