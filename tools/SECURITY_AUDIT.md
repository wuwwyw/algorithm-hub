# 连接凭据与登录安全检查

检查日期：2026-10-23。范围为当前工作区源码、本地 MySQL/Redis 的只读配置与权限检查、
独立本地 HTTP 实例的低风险探测。未修改服务代码、密码、ACL、账号或业务数据。
未打印任何密码、实际会话值或用户口令哈希。

后续配置改造已移除服务源码的数据库密码，将本地凭据放入权限600的 `.env`，
并提供不含该文件与旧构建产物的源码上传包。Redis GET格式字符串问题也已修复，
客户端增加了可选认证及重连认证。以下发现保留为原始检查记录；数据库账号权限、
实际Redis ACL、HTTPS和口令哈希等仍需分别处理，旧二进制中的原凭据也不会自动消失。

## 优先处理

### 1. 高：有效的数据库密码硬编码，编译产物也含明文

位置：`net/Tcpserver.hpp:88`。

本地只读连接确认该密码仍可登录。精确匹配扫描在8个位置发现同一凭据，
包括源码、可执行文件、目标文件以及之前生成的压测源码副本。
这意味着只隐藏源文件并不能防止从二进制中获取该字符串。
没有发现可用的 Git 历史，无法判断是否曾提交或对外分享。

建议：创建独立、最小权限的业务账号；通过受控的外部配置注入凭据，
避免命令行参数、日志和源码；切换应用后轮换旧密码。
清理或重建旧二进制和压测副本；若有历史提交或外发文件，按已泄露凭据处理。
本次未擅自轮换密码，以免中断当前连接和其他工具。

### 2. 高：业务 MySQL 账号有全局管理权限，3306监听全部IPv4接口

实际 `SHOW GRANTS` 显示权限范围为 `*.*`，包含 CREATE USER、FILE、SUPER、
SHUTDOWN，以及多项动态管理权限；不是仅有当前业务库的读写权限。
监听与服务变量确认3306绑定 `0.0.0.0`，`require_secure_transport=OFF`。
应用源码也没有显式要求TLS或证书校验。

建议：运行账号限制为业务所需表的 SELECT/INSERT/UPDATE/DELETE；
建表和迁移使用另一个账号。若数据库仅供同机服务使用，应限制为回环接口，
并核查宿主机和部署环境的防火墙。远程连接应要求TLS和服务端身份校验。
没有从外网扫描，因此不能把“绑定所有接口”直接等同于“公网可访问”。
没有验证实际 C++ MySQL 会话的协商密码套件；“未强制加密”不等于每次连接都明文。

### 3. 高：Cookie数据进入 hiredis 格式字符串，可触发内存错误

位置：`redis/Session_manager.hpp:36` → `redis/RedisConnection.hpp:38–39`。

当前将不受信任的Cookie拼进 `GET` 命令，再调用 `redisCommand(ctx, cmd.c_str())`。
hiredis 的第二个参数是printf风格的格式字符串。包含格式符的数据可能消耗并不存在的变参。
独立只读探针中，普通键正常结束，包含 `%s` 的查询触发 AddressSanitizer 内存错误，退出码1。
探针禁用了核心转储，没有输出Redis值；HTTP服务进程未被用于破坏性复现。
已证明内存安全风险，未证明远程代码执行，也没有验证所有非sanitizer构建的表现。

建议使用固定格式字符串，数据作为参数，例如：

```cpp
redisCommand(_ctx, "GET %b", key.data(), key.size());
```

