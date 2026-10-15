# Mustela

![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-17-00599C)
![status](https://img.shields.io/badge/status-M2_privacy_core-e0a03d)

Приватный кроссплатформенный браузерный движок на базе WebKit.

English version: [README.md](README.md)

## Зачем

Существующие движки заставляют выбирать между приватностью, покрытием платформ и скоростью. Mustela сохраняет зрелое ядро WebKit и надстраивает над ним настраиваемый слой приватности, где Linux и Windows — равноправные цели сборки.

- **Приватность по выбору.** Любое хранилище, сетевой вызов или поверхность для фингерпринтинга — это переключатель, а не обещание. Где возможно — выключено по умолчанию.
- **Кроссплатформенность.** WebKitGTK на Linux, WinCairo-вариант WebKit на Windows. Одна кодовая база, одна система сборки.
- **Скорость.** Тонкий эмбеддер над WebKit: без вшитых сервисов, фонового трафика и лишней стоимости запуска.

## Статус

`v0.4.0` — ядро приватности: политика куки (`always` / `no-third-party` / `never`), Intelligent Tracking Prevention, hardening-переключатели (JavaScript, hyperlink auditing, DNS prefetching, autoplay) и очистка browsing-data одной клавишей (Ctrl+Shift+Del). Тулбар показывает активную политику с первого взгляда.

Куда всё движется — в [дорожной карте](docs/ROADMAP.md).

## Экосистема

- [Kiwi](https://github.com/mel0k1/Kiwi) — приватный браузер поверх Mustela. Mustela — движок, Kiwi — продукт.

## Сборка (Linux)

```bash
sudo apt install build-essential cmake pkg-config libwebkit2gtk-4.1-dev
./scripts/build-linux.sh
./build/mustela
```

Запуск с эфемерным профилем в памяти:

```bash
./build/mustela --private
```

Полное руководство, включая Windows: [docs/BUILDING.md](docs/BUILDING.md).

## Структура

```
src/core/        настройки, политика приватности, модели browsing-data (переносимое ядро без UI-зависимостей)
src/shell/       оболочка-эмбеддер, по каталогу на порт
src/shell/gtk/   порт Linux (WebKitGTK)
src/shell/win/   порт Windows (WinCairo, в работе)
docs/            архитектура, модель приватности, роадмап, сборка
```

## Документация

- [Архитектура](docs/ARCHITECTURE.md)
- [Модель приватности](docs/PRIVACY.md)
- [Дорожная карта](docs/ROADMAP.md)
- [Сборка](docs/BUILDING.md)

## Лицензия

Mustela распространяется по [GNU LGPL-2.1](LICENSE) — той же лицензии, что и WebKit, поэтому код движка можно переносить в проект без лицензионных конфликтов.
