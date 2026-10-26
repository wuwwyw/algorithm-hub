#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <mysql/mysql.h>

//AI完成

enum class FetchResult {
    SUCCESS,   // 成功拿到一条记录
    NO_DATA,   // 查询成功，但没有记录
    ERROR      // 数据库/绑定等错误
};

// =========================
// 特殊类型
// =========================

// 二进制数据，对应 MySQL BLOB
using Binary = std::vector<std::uint8_t>;

// DECIMAL 没有对应的 C++ 原生精确类型
// 使用字符串保存，避免 double 带来的精度损失
struct Decimal {
    std::string value;
};

// MySQL 日期时间类型
struct Date {
    MYSQL_TIME value{};
};

struct Time {
    MYSQL_TIME value{};
};

struct DateTime {
    MYSQL_TIME value{};
};

struct Timestamp {
    MYSQL_TIME value{};
};

// 表示 SQL NULL
struct Null {};

// =========================
// Stmt
// =========================
class Stmt {
public:
    explicit Stmt(MYSQL* mysql) {
        if (mysql == nullptr) {
            throw std::invalid_argument("mysql connection is null");
        }
        _stmt = mysql_stmt_init(mysql);
        if (_stmt == nullptr) {
            throw std::runtime_error("mysql_stmt_init failed");
        }
    }

    ~Stmt() {
        if (_stmt != nullptr) {
            mysql_stmt_close(_stmt);
        }
    }

    // 禁止拷贝
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    // 当前类内部保存了外部结果变量的引用
    // 因此禁止移动，避免移动后保存的 this 指针失效
    Stmt(Stmt&&) = delete;
    Stmt& operator=(Stmt&&) = delete;

    // =========================
    // 准备 SQL
    // =========================
    bool prepare(const std::string& sql) {
        return mysql_stmt_prepare(
            _stmt,
            sql.c_str(),
            static_cast<unsigned long>(sql.size())
        ) == 0;
    }

    // =========================
    // 统一绑定 SQL 参数
    // =========================
    template <typename... Args>
    bool bind(Args&... args) {
        constexpr std::size_t count = sizeof...(Args);

        // 参数数量必须和 SQL 中 ? 的数量一致
        if (mysql_stmt_param_count(_stmt) != count) {
            return false;
        }

        // 一次性分配，避免后续扩容导致 MYSQL_BIND 内部指针失效
        _bind.assign(count, {});
        _length.assign(count, 0);

        std::size_t index = 0;

        // 展开参数包
        (bind_one(index++, args), ...);

        return mysql_stmt_bind_param(
            _stmt,
            _bind.data()
        ) == 0;
    }

    // =========================
    // 运行时数量的同类型参数绑定
    // 适用于 WHERE id IN (?, ?, ...) 这类占位符数量由运行时数据决定的 SQL。
    // values 必须在 execute() 完成前保持有效且不发生扩容。
    // =========================
    template <typename T>
    bool bind_many(std::vector<T>& values) {
        const std::size_t count = values.size();

        if (mysql_stmt_param_count(_stmt) != count) {
            return false;
        }

        _bind.assign(count, {});
        _length.assign(count, 0);

        std::size_t index = 0;
        for (T& value : values) {
            bind_one(index++, value);
        }

        // 没有占位符时无需调用 mysql_stmt_bind_param。
        if (count == 0) {
            return true;
        }

        return mysql_stmt_bind_param(
            _stmt,
            _bind.data()
        ) == 0;
    }