同时要求会话ID恰好是64个十六进制字符；校验不能替代正确的命令参数化。
参考：[hiredis Sending commands](https://github.com/redis/hiredis#sending-commands)。

### 4. 中：Redis无密码、默认用户可访问所有键并执行全部命令

只读验证：不带认证的PING成功；默认用户flags包含 `nopass`，命令范围 `+@all`，键范围 `*`。
当前绑定 `127.0.0.1 ::1` 且 protected-mode=yes，是已有的边界保护，未观察到公网监听。
但能够访问本机Redis的其他进程具有读取、伪造会话等能力；本次未执行这类写操作。

建议使用专用Redis ACL用户，键限制为 `session:*`，仅允许应用需要的 PING/GET/SET/DEL。
先实现应用连接后的AUTH及重连认证，再启用ACL，避免直接改配置造成服务不可用。
如跨主机访问，再配合私网限制和TLS。

### 5. 部署前必须处理：HTTP明文入口与Cookie缺少Secure

位置：`redis/Session_manager.hpp:58–63`、`net/IO.hpp`。

独立实例以普通HTTP请求返回200；当前服务本身没有TLS。
会话Cookie带 HttpOnly 和 SameSite=Lax，但没有Secure。
若客户端直接通过HTTP登录，用户名、密码和会话可能被链路上的观察者获取。
未检查项目外的反向代理、证书或公网入口，不能排除部署时有外层HTTPS。

建议由可信反向代理提供HTTPS，并限制后端监听范围；生产模式为会话Cookie添加Secure。
不要只添加Secure而仍让用户通过HTTP登录；开发环境和生产环境应有明确配置区分。
参考：[OWASP Session Management](https://cheatsheetseries.owasp.org/cheatsheets/Session_Management_Cheat_Sheet.html)。

### 6. 中：用户口令保护参数与登录防护有提升空间

位置：`sql/Passwd.hpp:15`、`sql/Api_handle.hpp:830`、`net/ThreadPool.hpp:48`。

采用PBKDF2-HMAC-SHA256、随机16字节盐和32字节摘要，方向正确。
实际只做数据库聚合检查：3条账号记录均标记为210000轮PBKDF2，没有读取单条口令哈希。
当前OWASP建议该算法使用600000轮；也可评估Argon2id。
口令最低长度仅6字节，源码未发现登录尝试限速、账号/IP退避或任务队列上限。
账号不存在时跳过哈希校验，还可能形成用户名存在性的时间差。

建议先加登录/注册限速和队列背压，再标定更强哈希参数。
旧哈希在成功登录时渐进重算，避免直接使已有账号无法登录。
未对真实用户做穷举或撞库；没有声称已实测耗尽线程池。
参考：[OWASP Password Storage](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html)。

## 已验证的正向结果

- 密码有随机盐和慢哈希，未见业务逻辑直接存储明文用户口令。
- 会话ID由 `RAND_bytes` 生成32字节随机数，以64个十六进制字符表示。
- 登录查询使用预处理和参数绑定；带SQL元字符的无效登录返回401，没有绕过本次检查。
- 未登录访问 `/api/me` 和 `/api/my/boards` 返回401。
- 无效登录JSON返回400。
- `/.env`、`/.git/config`、`/net/Tcpserver.hpp`、路径穿越形式的源码路径均返回404。
- 检查后独立HTTP服务仍正常，测试进程已清理。

这些结果不能证明所有接口都不存在SQL注入、越权或文件泄露。

## 修复顺序与复现

优先修复Redis格式字符串问题，随后准备最小权限账号和外部凭据配置，再协调密码轮换。
然后完善Redis ACL、HTTPS与Cookie配置，最后升级口令哈希和限速。
没有把“从源码删除密码”当作密码轮换，也没有自动改变服务账号权限。

在WSL项目目录：

```sh
python3 tools/security_check.py
cmake --build build -j2
g++ -std=c++17 -g -fsanitize=address,undefined tools/test_redis_format.cpp $(pkg-config --cflags --libs hiredis) -o build/security_audit/test_redis_format
python3 tools/security_http_checks.py
```

探针仅用于本地开发Redis，不应用于不属于自己的服务。
原始结果位于 `build/security_audit/connection_checks.json` 和 `http_checks.json`，均不含密码原文。
本次未覆盖依赖CVE扫描、Git历史/远端仓库、外部反代配置、跨用户完整越权测试和全面HTTP解析安全审计。
