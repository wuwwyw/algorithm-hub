"""Build an isolated tunable server and compare read-only local HTTP loads.

Production sources are copied, never rewritten. All trial data is saved under
build/epoll_bench. Run inside WSL with: python3 tools/bench_multi_epoll.py
"""
import argparse
import asyncio
import concurrent.futures
import json
import math
import os
from pathlib import Path
import random
import resource
import shlex
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/epoll_bench"


def prepare(configure_source=None):
    source = OUT / "source"
    source.mkdir(parents=True, exist_ok=True)
    for folder in ("net", "sql", "redis", "src"):
        shutil.copytree(ROOT / folder, source / folder, dirs_exist_ok=True)
    settings = (("net/Tcpserver.hpp", "loop_size", "int", "LOOPS"),
                ("net/ThreadPool.hpp", "thread_num", "size_t", "WORKERS"),
                ("net/Event_loop.hpp", "mysql_pool_sz", "size_t", "MYSQL"),
                ("net/Event_loop.hpp", "redis_pool_sz", "size_t", "REDIS"))
    import re
    for filename, name, typ, env in settings:
        path = source / filename
        text = path.read_text()
        text, count = re.subn(rf"const {typ} {name} = \d+;",
                            f'const {typ} {name} = std::stoul(std::getenv("BENCH_{env}"));', text)
        assert count == 1, name
        path.write_text(text)
    path = source / "net/Tcpserver.hpp"
    text = path.read_text().replace("std::array<std::mutex, loop_size> _bell_mutex;",
                                   "std::deque<std::mutex> _bell_mutex;")
    text = text.replace("_bell.resize(loop_size);", "_bell.resize(loop_size);\n        _bell_mutex.resize(loop_size);")
    path.write_text("#include <cstdlib>\n#include <deque>\n" + text)
    if configure_source:
        configure_source(source)
    flags = (ROOT / "build/CMakeFiles/algorithm_hub.dir/flags.make").read_text()
    definitions = next(line.split(" = ", 1)[1] for line in flags.splitlines()
                       if line.startswith("CXX_DEFINES = "))
    definitions = shlex.split(definitions.replace('\\"', '"'))
    # Keep literal quotes inside preprocessor string definitions.
    definitions = [f'{arg.split("=", 1)[0]}="{arg.split("=", 1)[1]}"' for arg in definitions]
    pkg = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "mysqlclient", "hiredis"], text=True))
    command = ["g++", "-std=c++17", "-O2", "-DNDEBUG", "-pthread", "-include", "unistd.h",
               "-I" + str(ROOT / "build/icu-dev/usr/include"), *definitions,
               str(source / "src/main.cpp"), "-o", str(OUT / "server"), *pkg,
               "-l:libicui18n.so.70", "-l:libicuuc.so.70", "-lhpdf", "-lcrypto"]
    print("Building isolated -O2 benchmark executable...", flush=True)
    subprocess.run(command, check=True)


def request(kind):
    if kind == "db":
        return (b'POST /api/boards/page HTTP/1.1\r\nHost: localhost\r\n'
                b'Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}', 200)
    if kind == "redis":
        # A nonexistent session causes a Redis GET, without creating any session.
        return (b'GET /api/me HTTP/1.1\r\nHost: localhost\r\n'
                b'Cookie: session_id=codex-benchmark-nonexistent-session\r\n\r\n', 401)
    return b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n', 200


async def read_response(reader):
    header = await reader.readuntil(b"\r\n\r\n")
    length = int(next(line.split(b":", 1)[1] for line in header.split(b"\r\n")
                      if line.lower().startswith(b"content-length:")))
    body = await reader.readexactly(length)
    return int(header.split()[1]), body


