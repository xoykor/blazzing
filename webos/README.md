# Blazzing for LG webOS

Early webOS port of Blazzing.

Architecture: [../docs/WEBOS_ARCHITECTURE.md](../docs/WEBOS_ARCHITECTURE.md)

## Current scope

Implemented:

- packageable web app skeleton;
- remote directional navigation;
- Back handling;
- HTML5 video wrapper;
- encrypted phone-pairing client using the production Blazzing Worker;
- AES-256-GCM decryption in the TV app.

Not implemented yet:

- QR rendering;
- M3U catalog parser/UI;
- Xtream;
- packaged network JS service;
- persistence;
- store submission.

## Package

Install the current webOS CLI first. Then:

```fish
cd webos
./package.fish
```

The generated IPK is written to `webos/dist`.

## Install on a configured TV

After adding the TV as an ares device:

```fish
ares-install --device <device-name> dist/*.ipk
ares-launch --device <device-name> io.github.xoykor.blazzing
```

The exact device setup is intentionally not automated because Developer Mode
credentials belong to the developer machine.
