"""Local multi-epoll integration checks; only static routes are requested."""
import concurrent.futures
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def response(sock, buffered=b""):
    data = buffered
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(65536)
        if not chunk:
            raise AssertionError("EOF before response headers")
        data += chunk
    header, data = data.split(b"\r\n\r\n", 1)
    size = int(next(line.split(b":", 1)[1] for line in header.split(b"\r\n")[1:]
                    if line.lower().startswith(b"content-length:")))
    while len(data) < size:
        chunk = sock.recv(65536)
        if not chunk:
            raise AssertionError("EOF before response body")
        data += chunk
    return int(header.split()[1]), data[:size], data[size:]


def main():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    log_path = ROOT / "build/multi_epoll_test.log"
    results = {}
    held = []
    with log_path.open("w") as log:
        server = subprocess.Popen([str(ROOT / "build/algorithm_hub"), "127.0.0.1", str(port)],
                                  cwd=ROOT, stdout=log, stderr=log)

        def connect():
            sock = socket.create_connection(("127.0.0.1", port), timeout=5)
            sock.settimeout(5)
            return sock

        def get(path="/"):
            return f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode()

        expected = (ROOT / "web/html/index.html").read_bytes()

        def check_home(sock, buffered=b""):
            status, body, rest = response(sock, buffered)
            assert status == 200 and body == expected, (status, len(body))
            return rest

        try:
            deadline = time.monotonic() + 10
            while True:
                if server.poll() is not None:
                    raise AssertionError(f"server exited: {server.returncode}; see {log_path}")
                try:
                    with connect() as sock:
                        sock.sendall(get())
                        check_home(sock)
                    break
                except ConnectionRefusedError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.05)
            results["startup_and_static_get"] = "PASS"
            # Exercise upload parsing without authentication or database writes.
            for code, expected_status in [(b'int main() {}', 401), (b'// \xff', 400)]:
                body = (b'--upload-test\r\nContent-Disposition: form-data; name="code"; filename="C.cpp"'
                        b'\r\nContent-Type: text/plain\r\n\r\n' + code + b'\r\n--upload-test--\r\n')
                header = (b'POST /api/my/boards HTTP/1.1\r\nHost: localhost\r\n'
                          b'Content-Type: multipart/form-data; boundary=upload-test\r\nContent-Length: '
                          + str(len(body)).encode() + b'\r\n\r\n')
                with connect() as sock:
                    sock.sendall(header + body)
                    status, reply, rest = response(sock)
                    assert status == expected_status, (status, reply)
                    if status == 400:
                        assert 'UTF-8' in json.loads(reply)['message']
                    sock.sendall(get())
                    check_home(sock, rest)
            for length in [b'-1', b'+1', b'1junk']:
                with connect() as sock:
                    sock.sendall(b'POST /api/my/boards HTTP/1.1\r\nContent-Length: ' + length + b'\r\n\r\n')
                    assert response(sock)[0] == 400
            results["upload_encoding_and_invalid_lengths"] = "PASS"


            # Keep 30 known client sockets open; inspect their server-side epoll membership.
            for _ in range(30):
                sock = connect()
                held.append(sock)
                sock.sendall(get())
                check_home(sock)
            client_ports = {sock.getsockname()[1] for sock in held}
            inodes = set()
            for line in Path("/proc/net/tcp").read_text().splitlines()[1:]:
                fields = line.split()
                local_port = int(fields[1].split(":")[1], 16)
                remote_port = int(fields[2].split(":")[1], 16)
                if local_port == port and remote_port in client_ports and fields[3] == "01":
                    inodes.add(fields[9])
            fd_dir = Path(f"/proc/{server.pid}/fd")
            known_fds = set()
            epolls = []
            for fd_path in fd_dir.iterdir():
                try:
                    target = os.readlink(fd_path)
                except FileNotFoundError:
                    continue
                if target.startswith("socket:[") and target[8:-1] in inodes:
                    known_fds.add(int(fd_path.name))
                if target == "anon_inode:[eventpoll]":
                    epolls.append(fd_path.name)
            counts = []
            for epfd in epolls:
                info = Path(f"/proc/{server.pid}/fdinfo/{epfd}").read_text()
                watched = {int(line.split()[1]) for line in info.splitlines() if line.startswith("tfd:")}
                count = len(watched & known_fds)
                if count:
                    counts.append(count)
            assert len(known_fds) == 30 and sorted(counts) == [10, 10, 10], counts
            results["connection_distribution"] = counts

            with connect() as sock:
                for _ in range(100):
                    sock.sendall(get())
                    check_home(sock)
            results["keep_alive_100_requests"] = "PASS"

            with connect() as sock:
                sock.sendall(get() * 20)
                rest = b""
                for _ in range(20):
                    rest = check_home(sock, rest)
            results["pipeline_20_requests"] = "PASS"

            # POST to a static GET-only route consumes a body without modifying database data.
            body = b"split-body-check" * 100
            head = (f"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: {len(body)}\r\n"
                    "Content-Type: application/octet-stream\r\n\r\n").encode()
            with connect() as sock:
                sock.sendall(head[:12])
                time.sleep(0.03)
                sock.sendall(head[12:] + body[:7])
                sock.settimeout(0.15)
                try:
                    premature = sock.recv(1)
                    raise AssertionError(f"response before complete body: {premature!r}")
                except socket.timeout:
                    pass
                sock.settimeout(5)
                for offset in range(7, len(body), 113):
                    sock.sendall(body[offset:offset + 113])
                    time.sleep(0.002)
                status, _, rest = response(sock)
                assert status == 405, status
                sock.sendall(get())
                check_home(sock, rest)
            results["split_header_body_and_reuse"] = "PASS"

            def worker(_):
                with connect() as sock:
                    for _ in range(25):
                        sock.sendall(get())
                        check_home(sock)
                return 25

            with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
                total = sum(pool.map(worker, range(48)))
            results["concurrent_requests"] = total
            assert server.poll() is None
            results["server_alive_after_checks"] = "PASS"
        finally:
            for sock in held:
                sock.close()
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            (ROOT / "build/multi_epoll_results.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
    print("Server process cleaned up; SIGTERM is not a destructor/shutdown test.")


if __name__ == "__main__":
    main()
