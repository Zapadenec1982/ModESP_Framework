/**
 * Deploy script: gzip bundles and copy to data/www/
 * Usage: npm run deploy (builds + deploys)
 *
 * Uses Node.js zlib instead of CLI gzip (Windows compatibility)
 */
import { createReadStream, createWriteStream, copyFileSync, existsSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { createGzip } from 'zlib';
import { pipeline } from 'stream/promises';

const __dirname = dirname(fileURLToPath(import.meta.url));
const dist = join(__dirname, '..', 'dist');
const www = join(__dirname, '..', '..', 'data', 'www');

// Gzip bundles using Node.js zlib (cross-platform)
console.log('Compressing bundles...');
for (const f of ['bundle.js', 'bundle.css']) {
  const src = join(dist, f);
  if (!existsSync(src)) {
    console.error(`Missing: ${src}`);
    process.exit(1);
  }
  const dst = join(dist, `${f}.gz`);
  await pipeline(
    createReadStream(src),
    createGzip({ level: 9 }),
    createWriteStream(dst)
  );
  console.log(`  ${f} -> ${f}.gz`);
}

// Copy to data/www/
console.log(`Deploying to ${www}`);
const indexSrc = join(dist, 'index.html');
if (existsSync(indexSrc)) {
  copyFileSync(indexSrc, join(www, 'index.html'));
}
copyFileSync(join(dist, 'bundle.js.gz'), join(www, 'bundle.js.gz'));
copyFileSync(join(dist, 'bundle.css.gz'), join(www, 'bundle.css.gz'));

console.log('Done! Files deployed to data/www/');
