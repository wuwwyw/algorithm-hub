# 列表分页

列表使用独立的数据库分页查询，旧列表、详情和导出接口保持不变。只读 POST 接口：

- `/api/boards/page`：公共模板。
- `/api/my/boards/page`：当前登录用户的模板，沿用 session 鉴权。

请求 JSON 包含 `page`、`pageSize`、`keyword`、`category`、`exactCategory`、
`language`、`difficulty`、`sort`，筛选项可省略。页码从 1 开始，每页默认 15 条、最多 100 条。
`sort` 支持 `newest`、`title`、`difficulty`。

`Board_page.hpp` 在数据库中执行筛选、搜索、排序，并通过 `LIMIT/OFFSET` 只读取当前页。
搜索覆盖完整的公共模板库或当前用户的个人库，包括代码内容。搜索结果翻页继续携带
相同搜索条件，不会先把所有匹配板子读到 C++ 再切片。
响应包含 `items`、`page`、`pageSize`、`total`、`totalPages`、`allTotal`、
`categories`、`languageCount`。空结果保留第 1 页，越界页码回退至最后一页。
统计和分类选项来自数据库汇总查询，`total` 来自带相同筛选条件的 `COUNT(*)`。
分类别名通过汇总所得的分类值映射，随后作为绑定参数加入页查询，保留旧分类兼容。

每次请求在同一个只读一致性快照内查询分类/语言汇总、匹配总数和当前页数据，避免
并发增删导致本次请求内的页码与数量不一致。仅当前页的数据查询包含标题、简介和代码。
搜索、统计、排序及 OFFSET 跳过记录时，数据库仍可能扫描多条记录；这不等于把完整
板子结果传给应用。旧 GET 列表接口仍可用；导出窗口打开时才请求完整个人列表。

需要 MySQL 8.0，名称排序使用 `utf8mb4_zh_0900_as_cs`，同名时按 ID 降序稳定排序。
ICU 用于分类兼容、分类选项排序和搜索词大小写处理。
Ubuntu 可安装 `libicu-dev` 后正常运行 CMake。当前环境无免密 sudo，验证时只将
该包解压到 `build/icu-dev`，并通过 CMake 的 ICU 路径选项使用系统现有 ICU 70 运行库。

回归测试：`tools/test_board_page.cpp` 使用连接专属的临时表验证数据库分页、全库搜索、
搜索结果翻页、个人数据隔离、通配字符的字面匹配、分类兼容、中文排序、空结果、
参数校验和删除后回退，不修改真实板子数据。运行时通过 `PAGINATION_DB_HOST`、
`PAGINATION_DB_USER`、`PAGINATION_DB_PASSWORD`、`PAGINATION_DB_NAME` 环境变量提供
测试连接。`tools/test_pagination_frontend.js` 验证两个页面的
请求、翻页、搜索结果分页、请求竞态和导出列表隔离。
