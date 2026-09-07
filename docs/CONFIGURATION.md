# Configuração e diagnóstico

A aplicação não requer arquivo de configuração. Overrides técnicos são fornecidos por variáveis de ambiente.

## `VIPTV_MPV_DEBUG`

Ativa diagnóstico detalhado do backend mpv e dos eventos relevantes de input/foco.

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

O backend sanitiza URLs presentes nas mensagens recentes do mpv antes de mantê-las em memória/logs próprios.

## `VIPTV_MPV_RENDERER`

Seleciona um caminho gráfico alternativo do mpv.

Valores úteis:

```text
gpu         comportamento padrão (também obtido sem variável)
gpu-next    usa --vo=gpu-next,gpu
next        alias de gpu-next
x11         usa --vo=x11
software-x11 alias de x11
```

Exemplo:

```fish
set -lx VIPTV_MPV_RENDERER gpu-next
./build/visual-iptv
```

Sem override, o backend usa:

```text
--vo=gpu
--gpu-context=x11
```

## `VIPTV_MPV_HWDEC`

Override de hardware decoding. Valores aceitos pelo backend:

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

Padrão:

```text
auto-safe
```

Exemplo para diagnóstico sem hardware decode:

```fish
set -lx VIPTV_MPV_HWDEC no
./build/visual-iptv
```

## `VIPTV_NO_AUDIO`

Se estiver definida, cria o player sem saída de áudio.

```fish
set -lx VIPTV_NO_AUDIO 1
./build/visual-iptv
```

## Variáveis `VIPTV_TEST_*`

Existem variáveis com prefixo `VIPTV_TEST_` usadas por testes e automações locais da aplicação. Elas não fazem parte da interface estável de configuração para usuário final e podem mudar junto da suíte de testes.
