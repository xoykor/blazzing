// SPDX-License-Identifier: MIT
import { githubContentsUrl, rawUrl } from "./sources.js";

const USER_AGENT = "Blazzing-Healing-Playlist/0.1";

/**
 * Ask GitHub for the current blob SHA for a branch/path.
 *
 * This is deliberately branch based. The returned SHA is only used to decide
 * whether the remote file changed; it is never persisted as the source URL.
 */
export async function currentRevision(source) {
  const response = await fetch(githubContentsUrl(source), {
    headers: {
      "Accept": "application/vnd.github+json",
      "User-Agent": USER_AGENT,
      "Cache-Control": "no-cache",
    },
    cf: { cacheTtl: 0, cacheEverything: false },
  });

  if (!response.ok) {
    throw new Error(`${source.id}: GitHub metadata HTTP ${response.status}`);
  }

  const value = await response.json();
  if (!value || typeof value.sha !== "string" || value.sha.length < 20) {
    throw new Error(`${source.id}: GitHub metadata without blob SHA`);
  }

  return {
    sha: value.sha,
    size: Number(value.size || 0),
    raw: rawUrl(source),
  };
}

/**
 * Fetch the latest bytes from the configured branch.
 *
 * No Cloudflare cache is allowed here. The caller should cache the parsed
 * result and only invoke this after currentRevision() reports a new blob SHA.
 */
export async function fetchCurrentSource(source) {
  const response = await fetch(rawUrl(source), {
    headers: {
      "Accept": "text/plain,*/*",
      "User-Agent": USER_AGENT,
      "Cache-Control": "no-cache",
    },
    cf: { cacheTtl: 0, cacheEverything: false },
  });

  if (!response.ok || !response.body) {
    throw new Error(`${source.id}: raw HTTP ${response.status}`);
  }

  return response;
}
