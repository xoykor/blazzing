# Blazzing webOS — QA checklist

Use this checklist before removing the draft status from the webOS pull request.

## Automated gate

- [ ] `npm test` passes.
- [ ] `npm run validate-release` passes.
- [ ] GitHub Actions **webOS** workflow passes.
- [ ] GitHub Actions **CI** workflow passes.
- [ ] The workflow publishes a `blazzing-webos-ipk` artifact.
- [ ] The IPK contains both the app and `io.github.xoykor.blazzing.network`.
- [ ] Release validation confirms `icon.png` is 80×80 and `largeicon.png` is 130×130.

## Simulator matrix

Record every installed Simulator version that is actually tested. Do not mark a
version as supported only because the app packages successfully.

| Simulator | Resolution | Boot/UI | Remote navigation | M3U | Xtream | Pluto | Player | Artwork cache | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ____ | 1280×720 | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| ____ | 1920×1080 | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| ____ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |
| ____ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ | ☐ |

For each tested Simulator:

- [ ] Home screen renders without clipping at 1280×720.
- [ ] Home screen renders without clipping at 1920×1080.
- [ ] Directional navigation never loses focus.
- [ ] OK activates buttons and checkboxes.
- [ ] Back returns to the previous logical screen.
- [ ] Search Apply/Clear works with the remote.
- [ ] Yellow key / F toggles favorites.
- [ ] Crossing the end of a 48-card window loads the next window and keeps focus.
- [ ] Crossing backward restores focus to the previous window.
- [ ] M3U categories, search and favorites work.
- [ ] A deliberately invalid stream exposes retry instead of trapping the UI.
- [ ] Xtream server + username can be remembered, while password remains empty after returning.
- [ ] Pluto Live and VOD catalogues load when the regional service is reachable.
- [ ] Artwork failure falls back to the initial tile without breaking navigation.

## Large M3U

The low-memory 128 MiB path depends on the packaged JavaScript service and cannot
be proven by browser-only fallback.

- [ ] Load a small M3U and verify normal playback.
- [ ] Load a playlist larger than 8 MiB and verify the paged-service path is used.
- [ ] Load or synthesize a playlist near the 128 MiB ceiling.
- [ ] Verify only 48 cards are present at one time.
- [ ] Verify next/previous window traversal remains responsive.
- [ ] Verify search works against the disk-backed playlist.
- [ ] Verify favorites work across page changes.
- [ ] Force/restart the service and verify the playlist reindexes automatically.
- [ ] Verify a playlist above 128 MiB is rejected cleanly.

## Playback

- [ ] Live stream starts.
- [ ] HLS stream starts.
- [ ] VOD starts.
- [ ] Episode starts.
- [ ] Pause/resume works with OK.
- [ ] VOD/episode progress resumes after leaving and reopening.
- [ ] Completed media clears the resume point.
- [ ] Xtream `direct_source` failure tries the generated provider URL.
- [ ] If all routes fail, **Tentar novamente** is focusable and functional.
- [ ] Returning from player restores the previous catalog/detail screen.

## Persistence and privacy

- [ ] Favorites survive app restart.
- [ ] Resume points survive app restart.
- [ ] Artwork cache survives app restart and stays bounded.
- [ ] Clearing remembered Xtream profile removes server + username.
- [ ] Xtream password is never found in localStorage/IndexedDB.
- [ ] M3U media URLs are not persisted by favorites/progress.
- [ ] Pluto JWT/signed media URLs are not persisted.
- [ ] Pairing session ciphertext is deleted after successful pickup.

## Real LG TV matrix

This section is required before claiming codec/HLS compatibility.

| TV model | webOS version | Resolution | Live/HLS | VOD | Remote | Result |
| --- | --- | --- | --- | --- | --- | --- |
| ____ | ____ | ____ | ☐ | ☐ | ☐ | ☐ |
| ____ | ____ | ____ | ☐ | ☐ | ☐ | ☐ |

On each TV:

- [ ] Install the CI-produced IPK.
- [ ] App launches from the launcher.
- [ ] Packaged network service starts and responds.
- [ ] Remote Back key is handled correctly.
- [ ] HLS playback works with representative Live, Xtream and Pluto streams.
- [ ] Video/audio codecs used by target providers are supported.
- [ ] 30+ minutes of playback does not leak enough memory to destabilize the app.
- [ ] Repeated catalog navigation does not accumulate decoded artwork indefinitely.
- [ ] App recovers from network loss and provider errors without restart.

## Distribution

- [ ] Final app icon and store artwork approved.
- [ ] Seller Lounge title/description/privacy text prepared.
- [ ] Supported webOS versions are based on tested hardware/simulators.
- [ ] Version is bumped once for the release candidate.
- [ ] Both CI workflows are green on the release candidate commit.
- [ ] Final IPK artifact is archived.
