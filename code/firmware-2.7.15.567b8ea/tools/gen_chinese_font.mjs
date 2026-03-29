#!/usr/bin/env node
/**
 * Chinese 12x12 Bitmap Font Generator for OLED (ESP32-C3 / SH1106)
 * 
 * Generates C header with XBM-format bitmaps for GB2312 Level 1 (3755 chars)
 * + common punctuation + fullwidth ASCII.
 * 
 * Usage:  node gen_chinese_font.mjs
 * Output: src/graphics/fonts/ChineseFont12x12.h
 * 
 * Font source: Windows built-in TTF (msyh.ttc / simhei.ttf / simsun.ttc)
 * Rasterizer:  @napi-rs/canvas (native, fast)
 */

import { createCanvas } from '@napi-rs/canvas';
import { existsSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

// ========== Config ==========
const W = 12, H = 12;
const BPR = Math.ceil(W / 8);     // 2 bytes per row
const BPG = BPR * H;              // 24 bytes per glyph
const GLYPH_SIZE = 2 + BPG;      // 26 bytes total (uint16 + bitmap)

// ========== Find Chinese font ==========
const FONT_PATHS = [
    'C:\\Windows\\Fonts\\msyh.ttc',
    'C:\\Windows\\Fonts\\msyhbd.ttc',
    'C:\\Windows\\Fonts\\simhei.ttf',
    'C:\\Windows\\Fonts\\simsun.ttc',
    'C:\\Windows\\Fonts\\simfang.ttf',
    'C:\\Windows\\Fonts\\simkai.ttf',
];
let fontPath = FONT_PATHS.find(p => existsSync(p));
if (!fontPath) { console.error('No Chinese TTF found!'); process.exit(1); }
console.log(`Font: ${fontPath}`);

// ========== Build character set ==========
function buildCharSet() {
    const set = new Set();

    // 1. Chinese punctuation (CJK Symbols and Punctuation block)
    const punct = "\uFF0C\u3002\uFF01\uFF1F\u3001\uFF1A\uFF1B\u201C\u201D\u2018\u2019\uFF08\uFF09\u300A\u300B\u3010\u3011\u00B7\uFF5E\u2014\u2026";
    for (const c of punct) set.add(c);

    // 2. Fullwidth digits, letters
    for (let i = 0xFF10; i <= 0xFF19; i++) set.add(String.fromCodePoint(i));
    for (let i = 0xFF21; i <= 0xFF3A; i++) set.add(String.fromCodePoint(i));
    for (let i = 0xFF41; i <= 0xFF5A; i++) set.add(String.fromCodePoint(i));

    // 3. GB2312 Level 1: U+4E00..U+5573 region + extras
    //    GB2312 Level 1 = 3755 most common characters
    //    We include the full CJK Unified Ideographs block up to U+9FFF
    //    but only those that exist in GB2312.
    //    For simplicity and maximum coverage, we include U+4E00-U+9FA5
    //    (this is the "CJK Unified Ideographs" block, ~20,902 chars)
    //    BUT we limit to ~6000 to fit in flash budget.
    //
    //    Practical approach: include U+4E00-U+7FFF (8192 codepoints, ~6800 assigned)
    //    This covers GB2312 Level 1 + Level 2 + many common extensions.
    //    At 26 bytes/glyph = ~177 KB. Our budget is ~1700 KB. Plenty of room.

    // Let's include U+4E00 to U+9FFF (full CJK block) - that's about 20992 codepoints
    // but many are unassigned. We'll filter to only assigned ones.
    // Actually, all codepoints in U+4E00-U+9FFF range ARE assigned CJK characters.
    // 0x9FFF - 0x4E00 = 0x51FF = 20991 codepoints
    // 20991 × 26 = 545,766 bytes ≈ 533 KB
    // This is too much. Let's cap at a reasonable number.

    // GB2312 total: 6763 chars. Let's aim for ~7000 chars max.
    // U+4E00 to U+73FF covers most GB2312 chars:
    // 0x73FF - 0x4E00 = 0x25FF = 9727 codepoints
    // 9727 × 26 = 252,902 bytes ≈ 247 KB
    // Still a lot. Let's be more selective.

    // Optimal: GB2312 Level 1 only (3755 chars) + punctuation + fullwidth
    // Level 1 chars are in the range U+4E00 to U+5573 (but not contiguous)
    // The actual GB2312-to-Unicode mapping has the Level 1 chars spread across
    // U+4E00-U+9FA5. For simplicity, let's just include the densest CJK blocks:
    // U+4E00-U+6FFF = 8448 codepoints = 220 KB. Good coverage.

    // CJK Unified Ideographs: U+4E00 to U+9FFF
    // Covers all GB2312 (6763 chars) + CJK Extension A/B common chars
    // ~20992 codepoints × 26 bytes = ~533 KB flash
    // Fits within app partition budget (2.75 MB, firmware ~2.65 MB with full font)
    for (let cp = 0x4E00; cp <= 0x9FFF; cp++) {
        set.add(String.fromCodePoint(cp));
    }

    return [...set].sort((a, b) => a.codePointAt(0) - b.codePointAt(0));
}

const chars = buildCharSet();
const estKB = (chars.length * GLYPH_SIZE / 1024).toFixed(0);
console.log(`Characters: ${chars.length} (est. ${estKB} KB flash)`);

if (chars.length * GLYPH_SIZE > 1700 * 1024) {
    console.error('ERROR: exceeds flash budget!');
    process.exit(1);
}

// ========== Rasterize ==========
function rasterize(ch) {
    const canvas = createCanvas(W, H);
    const ctx = canvas.getContext('2d');
    // Transparent background + black text → alpha is the only signal we need
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#000';
    ctx.font = `${H - 1}px "Microsoft YaHei","SimHei","SimSun",sans-serif`;
    ctx.textBaseline = 'top';
    ctx.textAlign = 'center';
    ctx.fillText(ch, W / 2, 0);

    const { data } = ctx.getImageData(0, 0, W, H);
    const bytes = new Uint8Array(BPG);
    for (let r = 0; r < H; r++) {
        for (let c = 0; c < W; c++) {
            // Transparent bg + black text: alpha > threshold = glyph pixel
            // Threshold 80: keep thin strokes (alpha 80-128) while cutting outer fringe (<80)
            // 40 = too bold (includes fringe), 128 = too thin (loses thin strokes)
            if (data[(r * W + c) * 4 + 3] > 80) {
                bytes[r * BPR + (c >> 3)] |= 0x80 >> (c & 7);
            }
        }
    }
    return bytes;
}

console.log('Rasterizing...');
const bitmaps = [];
const t0 = Date.now();
for (let i = 0; i < chars.length; i++) {
    bitmaps.push(rasterize(chars[i]));
    if ((i + 1) % 500 === 0 || i === chars.length - 1) {
        const pct = ((i + 1) / chars.length * 100).toFixed(0);
        console.log(`  ${pct}% (${i + 1}/${chars.length}) ${((Date.now() - t0) / 1000).toFixed(0)}s`);
    }
}
console.log(`Done in ${((Date.now() - t0) / 1000).toFixed(1)}s`);

// ========== Generate C header ==========
function genHeader() {
    const L = [];
    L.push('/**');
    L.push(` * Chinese 12x12 Bitmap Font - ${chars.length} glyphs`);
    L.push(` * Auto-generated ${new Date().toISOString().split('T')[0]}`);
    L.push(` * Flash: ~${estKB} KB`);
    L.push(' */');
    L.push('#pragma once');
    L.push('#include <Arduino.h>');
    L.push('#include <stdint.h>');
    L.push('');
    L.push('#define CFONT_W  12');
    L.push('#define CFONT_H  12');
    L.push('#define CFONT_BPR 2');
    L.push('#define CFONT_BPG 24');
    L.push(`#define CFONT_COUNT ${chars.length}`);
    L.push('');
    L.push('typedef struct { uint16_t cp; uint8_t bmp[24]; } CFontGlyph;');
    L.push('');
    L.push('class OLEDDisplay;');
    L.push('');
    L.push('const CFontGlyph cfont12_data[] PROGMEM = {');

    for (let i = 0; i < chars.length; i++) {
        const cp = chars[i].codePointAt(0);
        const hex = Array.from(bitmaps[i]).map(b => '0x' + b.toString(16).padStart(2, '0')).join(',');
        L.push(`  {0x${cp.toString(16).padStart(4,'0')},{${hex}}},`);
    }

    L.push('};');
    L.push('');
    // Binary search lookup
    L.push('inline const CFontGlyph* cfont12_find(uint16_t cp) {');
    L.push('  int lo=0, hi=CFONT_COUNT-1;');
    L.push('  while(lo<=hi){');
    L.push('    int m=(lo+hi)>>1;');
    L.push('    uint16_t v=pgm_read_word(&cfont12_data[m].cp);');
    L.push('    if(v==cp) return &cfont12_data[m];');
    L.push('    if(v<cp) lo=m+1; else hi=m-1;');
    L.push('  }');
    L.push('  return nullptr;');
    L.push('}');
    L.push('');
    // Draw char using setPixel
    L.push('inline int cfont12_draw(OLEDDisplay*d,int x,int y,uint16_t cp){');
    L.push('  const CFontGlyph*g=cfont12_find(cp);');
    L.push('  if(!g)return 0;');
    L.push('  for(int r=0;r<CFONT_H;r++)for(int c=0;c<CFONT_W;c++){');
    L.push('    uint8_t b=pgm_read_byte(&g->bmp[r*CFONT_BPR+(c>>3)]);');
    L.push('    if(b&(0x80>>(c&7)))d->setPixel(x+c,y+r);');
    L.push('  }');
    L.push('  return CFONT_W;');
    L.push('}');
    L.push('');
    // UTF-8 decode
    L.push('inline uint16_t cfont12_utf8(const char*p,int*len){');
    L.push('  uint8_t b=(uint8_t)*p;');
    L.push('  if(b<0x80){*len=1;return b;}');
    L.push('  if((b&0xE0)==0xC0){*len=2;return((b&0x1F)<<6)|(((uint8_t)p[1])&0x3F);}');
    L.push('  if((b&0xF0)==0xE0){*len=3;return((b&0x0F)<<12)|(((uint8_t)p[1])&0x3F)<<6|((uint8_t)p[2])&0x3F;}');
    L.push('  *len=1;return 0;');
    L.push('}');
    L.push('');
    // Draw mixed string
    L.push('inline uint16_t cfont12_drawStr(OLEDDisplay*d,int x,int y,const char*s){');
    L.push('  int cx=x;int len;');
    L.push('  while(*s){uint16_t cp=cfont12_utf8(s,&len);');
    L.push('    if(cp>=0x80&&cfont12_find(cp)){cx+=cfont12_draw(d,cx,y,cp);}');
    L.push('    s+=len;');
    L.push('  }');
    L.push('  return cx-x;');
    L.push('}');
    L.push('');
    L.push('inline uint16_t cfont12_strWidth(const char*s){');
    L.push('  int w=0;int len;');
    L.push('  while(*s){uint16_t cp=cfont12_utf8(s,&len);');
    L.push('    if(cp>=0x80&&cfont12_find(cp))w+=CFONT_W;');
    L.push('    s+=len;');
    L.push('  }');
    L.push('  return w;');
    L.push('}');
    L.push('');
    return L.join('\n');
}

const outPath = join(__dirname, '..', 'src', 'graphics', 'fonts', 'ChineseFont12x12.h');
writeFileSync(outPath, genHeader(), 'utf-8');
console.log(`\n✅ ${outPath}`);
console.log(`   ${chars.length} glyphs, ~${estKB} KB flash`);
