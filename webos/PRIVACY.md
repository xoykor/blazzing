# Blazzing webOS — Privacy Policy Draft

Last updated: September 19, 2026

This document is a draft for the Blazzing webOS application. It should be
published at a stable public URL before store submission.

## Overview

Blazzing is a media player for LG webOS. It can open user-provided M3U/M3U8
playlists, connect to compatible Xtream providers, and browse free regional
Pluto TV catalogs.

Blazzing does not sell user data.

## Data entered by the user

### M3U/M3U8 URLs

Playlist URLs are used to load the selected catalog and media streams.

For large playlists, the packaged webOS service may temporarily store the
playlist in the app's private temporary storage so it can page the catalog
without loading the full file into UI memory. Temporary playlist sessions are
released when the user leaves the catalog and also expire automatically.

Raw M3U media URLs are not stored in the favorites or playback-progress store.

### Xtream credentials

Xtream server URL, username, and password are used to authenticate with the
provider selected by the user.

Passwords are kept in memory only and are not written to localStorage or
IndexedDB.

If the user explicitly enables the **remember server and username** option,
Blazzing stores only:

- server URL;
- username.

The password is not included.

### Phone pairing

Phone pairing uses an encrypted payload. The TV generates the encryption key
locally. The pairing service stores ciphertext for the active pairing session;
the TV decrypts the payload locally and removes the session after successful
pickup or expiry.

## Local app storage

Blazzing can store the following data locally on the TV:

- favorites represented by opaque identifiers and safe display metadata;
- playback-resume positions for supported movies and episodes;
- an optional remembered Xtream server URL and username;
- a bounded artwork cache.

The artwork cache uses opaque hashed keys rather than raw provider image URLs as
its persistent index. It is bounded by size, item count, and age.

## Pluto TV

Blazzing may create a regional Pluto TV session to access Pluto TV catalogs and
streams.

Pluto session JWTs and signed media URLs remain in service memory and are not
stored in favorites, playback progress, or other persistent app storage.

## Network requests

Depending on the features used, Blazzing communicates with:

- playlist or media hosts selected by the user;
- Xtream providers configured by the user;
- Pluto TV services;
- the Blazzing pairing service.

These external services may process network information such as the device's IP
address according to their own policies.

## Analytics and advertising

The current Blazzing webOS alpha does not include a dedicated analytics SDK or
an advertising SDK.

If analytics, advertising, or additional telemetry is added in a future
version, this policy must be updated before that version is distributed.

## Data retention and deletion

Users can remove local remembered state by clearing the application's storage or
uninstalling the app.

Temporary large-playlist sessions expire automatically and are also released by
the app when leaving the relevant catalog.

Pairing sessions expire automatically and are removed after successful pickup.

## Children's privacy

Blazzing is a general-purpose media player and is not designed to collect
personal information from children.

Users are responsible for the content sources they configure and for ensuring
those sources are appropriate for the people using the TV.

## Changes to this policy

This policy may be updated when app functionality or data handling changes. The
published version should show its effective date.

## Contact

Before store submission, replace this section with the public support/contact
method that will be used for the distributed app.
