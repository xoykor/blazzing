# Configuração e diagnóstico

[English](CONFIGURATION.md)

A aplicação não requer arquivo de configuração. Overrides técnicos são fornecidos por variáveis de ambiente.

## `VIPTV_MPV_DEBUG`

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

Ativa diagnóstico detalhado do backend mpv e eventos relevantes de input/foco. URLs são sanitizadas antes de serem mantidas em logs próprios.

## `VIPTV_MPV_RENDERER`

Valores:

```text
gpu
gpu-next
next
x11
software-x11
```

Padrão:

```text
--vo=gpu
--gpu-context=x11
```

## `VIPTV_MPV_HWDEC`

Valores aceitos:

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

Padrão: `auto-safe`.

## `VIPTV_NO_AUDIO`

Se definida, cria o player sem saída de áudio.

## `VIPTV_TEST_*`

Reservadas para testes e automações; não fazem parte da interface estável de usuário.
