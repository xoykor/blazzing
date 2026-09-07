# Publicando no GitHub

O repositório já inclui `.gitignore`, licença MIT, documentação, CI, templates de issue/PR e não depende de arquivos gerados de build.

## Com GitHub CLI

Na raiz do projeto:

```fish
git init
git add .
git commit -m "Initial public release"
git branch -M main
gh repo create visual-iptv --public --source=. --remote=origin --push
```

O comando `gh` solicitará autenticação caso ela ainda não esteja configurada.

## Com um repositório já criado

Depois de criar um repositório vazio no GitHub, use a URL exibida pela própria página:

```fish
git init
git add .
git commit -m "Initial public release"
git branch -M main
git remote add origin SUA_URL_DO_REPOSITORIO
git push -u origin main
```

## Antes do primeiro push

Confira o que será publicado:

```fish
git status --short
git diff --cached --stat
```

Procure acidentalmente por dados privados:

```fish
git grep -n -i -E 'password|senha|username|usuario|https?://'
```

Ocorrências legítimas existem no código e nos testes; o objetivo é conferir que nenhuma delas contém credenciais ou hosts privados reais.

## Configuração recomendada do repositório

Descrição sugerida:

```text
Cliente IPTV visual nativo para Linux/X11 em C17, com Xtream/M3U e mpv incorporado por JSON IPC.
```

Topics sugeridos:

```text
iptv c x11 linux mpv xtream m3u sqlite cmake
```

Após o primeiro push, confirme que a workflow `CI` conclui os jobs Release e ASan/UBSan.
