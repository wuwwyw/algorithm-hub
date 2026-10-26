"""Compare backlog and accept batching using synchronized connection bursts."""
import argparse
import asyncio
import concurrent.futures
import json
import math
import os
from pathlib import Path
import random
import resource
import socket
import statistics
import subprocess
import time

import bench_multi_epoll as common

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/listen_bench"


def configure(source):
    path = source / "net/Socket.hpp"
    text = path.read_text()
    original_listen = "listen(_sockfd, 16)" if "listen(_sockfd, 16)" in text else "listen(_sockfd, backlog)"
    assert original_listen in text
    path.write_text(text.replace(original_listen,
                                'listen(_sockfd, std::stoi(std::getenv("BENCH_BACKLOG")))'))
    path = source / "net/Tcpserver.hpp"
    text = path.read_text()
    batch_begin = "while (true) {\n                    InetAddr client_addr;"
    if batch_begin in text:
        text = text.replace(batch_begin, "do {\n                    InetAddr client_addr;", 1)
        batch_end = "idx = (idx + 1)%loop_size;\n                }"
        assert batch_end in text
        text = text.replace(batch_end, 'idx = (idx + 1)%loop_size;\n'
                            '                } while (std::getenv("BENCH_DRAIN")[0] == \'1\');', 1)
        path.write_text(text)
        return
    begin = "for (int i = 0; i < n; i++) {\n                int client_fd = accept"
    assert begin in text
    text = text.replace(begin, "for (int i = 0; i < n; i++) {\n"
                        "                if (events[i].data.fd != _socket.Get_fd()) continue;\n"
                        "                do {\n                int client_fd = accept")
    end = "idx = (idx + 1)%loop_size;\n            }"
    assert end in text
    text = text.replace(end, 'idx = (idx + 1)%loop_size;\n'
                        '                } while (std::getenv("BENCH_DRAIN")[0] == \'1\');\n            }')
    text = text.replace("if (client_fd < 0) continue;", "if (client_fd < 0) break;")
    path.write_text(text)


def burst_process(port, count, start_at):
    async def run():
        samples, errors = [], {}
        body = (ROOT / "web/html/index.html").read_bytes()

        async def client():
            writer = None
            phase = "connect"
            await asyncio.sleep(max(0, start_at - time.monotonic()))
            begin = time.monotonic()
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection("127.0.0.1", port), 5)
                connected = time.monotonic()
                phase = "first_response"
                writer.write(common.request("static")[0])
                await writer.drain()
                status, response_body = await asyncio.wait_for(common.read_response(reader), 5)
                assert status == 200 and response_body == body, "invalid response"
                finished = time.monotonic()
                samples.append([(connected - begin) * 1000, (finished - begin) * 1000])
            except Exception as exc:
                label = phase + ": " + type(exc).__name__ + ": " + str(exc)
                errors[label] = errors.get(label, 0) + 1
            finally:
                if writer:
                    writer.close()
                    try:
                        await writer.wait_closed()
                    except Exception:
                        pass

        await asyncio.gather(*(client() for _ in range(count)))
        return samples, errors
    return asyncio.run(run())