def load_process(port, concurrency, duration, workload, start_at, ramp=False):
    async def run():
        latencies = []
        errors = {}
        deadline = start_at + duration
        choices = ["static", "static", "static", "static", "static", "db", "db", "db", "redis", "redis"]

        async def connection(index):
            writer = None
            sequence = index
            phase = "connect"
            try:
                if ramp:
                    await asyncio.sleep(index * 0.005)
                reader, writer = await asyncio.wait_for(asyncio.open_connection("127.0.0.1", port), 5)
                phase = "warmup"
                # Warm the connection and validate the endpoint before timing.
                data, expected = request(workload if workload != "mixed" else choices[sequence % 10])
                writer.write(data)
                await writer.drain()
                status, _ = await asyncio.wait_for(read_response(reader), 5)
                if status != expected:
                    raise RuntimeError(f"warmup HTTP {status}, expected {expected}")
                await asyncio.sleep(max(0, start_at - time.monotonic()))
                phase = "request"
                while time.monotonic() < deadline:
                    kind = choices[sequence % 10] if workload == "mixed" else workload
                    sequence += 1
                    data, expected = request(kind)
                    begin = time.monotonic()
                    writer.write(data)
                    await writer.drain()
                    status, _ = await asyncio.wait_for(read_response(reader), 5)
                    end = time.monotonic()
                    if status != expected:
                        label = f"HTTP {status}"
                        errors[label] = errors.get(label, 0) + 1
                    elif end <= deadline:
                        latencies.append((end - begin) * 1000)
            except Exception as exc:
                key = phase + ": " + type(exc).__name__ + ": " + str(exc)
                errors[key] = errors.get(key, 0) + 1
            finally:
                if writer:
                    writer.close()
                    try:
                        await writer.wait_closed()
                    except Exception:
                        pass

        await asyncio.gather(*(connection(i) for i in range(concurrency)))
        return latencies, errors

    before = resource.getrusage(resource.RUSAGE_SELF)
    latencies, errors = asyncio.run(run())
    after = resource.getrusage(resource.RUSAGE_SELF)
    return latencies, errors, (after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)


def cpu_ticks(pid):
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except FileNotFoundError:
        return 0


def snapshot(pid):
    tids = sorted(int(p.name) for p in Path(f"/proc/{pid}/task").iterdir())
    ticks = {tid: cpu_ticks(f"{pid}/task/{tid}") for tid in tids}
    status = Path(f"/proc/{pid}/status").read_text().splitlines()
    rss = int(next(line.split()[1] for line in status if line.startswith("VmRSS:")))
    return ticks, rss


def redis_get_calls():
    info = subprocess.check_output(["redis-cli", "INFO", "commandstats"], text=True)
    for line in info.splitlines():
        if line.startswith("cmdstat_get:"):
            return int(line.split("calls=", 1)[1].split(",", 1)[0])
    return 0


def listen_counters():
    lines = Path("/proc/net/netstat").read_text().splitlines()
    for i, line in enumerate(lines):
        if line.startswith("TcpExt:"):
            values = dict(zip(line.split()[1:], map(int, lines[i + 1].split()[1:])))
            return {key: values[key] for key in ("ListenOverflows", "ListenDrops")}
    return {}


