// SPDX-License-Identifier: GPL-3.0-or-later
import { DurableObject } from "cloudflare:workers";

const SESSION_TTL_MS = 45 * 1000;
const RATE_WINDOW_MS = 60 * 1000;
const RATE_CLEANUP_MS = 2 * 60 * 1000;
const MAX_CREATES_PER_MINUTE = 30;
const MAX_PAYLOAD_BYTES = 8192;
const SESSION_ID_RE = /^[a-f0-9]{32}$/;
const BASE64URL_RE = /^[A-Za-z0-9_-]+$/;

const ARTWORK_MAX_ITEMS = 32;
const ARTWORK_MAX_BODY_BYTES = 24 * 1024;
const ARTWORK_NEGATIVE_TTL_MS = 14 * 24 * 60 * 60 * 1000;
const TMDB_MIN_INTERVAL_MS = 35;
const TMDB_IMAGE_BASE = "https://image.tmdb.org/t/p/w342";

function response(status, body = null, headers = {}) {
  return new Response(body, { status, headers });
}

function json(status, value) {
  return response(status, JSON.stringify(value), {
    "Content-Type": "application/json; charset=utf-8"
  });
}

function harden(input, contentSecurityPolicy = null) {
  const headers = new Headers(input.headers);
  headers.set("Cache-Control", "no-store");
  headers.set("Referrer-Policy", "no-referrer");
  headers.set("X-Content-Type-Options", "nosniff");
  headers.set("X-Frame-Options", "DENY");
  headers.set("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  headers.set("Access-Control-Allow-Headers", "Content-Type");
  if (contentSecurityPolicy) headers.set("Content-Security-Policy", contentSecurityPolicy);
  return new Response(input.body, {
    status: input.status,
    statusText: input.statusText,
    headers
  });
}

function sessionRoute(pathname) {
  const match = pathname.match(/^\/api\/v1\/sessions\/([a-f0-9]{32})(\/payload)?$/);
  if (!match) return null;
  return { id: match[1], payload: Boolean(match[2]) };
}

function validEncryptedPayload(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const keys = Object.keys(value).sort();
  if (keys.length !== 2 || keys[0] !== "ciphertext" || keys[1] !== "iv") return false;
  if (typeof value.iv !== "string" || typeof value.ciphertext !== "string") return false;
  if (value.iv.length !== 16 || !BASE64URL_RE.test(value.iv)) return false;
  if (value.ciphertext.length < 22 || value.ciphertext.length > 7600) return false;
  return BASE64URL_RE.test(value.ciphertext);
}

async function readEncryptedPayload(request) {
  const declared = Number(request.headers.get("Content-Length") || "0");
  if (Number.isFinite(declared) && declared > MAX_PAYLOAD_BYTES) return null;
  const text = await request.text();
  if (new TextEncoder().encode(text).byteLength > MAX_PAYLOAD_BYTES) return null;
  try {
    const value = JSON.parse(text);
    return validEncryptedPayload(value) ? value : null;
  } catch {
    return null;
  }
}

function plain(value) {
  return String(value || "").trim();
}

function stripDiacritics(value) {
  return plain(value).normalize("NFD").replace(/[\u0300-\u036f]/g, "");
}

function extractYear(value) {
  const matches = String(value || "").match(/\b(?:19\d{2}|20\d{2})\b/g) || [];
  if (!matches.length) return null;
  const year = Number(matches[matches.length - 1]);
  return Number.isInteger(year) ? year : null;
}

function cleanArtworkTitle(value) {
  return plain(value)
    .replace(/[\[\{][^\]\}]*[\]\}]/g, " ")
    .replace(/\b(?:s\d{1,2}e\d{1,3}|\d{1,2}x\d{1,3})\b.*$/i, " ")
    .replace(/\b(?:temporada|season)\s*\d+.*$/i, " ")
    .replace(/\b(?:episodio|episódio|episode|ep)\s*\d+.*$/i, " ")
    .replace(/\b(?:19\d{2}|20\d{2})\b/g, " ")
    .replace(/\b(?:4k|uhd|fhd|full\s*hd|hd|sd|2160p|1080p|720p|480p|hdr10?|dolby\s*vision|imax|bluray|blu\s*ray|brrip|webrip|web\s*dl|web-dl|h\.?26[45]|x26[45]|hevc|av1|dublado|legendado|dual(?:\s*audio)?|multi(?:\s*audio)?|nacional|pt\s*br)\b/gi, " ")
    .replace(/[._]+/g, " ")
    .replace(/\s+/g, " ")
    .replace(/[-|:]+$/g, "")
    .trim();
}

function normalizeArtworkTitle(value) {
  return stripDiacritics(cleanArtworkTitle(value))
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, " ")
    .trim()
    .replace(/\s+/g, " ");
}

