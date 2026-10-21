/* Pack painted toolbar, bridge and map sprites into the native run-sprite asset.
 * Requires Node.js and sharp. Run from the repository root. */
const fs = require('node:fs');
const sharp = require('sharp');

// Same row/run layout as the landscape packer. Input retains straight RGBA;
// omit transparent margins, and preserve partial alpha in RGBA runs.
function EncodeSprite(pixels, size) {
  const chunks = [], offsets = [0];
  const kindAt = x => pixels[x * 4 + 3] === 0 ? 0 : pixels[x * 4 + 3] === 255 ? 1 : 2;
  for (let y = 0; y < size; ++y) {
    let first = y * size, last = first + size;
    while (first < last && !kindAt(first)) ++first;
    while (last > first && !kindAt(last - 1)) --last;
    const row = [];
    if (first < last) {
      const skip = first - y * size;
      row.push(...(skip < 255 ? [skip] : [255, skip & 255, skip >> 8]));
      for (let x = first; x < last;) {
        const kind = kindAt(x), start = x++;
        while (x < last && kindAt(x) === kind && x - start < 65535) ++x;
        const length = x - start;
        row.push(kind << 5 | Math.min(length - 1, 31));
        if (length >= 32) row.push(...(length <= 286 ? [length - 32] : [255, length & 255, length >> 8]));
        if (kind) for (let i = start; i < x; ++i) {
          row.push(...pixels.subarray(i * 4, i * 4 + (kind === 1 ? 3 : 4)));
        }
      }
    }
    chunks.push(Buffer.from(row));
    offsets.push(offsets[offsets.length - 1] + row.length);
  }
  const rows = Buffer.alloc(offsets.length * 4);
  offsets.forEach((offset, row) => rows.writeUInt32LE(offset, row * 4));
  return Buffer.concat([rows, ...chunks]);
}
async function BridgeArtwork(name, tint) {
  const {data, info} = await sharp(`assets/ui/bridges/generated/${name}.png`)
      .trim({background: '#00000000', threshold: 20}).ensureAlpha().raw().toBuffer({resolveWithObject: true});
  if (tint)
    for (let p = 0; p < data.length; p += 4) {
      const red = data[p], green = data[p + 1], blue = data[p + 2];
      if (red <= 40 || red <= green * 1.35 || red <= blue * 1.5) continue;
      for (let channel = 0; channel < 3; ++channel)
        data[p + channel] = Math.min(255, Math.round(red * tint[channel]));
    }
  return sharp(data, {raw: {width: info.width, height: info.height, channels: 4}}).png().toBuffer();
}

