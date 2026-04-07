*This project has been created as part of the 42 curriculum by <seong-ki>[, <jaoh>[, <jihyeki2>*

# Webserv

## Description

Webserv is an HTTP server implemented in C++98 as part of the 42 curriculum.

The goal of this project is to understand how web servers work by building one from scratch.
It handles client-server communication using the HTTP protocol and is capable of parsing
HTTP requests, processing them according to a configuration file, and sending appropriate responses.

Key objectives:
- Understand HTTP/1.1 request and response structure
- Implement a non-blocking server using poll() (or equivalent)
- Parse configuration files similar to NGINX
- Handle multiple clients simultaneously
- Support basic HTTP methods (GET, POST, DELETE)

---

## Instructions

### Compilation

```bash
make
```

### Execution

```bash
./webserv [configuration_file]
```

Example:

```bash
./webserv config/default.conf
```

---

### Testing

Using a browser:
- Open: http://localhost:8080

Using curl:

```bash
curl http://localhost:8080
```

POST request:

```bash
curl -X POST -d "data=hello" http://localhost:8080
```

DELETE request:

```bash
curl -X DELETE http://localhost:8080/file
```

---

## Features

### Configuration File
- NGINX-like syntax
- Multiple server blocks
- Route-based configuration (location)
- Supported directives:
  - listen (ip:port)
  - root
  - error_page
  - methods
  - autoindex

### HTTP Request Handling
- Request line parsing (method, path, version)
- Header parsing
- Body parsing using Content-Length
- Partial read handling

### HTTP Response
- Status code handling (200, 400, 404, 413, 501, etc.)
- Custom error pages
- Static file serving

### Server Behavior
- Non-blocking I/O using poll() (or equivalent)
- Multiple client handling
- Keep-alive connection support

---

## Technical Choices

- Language: C++98
- I/O Multiplexing: poll() (or select/kqueue depending on system)
- Architecture:
  - Config Parser (Tokenizer → Parser → Validation)
  - HTTP Parser (state-based parsing)
  - Request → Response pipeline

---

## Resources

### Documentation

- HTTP/1.1 RFC (RFC 7230)
- NGINX documentation
- POSIX socket programming
- poll()/select() manual

### Useful Links

- https://datatracker.ietf.org/doc/html/rfc7230
- https://nginx.org/en/docs/
- https://man7.org/linux/man-pages/man2/poll.2.html

---

## AI Usage

AI tools were used to assist in the following tasks:

- Understanding HTTP protocol structure and parsing strategies
- Designing the architecture of the configuration parser
- Reviewing edge cases for HTTP request parsing (partial read, errors)
- Generating initial documentation drafts (README structure)

All generated content was:
- Reviewed and tested manually
- Modified to fit project requirements
- Fully understood before integration

---

## Team

| Name   | Responsibility           | Description                                          |
|--------|--------------------------|------------------------------------------------------|
|seong-ki| Server Core / Networking | - Socket creation and configuration                  |
|        |                          | - Handling bind / listen / accept                    |
|        |                          | - Implementation of poll()-based non-blocking I/O    |
|        |                          | - Client connection management and event distribution|
|--------|--------------------------|------------------------------------------------------|
|jihyeki2| HTTP Parsing             | - Parsing raw buffer → HTTP Request                  |
|        |                          | - Separating request line / headers / body           |
|        |                          | - Content-Length based body processing               |
|        |                          | - Handling partial reads                             |
|        |                          | - Determining status codes for invalid requests      |
---------|--------------------------|------------------------------------------------------|
|  jaoh  | Response / Execution     | - Request-based response generation                  |
|        |                          | - Static file serving                                |
|        |                          | - Error page handling                                |
|        |                          | - CGI execution and result processing                |

---

## Notes

- The server is designed to remain stable under all conditions
- All I/O operations are non-blocking
- Only one poll() (or equivalent) is used for client handling
- The server should not crash under any circumstances
