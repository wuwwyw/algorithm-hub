"""Exercise authentication using a disposable Redis, never the application's Redis."""
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    password = secrets.token_hex(24)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="algorithm-redis-auth-") as directory:
        config = Path(directory) / "redis.conf"
        config.write_text(f"bind 127.0.0.1\nport {port}\nsave \"\"\nappendonly no\n"
                          f"requirepass {password}\ndir {directory}\n")
        config.chmod(0o600)
        server = subprocess.Popen(["redis-server", str(config)], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError("Disposable Redis failed to start")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.1):
                        break
                except OSError:
                    time.sleep(0.02)
            else:
                raise RuntimeError("Disposable Redis startup timed out")
            subprocess.run([str(ROOT / "build/test_redis_auth"), str(port)], check=True,
                           env=dict(os.environ, TEST_REDIS_PASSWORD=password), timeout=10)
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    main()
