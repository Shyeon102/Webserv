/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequestValidator.cpp                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: princessj <princessj@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/02/16 22:33:10 by jihyeki2          #+#    #+#             */
/*   Updated: 2026/02/17 02:35:04 by princessj        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpRequestValidator.hpp"

/*	Content-Length: 전체 길이 미리 알려줌
	Transfer-Encoding: chunked: 조각 단위로 길이 알려줌 
	둘다 있으면 서버가 혼란 -> 보통 400 처리 */
HttpParseResult HttpRequestValidator::validateTransferEncodingConflict(const std::map<std::string, std::string>& headers)
{
	// map을 읽기만 할거라서 const_iterator
    std::map<std::string, std::string>::const_iterator	itTE = headers.find("transfer-encoding");

	// Transfer-Encoding이 없으면 문제 없음
	// Content-Length만 있는 경우일 수도 있고 body가 아예 없는 GET 요청일 수도 있음
    if (itTE == headers.end())
        return HttpParseResult(HttpParseResult::PARSE_COMPLETE, 0, 0);

    const std::string	&te = itTE->second;

    // 현재는 chunked만 지원 : HTTP/1.1을 지원해야 함으로  chunked transfer encoding을 처리해야 한다. 그외는 구현 안해도 됨
    std::string lower = te;
    for (size_t i = 0; i < lower.size(); ++i) // chunked 소문자로 변경
        lower[i] = std::tolower(lower[i]);

    if (lower == "chunked")
    {
        // Content-Length와 동시에 존재하면 에러
        if (headers.find("content-length") != headers.end())
            return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);

		// chunked만 있으면 정상
        return HttpParseResult(HttpParseResult::PARSE_COMPLETE, 0, 0);
    }

    // chunked 외의 transfer-encoding은 미지원 : ex) gzip, compress, deflate -> 그래서 501 = Not Implemented (서버가 이 기능을 구현하지 않음을 의미)
    return HttpParseResult(HttpParseResult::PARSE_ERROR, 501, 0);
}

HttpParseResult	HttpRequestValidator::validateContentLength(const std::map<std::string, std::string>& headers)
{
	// map을 읽기만 할거라서 const_iterator
    std::map<std::string, std::string>::const_iterator	itCL = headers.find("content-length");

    if (itCL == headers.end())
        return HttpParseResult(HttpParseResult::PARSE_COMPLETE, 0, 0);

    const std::string	&cl = itCL->second; // ex) headers["content-length"] = "5"; -> map의 second: "5", first: "content-length"

    // 1) 비어있으면 에러
    if (cl.empty())
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);

    // 2) 숫자인지 검사
    for (size_t i = 0; i < cl.size(); ++i)
    {
        if (!std::isdigit(cl[i]))
            return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);
    }

    // 3) size_t 변환 : body 길이 (바이트 수 / 음수 불가능 / size_t = unsigned 정수)
    std::istringstream	iss(cl); // 문자열을 입력 스트림처럼 사용하는 객체 생성 : atoi는 "abc" -> 0 (에러 감지 불가능)
    size_t	value;
    iss >> value; // 문자열 ex) "123"을 숫자(123)로 변환

    if (iss.fail()) // 숫자 변환 실패 감지
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);

    return HttpParseResult(HttpParseResult::PARSE_COMPLETE, 0, 0);
}


HttpParseResult	HttpRequestValidator::validate(const HttpRequest& request)
{
    // 1) 기본 파싱 중 에러 발생
    if (request.hasError()) {
        if (request.getErrorType() == HttpRequest::ERROR_REQUEST_TOO_LARGE)
            return HttpParseResult(HttpParseResult::PARSE_ERROR, 413, 0);
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);
    }

    // 2) 아직 요청이 완성되지 않음
    if (!request.isComplete())
        return HttpParseResult(HttpParseResult::PARSE_NEED_MORE, 0, 0);

    // 3) HTTP 버전 검사: HTTP/1.1이 아니면 505 HTTP Version Not Supported
    if (request.getVersion() != "HTTP/1.1")
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 505, 0);

    // 4) Host 헤더 필수 (HTTP/1.1)
    const std::map<std::string, std::string>	&headers = request.getHeaders(); // std::map<std::string, std::string> headers; ex) key = "Host" : value = "localhost:8080"
    if (headers.count("host") == 0) // key("host")가 map안에 존재하면 1, 없으면 0 반환 [HTTP/1.1은 반드시 Host 필요. 없으면 400 Bad Request], 헤더 이름을 강제로 소문자로 저장해서 "host"로 검사
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 400, 0);

    // 5) Method 검사 (필수 구현 3개)
    std::string	method = request.getMethod();
    if (method != "GET" && method != "POST" && method != "DELETE")
        return HttpParseResult(HttpParseResult::PARSE_ERROR, 501, 0); // 그외 메서드 501
    
    // POST인데 Content-Length도 없고 chunked도 아님 -> 411
    if (method == "POST")
    {
        if (headers.find("content-length") == headers.end() &&
            headers.find("transfer-encoding") == headers.end())
            return HttpParseResult(HttpParseResult::PARSE_ERROR, 411, 0);
    }
	
	// 6) Content-Length 검사
	HttpParseResult	clResult = validateContentLength(headers);
	if (clResult.getStatus() == HttpParseResult::PARSE_ERROR) // 413
		return clResult;
	
	// 7) Transfer-Encoding + Content-Length 충돌 검사
	HttpParseResult teResult = validateTransferEncodingConflict(headers);
	if (teResult.getStatus() == HttpParseResult::PARSE_ERROR) // 501
    	return teResult;

    // 모든 검사 통과
    return HttpParseResult(HttpParseResult::PARSE_COMPLETE, 0, request.getConsumedLength()); // 여기에서 HttpRequest가 계산한 consumedLength를 result._consumedLength값에 반환/저장

}