function tokenSimilarity(a, b) {
  if (!a || !b) return 0;
  if (a === b) return 1;
  const left = new Set(a.split(" ").filter(Boolean));
  const right = new Set(b.split(" ").filter(Boolean));
  if (!left.size || !right.size) return 0;
  let overlap = 0;
  for (const token of left) if (right.has(token)) overlap += 1;
  const union = left.size + right.size - overlap;
  return union ? overlap / union : 0;
}

function candidateTitles(result) {
  if (result?.media_type === "tv" || result?.name || result?.original_name) {
    return [result?.name, result?.original_name];
  }
  return [result?.title, result?.original_title];
}

function candidateYear(result) {
  const date = result?.release_date || result?.first_air_date || "";
  const match = String(date).match(/^(\d{4})/);
  return match ? Number(match[1]) : null;
}

function scoreCandidate(descriptor, result) {
  const titles = candidateTitles(result).map(normalizeArtworkTitle).filter(Boolean);
  let score = 0;
  for (const title of titles) {
    score = Math.max(score, tokenSimilarity(descriptor.normalized, title));
  }
  if (!score) return 0;

  const actualYear = candidateYear(result);
  if (descriptor.year && actualYear) {
    const diff = Math.abs(descriptor.year - actualYear);
    if (diff === 0) score += 0.08;
    else if (diff === 1) score += 0.02;
    else score -= 0.18;
  }
  return Math.max(0, Math.min(1, score));
}

function artworkDescriptor(item) {
  const title = cleanArtworkTitle(item?.title);
  const normalized = normalizeArtworkTitle(title);
  if (normalized.length < 2) return null;
  let kind = plain(item?.kind).toLowerCase();
  if (!["movie", "tv", "auto"].includes(kind)) kind = "auto";
  const year = Number(item?.year) || extractYear(item?.title);
  return {
    id: plain(item?.id).slice(0, 128),
    title,
    normalized,
    year: Number.isInteger(year) && year >= 1900 && year <= 2100 ? year : null,
    kind
  };
}

function artworkKey(descriptor) {
  return [descriptor.kind, descriptor.normalized, descriptor.year || ""].join("|");
}

async function readArtworkRequest(request) {
  const declared = Number(request.headers.get("Content-Length") || "0");
  if (Number.isFinite(declared) && declared > ARTWORK_MAX_BODY_BYTES) return null;
  const body = await request.text();
  if (new TextEncoder().encode(body).byteLength > ARTWORK_MAX_BODY_BYTES) return null;
  try {
    const parsed = JSON.parse(body);
    if (!parsed || !Array.isArray(parsed.items) || parsed.items.length < 1 ||
        parsed.items.length > ARTWORK_MAX_ITEMS) return null;
    const items = parsed.items.map(artworkDescriptor);
    if (items.some((item) => !item)) return null;
    return items;
  } catch {
    return null;
  }
}

export class PairingSession extends DurableObject {
  async isLive(now = Date.now()) {
    const expiresAt = await this.ctx.storage.get("expiresAt");
    return typeof expiresAt === "number" && now < expiresAt;
  }

  async clear() {
    await this.ctx.storage.deleteAll();
  }

  async create() {
    const now = Date.now();
    if (await this.isLive(now)) return response(409, "session already exists");
    await this.clear();
    const expiresAt = now + SESSION_TTL_MS;
    await this.ctx.storage.put("expiresAt", expiresAt);
    await this.ctx.storage.setAlarm(expiresAt);
    return json(201, { expires_in: SESSION_TTL_MS / 1000 });
  }

  async submit(request) {
    if (!(await this.isLive())) {
      await this.clear();
      return response(410, "session expired");
    }
    if ((await this.ctx.storage.get("payload")) !== undefined) {
      return response(409, "payload already submitted");
    }
    const payload = await readEncryptedPayload(request);
    if (!payload) return response(400, "invalid payload");
    await this.ctx.storage.put("payload", payload);
    return response(204);
  }

  async poll() {
    if (!(await this.isLive())) {
      await this.clear();
      return response(410, "session expired");
    }
    const payload = await this.ctx.storage.get("payload");
    if (payload === undefined) return response(204);
    return json(200, payload);
  }

  async fetch(request) {
    const url = new URL(request.url);
    const isPayload = url.pathname.endsWith("/payload");
    if (isPayload) {
      if (request.method === "POST") return this.submit(request);
      if (request.method === "GET") return this.poll();
      return response(405, "method not allowed", { Allow: "GET, POST" });
    }
    if (request.method === "POST") return this.create();
    if (request.method === "DELETE") {
      await this.clear();
      return response(204);
    }
    return response(405, "method not allowed", { Allow: "POST, DELETE" });
  }

