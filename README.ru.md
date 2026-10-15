# Mustela

![ci](https://github.com/mel0k1/Mustela/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-17-00599C)
![status](https://img.shields.io/badge/status-M3_wincairo-e0a03d)

Приватный кроссплатформенный браузерный движок на базе WebKit.

English version: [README.md](README.md)

## Зачем

Существующие движки заставляют выбирать между приватностью, покрытием платформ и скоростью. Mustela сохраняет зрелое ядро WebKit и надстраивает над ним настраиваемый слой приватности, где Linux и Windows — равноправные цели сборки.

- **Приватность по выбору.** Любое хранилище, сетевой вызов или поверхность для фингерпринтинга — это переключатель, а не обещание. Где возможно — выключено по умолчанию.
- **Кроссплатформенность.** WebKitGTK на Linux, WinCairo-вариант WebKit на Windows. Одна кодовая база, одна система сборки.
- **Скорость.** Тонкий эмбеддер над WebKit: без вшитых сервисов, фонового трафика и лишней стоимости запуска.

## Статус

`v0.7.0` — Windows-порт стал настоящим: `src/shell/win/` встраивает апстримный WinCairo-вариант WebKit через его C API — табы, адресная строка с поиском, персистентный профиль в `%APPDATA%\mustela`, приватные табы на неперсистентных data store, политика куки, ITP, hardening-переключатели, весь fingerprint-бюджет (user scripts плюс скомпилированный content filter), история, очистка browsing-data и hot-reload настроек. CI собирает и тестирует три дорожки: GTK-шелл на Linux, ядро под MSVC и Windows-шелл, компилируемый против запиненных апстрим-заголовков WebKit. Переносимое ядро покрыто тест-сьютом на 125 проверок (`mustela_core_tests`), а нормализация URL переехала в ядро — им будет пользоваться и [Kiwi](https://github.com/mel0k1/Kiwi).

Ядро приватности v0.6 остаётся базой: политика куки, ITP, hardening, очистка browsing-data, per-site overrides, fingerprint-бюджет и hot-reload — всё решается через `PrivacyPolicy` в ядре.

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

## Тесты

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Сьют покрывает round-trip настроек, инварианты приватности, матчинг правил сайтов, экранирование истории, fingerprint-пресеты, browsing-data kinds и нормализацию URL. На Linux в том же прогоне собирается полный GTK-шелл; на Windows CI дополнительно компилирует `shell_win.cpp` против запиненных апстрим-заголовков WebKit C API.

## Структура

```
src/core/        настройки, политика приватности, правила сайтов, fingerprint-бюджет, browsing-data kinds, нормализация URL (переносимое ядро без UI-зависимостей)
src/shell/       оболочка-эмбеддер, по каталогу на порт
src/shell/gtk/   порт Linux (WebKitGTK)
src/shell/win/   порт Windows (WinCairo через WebKit C API)
src/tests/       тест-сьют ядра (ctest)
docs/            архитектура, модель приватности, роадмап, сборка
```

## Документация

- [Архитектура](docs/ARCHITECTURE.md)
- [Модель приватности](docs/PRIVACY.md)
- [Дорожная карта](docs/ROADMAP.md)
- [Сборка](docs/BUILDING.md)

## Лицензия

Mustela распространяется по [GNU LGPL-2.1](LICENSE) — той же лицензии, что и WebKit, поэтому код движка можно переносить в проект без лицензионных конфликтов.
