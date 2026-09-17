#!/usr/bin/env python3
from pathlib import Path

# Version.
path = Path('CMakeLists.txt')
s = path.read_text()
old = 'project(visual_iptv_x11 VERSION 1.2.16 LANGUAGES C)'
new = 'project(visual_iptv_x11 VERSION 1.3.0 LANGUAGES C)'
assert old in s and 'VERSION 1.3.0' not in s
path.write_text(s.replace(old, new, 1))

# English changelog.
path = Path('CHANGELOG.md')
s = path.read_text()
assert not s.startswith('## 1.3.0')
entry = '''## 1.3.0 — 2026-09-17

Visual rendering and interaction overhaul:

- introduces a shared Cairo/Pango rendering layer for antialiased rounded surfaces, gradients and proportional UTF-8 typography while retaining the native C17/X11 architecture;
- makes IPTV catalog cards responsive to available width and modernizes search caret metrics, top tabs, sidebar navigation, loading states and card metadata;
- adds reusable UI motion primitives for hover/focus transitions and smooth scrolling, with dedicated regression coverage;
- redesigns the startup hub with correct UTF-8 rendering, modern service cards and pointer hover feedback;
- finishes the IPTV login, saved-profile panel, metadata/details surface and embedded-player HUD using the same visual system;
- modernizes Pluto TV header, channel grid, loading/status states and player controls, and prevents cards from being obscured by the status footer;
- preserves existing keyboard/mouse navigation, providers, thumbnail scheduling and playback behavior while replacing legacy bitmap-font presentation paths;
- validates the redesign through Release tests, ASan/UBSan, Flatpak builds and automated Xvfb screenshot QA at 1600×900.

'''
path.write_text(entry + s)

# Brazilian Portuguese changelog.
path = Path('CHANGELOG.pt-BR.md')
s = path.read_text()
assert not s.startswith('## 1.3.0')
entry = '''## 1.3.0 — 2026-09-17

Revisão ampla da renderização visual e das interações:

- introduz uma camada compartilhada de renderização Cairo/Pango com superfícies arredondadas antialiasadas, gradientes e tipografia proporcional UTF-8, mantendo a arquitetura nativa C17/X11;
- torna os cards do catálogo IPTV responsivos à largura disponível e moderniza cursor da busca, abas superiores, navegação lateral, estados de carregamento e metadados dos cards;
- adiciona primitivas reutilizáveis de movimento da UI para transições de hover/foco e rolagem suave, com cobertura de regressão dedicada;
- redesenha o hub inicial com UTF-8 correto, cards modernos de serviço e feedback de hover pelo mouse;
- conclui a modernização do login IPTV, painel de perfis salvos, detalhes/metadados e HUD do player incorporado com o mesmo sistema visual;
- moderniza cabeçalho, grid de canais, estados de carregamento/status e controles do player da Pluto TV, impedindo que cards fiquem escondidos pela barra inferior;
- preserva navegação por teclado/mouse, providers, scheduler de miniaturas e comportamento de reprodução enquanto substitui caminhos visuais antigos baseados em fontes bitmap;
- valida o redesign com testes Release, ASan/UBSan, builds Flatpak e QA automatizado por screenshots em Xvfb a 1600×900.

'''
path.write_text(entry + s)

# AppStream metadata.
path = Path('flatpak/io.github.xoykor.Blazzing.metainfo.xml')
s = path.read_text()
assert '<release version="1.3.0"' not in s
needle = '  <releases>\n'
release = '''  <releases>\n    <release version="1.3.0" date="2026-09-17">\n      <description>\n        <p>Introduces the shared Cairo/Pango visual renderer, responsive IPTV cards, modernized hub/login/details/player UI, smooth interaction primitives and a matching Pluto TV redesign.</p>\n      </description>\n    </release>\n'''
assert needle in s
path.write_text(s.replace(needle, release, 1))
