const fs = require('fs');
const path = require('path');
const {spawnSync} = require('child_process');

const seedPath = process.argv[2];
if (!seedPath) {
    throw new Error('usage: node validate_seed.js <seed-file>');
}

const boards = require(path.resolve(seedPath));
const failures = [];
const tempDir = path.join(__dirname, '.validation-tmp');
fs.mkdirSync(tempDir, {recursive: true});

for (let index = 0; index < boards.length; ++index) {
    const board = boards[index];
    const filename = `algorithm-hub-board-${process.pid}-${index}.cpp`;
    const file = path.join(tempDir, filename);
    fs.writeFileSync(file, `#include <bits/stdc++.h>\n${board.code}\n`, 'utf8');

    const result = spawnSync(
        'wsl.exe',
        ['-d', 'Ubuntu-22.04', '--', 'g++', '-std=c++17', '-fsyntax-only', `/home/wyl/algorithm/tools/.validation-tmp/${filename}`],
        {encoding: 'utf8'}
    );
    fs.unlinkSync(file);

    if (result.status !== 0) {
        failures.push(`${board.title}:\n${result.stderr || result.stdout}`);
    }
}

fs.rmdirSync(tempDir);

if (failures.length) {
    console.error(failures.join('\n'));
    process.exit(1);
}

console.log(`validated snippets=${boards.length}`);
