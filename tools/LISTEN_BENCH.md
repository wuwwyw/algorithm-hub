# listen 配置与验证

2026-10-22 本机 WSL 测试后，默认 backlog 从 16 改为 **1024**。
配置位置为 `net/Socket.hpp` 的 `Socket::DEFAULT_LISTEN_BACKLOG`；
也可以通过 `BindAndListen(port, ip, backlog)` 覆盖。
`TcpServer` 在监听就绪后循环 `accept` 到 `EAGAIN/EWOULDBLOCK`，
遇到 `EINTR` 或 `ECONNABORTED` 重试。轮询分发仍为3个I/O线程。
业务线程/MySQL连接/Redis连接仍为16/12/8。

对比覆盖 backlog 16/64/128/256/512/1024/2048/4096，固定其余配置。
每次由4个客户端进程集中建立连接，每连接请求一次首页并校验完整响应。

- backlog 512：1024条集中连接的三轮测试均有溢出，首响应P99中位数约1123毫秒。
- backlog 1024：相同三轮测试无溢出，首响应P99中位数约254毫秒。
- 另做1024/2048/4096的相同负载复核，均无溢出，更大的队列没有稳定的额外收益。
- 修改后的实际项目构建，在256/512/1024条集中连接下各测三轮，全部成功，无溢出。
- 原有长连接、流水线、分段请求体与1200次并发请求回归通过。
- 空闲析构约42毫秒，文件描述符从5回到5。

测试机器可见12个逻辑CPU，客户端与数据库共享机器。计数来自系统级
`TcpExt.ListenOverflows/ListenDrops`；没有修改全局网络参数。
1024连接测试将测试进程及子进程的文件描述符软限制提高到8192，
未修改用户终端默认限制。日常启动时仍需按目标在线连接数检查 `ulimit -n`。

`backlog` 是等待应用接收的连接队列长度，不是在线连接数上限。
实际值受本机 `net.core.somaxconn`（测试时4096）限制。
参考：[Linux listen 手册](https://man7.org/linux/man-pages/man2/listen.2.html)。

在WSL项目目录中复现：

```sh
python3 tools/bench_listen.py
python3 tools/bench_listen.py --skip-build --headroom
cmake --build build -j2
python3 tools/bench_listen.py --skip-build --verify build/algorithm_hub --backlog 1024
python3 tools/test_multi_epoll.py
```

完整逐轮数据与汇总位于 `build/listen_bench/results.json` 和 `report.md`。
脚本会创建独立测试实例并在每轮结束时清理，不修改业务数据。
