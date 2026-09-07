# Security

[Português (Brasil)](SECURITY.pt-BR.md)

## Sensitive content

Do not publish Xtream credentials, private M3U URLs, authenticated stream URLs, Secret Service dumps, or real application databases in issues or pull requests.

Repository tests must use only fictional or controlled data.

## Reporting a vulnerability

If **Private vulnerability reporting** is enabled for the repository, prefer that channel for issues that could expose credentials or allow unintended code execution.

Otherwise, do not attach secrets to a public issue. Describe only the impact and affected component until the maintainer provides a private channel.

## Current security boundaries

- Xtream passwords are not persisted in SQLite.
- Secret Service is used when available.
- Media URLs are sent to mpv through IPC rather than argv.
- Recent mpv diagnostic messages go through URL sanitization.
- FFmpeg is launched through `exec`, without a shell.
