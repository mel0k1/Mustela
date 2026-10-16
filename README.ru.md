# Mustela

![ci](https://github.com/mel0k1/Mustela/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-2f74c0)
![license](https://img.shields.io/badge/license-LGPL--2.1-e0a03d)
![language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![status](https://img.shields.io/badge/status-M5_browsing-basics-e0a03d)

Приватный кроссплатформенный браузерный движок на базе WebKit.

English version: [README.md](README.md)

## Зачем

Существующие движки заставляют выбирать между приватностью, покрытием платформ и скоростью. Mustela сохраняет зрелое ядро WebKit и надстраивает над ним настраиваемый слой приватности, где Linux и Windows — равноправные цели сборки.

- **Приватность по выбору.** Любое хранилище, сетевой вызов или поверхность для фингерпринтинга — это переключатель, а не обещание. Где возможно — выключено по умолчанию.
- **Кроссплатформенность.** WebKitGTK на Linux, WinCairo-вариант WebKit на Windows. Одна кодовая база, одна система сборки.
- **Скорость.** Тонкий эмбеддер над WebKit: без вшитых сервисов, фонового трафика и лишней стоимости запуска.

## Статус

`v0.10.0` закрывает M6: Global Privacy Control (`privacy.gpc`) отправляет заголовок `Sec-GPC` через web-расширение GTK-порта и выставляет `navigator.globalPrivacyControl` на обоих портах; блок-лист трекеров (~60 доменов, `privacy.blocklist` + опциональный `privacy.blocklist_file`) компилируется в контент-фильтры на обоих портах; контекстное меню собрано самим Mustela на обоих портах (навигация, секции ссылки/картинки/медиа, действия редактирования по hit-test); F11 переключает полноэкранный режим окна, HTML5-fullscreen работает на GTK; session restore восстанавливает вкладки после перезапуска — с полной историей назад/вперёд на GTK и адресами вкладок на Windows (`shell.session_restore`). Ядро получило DoH-кодек (RFC 8484) как задел под secure DNS — пока ни одна сборка WebKit не открывает хук резолвера; сьют — 322 проверки.

`v0.9.1` завершает M5: загрузки сохраняются в `downloads.dir` (или системную папку Downloads) без коллизий имён и с прогрессом в статус-баре, `downloads.mode = "ask"` открывает диалог сохранения; зум на Ctrl+плюс/минус/0 (30–300%, дефолт `shell.zoom`); запросы разрешений (геолокация, камера, микрофон, уведомления) проходят через новую политику ядра — глобальные `deny`/`ask`/`allow` по типу плюс per-site правило `permissions` в `sites.toml`, запоминаемое из выбора "всегда/никогда"; невалидные TLS-сертификаты спрашивают на GTK (`security.tls_strict`), HTTP-аутентификация запрашивает логин/пароль на обоих портах; упавший web-процесс дважды авторелоадится с защитой от crash-петли на обоих портах. Пин Windows-заголовков вырос до 86 за счёт auth/credential-набора; сьют — 245 проверок.

`v0.9.0` сделал движок настоящей браузерной платформой: попапы и `target=_blank` открываются табами и наследуют приватный режим исходной вкладки, `alert`/`confirm`/`prompt`/beforeunload показываются нативными диалогами с санитизацией текста, поиск по странице (Ctrl+F) работает с количеством совпадений и закольцовыванием на обоих портах, при наведении на ссылку в статус-баре виден её адрес.

`v0.8.1` добавил кроссплатформенный бенчмарк: `mustela-bench-win` гоняет тот же замер загрузок на WinCairo C API — скрытое host-окно с non-persistent data store, те же CLI, JSON-статистика и бюджетные коды выхода; CI на каждый пуш компилирует его против запиненных апстрим-заголовков WebKit. Второй полный gap-обзор движка — в [docs/GAPS.md](docs/GAPS.md).

База v0.7 — Windows-порт: `src/shell/win/` встраивает апстримный WinCairo-вариант WebKit через его C API — табы, адресная строка с поиском, персистентный профиль в `%APPDATA%\mustela`, приватные табы на неперсистентных data store, политика куки, ITP, hardening-переключатели, весь fingerprint-бюджет (user scripts плюс скомпилированный content filter), история, очистка browsing-data и hot-reload настроек — а ядро приватности v0.6 (политика куки, ITP, hardening, per-site overrides, fingerprint-бюджет, hot-reload) по-прежнему решает всё через `PrivacyPolicy` в ядре.

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

Сьют покрывает round-trip настроек, инварианты приватности, резолв политики разрешений, матчинг правил сайтов (включая per-site permissions), экранирование истории, fingerprint-пресеты, browsing-data kinds, нормализацию URL, перф-модуль (статистика, кодек записей, бюджеты) и правила санитизации ui-text (диалоги, поиск, имена файлов). На Linux в том же прогоне собираются полный GTK-шелл и `mustela-bench`, а bench грузит локальную фикстуру под xvfb; на Windows CI дополнительно компилирует `shell_win.cpp` и `bench_win.cpp` против запиненных апстрим-заголовков WebKit C API.

## Структура

```
src/core/        настройки, политика приватности, политика разрешений, правила сайтов, fingerprint-бюджет, browsing-data kinds, нормализация URL, санитизация ui-text (переносимое ядро без UI-зависимостей)
src/shell/       оболочка-эмбеддер, по каталогу на порт
src/shell/gtk/   порт Linux (WebKitGTK)
src/shell/win/   порт Windows (WinCairo через WebKit C API)
src/bench/       харнесс замера загрузок страниц (варианты GTK и WinCairo)
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