  async alarm() {
    await this.clear();
  }
}

export class PairingRateLimit extends DurableObject {
  async allow() {
    const now = Date.now();
    let start = await this.ctx.storage.get("windowStart");
    let count = await this.ctx.storage.get("count");
    if (typeof start !== "number" || typeof count !== "number" || now - start >= RATE_WINDOW_MS) {
      start = now;
      count = 1;
      await this.ctx.storage.put("windowStart", start);
      await this.ctx.storage.put("count", count);
      await this.ctx.storage.setAlarm(now + RATE_CLEANUP_MS);
      return true;
    }
    if (count >= MAX_CREATES_PER_MINUTE) return false;
    await this.ctx.storage.put("count", count + 1);
    return true;
  }

  async alarm() {
    await this.ctx.storage.deleteAll();
  }
}

export class ArtworkCatalog extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.env = env;
    this.nextTmdbAt = 0;
    this.ctx.storage.sql.exec(
      "CREATE TABLE IF NOT EXISTS artwork (" +
      "cache_key TEXT PRIMARY KEY, kind TEXT NOT NULL, title TEXT NOT NULL, " +
      "year INTEGER, url TEXT NOT NULL, tmdb_id INTEGER, score REAL NOT NULL DEFAULT 0, " +
      "checked_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)"
    );
  }

  cached(key) {
    const rows = this.ctx.storage.sql.exec(
      "SELECT cache_key, kind, title, year, url, tmdb_id, score, checked_at, updated_at " +
      "FROM artwork WHERE cache_key = ? LIMIT 1", key
    ).toArray();
    return rows[0] || null;
  }

  async rateSlot() {
    const now = Date.now();
    const slot = Math.max(now, this.nextTmdbAt);
    this.nextTmdbAt = slot + TMDB_MIN_INTERVAL_MS;
    const delay = slot - now;
    if (delay > 0) await new Promise((resolve) => setTimeout(resolve, delay));
  }

  async tmdbLookup(descriptor) {
    const token = plain(this.env.TMDB_API_TOKEN);
    if (!token) return { unavailable: true, url: "", id: null, score: 0 };

    await this.rateSlot();

    let path = "/search/multi";
    if (descriptor.kind === "movie") path = "/search/movie";
    else if (descriptor.kind === "tv") path = "/search/tv";

    const url = new URL("https://api.themoviedb.org/3" + path);
    url.searchParams.set("query", descriptor.title);
    url.searchParams.set("include_adult", "false");
    url.searchParams.set("language", "pt-BR");
    url.searchParams.set("page", "1");
    if (descriptor.year) {
      if (descriptor.kind === "movie") url.searchParams.set("year", String(descriptor.year));
      if (descriptor.kind === "tv") url.searchParams.set("first_air_date_year", String(descriptor.year));
    }

    const response = await fetch(url, {
      headers: {
        accept: "application/json",
        authorization: "Bearer " + token
      }
    });
    if (!response.ok) throw new Error("TMDB HTTP " + response.status);

    const data = await response.json();
    const results = Array.isArray(data?.results) ? data.results : [];
    let best = null;
    let bestScore = 0;
    for (const result of results.slice(0, 10)) {
      if (!result?.poster_path) continue;
      if (descriptor.kind === "auto" && result?.media_type &&
          !["movie", "tv"].includes(result.media_type)) continue;
      const score = scoreCandidate(descriptor, result);
      if (score > bestScore) {
        best = result;
        bestScore = score;
      }
    }

    if (!best || bestScore < 0.86) {
      return { unavailable: false, url: "", id: null, score: bestScore };
    }

    return {
      unavailable: false,
      url: TMDB_IMAGE_BASE + best.poster_path,
      id: Number(best.id) || null,
      score: Number(bestScore.toFixed(3))
    };
  }

  async resolveOne(descriptor) {
    const key = artworkKey(descriptor);
    const now = Date.now();
    const cached = this.cached(key);
    if (cached && (cached.url || now - Number(cached.checked_at) < ARTWORK_NEGATIVE_TTL_MS)) {
      return { id: descriptor.id, url: cached.url || "", cached: true };
    }

    try {
      const result = await this.tmdbLookup(descriptor);
      if (result.unavailable) {
        return { id: descriptor.id, url: "", cached: false, unavailable: true };
      }
      this.ctx.storage.sql.exec(
        "INSERT INTO artwork(cache_key, kind, title, year, url, tmdb_id, score, checked_at, updated_at) " +
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) " +
        "ON CONFLICT(cache_key) DO UPDATE SET kind=excluded.kind, title=excluded.title, " +
        "year=excluded.year, url=excluded.url, tmdb_id=excluded.tmdb_id, score=excluded.score, " +
        "checked_at=excluded.checked_at, updated_at=excluded.updated_at",
        key, descriptor.kind, descriptor.title, descriptor.year, result.url,
        result.id, result.score, now, result.url ? now : (cached?.updated_at || now)
      );
      return { id: descriptor.id, url: result.url, cached: false };
    } catch {
      return { id: descriptor.id, url: "", cached: false };
    }
  }

  async resolve(items) {
    const output = new Array(items.length);
    let cursor = 0;
    const workers = Array.from({ length: Math.min(4, items.length) }, async () => {
      while (true) {
        const index = cursor++;
        if (index >= items.length) return;
        output[index] = await this.resolveOne(items[index]);
      }
    });
    await Promise.all(workers);
    return output;
  }

  async exportBatch(cursor = "", limit = 1000) {
    let afterTime = 0;
    let afterKey = "";
    if (cursor) {
      const split = cursor.indexOf(":");
      if (split > 0) {
        afterTime = Number(cursor.slice(0, split)) || 0;
        afterKey = cursor.slice(split + 1);
      }
    }

    const bounded = Math.max(1, Math.min(2000, Number(limit) || 1000));
    const rows = this.ctx.storage.sql.exec(
      "SELECT cache_key, kind, title, year, url, tmdb_id, score, updated_at " +
      "FROM artwork WHERE url <> '' AND " +
      "(updated_at > ? OR (updated_at = ? AND cache_key > ?)) " +
      "ORDER BY updated_at ASC, cache_key ASC LIMIT ?",
      afterTime, afterTime, afterKey, bounded
    ).toArray();

    let nextCursor = "";
    if (rows.length === bounded) {
      const last = rows[rows.length - 1];
      nextCursor = String(last.updated_at) + ":" + String(last.cache_key);
    }
    return { items: rows, next_cursor: nextCursor };
  }
}

