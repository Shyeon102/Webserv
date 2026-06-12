#!/usr/bin/env python3
import sys, os

print("Content-Type: text/plain")
print()
print("Hello from CGI")
print("METHOD:", os.environ.get("REQUEST_METHOD", ""))

body = sys.stdin.read()
if body:
    print("BODY:", body)