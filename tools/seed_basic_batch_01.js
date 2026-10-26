const common = 'using namespace std;\n\n';

const boards = [
    {
        title: '快速排序（三路划分）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '以 pivot 为界分为小于、等于和大于三段；大量重复元素时比普通双路划分更稳定。',
        code: common + `void quick_sort(vector<int>& a, int left, int right) {
    if (left >= right) return;
    int pivot = a[left + (right - left) / 2];
    int less = left, cur = left, greater = right;
    while (cur <= greater) {
        if (a[cur] < pivot) swap(a[less++], a[cur++]);
        else if (a[cur] > pivot) swap(a[cur], a[greater--]);
        else ++cur;
    }
    quick_sort(a, left, less - 1);
    quick_sort(a, greater + 1, right);
}`
    },
    {
        title: '归并排序（稳定排序）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '分治后合并两个有序区间；相等时优先取左侧元素，因此排序稳定。',
        code: common + `void merge_sort(vector<int>& a, int left, int right) {
    if (right - left <= 1) return;
    int mid = left + (right - left) / 2;
    merge_sort(a, left, mid);
    merge_sort(a, mid, right);
    vector<int> merged;
    int i = left, j = mid;
    while (i < mid || j < right) {
        if (j == right || (i < mid && a[i] <= a[j])) merged.push_back(a[i++]);
        else merged.push_back(a[j++]);
    }
    copy(merged.begin(), merged.end(), a.begin() + left);
}`
    },
    {
        title: '二分查找（第一个不小于目标值）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '在非降序数组中找第一个大于等于 target 的位置；若不存在，返回数组长度。',
        code: common + `int first_not_less(const vector<int>& a, int target) {
    int left = 0, right = static_cast<int>(a.size());
    while (left < right) {
        int mid = left + (right - left) / 2;
        if (a[mid] < target) left = mid + 1;
        else right = mid;
    }
    return left;
}`
    },
    {
        title: '有序数组两数之和（双指针）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '数组已排序时，两端指针向中间移动可在线性时间内寻找和为 target 的一对元素。',
        code: common + `optional<pair<int, int>> two_sum_sorted(const vector<long long>& a, long long target) {
    int left = 0, right = static_cast<int>(a.size()) - 1;
    while (left < right) {
        long long sum = a[left] + a[right];
        if (sum == target) return pair<int, int>{left, right};
        if (sum < target) ++left;
        else --right;
    }
    return nullopt;
}`
    },
    {
        title: '一维前缀和（区间和）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: 'prefix[i] 保存前 i 个元素的和，闭区间 [left, right] 的和可 O(1) 查询。',
        code: common + `struct PrefixSum {
    vector<long long> prefix;
    explicit PrefixSum(const vector<int>& a) : prefix(a.size() + 1, 0) {
        for (size_t i = 0; i < a.size(); ++i) prefix[i + 1] = prefix[i] + a[i];
    }
    long long query(int left, int right) const {
        return prefix[right + 1] - prefix[left];
    }
};`
    },
    {
        title: '二维前缀和（子矩阵求和）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '采用一位偏移的二维前缀和，闭矩形 [x1,y1] 到 [x2,y2] 可 O(1) 求和。',
        code: common + `struct PrefixSum2D {
    vector<vector<long long>> pre;
    explicit PrefixSum2D(const vector<vector<int>>& a) {
        int n = static_cast<int>(a.size());
        int m = n == 0 ? 0 : static_cast<int>(a[0].size());
        pre.assign(n + 1, vector<long long>(m + 1, 0));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < m; ++j)
                pre[i + 1][j + 1] = a[i][j] + pre[i][j + 1] + pre[i + 1][j] - pre[i][j];
    }
    long long query(int x1, int y1, int x2, int y2) const {
        return pre[x2 + 1][y2 + 1] - pre[x1][y2 + 1] - pre[x2 + 1][y1] + pre[x1][y1];
    }
};`
    },
    {
        title: '一维差分（区间加）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '对每个闭区间加法仅修改差分数组两处，最后一次前缀和还原结果。',
        code: common + `vector<long long> range_add(int n, const vector<array<int, 3>>& operations) {
    vector<long long> diff(n + 1, 0);
    for (const auto& op : operations) {
        int left = op[0], right = op[1], delta = op[2];
        diff[left] += delta;
        if (right + 1 < n) diff[right + 1] -= delta;
    }
    for (int i = 1; i < n; ++i) diff[i] += diff[i - 1];
    diff.pop_back();
    return diff;
}`
    },
    {
        title: '离散化（保序去重）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '把可能很大的数值映射到连续下标，且保持原数值的大小关系。',
        code: common + `vector<int> compress_coordinates(const vector<long long>& values) {
    vector<long long> order = values;
    sort(order.begin(), order.end());
    order.erase(unique(order.begin(), order.end()), order.end());
    vector<int> result;
    result.reserve(values.size());
    for (long long value : values) {
        result.push_back(static_cast<int>(lower_bound(order.begin(), order.end(), value) - order.begin()));
    }
    return result;
}`
    },
    {
        title: '枚举非空子集（位运算）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '通过 sub = (sub - 1) & mask 枚举 mask 的全部非空子集。',
        code: common + `vector<int> enumerate_nonempty_submasks(int mask) {
    vector<int> submasks;
    for (int sub = mask; sub != 0; sub = (sub - 1) & mask) {
        submasks.push_back(sub);
    }
    return submasks;
}`
    },
    {
        title: '二分答案（最小化最大分段和）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '元素非负时，二分允许的最大段和，再用贪心检查是否能分成不超过 parts 段。',
        code: common + `long long minimize_largest_segment_sum(const vector<int>& a, int parts) {
    long long left = *max_element(a.begin(), a.end());
    long long right = accumulate(a.begin(), a.end(), 0LL);
    auto feasible = [&](long long limit) {
        int used = 1;
        long long sum = 0;
        for (int value : a) {
            if (sum + value > limit) ++used, sum = 0;
            sum += value;
        }
        return used <= parts;
    };
    while (left < right) {
        long long mid = left + (right - left) / 2;
        if (feasible(mid)) right = mid;
        else left = mid + 1;
    }
    return left;
}`
    }
];

function quote(value) {
    return `'${String(value).replace(/\\/g, '\\\\').replace(/'/g, "''")}'`;
}

if (require.main === module) {
    const values = boards.map(board => `(${quote(board.title)}, ${quote(board.category)}, ${quote(board.difficulty)}, ${quote(board.description)}, ${quote(board.code)}, ${quote(board.language)}, NULL, NULL)`).join(',\n');
    process.stdout.write(`START TRANSACTION;\nINSERT INTO board (title, category, difficulty, description, code, language, author_id, source_board_id) VALUES\n${values};\nCOMMIT;\n`);
}

module.exports = boards;
