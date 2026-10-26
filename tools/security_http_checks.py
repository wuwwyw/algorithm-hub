"""Benign HTTP checks against a temporary local instance; no valid-user login."""
import json
import os
from pathlib import Path
import resource
import re
import socket
import subprocess
import time

from test_multi_epoll import response

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/security_audit"


def disable_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    results = {}
    probe = OUT / "test_redis_format"
    for name, value in (("ordinary", "ordinary"), ("percent_string", "%s")):
        check = subprocess.run([str(probe), value], capture_output=True, timeout=5,
                               preexec_fn=disable_core,
                               env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0:disable_coredump=1"))
        # Keep only a sanitizer classification, not memory contents or raw Redis output.
        results["redis_format_" + name] = {
            "exit_code": check.returncode,
            "sanitizer_error": b"ERROR: AddressSanitizer" in check.stderr or b"runtime error:" in check.stderr,
        }
        classification = re.search(rb"ERROR: AddressSanitizer: ([A-Za-z_-]+)", check.stderr)
        if classification:
            results["redis_format_" + name]["sanitizer_class"] = classification.group(1).decode()
        assert check.returncode == 0 and not results["redis_format_" + name]["sanitizer_error"], \
            "Redis literal-key regression failed"
    with socket.socket() as probe_socket:
        probe_socket.bind(("127.0.0.1", 0))
        port = probe_socket.getsockname()[1]
    with (OUT / "http_server.log").open("w") as log:
        server = subprocess.Popen([str(ROOT / "build/algorithm_hub"), "127.0.0.1", str(port)],
                                  cwd=ROOT, stdout=log, stderr=log, preexec_fn=disable_core)
        def http(path, body=None):
            with socket.create_connection(("127.0.0.1", port), 3) as sock:
                sock.settimeout(3)
                method = "GET" if body is None else "POST"
                headers = f"{method} {path} HTTP/1.1\r\nHost: localhost\r\n"
                data = b"" if body is None else body.encode()
                if body is not None:
                    headers += f"Content-Type: application/json\r\nContent-Length: {len(data)}\r\n"
                sock.sendall(headers.encode() + b"\r\n" + data)
                status, _, _ = response(sock)
                return status
        try:
            for _ in range(200):
                if server.poll() is not None:
                    raise RuntimeError("Test server exited")
                try:
                    results["plain_http_home"] = http("/")
                    break
                except ConnectionRefusedError:
                    time.sleep(0.02)
            for path in ("/.env", "/.git/config", "/net/Tcpserver.hpp", "/../../net/Tcpserver.hpp"):
                results["sensitive_path " + path] = http(path)
            for path in ("/api/me", "/api/my/boards"):
                results["unauthenticated " + path] = http(path)
            results["invalid_login_json"] = http("/api/login", "{")
            results["login_sql_metacharacters"] = http("/api/login", json.dumps(
                {"username": "audit' OR '1'='1", "password": "audit-nonexistent-password"}))
            results["server_still_running"] = server.poll() is None
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
    (OUT / "http_checks.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
