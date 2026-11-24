// Fails on any warning qmllint reports for the window's QML: Omarchy treats
// them as errors. Usage: node tests/qmllint.mjs <qmllint> <src dir>
//
// Reads qmllint's JSON: its text output leaves some files' warnings out, and
// it exits 0 on warnings. The paths are absolute because with a relative one
// qmllint does not match Fmt.qml to its `singleton` line in src/qmldir.

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [qmllint, srcArg] = process.argv.slice(2);
const src = path.resolve(srcArg);
const files = fs.readdirSync(src).filter(f => f.endsWith('.qml')).sort().map(f => path.join(src, f));

// Off as in Omarchy's skill: the views read the window's ids and the context
// properties (theme, settings) unqualified.
const run = spawnSync(qmllint, ['--unqualified', 'disable', '--json', '-', ...files], { encoding: 'utf8' });
if (run.error || !run.stdout) {
    console.error(`qmllint did not run: ${run.error ? run.error.message : run.stderr}`);
    process.exit(1);
}

let count = 0;
for (const file of JSON.parse(run.stdout).files) {
    for (const w of file.warnings) {
        if (w.type === 'info')
            continue;
        console.error(`${path.relative(src, file.filename)}:${w.line}:${w.column}: ${w.message} [${w.id}]`);
        ++count;
    }
}
console.log(`qmllint: ${files.length} files, ${count} warnings`);
process.exit(count ? 1 : 0);
