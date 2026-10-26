"""Summarize saved benchmark trials without rerunning the load."""
import collections
import json
from pathlib import Path
import statistics

root = Path(__file__).resolve().parents[1]
out = root / "build/epoll_bench"
rows = json.loads((out / "results.json").read_text())
groups = collections.defaultdict(list)
for row in rows:
    groups[(row["stage"], tuple(row["config"]))].append(row)

lines = [
    "# 多 epoll 本地对比压测", "",
    "环境：WSL Ubuntu 22.04，Intel i5-1334U，12 个可见逻辑 CPU，约 15.5 GiB 内存。",
    "客户端、服务端、MySQL 和 Redis 在同一台机器，共享 CPU；结果不等同于独立压测机得到的部署容量。", "",
    "服务端使用独立源码副本，以 -O2 -DNDEBUG 编译。仅在副本中将线程/池参数改为启动时读取，"
    "并用可稳定引用元素的 deque 存放动态数量的锁；当前项目参数和业务实现未修改。", "",
    "配置顺序：I/O 循环 / 业务线程 / MySQL 连接 / Redis 连接。",
    "四个独立客户端进程，HTTP 长连接，每个连接最多一个在途请求，不使用流水线。",
    "常规筛选每配置两轮，每轮预热后统计 5 秒、64 并发；最终混合复核每轮 15 秒、128 并发、三轮；"
    "最终数据库复核每轮 10 秒、128 并发、两轮。每阶段顺序打乱。", "",
    "补充验证：固定 24 个业务线程、12/8 条数据库连接，对 2/3/4 个 I/O 循环各测两轮，"
    "每轮 10 秒、128 并发；原配置与候选配置各做一次 256 并发、30 秒持续测试。"
    "Redis 命令计数补测为 64 并发、5 秒。之后补测集中建连的超时阶段、逐步建连后的"
    "256 并发（每配置两轮、每轮15秒），以及一次5秒的监听队列计数检查。", "",
    "工作负载：static 为 GET /；db 为 POST /api/boards/page，正文 {}；redis 为 GET /api/me，"
    "携带不存在的会话，预期 401 且实际触发 Redis 查询；mixed 按每连接请求序列约 50% static / 30% db / 20% redis。",
    "未创建账号、会话或修改业务数据。只有预期状态码的完整响应计入吞吐。", "",
    "RPS 是每轮有效响应数除以统计时长；下表取各轮中位数。P95/P99 是各轮分位数的中位数，"
    "并非合并样本的分位数。CPU 的 100% 表示占用一个逻辑核，进程 CPU 可能超过 100%。", "",
    "| 阶段 | 配置 | 轮数 | RPS中位数 | RPS范围 | P95毫秒 | P99毫秒 | 服务CPU% | MySQL CPU% | 客户端CPU% | RSS MiB | 错误 |",
    "|---|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|",
]
summary = []
for (stage, config), samples in groups.items():
    def median(key):
        return round(statistics.median(s[key] for s in samples), 2)
    errors = sum(sum(s["errors"].values()) for s in samples)
    result = dict(stage=stage, config=config, trials=len(samples), rps=median("rps"),
                  p95_ms=median("p95_ms"), p99_ms=median("p99_ms"), errors=errors)
    summary.append(result)
    lines.append(f'| {stage} | {"/".join(map(str, config))} | {len(samples)} | {median("rps")} | '
                 f'{min(s["rps"] for s in samples)}–{max(s["rps"] for s in samples)} | '
                 f'{median("p95_ms")} | {median("p99_ms")} | {median("server_cpu_pct")} | '
                 f'{median("mysql_cpu_pct")} | {median("client_cpu_pct")} | {median("rss_mb")} | {errors} |')
lines += ["", "## 参数建议", "",
          "建议保留 I/O 循环 3、业务线程 16、MySQL 连接 12、Redis 连接 8，作为当前机器上的默认配置。"
          "静态页面占比高时可以尝试 4 个 I/O 循环。保留已有 3 个循环，也能覆盖本次混合负载。",
          "24 个业务线程在部分 128 并发测试中有吞吐收益，但不是稳定优于 16："
          "逐步建连后的 256 并发复核中，原配置吞吐和尾延迟均好于 4/24/12/8 候选配置。"
          "128 并发纯数据库复核中，增加业务线程的吞吐收益较小，P99 反而变高。",
          "MySQL 12 与 16 条连接的吞吐基本持平；16 条在 64 并发筛选中尾延迟较好，"
          "若选择 16，应再结合目标负载验证。Redis 8 条是已测 1/2/4/8 档位中表现最好的，未测更大档位。",
          "混合负载下 2/3/4 个 I/O 循环没有呈现足够稳定、单调的提升；不建议依据 CPU 逻辑核数直接开满。",
          "各轮吞吐波动明显，候选与基线区间有重叠，结论是参数起点，不能称为统计显著的全局最优值。", "",
          "## 集中建连问题", "",
          "256 并发、30秒集中建连测试中，原配置记录8次未分类超时；带阶段记录的复测中，"
          "原配置记录13次 warmup 阶段超时，即连接建立后首个响应等待超时。"
          "逐步建连后的两轮256并发测试中，两组配置均未记录错误。",
          "最后一次集中建连的5秒诊断没有出现超时，但同机 TcpExt ListenOverflows 与 ListenDrops"
          "均增加630。这两个计数为系统范围，不是单进程计数；本轮集中建连负载与监听队列压力相符。"
          "该轮测试的原始源码快照使用 listen(fd, 16)，且每次就绪只 accept 一个连接。"
          "后续已单独验证并将正式配置改为 backlog 1024、批量 accept；"
          "新结果见 ../listen_bench/report.md，不要将此处原始快照配置视为当前配置。", "",
          "## 验证范围与限制", "",
          "空闲服务器构造/析构测试中，文件描述符从 5 回到 5，约 52 毫秒；网络功能回归通过。",
          "没有验证带活动请求的优雅退出；主服务的无限接收循环还没有正常停止入口，"
          "压测实例由测试脚本终止，不能把这个操作算作优雅退出验证。",
          "这是逐阶段筛选加候选复核，不是所有参数的笛卡尔积全局搜索。短时压测未覆盖登录、"
          "写操作、PDF 导出、真实网络延迟、持续数小时的负载和不同数据规模。",
          "闭环客户端会在响应慢时降低发压速率，因此延迟不能直接当作固定到达速率下的 SLA。",
          "尚未测连接池等待时间，不能仅由进程 CPU 推断具体等待来源。", "",
          "完整逐轮结果：results.json（含各 I/O 线程 CPU、错误、RSS、客户端 CPU）。"]
totals = sum(r["requests"] for r in rows)
errors = sum(sum(r["errors"].values()) for r in rows)
lines += ["", f"合计 {len(rows)} 轮、{totals:,} 个计入统计的有效响应、{errors} 个记录到的错误。"]
data_sizes = sorted({r["database_total"] for r in rows if "database_total" in r})
if data_sizes:
    lines += [f"补测读取到分页接口的 total={data_sizes}，该数据规模应作为解释容量的前提。"]
(out / "report.md").write_text("\n".join(lines) + "\n")
(out / "summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
