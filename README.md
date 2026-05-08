*This project has been created as part of the 42 curriculum by seong-ki, jaoh, jihyeki2.*

# Webserv

## Description

Webserv is a C++98 HTTP server built for the 42 curriculum. The project goal is to
understand how a web server works internally by implementing request parsing,
configuration parsing, socket management, routing, and response generation without
using an existing HTTP server framework.

The server reads an NGINX-like configuration file, listens on one or more ports,
accepts multiple clients with non-blocking I/O, parses HTTP/1.1 requests, and sends
responses for static files, uploads, redirects, error pages, and CGI scripts.

## Instructions

### Compilation

Build the executable with:

```bash
make
```

Useful Makefile targets:

```bash
make clean
make fclean
make re
```

### Execution

Run the server with an explicit configuration file:

```bash
./webserv conf.conf
```

The executable also accepts any compatible configuration file:

```bash
./webserv tests/conf_basic.conf
```

If no argument is provided, the program uses the default path defined in
`includes/Config.hpp`:

```bash
./webserv
```

### Basic Tests

With `conf.conf`, the server listens on port `8080`.

```bash
curl http://localhost:8080/
curl -X POST -d "data=hello" http://localhost:8080/post_body
```

A browser can also be used:

```text
http://localhost:8080/
```

The repository includes additional configuration examples and stress tests in
`tests/`. For example:

```bash
sh tests/stress_test.sh
```

## Features

- HTTP/1.1 request parsing: request line, headers, body, partial reads, and chunked bodies
- HTTP methods: `GET`, `POST`, and `DELETE`
- Static file serving from configured roots
- Upload handling with `upload_store`
- CGI execution through `cgi_pass`
- Custom error pages
- Redirection with `return`
- Autoindex directory listing
- Multiple server blocks and multiple listen ports
- Route-based `location` configuration
- Method restrictions with `methods` and `allow_methods`
- Body size limits with `client_max_body_size`
- Keep-alive connection handling
- Connection limits and timeout-related directives
- Non-blocking I/O with `poll()`

## Configuration Overview

The configuration format is inspired by NGINX. Supported directives include:

- `listen`
- `server_name`
- `root`
- `index`
- `error_page`
- `methods`
- `allow_methods`
- `autoindex`
- `client_max_body_size`
- `return`
- `upload_store`
- `cgi_pass`
- `max_connections`
- `idle_timeout`
- `write_timeout`
- `keepalive_max`

Example:

```nginx
server {
    listen 8080;
    server_name localhost;
    root ./YoupiBanane;
    index index.html;
    client_max_body_size 1000000;

    location / {
        allow_methods GET;
        autoindex off;
    }
}
```

## Technical Choices

- Language: C++98
- Build system: Makefile
- I/O multiplexing: `poll()`
- Parsing approach: tokenizer and parser for configuration files; state-based HTTP request parsing
- Routing approach: select the best matching server and location, then dispatch to method-specific handlers

## Team

| Login | Main responsibility |
| --- | --- |
| seong-ki | Server core, sockets, non-blocking I/O, client connection handling |
| jihyeki2 | HTTP request parsing, partial reads, request validation, status-code decisions |
| jaoh | Response generation, static files, error pages, CGI execution |

## Resources

### References

- HTTP Semantics: https://www.rfc-editor.org/rfc/rfc9110
- HTTP/1.1: https://www.rfc-editor.org/rfc/rfc9112
- RFC 7230, older HTTP/1.1 message syntax reference: https://datatracker.ietf.org/doc/html/rfc7230
- NGINX documentation: https://nginx.org/en/docs/
- POSIX sockets overview: https://man7.org/linux/man-pages/man7/socket.7.html
- `socket(2)`: https://man7.org/linux/man-pages/man2/socket.2.html
- `bind(2)`: https://man7.org/linux/man-pages/man2/bind.2.html
- `listen(2)`: https://man7.org/linux/man-pages/man2/listen.2.html
- `accept(2)`: https://man7.org/linux/man-pages/man2/accept.2.html
- `recv(2)`: https://man7.org/linux/man-pages/man2/recv.2.html
- `send(2)`: https://man7.org/linux/man-pages/man2/send.2.html
- `poll(2)`: https://man7.org/linux/man-pages/man2/poll.2.html

### AI Usage

AI tools were used as support during documentation and design review. They helped with:

- Drafting and organizing README sections
- Reviewing HTTP parsing edge cases, including partial reads and malformed requests
- Comparing configuration-parser behavior with common NGINX-style concepts
- Producing test ideas and curl examples for manual validation

AI-generated suggestions were reviewed manually, adapted to the actual implementation,
and checked against the project requirements before being included.
