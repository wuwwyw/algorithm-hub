# Algorithm Hub

基于 C++17 的 Linux HTTP 服务端，用于管理和导出算法模板。
项目围绕多 Reactor 网络架构、HTTP 请求处理、数据库访问和连接生命周期管理展开。

## 项目功能

- 公共算法板子的浏览与详情查看
- 用户注册、登录及 Redis 会话管理
- 个人算法板子的管理
- 基于 MySQL 的业务数据持久化
- 算法代码及相关内容导出为 PDF
- HTML、CSS 编写的网页界面
- 网络服务、配置、部署及 PDF 导出相关测试工具

## 技术栈

| 领域 | 技术 |
| --- | --- |
| 编程语言 | C++17 |
| 网络编程 | Linux Socket、非阻塞 I/O、epoll |
| 并发架构 | 多 Reactor、工作线程池、eventfd |
| 连接管理 | shared_ptr、原子状态、timerfd |
| HTTP 服务 | HTTP 解析、响应生成、Trie 路由 |
| 关系型数据库 | MySQL、预处理语句、连接池 |
| 缓存与会话 | Redis、hiredis、连接池 |
| 安全与文本处理 | OpenSSL、ICU |
| PDF 导出 | libharu、TrueType 字体 |
| 构建系统 | CMake |
| 前端 | HTML、CSS、JavaScript |

## 服务端架构

### 1. 主线程与连接分发

主线程负责 accept 接收客户端连接，并通过 eventfd 通知对应的 Reactor 线程。
待分配的客户端文件描述符保存在受互斥锁保护的共享队列中，连接采用轮询方式分配。

### 2. 多 Reactor 线程

当前代码配置了 3 个 Reactor 线程。
每个 Event_loop 拥有独立的 epoll 实例，并使用 eventfd 接收连接分配通知。
Reactor 线程负责客户端 Socket 的非阻塞 I/O 和事件处理。

### 3. 工作线程池

多个 Reactor 共用工作线程池。
网络 I/O 与 HTTP 请求处理分离，工作线程负责请求解析、正文完整性检查、路由及业务逻辑处理。

### 4. 连接生命周期管理

通过 shared_ptr 管理客户端连接对象的共享生命周期。
使用 worker_busy 和 close_requested 等原子状态协调工作线程与 Reactor 线程，避免工作线程仍在处理连接时直接关闭对应资源。

### 5. 数据库与空闲连接管理

业务层通过 MySQL 和 Redis 连接池访问数据。
每个 Reactor 使用 timerfd 定期检查空闲连接，当前空闲超时时间为 60 秒。

当前代码中的连接池配置为 MySQL 12 个连接、Redis 8 个连接。

## 项目结构

```text
algorithm/
├── src/                  # 程序入口
├── net/                  # Socket、epoll、HTTP、Reactor、线程池
├── sql/                  # MySQL 访问、业务处理、PDF 导出
├── redis/                # Redis 连接池与会话管理
├── web/
│   ├── html/             # 网页
│   └── css/              # 样式
├── assets/
│   ├── fonts/            # PDF 导出字体及字体许可证
│   └── images/           # 图片资源
├── tools/                # 测试、性能测试、部署与打包工具
├── deploy/               # systemd 服务配置模板
├── .env.example          # 环境配置模板
├── CMakeLists.txt
└── DEPLOY.md             # 部署说明
```

## 环境依赖

- Linux 环境
- 支持 C++17 的编译器
- CMake 3.22.1 或更高版本
- MySQL 客户端开发库
- hiredis
- OpenSSL
- ICU
- libharu

Ubuntu 环境可执行：

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    libicu-dev \
    libhpdf-dev
```

## 构建项目

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

## 配置与运行

复制配置模板：

```bash
cp .env.example .env
chmod 600 .env
```

编辑 `.env`，填写本地 MySQL、Redis 和服务监听配置。不要将真实密码、生产环境凭据或访问令牌提交到公开仓库。

```bash
nano .env
```

检查配置：

```bash
bash tools/run_server.sh --check-config
```

启动服务：

```bash
bash tools/run_server.sh
```

更多部署说明见 [DEPLOY.md](DEPLOY.md)。

注意：当前仓库尚未提供独立的数据库建表 SQL 脚本。首次部署前，需要根据项目实际使用的表结构准备数据库。

## 测试

`tools/` 目录包含网络服务、配置、分页、Redis、PDF 导出及部署打包相关的测试工具。具体依赖和运行方式以各测试脚本为准。

## 许可证

本项目暂未添加项目级开源许可证。

公开仓库用于项目展示，不代表授予通用的代码复制、修改和再分发许可。

`assets/fonts/` 中的字体资源附有 `OFL.txt`，其许可范围仅适用于相应字体软件，不代表本项目源码采用相同许可证。