async function pairPage(request, env, sessionId) {
  if (!SESSION_ID_RE.test(sessionId)) return response(404, "not found");
  const assetUrl = new URL("/index.html", request.url);
  const page = await env.ASSETS.fetch(new Request(assetUrl, request));
  return harden(
    page,
    "default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; " +
      "img-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'"
  );
}

async function staticAsset(request, env) {
  const asset = await env.ASSETS.fetch(request);
  return harden(asset, "default-src 'none'");
}

async function resolveArtwork(request, env) {
  if (request.method !== "POST") {
    return response(405, "method not allowed", { Allow: "POST" });
  }
  const items = await readArtworkRequest(request);
  if (!items) return response(400, "invalid artwork request");
  const stub = env.ARTWORK_CATALOG.getByName("global");
  const resolved = await stub.resolve(items);
  return json(200, { items: resolved });
}

async function exportArtwork(request, env) {
  if (request.method !== "GET") {
    return response(405, "method not allowed", { Allow: "GET" });
  }
  const url = new URL(request.url);
  const cursor = plain(url.searchParams.get("cursor"));
  const limit = Number(url.searchParams.get("limit") || 1000);
  const stub = env.ARTWORK_CATALOG.getByName("global");
  return json(200, await stub.exportBatch(cursor, limit));
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "OPTIONS" &&
        (url.pathname.startsWith("/api/v1/sessions/") ||
         url.pathname.startsWith("/api/v1/artwork/"))) {
      return harden(response(204));
    }

    if (url.pathname === "/healthz") {
      if (request.method !== "GET" && request.method !== "HEAD") {
        return harden(response(405, "method not allowed", { Allow: "GET, HEAD" }));
      }
      return harden(response(204));
    }

    if (url.pathname === "/api/v1/artwork/resolve") {
      return harden(await resolveArtwork(request, env));
    }

    if (url.pathname === "/api/v1/artwork/export") {
      return harden(await exportArtwork(request, env));
    }

    if (url.pathname === "/pair.js") {
      if (request.method !== "GET") return harden(response(405, "method not allowed", { Allow: "GET" }));
      return staticAsset(request, env);
    }

    const pairMatch = url.pathname.match(/^\/pair\/([a-f0-9]{32})$/);
    if (pairMatch) {
      if (request.method !== "GET") return harden(response(405, "method not allowed", { Allow: "GET" }));
      return pairPage(request, env, pairMatch[1]);
    }

    const route = sessionRoute(url.pathname);
    if (route) {
      if (!route.payload && request.method === "POST") {
        const ip = request.headers.get("CF-Connecting-IP") || "unknown";
        const allowed = await env.PAIRING_RATE_LIMIT.getByName(ip).allow();
        if (!allowed) return harden(response(429, "rate limit exceeded"));
      }
      const stub = env.PAIRING_SESSIONS.getByName(route.id);
      return harden(await stub.fetch(request));
    }

    return harden(response(404, "not found"));
  }
};