(async () => {
  const toolbarIconCount = 108;
  const painted = new Map();
  const manifest = JSON.parse(fs.readFileSync('assets/ui/toolbar/painted/manifest.json', 'utf8'));
  for (const sheet of manifest.sheets) {
    const path = `assets/ui/toolbar/painted/${sheet.file}`;
    const {width, height} = await sharp(path).metadata();
    if (sheet.icons.length !== sheet.columns * sheet.rows) throw new Error(`Invalid painted grid: ${path}`);
    for (const [cell, icon] of sheet.icons.entries()) {
      if (icon === null) continue;
      if (!Number.isInteger(icon) || icon < 0 || icon >= toolbarIconCount || (icon >= 98 && icon <= 100) || painted.has(icon))
        throw new Error(`Invalid or duplicate painted icon: ${icon}`);
      const column = cell % sheet.columns, row = Math.floor(cell / sheet.columns);
      const left = Math.round(column * width / sheet.columns), top = Math.round(row * height / sheet.rows);
      const bounds = sheet.bounds?.[icon] ?? [left, top,
        Math.round((column + 1) * width / sheet.columns) - left,
        Math.round((row + 1) * height / sheet.rows) - top];
      const cellImage = await sharp(path)
          .extract({left: bounds[0], top: bounds[1], width: bounds[2], height: bounds[3]}).png().toBuffer();
      painted.set(icon, await sharp(cellImage).trim({background: '#00000000', threshold: 12}).png().toBuffer());
    }
  }
  for (const [alias, source] of Object.entries(manifest.aliases)) {
    if (painted.has(+alias) || !painted.has(source)) throw new Error(`Invalid painted alias: ${alias}`);
    painted.set(+alias, painted.get(source));
  }
  // The two explicit span tools follow the original tool icons. Keep painted sheet IDs stable.
  for (const icon of [...painted.keys()].filter(icon => icon >= 97).sort((a, b) => b - a)) {
    painted.set(icon + 2, painted.get(icon));
    painted.delete(icon);
  }
  // A denser silhouette and taller aspect keep the small toolbar supports readable.
  const bridgeImage = await sharp(await BridgeArtwork('toolbar-bridge'))
      .resize(256, 128, {fit: 'fill'}).png().toBuffer();
  painted.set(97, bridgeImage);
  painted.set(98, bridgeImage);
  for (const [directory, count] of [['map-data', 23], ['bridges', 33], ['toolbar', toolbarIconCount]]) {
    if (process.argv.includes('--map-only') && directory !== 'map-data') continue;
    if (process.argv.includes('--toolbar-only') && directory !== 'toolbar') continue;
    const size = 128;
    let data;
    if (directory === 'toolbar') {
      data = Buffer.alloc(count * size * size * 4);
      for (let icon = 0; icon < count; ++icon) {
        const disabled = icon >= 100 && icon <= 102;
        const source = disabled ? [10, 11, 27][icon - 100] : icon;
        if (!painted.has(source)) throw new Error(`Missing painted toolbar icon: ${source}`);
        const columns = icon === 97 || icon === 98 ? 2 : 1;
        let fittedImage = sharp(painted.get(source))
            .resize((size - 4) * columns, size - 4, {fit: 'contain', kernel: 'lanczos3', background: '#00000000'})
            .extend({top: 2, bottom: 2, left: 2 * columns, right: 2 * columns, background: '#00000000'});
        // Keep the square atlas cells; the bridge renderer restores their two-column aspect.
        if (columns === 2) fittedImage = sharp(await fittedImage.png().toBuffer()).resize(size, size, {fit: 'fill'});
        const fitted = await fittedImage.raw().toBuffer();
        if (disabled)
          for (let p = 0; p < fitted.length; p += 4) {
            const gray = (Math.floor((fitted[p] + fitted[p + 1] + fitted[p + 2]) / 3) + 131) >> 1;
            fitted[p] = fitted[p + 1] = fitted[p + 2] = gray;
          }
        for (let y = 0; y < size; ++y)
          fitted.copy(data, (y * count * size + icon * size) * 4, y * size * 4, (y + 1) * size * 4);
      }
    } else if (directory === 'bridges') {
      const gray = [1.03, 1.09, 1.15], yellow = [1.35, 0.97, 0.18];
      const brown = [0.92, 0.57, 0.30], red = [1.15, 0.16, 0.13];
      const native = [['wood'], ['concrete'], ['arch'], ['suspension', gray], ['suspension'],
        ['suspension', yellow], ['cantilever'], ['cantilever', brown], ['cantilever', red],
        ['girder'], ['tubular'], ['tubular', yellow], ['tubular', gray]];
      const legacy = [['power'], ['girder', gray], ['concrete'], ['drawbridge'], ['suspension'],
        ['highway'], ['reinforced-highway']];
      const choices = [...legacy, ...native, ...native];
      const cells = [];
      for (const [icon, [name, tint]] of choices.entries()) {
        // Fit in the actual row aspect before encoding the square atlas cell.
        const fitted = await sharp(await BridgeArtwork(name, tint))
            .resize(176, 80, {fit: 'fill'})
            .extend({left: 4, right: 4, top: 8, bottom: 8, background: '#00000000'})
            .png().toBuffer();
        const input = await sharp(fitted).resize(size, size, {fit: 'fill'}).png().toBuffer();
        cells.push({input, left: icon * size, top: 0});
      }
      data = await sharp({create: {width: count * size, height: size, channels: 4, background: '#00000000'}})
          .composite(cells).raw().toBuffer();
    } else {
      const mapManifest = JSON.parse(fs.readFileSync('assets/ui/map-data/painted/manifest.json', 'utf8'));
      const cells = new Map();
      for (const sheet of mapManifest.sheets) {
        const path = `assets/ui/map-data/painted/${sheet.file}`;
        if (sheet.icons.length !== sheet.columns * sheet.rows) throw new Error(`Invalid map grid: ${path}`);
        for (const icon of sheet.icons) {
          if (icon === null) continue;
          const bounds = sheet.bounds[icon];
          if (!Number.isInteger(icon) || icon < 0 || icon >= count || cells.has(icon) || !bounds)
            throw new Error(`Invalid or duplicate map icon: ${icon}`);
          const crop = await sharp(path)
              .extract({left: bounds[0], top: bounds[1], width: bounds[2], height: bounds[3]})
              .png().toBuffer();
          const cropped = await sharp(crop).trim({background: '#00000000', threshold: 12}).png().toBuffer();
          const input = await sharp(cropped)
              .resize(size - 4, size - 4, {fit: 'contain', kernel: 'lanczos3', background: '#00000000'})
              .extend({top: 2, bottom: 2, left: 2, right: 2, background: '#00000000'})
              .png().toBuffer();
          cells.set(icon, {input, left: icon * size, top: 0});
        }
      }
      if (cells.size !== count) throw new Error(`Missing map icons: expected ${count}, got ${cells.size}`);
      data = await sharp({create: {width: count * size, height: size, channels: 4, background: '#00000000'}})
          .composite([...cells.values()]).ensureAlpha().raw().toBuffer();
    }
    // Default references retain the detailed source. Sparse records supply 1x/2x
    // images; 4x uses the default. This is the same record layout as map variants.
    const variantBytes = count * 8, headerCount = count * 3;
    const headerOffset = (24 + count * 2 + variantBytes + 3) & ~3;
    const pixelOffset = headerOffset + headerCount * 12;
    const output = Buffer.alloc(pixelOffset), payloads = [];
    output.write('SC2KICO3');
    output.writeUInt32LE(count, 8);
    output.writeUInt32LE(headerCount, 12);
    output.writeUInt32LE(variantBytes, 16);
    output.writeUInt32LE(headerOffset, 20);
    let offset = pixelOffset;
    for (let icon = 0; icon < count; ++icon) {
      output.writeUInt16LE(icon + 1, 24 + icon * 2);
      const record = 24 + count * 2 + icon * 8;
      output.writeUInt16LE(icon, record);
      output[record + 2] = 3;
      output.writeUInt16LE(count + icon * 2 + 1, record + 4);
      output.writeUInt16LE(count + icon * 2 + 2, record + 6);
    }
    for (let index = 0; index < headerCount; ++index) {
      const icon = index < count ? index : Math.floor((index - count) / 2);
      const level = index < count ? 2 : (index - count) % 2;
      const detail = 32 << level;
      const authored = `assets/ui/${directory}/variants/${icon}-${1 << level}x.png`;
      let image = fs.existsSync(authored) ? sharp(authored) :
        sharp(data, {raw: {width: count * size, height: size, channels: 4}})
          .extract({left: icon * size, top: 0, width: size, height: size});
      const pixels = await image.resize(detail, detail, {fit: 'fill', kernel: 'lanczos3'})
          .ensureAlpha().raw().toBuffer();
      const payload = EncodeSprite(pixels, detail);
      const header = headerOffset + index * 12;
      output.writeUInt32LE(detail, header);
      output.writeUInt32LE(offset, header + 4);
      output.writeUInt32LE(payload.length, header + 8);
      payloads.push(payload);
      offset += payload.length;
    }
    fs.writeFileSync(`assets/ui/${directory}/icons.dat`, Buffer.concat([output, ...payloads]));
    if (directory === 'map-data') {
      const previews = [];
      for (let icon = 0; icon < count; ++icon) {
        const input = await sharp(data, {raw: {width: count * size, height: size, channels: 4}})
            .extract({left: icon * size, top: 0, width: size, height: size})
            .resize(72, 72, {kernel: 'lanczos3'}).png().toBuffer();
        previews.push({input, left: icon % 8 * 80 + 4, top: Math.floor(icon / 8) * 80 + 4});
      }
      await sharp({create: {width: 640, height: 240, channels: 4, background: '#94835d'}})
          .composite(previews).png().toFile('assets/ui/map-data/preview.png');
    }
    if (directory === 'toolbar') {
      const previews = [];
      let slot = 0;
      for (let icon = 0; icon < count; ++icon) {
        const columns = icon === 97 || icon === 98 ? 2 : 1;
        if (slot % 14 + columns > 14) slot += 14 - slot % 14;
        const input = await sharp(data, {raw: {width: count * size, height: size, channels: 4}})
            .extract({left: icon * size, top: 0, width: size, height: size})
            .resize(72 * columns, 72, {fit: 'fill'}).png().toBuffer();
        previews.push({input, left: slot % 14 * 80 + 4, top: Math.floor(slot / 14) * 80 + 4});
        slot += columns;
      }
      await sharp({create: {width: 1120, height: 640, channels: 4, background: '#838583'}})
          .composite(previews).png().toFile('assets/ui/toolbar/preview.png');
    }
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
