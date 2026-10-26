const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

function element() {
    return {
        value: '', children: [], handlers: {}, dataset: {}, style: {}, disabled: false,
        classList: {add() {}, remove() {}, toggle() {}},
        set innerHTML(value) { this.html = value; this.children = []; },
        get innerHTML() { return this.html || ''; },
        append(...items) { this.children.push(...items); },
        appendChild(item) { this.children.push(item); },
        addEventListener(type, callback) { this.handlers[type] = callback; },
        setAttribute() {}, scrollIntoView() {}, remove() {}, reset() {},
    };
}
const tick = () => new Promise(resolve => setImmediate(resolve));
async function check(name) {
    const personal = name === 'my-boards';
    const endpoint = personal ? '/api/my/boards/page' : '/api/boards/page';
    const elements = new Map();
    const get = selector => {
        if (!elements.has(selector)) elements.set(selector, element());
        return elements.get(selector);
    };
    get('#sortFilter').value = 'newest';
    const requests = [];
    const context = vm.createContext({
        console, AbortController, FormData, Set, clearTimeout, setTimeout,
        location: {}, window: {}, navigator: {},
        document: {querySelector: get, querySelectorAll: () => [],
            createElement: element, addEventListener() {}, body: element()},
        fetch(url, options) {
            if (url === '/api/me') return Promise.resolve({ok: true, json: async () => ({})});
            return new Promise(resolve => requests.push({url, options, resolve}));
        },
    });
    const source = fs.readFileSync(path.join(__dirname, '../web/html', name + '.html'), 'utf8');
    const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
    vm.runInContext(script, context);
    await tick();
    const run = code => vm.runInContext(code, context);
    function take() {
        assert.ok(requests.length);
        return requests.shift();
    }
    function answer(request, page, total, count, status = 200) {
        const items = Array.from({length: count}, (_, i) => ({id: (page - 1) * 15 + i + 1,
            title: '模板', category: '图论', language: 'C++', difficulty: '简单', code: 'code'}));
        request.resolve({ok: status === 200, status, json: async () => ({items,
            page, total, allTotal: 36, categories: ['图论', '数论'], languageCount: 2})});
    }
    let request = take();
    assert.equal(request.url, endpoint);
    assert.equal(JSON.parse(request.options.body).page, 1);
    answer(request, 1, 36, 15);
    await tick();
    assert.equal(get('#templateGrid').children.length, 15);
    assert.equal(get(personal ? '#totalCount' : '#publicCount').textContent, 36);
    run('goToPage(2)');
    request = take();
    assert.equal(JSON.parse(request.options.body).page, 2);
    answer(request, 2, 36, 15);
    await tick();
    assert.equal(run('currentPage'), 2);
    get('#searchInput').value = 'code';
    run('applyFilters()');
    request = take();
    assert.equal(JSON.parse(request.options.body).keyword, 'code');
    assert.equal(JSON.parse(request.options.body).page, 1);
    answer(request, 1, 20, 15);
    await tick();
    run('goToPage(2)');
    request = take();
    assert.equal(JSON.parse(request.options.body).keyword, 'code');
    answer(request, 2, 20, 5);
    await tick();
    assert.equal(get('#templateGrid').children.length, 5);
    assert.equal(run('filteredTotal'), 20);
    // Even if a transport ignores cancellation, old results must not win.
    run('loadTemplates(1)');
    const old = take();
    run('loadTemplates(2)');
    request = take();
    assert.equal(old.options.signal.aborted, true);
    answer(request, 2, 20, 5);
    await tick();
    answer(old, 1, 20, 15);
    await tick();
    assert.equal(run('currentPage'), 2);
    assert.equal(get('#templateGrid').children.length, 5);
    // Removal of the last page is reflected using the server's clamped page.
    run('loadTemplates(2)'); request = take(); answer(request, 1, 15, 15);
    await tick();
    assert.equal(run('currentPage'), 1);
    if (personal) {
        // Export loads its own full list, not just the current page or search results.
        run('openExportModal()'); request = take();
        assert.equal(request.url, '/api/my/boards');
        request.resolve({ok: true, status: 200, json: async () => [
            {id: 99, title: '其他页', category: '图论'}, {id: 100, title: '其他页2', category: '数论'}]});
        await tick();
        get('#toggleAllExport').handlers.click();
        assert.equal(run('selectedExportTemplates().length'), 2);
        assert.equal(run('selectedExportTemplates()[0].id'), 99);
        assert.equal(run('filteredTotal'), 15);
        assert.equal(get('#categoryFilter').children.length, 2);
    }
    run('loadTemplates(1)'); request = take(); answer(request, 1, 0, 0, 500);
    await tick();
    assert.match(get('#templateGrid').innerHTML, /模板加载失败/);
    get('#retryTemplates').handlers.click(); request = take(); answer(request, 1, 0, 0);
    await tick();
    assert.match(get('#templateGrid').innerHTML, /没有找到/);
    assert.equal(requests.length, 0);
    if (personal) {
        get('input[name="codeSource"]:checked').value = 'editor';
        get('#codeFile').files = [];
        get('#code').value = 'int main() {}';
        get('#category').value = '基础算法';
        context.fetch = async () => { throw new Error('connection lost'); };
        await get('#templateForm').handlers.submit({preventDefault() {}});
        assert.match(get('#formError').textContent, /网络连接中断/);
        assert.equal(get('#code').value, 'int main() {}');
    }
    console.log(name + ': frontend pagination tests passed');
}
(async () => { await check('index'); await check('my-boards'); })().catch(error => {
    console.error(error); process.exitCode = 1;
});
