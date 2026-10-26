"""Read-only credential/deployment checks. Never print credential values."""
import json
import os
from pathlib import Path
import re
import socket
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/security_audit"


def run(args, **kwargs):
    return subprocess.run(args, capture_output=True, text=True, timeout=10, **kwargs)


def redis_read(*args):
    # Read only fixed audit commands; return decoded RESP without exposing raw replies.
    with socket.create_connection(("127.0.0.1", 6379), 3) as sock:
        sock.settimeout(3)
        encoded = [str(arg).encode() for arg in args]
        sock.sendall(b"*" + str(len(encoded)).encode() + b"\r\n" + b"".join(
            b"$" + str(len(arg)).encode() + b"\r\n" + arg + b"\r\n" for arg in encoded))
        stream = sock.makefile("rb")
        def read():
            line = stream.readline()
            prefix, value = line[:1], line[1:-2]
            if prefix == b"-":
                raise RuntimeError("Redis command unsupported or denied")
            if prefix == b"+":
                return value.decode()
            if prefix == b":":
                return int(value)
            if prefix == b"$":
                size = int(value)
                if size < 0:
                    return None
                result = stream.read(size)
                stream.read(2)
                return result.decode()
            if prefix == b"*":
                return [read() for _ in range(int(value))]
            raise RuntimeError("Unexpected Redis protocol reply")
        return read()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "net/Tcpserver.hpp").read_text()
    match = re.search(r'MysqlPool\s+_mysql_pool\s*\{\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"', source)
    settings = {}
    env_file = Path(os.environ.get("ALGORITHM_CONFIG_FILE", str(ROOT / ".env")))
    if env_file.exists():
        for line in env_file.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            value = value.strip()
            if len(value) >= 2 and value[0] in "\"'" and value[-1] == value[0]:
                value = value[1:-1]
            settings[key.strip()] = value
    def setting(name, fallback=""):
        return os.environ.get(name, settings.get(name, fallback))
    if match:
        host, user, password, database = match.groups()
    else:
        host = setting("ALGORITHM_DB_HOST", "127.0.0.1")
        user = setting("ALGORITHM_DB_USER", "algorithm_app")
        password = setting("ALGORITHM_DB_PASSWORD")
        database = setting("ALGORITHM_DB_NAME", "algorithm_hub")
    assert password, "Configure ALGORITHM_DB_PASSWORD or the machine-local .env first"
    result = {"mysql_password_literal": bool(match), "credential_locations": [],
              "local_config_private": env_file.exists() and (env_file.stat().st_mode & 0o077) == 0}
    secret = password.encode()
    # Exclude bundled ICU headers; include copies, objects and executables in build/output.
    for base in ("net", "sql", "redis", "src", "tools", "web", "build", "output", ".vscode", "DataBase-mysql"):
        directory = ROOT / base
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.is_symlink() or "icu-dev" in path.parts or "security_audit" in path.parts:
                continue
            if path.stat().st_size > 100 * 1024 * 1024:
                continue
            if secret and secret in path.read_bytes():
                result["credential_locations"].append(str(path.relative_to(ROOT)))
    result["git_history_available"] = run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"]).returncode == 0
    mysql_env = dict(os.environ, MYSQL_PWD=password)
    def mysql(sql):
        # Password is inherited by this child only, never added to command arguments or reports.
        return run(["mysql", "--batch", "--skip-column-names", "-h", host,
                    "-P", setting("ALGORITHM_DB_PORT", "3306"), "-u", user,
                    "-D", database, "-e", sql], env=mysql_env)
    grants = mysql("SHOW GRANTS FOR CURRENT_USER()")
    result["mysql_credential_accepted"] = grants.returncode == 0
    if grants.returncode == 0:
        # Do not retain account identifiers or authentication material from grant output.
        result["mysql_grant_scopes"] = [line.split(" TO ", 1)[0] for line in grants.stdout.splitlines()]
        result["mysql_has_global_privileges"] = any(" ON *.* " in line and not line.startswith("GRANT USAGE ")
                                                    for line in grants.stdout.splitlines())
        result["mysql_has_grant_option"] = "WITH GRANT OPTION" in grants.stdout
    variables = mysql("SHOW GLOBAL VARIABLES WHERE Variable_name IN ('bind_address','require_secure_transport')")
    if variables.returncode == 0:
        result["mysql_server_variables"] = dict(line.split("\t", 1) for line in variables.stdout.splitlines())
    hashes = mysql("SELECT COUNT(*), COALESCE(SUM(password_hash LIKE 'pbkdf2_sha256$210000$%'),0), "
                   "COALESCE(SUM(password_hash NOT LIKE 'pbkdf2_sha256$%'),0) FROM user")
    if hashes.returncode == 0:
        values = list(map(int, hashes.stdout.strip().split("\t")))
        result["password_hash_aggregate"] = dict(zip(("accounts", "pbkdf2_210000", "other_schemes"), values))
    redis_env = dict(os.environ)
    redis_env.pop("REDISCLI_AUTH", None)
    pong = run(["redis-cli", "-h", "127.0.0.1", "-p", "6379", "PING"], env=redis_env)
    result["redis_unauthenticated_ping"] = pong.stdout.strip() == "PONG"
    try:
        raw = redis_read("ACL", "GETUSER", "default")
        values = raw if isinstance(raw, dict) else dict(zip(raw[::2], raw[1::2]))
        result["redis_default_user"] = {"flags": values.get("flags"), "commands": values.get("commands"),
                                        "keys": values.get("keys")}
    except (ValueError, TypeError, RuntimeError):
        result["redis_acl_readable"] = False
    try:
        result["redis_network_config"] = {}
        for setting in ("bind", "protected-mode"):
            raw = redis_read("CONFIG", "GET", setting)
            result["redis_network_config"].update(dict(zip(raw[::2], raw[1::2])))
    except (ValueError, TypeError, RuntimeError):
        result["redis_config_readable"] = False
    listeners = run(["ss", "-ltnH"])
    result["database_listeners"] = [line.split()[3] for line in listeners.stdout.splitlines()
                                     if line.split()[3].rsplit(":", 1)[-1] in ("3306", "33060", "6379")]
    text = json.dumps(result, indent=2)
    assert password not in text, "Refusing to output a credential"
    (OUT / "connection_checks.json").write_text(text)
    print(text)


if __name__ == "__main__":
    main()