def trial(backlog, drain, connections, pool, binary=None):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, BENCH_LOOPS="3", BENCH_WORKERS="16", BENCH_MYSQL="12", BENCH_REDIS="8",
               BENCH_BACKLOG=str(backlog), BENCH_DRAIN=str(int(drain)))
    with (OUT / "server.log").open("w") as log:
        server = subprocess.Popen([str(binary or OUT / "server"), "127.0.0.1", str(port)],
                                  cwd=ROOT, env=env, stdout=log, stderr=log)
        try:
            for _ in range(200):
                if server.poll() is not None:
                    raise RuntimeError("server exited; inspect server.log")
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.1) as sock:
                        sock.settimeout(5)
                        sock.sendall(common.request("static")[0])
                        from test_multi_epoll import response
                        assert response(sock)[0] == 200
                    break
                except ConnectionRefusedError:
                    time.sleep(0.02)
            else:
                raise RuntimeError("startup timeout")
            ss = subprocess.check_output(["ss", "-ltnH", f"sport = :{port}"], text=True).split()
            effective_backlog = int(ss[2])
            assert effective_backlog == min(backlog, int(Path("/proc/sys/net/core/somaxconn").read_text()))
            counters_before = common.listen_counters()
            start = time.monotonic() + 0.5
            futures = [pool.submit(burst_process, port, connections // 4, start) for _ in range(4)]
            samples, errors = [], {}
            for future in futures:
                values, failures = future.result(timeout=15)
                samples.extend(values)
                for key, value in failures.items():
                    errors[key] = errors.get(key, 0) + value
            elapsed = time.monotonic() - start
            counters_after = common.listen_counters()
            def pct(column, fraction):
                values = sorted(row[column] for row in samples)
                return round(values[max(0, math.ceil(len(values) * fraction) - 1)], 3) if values else None
            assert len(samples) + sum(errors.values()) == connections
            return dict(backlog=backlog, effective_backlog=effective_backlog, drain=drain,
                        connections=connections, successes=len(samples), errors=errors,
                        connect_p95_ms=pct(0, 0.95), connect_p99_ms=pct(0, 0.99),
                        total_p50_ms=pct(1, 0.5), total_p95_ms=pct(1, 0.95), total_p99_ms=pct(1, 0.99),
                        burst_elapsed_s=round(elapsed, 3),
                        listen_counter_delta={k: counters_after[k] - v for k, v in counters_before.items()})
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


def report(rows):
    groups = {}
    for row in rows:
        groups.setdefault((row["stage"], row["connections"], row["backlog"], row["drain"]), []).append(row)
    lines = ["# listen backlog 集中建连对比", "",
             "结论：默认 backlog 采用1024，接收连接时循环 accept 到 EAGAIN；线程/连接池保留3/16/12/8。",
             "512档在1024条集中连接时仍有溢出；1024档在已测范围内消除了溢出，"
             "2048与4096没有表现出稳定的进一步收益。这是本机已测负载下的默认值，不是任意负载的全局最优值。", "",
             "固定配置：3 个 I/O 循环 / 16 个业务线程 / 12 条 MySQL / 8 条 Redis。",
             "四个独立客户端进程在同一时间开始连接，每个连接完成一次 GET /，校验状态和完整正文后关闭。",
             "延迟从发起连接到完整响应；单连接建连和首响应各设5秒超时。"
             "延迟统计只包含成功响应，失败数另列；不使用失败后的低吞吐掩盖连接问题。",
             "backlog 与 accept 单次/批量分别变化，其余实现相同，使用 -O2 独立副本。"
             "复核阶段另用修改后的正式源码、现有CMake配置构建；不要跨构建类型归因耗时差异。"
             "系统 somaxconn=4096，未修改系统网络参数。",
             "1024 连接测试将本测试进程及子进程的文件描述符软限制提高到8192；"
             "没有修改终端默认值或系统限制。", "",
             "ListenOverflows/ListenDrops 为同机系统计数增量，非单个监听socket计数。"
             "分位数列为各轮分位数的中位数；最长耗时为所有轮中最长的一轮。", "",
             "| 阶段 | 集中连接数 | backlog | accept批量 | 轮数 | 成功/总数 | 错误 | 队列溢出 | 首响应P95 ms | 首响应P99 ms | 最长一轮秒 |",
             "|---|---:|---:|---|---:|---|---:|---:|---:|---:|---:|"]
    summary = []
    for (stage, count, backlog, drain), samples in groups.items():
        errors = sum(sum(r["errors"].values()) for r in samples)
        overflow = sum(r["listen_counter_delta"]["ListenOverflows"] for r in samples)
        med = lambda key: round(statistics.median(r[key] for r in samples if r[key] is not None), 3)
        summary.append(dict(stage=stage, connections=count, backlog=backlog, drain=drain,
                            trials=len(samples), errors=errors, overflows=overflow,
                            p95_ms=med("total_p95_ms"), p99_ms=med("total_p99_ms")))
        lines.append(f'| {stage} | {count} | {backlog} | {drain} | {len(samples)} | '
                     f'{sum(r["successes"] for r in samples)}/{len(samples)*count} | {errors} | {overflow} | '
                     f'{med("total_p95_ms")} | {med("total_p99_ms")} | '
                     f'{max(r["burst_elapsed_s"] for r in samples)} |')
    lines += ["", "仅代表本机测试范围；backlog 是等待 accept 的已建立连接队列长度，不是最大在线连接数。",
              "参考：[Linux listen 手册](https://man7.org/linux/man-pages/man2/listen.2.html)。"]
    lines += [f'共{len(rows)}轮，{sum(r["connections"] for r in rows)}次连接尝试，'
              f'{sum(sum(r["errors"].values()) for r in rows)}次记录到的失败。']
    (OUT / "report.md").write_text("\n".join(lines) + "\n")
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--backlog", type=int)
    parser.add_argument("--headroom", action="store_true")
    args = parser.parse_args()
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    resource.setrlimit(resource.RLIMIT_NOFILE, (min(8192, hard), hard))
    OUT.mkdir(parents=True, exist_ok=True)
    common.OUT = OUT
    if not args.skip_build and not args.verify:
        common.prepare(configure)
    rows = json.loads((OUT / "results.json").read_text()) if args.verify or args.headroom else []
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as pool:
        def stage(name, configs, repeats):
            schedule = configs * repeats
            random.Random(20261022).shuffle(schedule)
            for backlog, drain, count in schedule:
                row = trial(backlog, drain, count, pool, args.verify)
                row["stage"] = name
                rows.append(row)
                (OUT / "results.json").write_text(json.dumps(rows, indent=2))
                report(rows)
                print(json.dumps(row), flush=True)
        if args.headroom:
            stage("headroom_1024", [(n, True, 1024) for n in (1024, 2048, 4096)], 3)
        elif args.verify:
            assert args.backlog
            stage("production_verify", [(args.backlog, True, n) for n in (256, 512, 1024)], 3)
        else:
            stage("screen_256", [(n, drain, 256) for n in (16, 64, 128, 256, 512, 1024)
                                  for drain in (False, True)], 2)
            stage("larger_bursts", [(n, True, clients) for n in (128, 256, 512, 1024)
                                    for clients in (512, 1024)], 3)
    print("DONE " + str(OUT / "report.md"), flush=True)


if __name__ == "__main__":
    main()
