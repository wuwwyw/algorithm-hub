# 同一份源码用于本地与云端

源码保持一份，每台机器只维护自己的 `.env`，不要把本地 `.env` 上传到云端。
配置不需要重新编译；修改后重新启动程序即可。

## 配置读取顺序

1. 进程环境变量（优先级最高，包括设置为空字符串的情况）。
2. `ALGORITHM_CONFIG_FILE` 指定的文件；未指定时读取构建源码目录下的 `.env`。
3. 非敏感默认值：监听127.0.0.1:8080、MySQL 127.0.0.1:3306、
   业务账号algorithm_app、业务库algorithm_hub、Redis 127.0.0.1:6379。

数据库密码没有默认值，缺失时直接报错退出。文件存在却无法读取、端口不合法也会报错。
`--check-config` 只检查配置，不验证数据库连通性，不显示密码。
旧的 `algorithm_hub IPv4 port` 启动形式仍可使用，参数覆盖监听地址和端口。

`.env` 支持空行、整行 `#` 注释、`KEY=VALUE` 和成对单/双引号。
值按字面读取，不执行命令、不展开 `$VARIABLE`、不处理反斜杠转义，也不支持行尾注释。
例如密码中的 `#`、`$`、`!` 均保留；如需保留首尾空格，使用成对引号。
不要使用 `source .env`，它不是shell脚本。

## 本地使用（WSL）

这次迁移已将原有本地数据库连接设置放进权限600的 `.env`，没有修改本地账号密码。
以后新的开发机器先复制 `.env.example` 并填写本机凭据。

```sh
cmake --build build -j2
bash tools/run_server.sh --check-config
bash tools/run_server.sh
```

脚本从任意工作目录启动均可，默认使用项目根目录的 `.env`；
优先选择 `build-cloud/algorithm_hub`，没有时选择 `build/algorithm_hub`。
可以用 `ALGORITHM_BINARY=/绝对路径/algorithm_hub` 明确选择可执行文件。

如果直接运行可执行文件，默认配置文件路径来自构建时的源码目录。
如果移动的是可执行文件而不是重新编译源码，请用 `ALGORITHM_CONFIG_FILE` 显式指定配置文件。

## 打包上传

```sh
python3 tools/package_source.py
scp output/algorithm-source.tar.gz ubuntu@云服务器公网IP:/home/ubuntu/
```

上传包按白名单包含源码、网页、字体、工具和 `.env.example`，
不包含本机 `.env`、数据库备份、旧二进制、旧压测副本或构建缓存。
`.gitignore` 只影响Git，不能保护直接复制整个项目目录；请使用这个上传包。

## 云服务器首次配置

先完成数据库迁移，并创建只拥有业务库读写权限的 `algorithm_app` 账号。
MySQL与Redis应仅供本机或受控私网访问，不需要开放3306/6379的公网入口。

在云端 `/home/ubuntu` 解压，覆盖的是代码，不会覆盖已有的 `.env`：

```sh
cd /home/ubuntu
tar -xzf algorithm-source.tar.gz
cd /home/ubuntu/algorithm
```

首次使用才复制模板；如果已有 `.env`，直接编辑保留的文件：

```sh
test -f .env || (umask 077; cp .env.example .env)
chmod 600 .env
nano .env
```

主要填写：

```text
ALGORITHM_DB_USER=algorithm_app
ALGORITHM_DB_PASSWORD=云服务器上生成的业务账号密码
```

数据库与Redis同机运行时，HOST保持127.0.0.1即可。
数据库名仍为algorithm_hub；需要其他地址、端口或账号时修改相应配置项。
不要复用本机高权限账号，不要把真实密码发到聊天或写进源码。

依赖已安装后构建、启动：

```sh
cmake -S . -B build-cloud -DCMAKE_BUILD_TYPE=Release
cmake --build build-cloud -j2
bash tools/run_server.sh --check-config
bash tools/run_server.sh
```

全新构建会自动使用本机源码目录下的网页、Logo与三种字体，无需填写本机绝对路径。
不要从另一台机器复制CMake缓存。已有构建目录中显式设置过的字体路径仍以缓存值为准。

另开终端验证首页与数据库：

```sh
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/
curl -s http://127.0.0.1:8080/api/boards/page \
  -H 'Content-Type: application/json' -d '{"page":1,"pageSize":1}'
```

前者应返回200，后者应返回分页JSON。监听127.0.0.1适合接在HTTPS反向代理后面。
HTTPS与云数据库权限需要单独配置；后台运行和开机启动见下一节。

## 关闭Xshell后继续运行：systemd

`deploy/algorithm-hub.service` 为云端 ubuntu 用户及 `/home/ubuntu/algorithm` 准备。
它读取现有 `.env`，不会读取交互终端里临时export的变量，不在服务文件中保存密码。
配置前先用 `bash tools/run_server.sh --check-config` 检查 `.env`。
若使用公网直连8080，确认 `.env` 的 ALGORITHM_HOST=0.0.0.0。

先在手动运行网站的窗口按Ctrl+C，释放8080端口，然后在云服务器执行：

```sh
sudo install -m 644 /home/ubuntu/algorithm/deploy/algorithm-hub.service /etc/systemd/system/algorithm-hub.service
sudo systemctl daemon-reload
sudo systemctl enable --now algorithm-hub
sudo systemctl status algorithm-hub --no-pager
```

显示active (running)后，验证首页返回200：

```sh
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/
```

此后关闭Xshell、关闭个人电脑不影响云端进程；服务异常退出后会重试启动，
云服务器开机也会启动服务。云主机本身仍需保持运行，MySQL与Redis也需可用。
此配置不等同于应用已实现有活动请求时的优雅退出。

常用命令：

```sh
sudo systemctl restart algorithm-hub
sudo systemctl stop algorithm-hub
sudo journalctl -u algorithm-hub -n 50 --no-pager
```

修改 `.env` 或更新可执行文件后执行restart即可；修改service文件后需要重新安装并daemon-reload。
如果启动失败，先查看日志，尤其检查配置文件权限、数据库连接和8080是否被旧进程占用。
服务以ubuntu用户运行；不要用sudo把应用直接作为root运行。

## Redis认证（可选）

- 无认证的本机Redis：USER与PASSWORD均留空，保持原有本地行为。
- 密码认证：仅填写 `ALGORITHM_REDIS_PASSWORD`。
- Redis ACL：同时填写 `ALGORITHM_REDIS_USER` 与 `ALGORITHM_REDIS_PASSWORD`。

客户端会在每次初始连接和重连时认证。应先准备好客户端配置，再协调Redis端ACL变更；
本改动不修改运行中Redis的密码或权限。GET查询使用固定格式和二进制安全参数，
避免Cookie里的格式符被解释为变参。

## 验证命令

```sh
g++ -std=c++17 tools/test_server_config.cpp -o build/test_server_config
build/test_server_config
python3 tools/test_multi_epoll.py
g++ -std=c++17 tools/test_redis_auth.cpp $(pkg-config --cflags --libs hiredis) -o build/test_redis_auth
python3 tools/test_redis_auth.py
```

认证测试使用临时Redis实例，不修改业务Redis。
如果数据库连接失败，先检查服务、配置、账号授权与导入数据，而不是把密码写回源码。