    // =========================
    // 生成 IN (?, ?, ...) 所需的占位符列表
    // count 为 0 时返回空字符串；构造 IN 条件前应先拒绝空列表。
    // =========================
    static std::string make_placeholders(std::size_t count) {
        std::string placeholders;

        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                placeholders += ',';
            }
            placeholders += '?';
        }

        return placeholders;
    }

    // =========================
    // 执行 SQL
    // =========================
    bool execute() {
        return mysql_stmt_execute(_stmt) == 0;
    }

    // =========================
    // 获取原生 MYSQL_STMT
    // =========================
    MYSQL_STMT* get() {
        return _stmt;
    }

    // =========================
    // 获取错误信息
    // =========================
    const char* error() const {
        return mysql_stmt_error(_stmt);
    }

    // =========================
    // 获取错误码
    // =========================
    unsigned int error_code() const {
        return mysql_stmt_errno(_stmt);
    }

    // =========================
    // 绑定查询结果
    // =========================
    template <typename... Args>
    bool bind_result(Args&... args) {
        constexpr std::size_t count = sizeof...(Args);

        // 查询结果列数必须和绑定变量数量一致
        if (mysql_stmt_field_count(_stmt) != count) {
            return false;
        }

        // 分配结果绑定数组
        _result_bind.assign(count, {});

        // 保存字符串实际长度
        _result_length.assign(count, 0);

        // 保存 SQL NULL 状态
        _result_is_null = std::make_unique<bool[]>(count);

        for (std::size_t i = 0; i < count; ++i) {
            _result_is_null[i] = false;
        }

        // 保存每个结果对应的后处理函数
        _result_finishers.clear();
        _result_finishers.reserve(count);

        std::size_t index = 0;

        // 第一步：设置 MYSQL_BIND
        (bind_result_one(index++, args), ...);

        // 第二步：保存结果变量对应的后处理函数
        index = 0;
        (register_result_finisher(index++, args), ...);

        // 把结果绑定到 MYSQL_STMT
        if (mysql_stmt_bind_result(
                _stmt,
                _result_bind.data()) != 0) {
            return false;
        }

        // 将结果集存储到客户端
        return mysql_stmt_store_result(_stmt) == 0;
    }

    // =========================
    // 获取下一行
    // =========================
    // 每调用一次读取一行
    // 没有更多数据时返回 false
    FetchResult fetch() {
        const int rc = mysql_stmt_fetch(_stmt);

        // 没有更多数据
        if (rc == MYSQL_NO_DATA) {
            return FetchResult::NO_DATA;
        }

        // 除 MYSQL_DATA_TRUNCATED 外的错误
        if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
            return FetchResult::ERROR;
        }

        if (!finish_results()) {
            return FetchResult::ERROR;
        }

        // 对需要额外处理的结果进行后处理
        return FetchResult::SUCCESS;
    }

