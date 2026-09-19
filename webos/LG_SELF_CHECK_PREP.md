# LG App Self Checklist — preparation matrix

This file is a project-side preparation aid. It does **not** replace the current
LG App Self Checklist spreadsheet/template. Before submission, download the
latest official checklist and enter actual test results from the target
Simulator/TV environment.

Status legend:

- **CI evidence** — a repeatable automated check exists in this repository.
- **Manual required** — must still be exercised in Simulator/TV or reviewed by a
  human before it can be marked as passed in LG's official document.
- **N/A candidate** — may be N/A depending on the final feature set; confirm
  against the current official checklist before submission.

## Content

| Area | Project evidence | Submission status |
| --- | --- | --- |
| Bundled/private catalog content | Blazzing does not bundle private M3U/Xtream sources or provider credentials. | Manual required |
| User-provided M3U/Xtream content | Content is supplied by the user/provider and is not curated by the app. | Manual/legal review required |
| Pluto TV regional catalog | Loaded from Pluto TV at runtime. | Manual content review required |
| Restricted/inappropriate content categories | No such content is intentionally bundled by the Blazzing UI itself. | Manual review required |

Do not mark content items as passed solely from source-code tests because runtime
catalog content can vary by provider, account, region, and date.

## Mandatory UX

| LG check area | Current implementation/evidence | Status |
| --- | --- | --- |
| 4-way navigation | Central spatial navigation handles Up/Down/Left/Right on every `.focusable`; UI contract tests cover required surfaces. | CI evidence + manual required |
| OK | Focused controls are activated with OK; checkboxes/radio controls are supported. | CI evidence + manual required |
| Back inside app | Logical Back returns from player/details/catalog/forms to the previous app state. | CI evidence + manual required |
| Back on entry page | `disableBackHistoryAPI: true`; Home Back calls `webOS.platformBack()`. | CI evidence + manual on applicable webOS versions |
| Magic Remote pointer | Pointer hover resolves nested content to the nearest focusable ancestor; controls remain clickable. | CI evidence + manual required |
| Selection effect | `.focusable:focus` / `.is-focused` applies border, shadow, and scale; selected catalog groups have a distinct state. | CI evidence + visual review required |

## Recommended UX

| LG check area | Current implementation/evidence | Status |
| --- | --- | --- |
| 1920×1080 graphics | Default layout is the 1080p layout. | Manual required |
| 1280×720 graphics | Dedicated `max-width: 1366px` compact layout. | Manual required |
| Text size at 1080p | Base stylesheet is regression-tested so explicit font sizes are not below 20 px. | CI evidence + visual review |
| Text size at 720p | Compact overrides stay at 14 px or larger. | CI evidence should be retained + visual review |
| Clickable target size | Main buttons/cards are substantially larger than the recommended image-button minimums. | Visual/manual review |
| Loading cue | Pairing, M3U, Xtream, Pluto, catalog, detail and player flows display textual loading/status cues. | Source evidence + manual review |
| Wheel/list direction | Native vertical overflow is used for scrollable lists/detail text. | Manual required |
| Virtual keyboard | Text inputs use native HTML input fields. | Manual required |
| Playback controls | Visible Play/Pause, Retry (when needed), and Back controls are focusable/clickable; OK also toggles playback. | CI evidence + manual required |

## Privacy and credentials

| LG check area | Current implementation/evidence | Status |
| --- | --- | --- |
| Hardcoded passwords/tokens/private keys | `npm run validate-sensitive` scans runtime JS for named literal secrets and private-key markers. | CI evidence |
| Xtream password persistence | Password stays in memory and is cleared from the UI; storage tests verify remembered profile contains only server + username. | CI evidence + manual inspection |
| M3U favorite persistence | Raw media URLs are not persisted as favorite/progress values. | CI evidence |
| Pluto JWT/signed URLs | Kept in service memory and excluded from persisted catalog state. | CI evidence |
| Artwork cache | Persistent keys are opaque hashes; cache is bounded. | CI evidence |
| Pairing payload | AES-GCM encrypted; pairing session is removed after pickup/expiry. | CI evidence + integration test/manual review |
| Privacy policy | Draft exists in `PRIVACY.md`. | Public URL + final contact still required |

## Release/package evidence

- `npm run validate-release` checks package IDs, aligned versions, required
  package inputs, and 80×80 / 130×130 PNG icon dimensions.
- `npm run validate-compat` rejects runtime syntax that raises the intended
  legacy webOS engine floor.
- `npm run validate-sensitive` rejects obvious embedded runtime secrets.
- `npm test` runs parser, persistence, artwork, Pluto, and UI/DOM regressions.
- GitHub Actions builds a real installable IPK and uploads
  `blazzing-webos-ipk`.

## Must remain manual before submission

- execute the Simulator matrix at 1280×720 and 1920×1080;
- execute a real-TV matrix, including representative codecs/HLS;
- verify entry-page Back behavior on the webOS versions claimed as supported;
- validate Magic Remote pointer, wheel, keyboard, and focus behavior visually;
- test prolonged playback/resource usage;
- review runtime content with the QA account/region used for submission;
- complete the latest official LG checklist with those real results;
- complete the current official UX Scenario template.