def trial(config, workload, concurrency, duration, pool, ramp=False):
    loops, workers, mysql, redis = config
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, BENCH_LOOPS=str(loops), BENCH_WORKERS=str(workers),
               BENCH_MYSQL=str(mysql), BENCH_REDIS=str(redis))
    with (OUT / "server.log").open("w") as log:
        listen_before = listen_counters()
        server = subprocess.Popen([str(OUT / "server"), "127.0.0.1", str(port)],
                                  env=env, cwd=ROOT, stdout=log, stderr=log)
        try:
            for _ in range(200):
                if server.poll() is not None:
                    raise RuntimeError("benchmark server exited; inspect server.log")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.1):
                        break
                except OSError:
                    time.sleep(0.02)
            else:
                raise RuntimeError("startup timeout")
            from test_multi_epoll import response as receive_sync
            with socket.create_connection(("127.0.0.1", port), 5) as sock:
                sock.settimeout(5)
                sock.sendall(request("db")[0])
                status, body, _ = receive_sync(sock)
                assert status == 200, status
                database_total = json.loads(body)["total"]
            redis_before = redis_get_calls()
            start = time.monotonic() + 1.5
            tasks = [pool.submit(load_process, port, concurrency // 4, duration, workload, start, ramp)
                     for _ in range(4)]
            time.sleep(max(0, start - time.monotonic()))
            before, _ = snapshot(server.pid)
            mysql_pid = int(subprocess.check_output(["pgrep", "-x", "mysqld"], text=True).strip())
            mysql_before = cpu_ticks(mysql_pid)
            time.sleep(duration)
            after, rss = snapshot(server.pid)
            mysql_after = cpu_ticks(mysql_pid)
            latencies, errors, client_cpu = [], {}, 0
            for task in tasks:
                samples, failures, usage = task.result(timeout=10)
                latencies.extend(samples)
                client_cpu += usage
                for key, value in failures.items():
                    errors[key] = errors.get(key, 0) + value
            latencies.sort()
            redis_calls = redis_get_calls() - redis_before
            listen_after = listen_counters()
            if workload == "redis":
                assert redis_calls >= len(latencies), "Redis query count is below successful HTTP responses"
            def pct(percent):
                return round(latencies[min(len(latencies) - 1, math.ceil(len(latencies) * percent) - 1)], 3) if latencies else None
            hz = os.sysconf("SC_CLK_TCK")
            thread_cpu = {str(tid): round((after.get(tid, 0) - tick) / hz / duration * 100, 1)
                          for tid, tick in before.items()}
            return dict(config=list(config), workload=workload, concurrency=concurrency, seconds=duration,
                        requests=len(latencies), rps=round(len(latencies) / duration, 1),
                        connection_ramp=ramp,
                        listen_counter_delta={k: listen_after[k] - v for k, v in listen_before.items()},
                        p50_ms=pct(0.5), p95_ms=pct(0.95), p99_ms=pct(0.99), errors=errors,
                        server_cpu_pct=round(sum(thread_cpu.values()), 1), thread_cpu_pct=thread_cpu,
                        io_cpu_pct=[thread_cpu[str(t)] for t in sorted(before)[-loops:]],
                        mysql_cpu_pct=round((mysql_after - mysql_before) / hz / duration * 100, 1),
                        database_total=database_total, redis_get_calls=redis_calls,
                        client_cpu_pct=round(client_cpu / duration * 100, 1), rss_mb=round(rss / 1024, 1))
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--supplement", action="store_true")
    parser.add_argument("--verify256", action="store_true")
    parser.add_argument("--burst-only", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        prepare()
    results = json.loads((OUT / "results.json").read_text()) if args.supplement or args.verify256 or args.burst_only else []
    def save():
        (OUT / "results.json").write_text(json.dumps(results, indent=2))

    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as pool:
        def stage(name, configs, workload, concurrency=64, duration=5, repeats=2, ramp=False):
            print("STAGE " + name, flush=True)
            schedule = configs * repeats
            random.Random(20261022).shuffle(schedule)
            rows = []
            for config in schedule:
                row = trial(config, workload, concurrency, duration, pool, ramp)
                row["stage"] = name
                results.append(row)
                rows.append(row)
                save()
                print(json.dumps({k: v for k, v in row.items() if k != "thread_cpu_pct"}), flush=True)
            def score(config):
                samples = [r for r in rows if r["config"] == list(config)]
                return sum(r["rps"] for r in samples) / len(samples) if all(not r["errors"] for r in samples) else -1
            best = max(configs, key=score)
            print("BEST " + name + " " + str(best), flush=True)
            return best

        base = (3, 16, 12, 8)
        if args.burst_only:
            stage("burst_backlog_check", [base], "mixed", 256, 5, 1)
        elif args.verify256:
            stage("burst_256_diagnostic", [base], "mixed", 256, 5, 1)
            stage("ramped_mixed_256", [base, (4, 24, 12, 8)], "mixed", 256, 15, 2, True)
        elif args.supplement:
            candidate = max((r for r in results if r["stage"] == "final_mixed_128"),
                            key=lambda r: r["rps"])["config"]
            configs = [(n, candidate[1], candidate[2], candidate[3]) for n in (2, 3, 4)]
            stage("loops_mixed_128", configs, "mixed", 128, 10, 2)
            stage("sustained_mixed_256", list(dict.fromkeys([base, tuple(candidate)])), "mixed", 256, 30, 1)
            stage("redis_command_validation", [tuple(candidate)], "redis", 64, 5, 1)
        else:
            loops = stage("loops", [(n, 16, 12, 8) for n in (1, 2, 3, 4, 6)], "static")
            workers = stage("workers", [(loops[0], n, 12, 8) for n in (4, 8, 16, 24, 32)], "mixed")
            mysql = stage("mysql", [(workers[0], workers[1], n, 8) for n in (2, 4, 8, 12, 16)], "db")
            redis = stage("redis", [(mysql[0], mysql[1], mysql[2], n) for n in (1, 2, 4, 8)], "redis")
            stage("final_mixed_128", list(dict.fromkeys([base, redis])), "mixed", 128, 15, 3)
            stage("final_db_128", list(dict.fromkeys([base, redis])), "db", 128, 10, 2)
    print("DONE " + str(OUT / "results.json"), flush=True)


if __name__ == "__main__":
    main()
