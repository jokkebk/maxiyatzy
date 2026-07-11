// Range-request loader for the solved .mytz value table. One scorecard
// mask's values are a contiguous run of at most 77 XOR-delta float32 words
// at a computable offset, so a static host that answers 206 Partial Content
// is all the "server" the advisor needs (~6 kB of ranges per turn). If the
// host ignores Range (plain 200), the whole file is downloaded once and
// sliced locally.

const FIXED_HEADER = 8 + 5 * 4 + 2 * 8;          // 44
const STATE_RECORDS = 64 * (1 + 77);             // 4992
const PAYLOAD_START = FIXED_HEADER + STATE_RECORDS;
const MASK_COUNT = 1 << 20;
const BONUS_THRESHOLD = 75;

export class ValueTable {
  constructor(url) {
    this.url = url;
    this.states = [];        // upper mask -> Uint8Array of canonical states
    this.slot = [];          // upper mask -> Int8Array(128) state -> slot
    this.futureMax = new Int32Array(64);
    this.offsets = null;     // Uint32Array(MASK_COUNT + 1), value indices
    this.cache = new Map();  // mask -> Float32Array
    this.whole = null;       // ArrayBuffer fallback when ranges unsupported
    this.rangeSupported = true;
  }

  async init() {
    const head = await this.fetchRange(0, PAYLOAD_START);
    const bytes = new Uint8Array(head);
    const view = new DataView(head);
    const magic = String.fromCharCode(...bytes.slice(0, 7));
    if (magic !== 'MYTZDP1' || view.getUint32(8, true) !== 1 ||
        view.getUint32(16, true) !== MASK_COUNT ||
        view.getUint32(20, true) !== 77 || view.getUint32(24, true) !== 2) {
      throw new Error('unsupported .mytz header');
    }
    for (let m = 0; m < 64; m++) {
      const base = FIXED_HEADER + m * 78;
      const count = bytes[base];
      this.states.push(bytes.slice(base + 1, base + 1 + count));
      const slots = new Int8Array(128).fill(-1);
      for (let i = 0; i < count; i++) slots[bytes[base + 1 + i]] = i;
      this.slot.push(slots);
      let fm = 0;
      for (let f = 1; f <= 6; f++) if (!(m & (1 << (f - 1)))) fm += 6 * f;
      this.futureMax[m] = fm;
    }
    this.offsets = new Uint32Array(MASK_COUNT + 1);
    const counts = this.states.map(s => s.length);
    for (let mask = 0; mask < MASK_COUNT; mask++)
      this.offsets[mask + 1] = this.offsets[mask] + counts[mask & 63];
  }

  async fetchRange(start, length) {
    if (this.whole) return this.whole.slice(start, start + length);
    const response = await fetch(this.url, {
      headers: { Range: `bytes=${start}-${start + length - 1}` },
    });
    if (response.status === 206) return await response.arrayBuffer();
    if (response.status === 200) {
      // host ignored the Range header: fall back to one full download
      this.rangeSupported = false;
      this.whole = await response.arrayBuffer();
      return this.whole.slice(start, start + length);
    }
    throw new Error(`table fetch failed: HTTP ${response.status}`);
  }

  // fetch and decode the value records for every mask in `masks`
  async ensure(masks) {
    const missing = [...new Set(masks)].filter(m => !this.cache.has(m));
    await Promise.all(missing.map(async mask => {
      const count = this.states[mask & 63].length;
      const raw = await this.fetchRange(PAYLOAD_START + this.offsets[mask] * 4, count * 4);
      const words = new Uint32Array(raw.slice(0, count * 4));
      const values = new Float32Array(count);
      const bits = new Uint32Array(1);
      const float = new Float32Array(bits.buffer);
      for (let i = 0; i < count; i++) {
        bits[0] ^= words[i];
        values[i] = float[0];
      }
      this.cache.set(mask, values);
    }));
  }

  canonicalState(mask, upperTotal) {
    if (upperTotal >= BONUS_THRESHOLD) return 75;
    if (upperTotal + this.futureMax[mask & 63] < BONUS_THRESHOLD) return 76;
    return upperTotal;
  }

  valueFromTotal(mask, upperTotal) {
    if (mask === MASK_COUNT - 1) return 0;
    const values = this.cache.get(mask);
    if (!values) throw new Error(`mask ${mask} not prefetched`);
    const slot = this.slot[mask & 63][this.canonicalState(mask, upperTotal)];
    if (slot < 0) throw new Error(`state not stored for mask ${mask}`);
    return values[slot];
  }
}

// prefetch every successor mask reachable by filling one open category
export async function ensureTurn(table, mask) {
  const masks = [];
  for (let cat = 0; cat < 20; cat++)
    if (!(mask & (1 << cat))) masks.push(mask | (1 << cat));
  await table.ensure(masks);
}