private:
    // =========================
    // 整数参数
    // =========================
    template <typename T>
    void bind_integer(
        std::size_t index,
        T& value
    ) {
        using U = std::remove_cv_t<T>;
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer = &value;
        bind.is_unsigned = std::is_unsigned_v<U>;

        if constexpr (sizeof(U) == 1) {
            bind.buffer_type = MYSQL_TYPE_TINY;
        }
        else if constexpr (sizeof(U) == 2) {
            bind.buffer_type = MYSQL_TYPE_SHORT;
        }
        else if constexpr (sizeof(U) == 4) {
            bind.buffer_type = MYSQL_TYPE_LONG;
        }
        else if constexpr (sizeof(U) == 8) {
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
        }
        else {
            static_assert(
                sizeof(U) == 0,
                "Unsupported integral type size"
            );
        }
    }

    // =========================
    // 整数参数统一入口
    // =========================
    template <typename T>
    std::enable_if_t<
        std::is_integral_v<std::remove_cv_t<T>>,
        void
    >
    bind_one(
        std::size_t index,
        T& value
    ) {
        bind_integer(index, value);
    }

    // =========================
    // float 参数
    // =========================
    void bind_one(
        std::size_t index,
        float& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_FLOAT;
        bind.buffer = &value;
    }

    // =========================
    // double 参数
    // =========================
    void bind_one(
        std::size_t index,
        double& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_DOUBLE;
        bind.buffer = &value;
    }

    // =========================
    // string 参数
    // =========================
    void bind_one(
        std::size_t index,
        std::string& value
    ) {
        bind_one(
            index,
            static_cast<const std::string&>(value)
        );
    }

    // const string 参数
    void bind_one(
        std::size_t index,
        const std::string& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = const_cast<char*>(value.data());
        bind.buffer_length =
            static_cast<unsigned long>(value.size());

        _length[index] =
            static_cast<unsigned long>(value.size());

        bind.length = &_length[index];
    }

    // =========================
    // Binary 参数
    // =========================
    void bind_one(
        std::size_t index,
        Binary& value
    ) {
        bind_one(
            index,
            static_cast<const Binary&>(value)
        );
    }

    // const Binary 参数
    void bind_one(
        std::size_t index,
        const Binary& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_BLOB;
        bind.buffer = const_cast<std::uint8_t*>(value.data());
        bind.buffer_length =
            static_cast<unsigned long>(value.size());

        _length[index] =
            static_cast<unsigned long>(value.size());

        bind.length = &_length[index];
    }

    // =========================
    // Decimal 参数
    // =========================
    void bind_one(
        std::size_t index,
        Decimal& value
    ) {
        bind_one(index, value.value);
    }

    // =========================
    // Date 参数
    // =========================
    void bind_one(
        std::size_t index,
        Date& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_DATE;
        bind.buffer = &value.value;
    }

    // =========================
    // Time 参数
    // =========================
    void bind_one(
        std::size_t index,
        Time& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_TIME;
        bind.buffer = &value.value;
    }

    // =========================
    // DateTime 参数
    // =========================
    void bind_one(
        std::size_t index,
        DateTime& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_DATETIME;
        bind.buffer = &value.value;
    }

    // =========================
    // Timestamp 参数
    // =========================
    void bind_one(
        std::size_t index,
        Timestamp& value
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_TIMESTAMP;
        bind.buffer = &value.value;
    }

    // =========================
    // SQL NULL 参数
    // =========================
    void bind_one(
        std::size_t index,
        Null&
    ) {
        MYSQL_BIND& bind = _bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_NULL;
    }

    // =========================
    // 整数结果
    // =========================
    template <typename T>
    std::enable_if_t<
        std::is_integral_v<std::remove_cv_t<T>>,
        void
    >
    bind_result_one(
        std::size_t index,
        T& value
    ) {
        using U = std::remove_cv_t<T>;
        MYSQL_BIND& bind = _result_bind[index];

        bind = {};
        bind.buffer = &value;
        bind.is_unsigned = std::is_unsigned_v<U>;

        if constexpr (sizeof(U) == 1) {
            bind.buffer_type = MYSQL_TYPE_TINY;
        }
        else if constexpr (sizeof(U) == 2) {
            bind.buffer_type = MYSQL_TYPE_SHORT;
        }
        else if constexpr (sizeof(U) == 4) {
            bind.buffer_type = MYSQL_TYPE_LONG;
        }
        else if constexpr (sizeof(U) == 8) {
            bind.buffer_type = MYSQL_TYPE_LONGLONG;
        }
        else {
            static_assert(
                sizeof(U) == 0,
                "Unsupported integral result type size"
            );
        }
    }

    // =========================
    // float 结果
    // =========================
    void bind_result_one(
        std::size_t index,
        float& value
    ) {
        MYSQL_BIND& bind = _result_bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_FLOAT;
        bind.buffer = &value;
    }

    // =========================
    // double 结果
    // =========================
    void bind_result_one(
        std::size_t index,
        double& value
    ) {
        MYSQL_BIND& bind = _result_bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_DOUBLE;
        bind.buffer = &value;
    }

    // =========================
    // string 结果
    // =========================
    // 第一次 fetch 时不直接分配 string buffer
    // 先获取字符串真实长度
    // 然后在 finish_result 中读取完整数据
    void bind_result_one(
        std::size_t index,
        std::string&
    ) {
        MYSQL_BIND& bind = _result_bind[index];

        bind = {};
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = nullptr;
        bind.buffer_length = 0;

        bind.length = &_result_length[index];
        bind.is_null = &_result_is_null[index];
    }

    // =========================
    // 注册结果后处理函数
    // =========================
    template <typename T>
    void register_result_finisher(
        std::size_t index,
        T& value
    ) {
        _result_finishers.emplace_back(
            [this, index, &value]() -> bool {
                return finish_result(index, value);
            }
        );
    }

    // =========================
    // 普通结果类型后处理
    // =========================
    template <typename T>
    std::enable_if_t<
        !std::is_same_v<
            std::remove_cv_t<T>,
            std::string
        >,
        bool
    >
    finish_result(
        std::size_t,
        T&
    ) {
        // 整数、float、double 等类型
        // mysql_stmt_fetch 已经直接写入变量
        return true;
    }

    // =========================
    // string 结果后处理
    // =========================
    bool finish_result(
        std::size_t index,
        std::string& value
    ) {
        // SQL NULL 按空字符串处理
        if (_result_is_null[index]) {
            value.clear();
            return true;
        }

        const unsigned long length =
            _result_length[index];

        // 根据数据库实际长度调整 string
        value.resize(length);

        // 空字符串无需再次读取
        if (length == 0) {
            return true;
        }

        MYSQL_BIND& bind = _result_bind[index];

        bind.buffer = value.data();
        bind.buffer_length = length;

        // 获取完整字符串内容
        return mysql_stmt_fetch_column(
            _stmt,
            &bind,
            index,
            0
        ) == 0;
    }

    // =========================
    // 执行所有结果后处理
    // =========================
    bool finish_results() {
        for (auto& finisher : _result_finishers) {
            if (!finisher()) {
                return false;
            }
        }

        return true;
    }

private:
    // MySQL 预处理语句对象
    MYSQL_STMT* _stmt{};
    // SQL 参数绑定
    std::vector<MYSQL_BIND> _bind;
    // SQL 参数长度
    std::vector<unsigned long> _length;
    // 查询结果绑定
    std::vector<MYSQL_BIND> _result_bind;
    // 查询结果字符串实际长度
    std::vector<unsigned long> _result_length;
    // 查询结果是否为 SQL NULL
    std::unique_ptr<bool[]> _result_is_null;
    // 每个结果变量对应的后处理函数
    std::vector<std::function<bool()>> _result_finishers;
};
