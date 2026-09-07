# Publicando no GitHub

[English](GITHUB_PUBLISHING.md)

O repositório inclui `.gitignore`, licença MIT, documentação, CI e templates.

## GitHub CLI

```sh
git init
git add .
git commit -m "Initial public release"
git branch -M main
gh repo create blazzing --public --source=. --remote=origin --push
```

## Repositório existente

```sh
git remote add origin https://github.com/xoykor/blazzing.git
git push -u origin main
```

Antes de publicar, revise `git status --short`, `git diff --cached --stat` e procure dados privados acidentalmente adicionados.
