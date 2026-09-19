// SPDX-License-Identifier: MIT

/**
 * Upstream registries are intentionally described as repo + branch + path.
 * We NEVER pin a commit SHA here.
 *
 * That means an update pushed by Ramys or Saimo is visible to the resolver on
 * the next refresh without changing or redeploying Blazzing.
 */
export const SOURCES = [
  {
    id: "saimo-catalogo",
    owner: "gabrielsaimo",
    repo: "SaimoPlayer",
    branch: "main",
    path: "catalogo.txt",
    format: "saimo-catalog",
    priority: 0,
  },
  {
    id: "saimo-canais",
    owner: "gabrielsaimo",
    repo: "SaimoPlayer",
    branch: "main",
    path: "canais.txt",
    format: "m3u",
    priority: 10,
  },
  {
    id: "ramys-br01",
    owner: "Ramys",
    repo: "Iptv-Brasil-2026",
    branch: "master",
    path: "CanaisBR01.m3u8",
    format: "m3u",
    priority: 20,
  },
  {
    id: "ramys-br02",
    owner: "Ramys",
    repo: "Iptv-Brasil-2026",
    branch: "master",
    path: "CanaisBR02.m3u8",
    format: "m3u",
    priority: 21,
  },
  {
    id: "ramys-br03",
    owner: "Ramys",
    repo: "Iptv-Brasil-2026",
    branch: "master",
    path: "CanaisBR03.m3u8",
    format: "m3u",
    priority: 22,
  },
  {
    id: "ramys-br04",
    owner: "Ramys",
    repo: "Iptv-Brasil-2026",
    branch: "master",
    path: "CanaisBR04.m3u8",
    format: "m3u",
    priority: 23,
  },
];

export function rawUrl(source) {
  const path = source.path
    .split("/")
    .map(encodeURIComponent)
    .join("/");
  return `https://raw.githubusercontent.com/${source.owner}/${source.repo}/${source.branch}/${path}`;
}

export function githubContentsUrl(source) {
  const path = source.path
    .split("/")
    .map(encodeURIComponent)
    .join("/");
  return `https://api.github.com/repos/${source.owner}/${source.repo}/contents/${path}?ref=${encodeURIComponent(source.branch)}`;
}
