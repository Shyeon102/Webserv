# ojo part config test set

## valid
- `ojo_router_methods_valid.conf`: longest-prefix router + methods/allow_methods 조합
- `ojo_get_autoindex_valid.conf`: GET + index + autoindex
- `ojo_post_upload_valid.conf`: POST + upload_store + client_max_body_size
- `ojo_delete_valid.conf`: DELETE 전용 location
- `ojo_cgi_valid.conf`: cgi_pass(.py/.php)
- `ojo_error_redirect_valid.conf`: error_page + return(redirect)

## invalid
- `ojo_invalid_duplicate_allow_methods.conf`: allow_methods 중복 메서드
- `ojo_invalid_cgi_extension.conf`: cgi_pass 확장자 점(.) 누락
- `ojo_invalid_return_status.conf`: return status가 3xx 아님
- `ojo_invalid_duplicate_location.conf`: location path 중복

## quick run
- parse probe가 있을 때: `./config_probe tests/ojo_configs/<file>`
- webserv로 파싱 확인: `./webserv tests/ojo_configs/<file>`

## httprequest/httpresponse set
- `ojo_httprequest_valid.conf` (valid): GET/POST/DELETE + upload_store
- `ojo_httprequest_chunked_cgi_valid.conf` (valid): POST/CGI 경로 테스트용
- `ojo_httprequest_invalid_body_size.conf` (invalid): client_max_body_size 0
- `ojo_httprequest_invalid_method.conf` (invalid): allow_methods에 PUT 포함
- `ojo_httpresponse_errorpage_valid.conf` (valid): error_page 404/500
- `ojo_httpresponse_redirect_valid.conf` (valid): return 301 redirect
- `ojo_httpresponse_invalid_errorpage.conf` (invalid): error_page status 비숫자
