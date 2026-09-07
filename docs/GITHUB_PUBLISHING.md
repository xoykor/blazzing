# Publishing on GitHub

[Português (Brasil)](GITHUB_PUBLISHING.pt-BR.md)

The repository includes a `.gitignore`, MIT license, documentation, CI, issue/PR templates, and does not depend on generated build files.

## With GitHub CLI

From the project root:

```sh
git init
git add .
git commit -m "Initial public release"
git branch -M main
gh repo create blazzing --public --source=. --remote=origin --push
```

`gh` will request authentication if needed.

## With an existing repository

```sh
git init
git add .
git commit -m "Initial public release"
git branch -M main
git remote add origin https://github.com/xoykor/blazzing.git
git push -u origin main
```

## Before pushing

Inspect what will be published:

```sh
git status --short
git diff --cached --stat
```

Search for accidentally committed private data:

```sh
git grep -n -i -E 'password|senha|username|usuario|https?://'
```

Legitimate matches exist in source code and tests. The goal is to confirm that none contain real credentials or private hosts.

## Suggested repository metadata

Description:

```text
Fast native visual IPTV client for Linux/X11 in C17, with Xtream/M3U and embedded mpv.
```

Suggested topics:

```text
iptv c x11 linux mpv xtream m3u sqlite cmake
```
