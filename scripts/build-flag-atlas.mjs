// Renders the flag atlas from a flag-icons release with headless Edge.
//
//   node scripts/build-flag-atlas.mjs --flags <flag-icons folder> [--edge <msedge.exe>]
//
// Reads the countries and the layout from scripts/countries.json (the table
// gen-countries.mjs writes from the same file) and each country's 4x3 SVG from
// <flag-icons folder>/flags/4x3. All of them are placed on one HTML page, one
// cell per country in index order, and Edge renders the page at device scale 1
// on a transparent background. Writes assets/flags/flags.png, flags.json (code
// to index, and the layout) and the release's LICENSE. Check the result with
// python -I scripts/check-flag-atlas.py.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const option = (name, fallback) => { const at = args.indexOf(name); return at >= 0 ? args[at + 1] : fallback; };
const flags = option('--flags');
const edge = option('--edge', 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe');
if (!flags) { console.error('--flags <flag-icons folder> is required'); process.exit(1); }

const manifest = JSON.parse(fs.readFileSync(path.join(root, 'scripts', 'countries.json'), 'utf8'));
const { cellWidth, cellHeight, padding, columns } = manifest.atlas;
const countries = manifest.countries;
const slotWidth = cellWidth + 2 * padding, slotHeight = cellHeight + 2 * padding;
const width = columns * slotWidth, height = Math.ceil(countries.length / columns) * slotHeight;

// Each SVG goes in as a data URL, so the page reads nothing from the release folder.
// Where two stripes meet inside a pixel, each edge is antialiased on its own and
// the pixel comes out partly transparent. The same flag drawn over itself keeps
// every colour and only raises those alphas, so the stripes close up; Nepal's
// pennant keeps a transparent outside with a slightly firmer edge.
const Layers = 8;
const cells = countries.map((country, index) => {
    const svg = fs.readFileSync(path.join(flags, 'flags', '4x3', `${country.code.toLowerCase()}.svg`));
    const x = (index % columns) * slotWidth + padding, y = Math.floor(index / columns) * slotHeight + padding;
    return `<img src="data:image/svg+xml;base64,${svg.toString('base64')}" style="left:${x}px;top:${y}px" alt="">`.repeat(Layers);
});
const page = `<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;padding:0;background:transparent;overflow:hidden;width:${width}px;height:${height}px}
img{position:absolute;display:block;width:${cellWidth}px;height:${cellHeight}px}
</style></head><body>${cells.join('')}</body></html>`;

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'flag-atlas-'));
try {
    const html = path.join(work, 'atlas.html'), shot = path.join(work, 'atlas.png');
    fs.writeFileSync(html, page);
    // Its own profile, so a running Edge cannot take the request over.
    execFileSync(edge, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
        '--default-background-color=00000000', `--window-size=${width},${height}`, `--user-data-dir=${path.join(work, 'profile')}`,
        '--no-first-run', '--disable-extensions', `--screenshot=${shot}`, pathToFileURL(html).href], { stdio: 'ignore', timeout: 120000 });
    if (!fs.existsSync(shot)) throw new Error('Edge wrote no screenshot');
    const out = path.join(root, 'assets', 'flags');
    fs.mkdirSync(out, { recursive: true });
    fs.copyFileSync(shot, path.join(out, 'flags.png'));
    const index = Object.fromEntries(countries.map((country, i) => [country.code, i]));
    fs.writeFileSync(path.join(out, 'flags.json'), JSON.stringify({
        source: manifest.source, cellWidth, cellHeight, padding, columns, width, height, countries: index,
    }, null, 2) + '\n');
    fs.copyFileSync(path.join(flags, 'LICENSE'), path.join(out, 'LICENSE'));
    console.log(`Wrote assets/flags/flags.png (${width}x${height}, ${countries.length} flags), flags.json and LICENSE`);
} finally {
    fs.rmSync(work, { recursive: true, force: true });
}
