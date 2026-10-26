const common = 'using namespace std;\n\n';

const boards = [
    {
        title: '堆排序（标准库堆）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '先建大根堆再依次弹出堆顶；原地完成升序排序，时间复杂度 O(n log n)。',
        code: common + `void heap_sort(vector<int>& a) {
    make_heap(a.begin(), a.end());
    sort_heap(a.begin(), a.end());
}`
    },
    {
        title: '计数排序（值域有限）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '适合整数值域较小的情况；min_value 和 max_value 为数组允许的最小、最大值。',
        code: common + `vector<int> counting_sort(const vector<int>& a, int min_value, int max_value) {
    vector<int> count(max_value - min_value + 1, 0);
    for (int value : a) ++count[value - min_value];
    vector<int> result;
    result.reserve(a.size());
    for (int i = 0; i < static_cast<int>(count.size()); ++i) {
        result.insert(result.end(), count[i], i + min_value);
    }
    return result;
}`
    },
    {
        title: '滑动窗口（最长无重复子串）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '记录每个字节最近出现的位置，左端点仅右移；适用于 ASCII 或 UTF-8 原始字节序列。',
        code: common + `int longest_unique_substring(const string& s) {
    vector<int> last(256, -1);
    int answer = 0, left = 0;
    for (int right = 0; right < static_cast<int>(s.size()); ++right) {
        unsigned char ch = static_cast<unsigned char>(s[right]);
        left = max(left, last[ch] + 1);
        last[ch] = right;
        answer = max(answer, right - left + 1);
    }
    return answer;
}`
    },
    {
        title: '二维差分（矩形加）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '对闭矩形做加法时修改四个角，再做二维前缀和恢复每个位置的增量。',
        code: common + `vector<vector<long long>> rectangle_add(int n, int m, const vector<array<int, 5>>& ops) {
    vector<vector<long long>> diff(n + 1, vector<long long>(m + 1, 0));
    for (const auto& op : ops) {
        int x1 = op[0], y1 = op[1], x2 = op[2], y2 = op[3], delta = op[4];
        diff[x1][y1] += delta;
        if (x2 + 1 < n) diff[x2 + 1][y1] -= delta;
        if (y2 + 1 < m) diff[x1][y2 + 1] -= delta;
        if (x2 + 1 < n && y2 + 1 < m) diff[x2 + 1][y2 + 1] += delta;
    }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) {
            if (i) diff[i][j] += diff[i - 1][j];
            if (j) diff[i][j] += diff[i][j - 1];
            if (i && j) diff[i][j] -= diff[i - 1][j - 1];
        }
    diff.pop_back();
    for (auto& row : diff) row.pop_back();
    return diff;
}`
    },
    {
        title: '区间合并', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '先按左端点排序，再将相交或相接的闭区间合并。',
        code: common + `vector<pair<int, int>> merge_intervals(vector<pair<int, int>> intervals) {
    sort(intervals.begin(), intervals.end());
    vector<pair<int, int>> result;
    for (const auto& interval : intervals) {
        if (result.empty() || result.back().second < interval.first) result.push_back(interval);
        else result.back().second = max(result.back().second, interval.second);
    }
    return result;
}`
    },
    {
        title: '全排列生成（回溯）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '通过 used 数组回溯生成所有排列；输入元素应互不相同。',
        code: common + `void generate_permutations(const vector<int>& values, vector<int>& path, vector<bool>& used, vector<vector<int>>& answer) {
    if (path.size() == values.size()) {
        answer.push_back(path);
        return;
    }
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        if (used[i]) continue;
        used[i] = true;
        path.push_back(values[i]);
        generate_permutations(values, path, used, answer);
        path.pop_back();
        used[i] = false;
    }
}`
    },
    {
        title: '子集生成（回溯）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '每个元素都有选择和不选择两种分支，生成所有子集。',
        code: common + `void generate_subsets(const vector<int>& values, int index, vector<int>& path, vector<vector<int>>& answer) {
    if (index == static_cast<int>(values.size())) {
        answer.push_back(path);
        return;
    }
    generate_subsets(values, index + 1, path, answer);
    path.push_back(values[index]);
    generate_subsets(values, index + 1, path, answer);
    path.pop_back();
}`
    },
    {
        title: '选择第 k 小元素（nth_element）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: 'k 从 0 开始；平均线性时间，函数会改变原数组中元素的排列。',
        code: common + `int kth_smallest(vector<int>& a, int k) {
    nth_element(a.begin(), a.begin() + k, a.end());
    return a[k];
}`
    },
    {
        title: '整数平方根（二分）', category: '基础算法', difficulty: '简单', language: 'C++',
        description: '返回不超过 x 的最大整数平方根；使用除法比较避免 mid * mid 溢出。',
        code: common + `long long integer_sqrt(long long x) {
    long long left = 0, right = min(x, 3037000499LL), answer = 0;
    while (left <= right) {
        long long mid = left + (right - left) / 2;
        if (mid == 0 || mid <= x / mid) answer = mid, left = mid + 1;
        else right = mid - 1;
    }
    return answer;
}`
    },
    {
        title: '尺取法（最短和至少为 target 的子数组）', category: '基础算法', difficulty: '中等', language: 'C++',
        description: '仅适用于元素非负数组；维护和至少为 target 的窗口并尽量收缩左端。',
        code: common + `int shortest_subarray_at_least(const vector<int>& a, long long target) {
    int answer = numeric_limits<int>::max();
    long long sum = 0;
    for (int left = 0, right = 0; right < static_cast<int>(a.size()); ++right) {
        sum += a[right];
        while (sum >= target) {
            answer = min(answer, right - left + 1);
            sum -= a[left++];
        }
    }
    return answer == numeric_limits<int>::max() ? 0 : answer;
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
