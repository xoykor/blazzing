# Data and privacy

[Português (Brasil)](DATA_AND_PRIVACY.pt-BR.md)

## Persistent files

SQLite database:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Thumbnail cache:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

The mpv JSON IPC socket is temporary and exists only while the application is running.

## What SQLite stores

The current schema stores:

- catalog categories and items;
- favorites;
- history/auxiliary state;
- thumbnail metadata;
- settings;
- profiles;
- VOD/episode progress;
- aggregated series progress;
- rich movie/series metadata.

Xtream profiles include the server, alternate server, and username, but **do not include the password**.

## Passwords

When `secret-tool` is available, the password is stored and retrieved through the desktop Secret Service and associated with the profile ID. If Secret Service is unavailable, the profile remains saved without its password and the password must be entered again.

Password buffers allocated by the core and some jobs are overwritten before being freed.

## Stream URLs

The mpv process is persistent. Media URLs are not placed in `argv`; they are sent through the Unix JSON IPC socket after the process is running.

mpv messages may occasionally repeat a stream URL. Before retaining recent diagnostic text, the backend replaces recognized `http://...` or `https://...` fragments with `[URL hidden]`.

## Repository and bug reports

Never publish the following in issues, screenshots, logs, or commits:

- real Xtream username/password pairs;
- private playlist URLs;
- complete authenticated stream URLs;
- Secret Service dumps;
- `catalog.db` from a real account.

When reporting problems, use fictional endpoints or a test server you control.

## Phone pairing relay

Phone-assisted M3U entry uses a short-lived public HTTPS relay. Blazzing
generates the session ID and AES-256 key locally. The browser encrypts the
playlist name and URL with AES-256-GCM before submitting them.

The relay stores only the encrypted IV/ciphertext in RAM and expires sessions
after five minutes. The AES key is carried in the QR URL fragment and is not
included in normal HTTP requests. After the browser loads, its JavaScript
removes the fragment from the visible URL/history.

The relay serves that JavaScript, so operators of a modified/malicious relay
frontend could theoretically change the page to capture plaintext. Use the
official Blazzing relay or a relay you control.
