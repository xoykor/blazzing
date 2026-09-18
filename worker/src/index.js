// SPDX-License-Identifier: MIT
import { DurableObject } from "cloudflare:workers";

const SESSION_TTL_MS = 5 * 60 * 1000;
const RATE_WINDOW_MS = 60 * 1000;
const RATE_CLEANUP_MS = 2 * 60 * 1000;
const MAX_CREATES_PER_MINUTE = 30;
const MAX_PAYLOAD_BYTES = 8192;
const SESSION_ID_RE = /^[a-f0-9]{32}$/;
const BASE64URL_RE = /^[A-Za-z0-9_-]+$/;

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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/healthz") {
      if (request.method !== "GET" && request.method !== "HEAD") {
        return harden(response(405, "method not allowed", { Allow: "GET, HEAD" }));
      }
      return harden(response(204));
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
