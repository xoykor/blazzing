# Configuration and diagnostics

[Português (Brasil)](CONFIGURATION.pt-BR.md)

Blazzing does not require a configuration file. Technical overrides are provided through environment variables.

## `VIPTV_MPV_DEBUG`

Enables detailed mpv-backend diagnostics and relevant input/focus event logging.

Fish:

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

Bash/Zsh:

```sh
VIPTV_MPV_DEBUG=1 ./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

The backend sanitizes URLs found in recent mpv messages before retaining them in its own in-memory diagnostics/log output.

## `VIPTV_MPV_RENDERER`

Selects an alternate mpv graphics path.

Useful values:

```text
gpu          default behavior
gpu-next     uses --vo=gpu-next,gpu
next         alias for gpu-next
x11          uses --vo=x11
software-x11 alias for x11
```

Default behavior:

```text
--vo=gpu
--gpu-context=x11
```

## `VIPTV_MPV_HWDEC`

Hardware-decoding override. Accepted values:

```text
no
auto
auto-safe
auto-copy
auto-copy-safe
vaapi
vaapi-copy
vulkan
vulkan-copy
```

Default:

```text
auto-safe
```

Example with hardware decoding disabled:

```sh
VIPTV_MPV_HWDEC=no ./build/visual-iptv
```

## `VIPTV_NO_AUDIO`

When set, the player starts without audio output.

```sh
VIPTV_NO_AUDIO=1 ./build/visual-iptv
```

## `VIPTV_TEST_*`

Variables prefixed with `VIPTV_TEST_` are used by tests and local automation. They are not part of the stable end-user configuration interface and may change with the test suite.
