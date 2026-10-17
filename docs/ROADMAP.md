# Roadmap

Milestones are capability-based; dates are intentionally not assigned.

## M0 — Bootstrap (current)

- Repository, CMake build, LGPL-2.1 licensing.
- `mustela` shell: GTK port opens a web view, `--private` runs an ephemeral profile.
- Windows port stub, build docs for both platforms.

## M1 — Shell baseline (done)

- Navigation: back, forward, reload, URL bar, loading progress. (done)
- Search fallback for non-URL input. (done)
- Basic tab model: open, close, switch, private tabs behind policy. (done)
- Mustela-owned profile directory, separate from system defaults. (done)
- Settings file (TOML) loaded at startup, saved on exit. (done)

## M1.5 — Engine APIs and shell demos (done)

- History store in the core, private tabs excluded at the policy level. (done)
- Runtime privacy toggle in the shell UI. (done)
- History window in the shell (Ctrl+H) with clear action. (done)
- Settings hot-reload on file change. (moved to M2)

## M2 — Privacy core (done)

- Cookie accept policy per context: `always`, `no-third-party`, `never`. (done)
- Intelligent Tracking Prevention on the persistent profile. (done)
- Hardening switches: JavaScript, hyperlink auditing, DNS prefetching, autoplay gesture. (done)
- Browsing-data kinds (`DataKind`, `ClearRequest`) and a clear action in the shell (Ctrl+Shift+Del). (done)
- Settings hot-reload on file change. (done)
- Per-site overrides: `sites.toml`, JavaScript allow/deny, cookie deny with origin wipe, subdomain matching, `resolve_site` in the core, site-rules window in the shell. (done)

## M2.5 — Fingerprint budget (done)

- Switches for WebGL, canvas readback, remote fonts and referrer trimming, enforced through user scripts and a compiled content filter. (done)
- A measurable fingerprint budget documented in PRIVACY.md: surface-by-surface trade-offs, presets and honest limits. (done)
- Per-site fingerprint rules on top of `SiteRules`: `allow` exempts a host, `deny` forces the strict set. (done)

## M3 — Windows port (done)

- Shell on the upstream WinCairo flavor of WebKit through its C API: tabs, URL bar with search fallback, window geometry persistence. (done)
- Privacy-core parity: cookie accept policy, ITP, hardening switches, per-site overrides, fingerprint budget via user scripts and a compiled content filter, history, clear-browsing-data, settings hot-reload. (done)
- Persistent profile under `%APPDATA%\mustela`, private tabs on non-persistent data stores. (done)
- Windows shell compiled against pinned upstream WebKit C API headers on every push. (done)

Known gaps, tracked for the next pass: per-site cookie denial works through the global accept policy (the C API has no per-origin cookie wipe on Windows), clear-data covers cookies and storage but not the disk cache, and there is no site-rules window — edit `sites.toml`, hot-reload applies it live.

## CI (done)

- Linux: full GTK shell build against `webkit2gtk-4.1` plus the core test suite on every push. (done)
- Windows: core and tests under MSVC. (done)
- Windows shell syntax check against 86 pinned upstream WebKit C API headers (`scripts/fetch-win-headers.ps1`). (done)

## M4 — Performance (done)

- Startup budget: time-to-first-map on Linux, time-to-first-paint on Windows, enforced against `perf.startup_budget_ms` and logged. (done)
- Process model tuning: `perf.process_model` (`per-view` or `shared`) wired through `WebKitWebContext` and `WKContextSetProcessModel`. (done)
- Memory ceiling: process-tree RSS sampling every minute (`/proc` on Linux, `GetProcessMemoryInfo` + Toolhelp on Windows), `perf.memory_ceiling_mb` drives a status-bar warning, samples go to the log. (done)
- Page-load timing between provisional start and finish navigation, appended to `perf.jsonl` in the profile. (done)
- `mustela-bench` harness: real loads through a minimal offscreen WebKit shell, JSON stats (min/median/p95/max), budget exit code; runs under `xvfb-run` in CI on every push. (done)
- Regression tracking: `scripts/perf_check.py` summarizes `perf.jsonl` and compares medians against a recorded baseline with tolerance. (done)

Known gaps, tracked for the next pass: the ceiling only warns — there is no tab discarding (WebKitGTK and the WinCairo C API expose no per-view suspend), `perf.process_model` is applied at startup only, and the WinCairo bench compiles in CI but does not run there (GitHub Windows runners ship no WinCairo runtime).

## M4.5 — Windows bench and gap analysis (done)

- `mustela-bench-win`: the same page-load harness on the WinCairo C API — hidden host window, non-persistent data store, identical CLI (`--url/--runs/--max-ms/--out`), identical JSON stats and exit codes. (done)
- Built with the Windows SDK branch in CMake (`mustela-bench-win`) and compile-checked against the pinned upstream headers on every push. (done)
- Second full engine review at v0.8.0 recorded in [GAPS.md](GAPS.md): browsing-platform gaps (popups, dialogs, downloads, find, zoom, permissions, certificates), privacy deepening candidates (GPC, tracking blocklist, secure DNS), performance leftovers and the Kiwi API surface, with a suggested milestone order. (done)

## M5 — Browsing basics (done)

- New windows and popups open as tabs on both ports: GTK `create` signal with related views, Windows `WKPageUIClient` V6 `createNewPage`; privacy context and data store are inherited from the originating tab, `ready-to-show`/`showPage` focuses the popup, JS `window.close()` closes the tab. (done)
- JavaScript dialogs are native: alert, confirm, prompt (with editable default value) and beforeunload confirmation on both ports — GTK `script-dialog`, Windows `WKPageUIClient` async dialog listeners. (done)
- Dialog text, find queries and download file names pass through `mustela::ui_text`: control characters stripped, CR/LF normalized, byte-capped with UTF-8-safe truncation — unit-tested in the core. (done)
- Find in page: find bar (Ctrl+F) with next/previous (Enter/Shift+Enter), match counts, case-insensitive wrap-around search, re-search on tab switch — GTK `WebKitFindController`, Windows `WKPageFindClient` + `WKPageFindString` with the match overlay. (done)
- Status-bar link preview on both ports: GTK `mouse-target-changed`, Windows `mouseDidMoveOverElement`/`setStatusText`. (done)
- Downloads on both ports: GTK `download-started`, Windows `WKContextDownloadClient`; destination resolved from `downloads.dir` (platform Downloads folder by default) with collision-free file names, `downloads.mode = "ask"` opens a save dialog (GtkFileChooser / GetSaveFileName); progress in the status bar, cancel on dialog refusal. (done)
- Zoom on both ports: Ctrl+plus/minus/0 steps 30%–300% — GTK `zoom_level`, Windows `WKPageSetScaleFactor` re-applied on commit; default from `shell.zoom`. (done)
- Permission policy in the core (`permissions.hpp`): global choices `deny`/`ask`/`allow` per permission kind (`permissions.geolocation`, `.camera`, `.microphone`, `.notifications`, all deny by default) resolved against a per-site `permissions` rule in `sites.toml`; prompts offer allow/deny/always/never, "always" and "never" are remembered per host (private tabs never write rules). GTK handles geolocation, camera/microphone and notifications via `permission-request`; Windows covers geolocation and camera/microphone through `WKPageUIClient` V6, notifications stay auto-denied (the pinned C API exposes no per-request decision call). (done)
- Certificate and auth surface: GTK `accept-certificate` prompts before continuing on invalid certificates (strict TLS policy default, `security.tls_strict = false` ignores), `authenticate` prompts for user/password with session credentials; Windows `didReceiveAuthenticationChallenge` (pinned headers extended with the auth/credential set) prompts for basic auth and rejects server-trust challenges — the WinCairo C API has no certificate-acceptance call. (done)
- Web process crash recovery on both ports: GTK `web-process-terminated`, Windows `webProcessDidTerminate`; up to two automatic reloads per tab with a crash-loop guard, status message with the reason, manual Ctrl+R afterwards; the counter resets on a successful load. (done)
- Known gaps kept for later: HTML5 fullscreen, context menu, session restore (see [GAPS.md](GAPS.md)).

## M6 — Privacy deepening and browsing completion (done)

- Global Privacy Control on both ports: `privacy.gpc` (on by default) injects a document-start user script exposing `navigator.globalPrivacyControl`, and a GTK web-process extension (`src/webext/gtk`) adds the `Sec-GPC: 1` header to every request; the WinCairo C API cannot mutate request headers, so Windows ships the JS surface only. (done)
- Tracking-domain blocklist in the core (`blocklist.hpp`): ~60 built-in ad/tracker domains plus an optional `privacy.blocklist_file`, compiled per profile into content rules on both ports (GTK `WebKitUserContentFilterStore`, Windows `WKUserContentExtensionStore`) and enforced through the same user-content controllers as the fingerprint filters; suffix matching, domain validation and rule-set building are unit-tested. (done)
- Secure DNS groundwork in the core (`doh.hpp`): RFC 8484 wire codec — query build, response parse with name compression, A/AAAA/CNAME answers, base64url framing (`query_uri`) — plus the `privacy.dns_over_https` setting; neither WebKit flavor exposes a DoH resolver hook through its public API, so runtime wiring waits for a proxy milestone and the gap stays tracked in [GAPS.md](GAPS.md). (done, runtime deferred)
- Context menu on both ports: GTK `context-menu` handler rebuilds the menu from curated stock actions (navigation, link/image/media sections, editing actions by hit-test); Windows `WKPageContextMenuClient` V2 builds the same menu from `WKContextMenuItem` stock tags with the same hit-test sections. (done)
- Fullscreen on both ports: F11 toggles window fullscreen (GTK `gtk_window_fullscreen`, Windows borderless-on-monitor with placement restore); HTML5 element fullscreen wires through GTK `enter-fullscreen`/`leave-fullscreen` — the pinned WinCairo C API exposes no production fullscreen client, so element requests stay GTK-only. (done)
- Session restore in the core (`session.hpp`): JSON session file with tabs (url, title, history lists, opaque per-port state) and the active index; GTK persists `WebKitWebViewSessionState` blobs and restores full back/forward history per tab, Windows restores tab URLs (the C API has no session-state restore); private tabs are never saved, `shell.session_restore` switches the feature. (done)
- Known gaps kept: public-suffix guard in `SiteRules` and a settings load report (M6 leftovers), per-origin cookie wipe and disk cache clear on Windows (upstream-dependent), secure-DNS runtime hook (blocked on upstream APIs).

## M7 — Local proxy network layer (done)

- Local proxy engine in the core (`proxy_server.hpp`): loopback-only HTTP proxy with CONNECT tunneling and plain-HTTP forwarding; tracker domains are refused at connection setup with 403 (suffix match through the shared blocklist), plain-HTTP requests are additionally checked per request; the engine never intercepts TLS, so tunnel payloads stay opaque to it. (done)
- Upstream chaining: `proxy.mode = "upstream"` routes the engine through an external proxy directly, and `proxy.upstream` (with `user`/`password` for Basic auth) chains the local proxy through it when both are configured. (done)
- DoH resolver in the core (`DohResolver`): the RFC 8484 codec from M6 gets a runtime hook — the transport is injected as a callable, A answers are TTL-cached (30–600 s), IP literals bypass DNS, and a failed DoH lookup fails the connection instead of leaking the name to the system resolver. GTK supplies a libsoup transport, Windows a WinHTTP transport. (done)
- GTK wiring: `proxy.mode = "local"` starts the engine-internal proxy and points both data managers at it via `webkit_website_data_manager_set_network_proxy_settings` (custom mode, loopback bypass list); `proxy.mode = "upstream"` points WebKit at the upstream directly; settings hot-reload restarts the proxy with the new config. (done)
- Windows wiring: `http_proxy`/`https_proxy`/`no_proxy` are set before `WKContext` creation (the WinCairo curl backend reads the environment); DoH goes through WinHTTP; proxy settings apply at startup, a restart is required after changes. (done)
- Test suite 322 → 386 checks: proxy config keys, proxy URL parsing, resolver behavior (cache, failure, NXDOMAIN) and loopback integration tests — blocked CONNECT and GET, subdomain blocking, bidirectional tunnel echo, request forwarding with origin-form rewrite, upstream-chained tunnel and forward, fail-closed 502 on DoH failure. (done)
- Honest limits: HTTPS blocking is connection-level (authority from CONNECT — no URL inspection without interception), plain HTTP blocking is request-level; `upstream`-only mode bypasses the local engine (no blocking, no DoH there); the Windows curl proxy hook is compile-verified but cannot execute in CI (no WinCairo runtime on runners).

## M8 — Settings hygiene and suffix guard (done)

- Public-suffix guard in the core (`psl.hpp`): a compact bundled public-suffix list — common gTLDs, popular ccTLD second levels, wildcard (`*.ck`, `*.kobe.jp`) and exception (`!www.ck`) rules — with `is_public_suffix` and `registrable_domain`. Registry-suffix sections in `sites.toml` are skipped on load, `set()` refuses them and rule lookup stops climbing at the effective top level, so one entry can never cover a whole registry; IP literals and `localhost` remain addressable. (done)
- Settings load report in the core (`SettingsReport`): `load_settings` reports the schema `version` (a file written by a newer engine warns), applied/unknown/invalid key counts and the offending keys themselves; `save_settings` stamps `version = 1`; GTK prints issues on stdout, Windows through the debug output — on startup and on every hot-reload. (done)
- Test suite 386 → 468 checks: PSL matching (exact, wildcard, exception, unlisted TLD, IP and localhost cases), the site-rules guard (load skip, set refusal, save round-trip) and the load report (unknown keys, invalid values, version warning, clean round-trip, missing file). (done)

## M9 — Browser dressing (done)

- Devtools on both ports: Ctrl+Shift+I / F12 toggles the Web Inspector — GTK through `webkit_web_view_get_inspector` (`show`/`close` via `webkit_web_inspector_is_attached`), Windows through `WKPageGetInspector` + `WKPageShowWebInspectorForTesting` (show-only in the pinned C API — close the inspector window itself); `shell.devtools` (on by default) drives `enable-developer-extras` / `WKPreferencesSetDeveloperExtrasEnabled` and re-applies on hot-reload. (done)
- Branded error pages on both ports: failed main-frame loads render a Mustela-styled page instead of WebKit's stock error page. The core builds the HTML (`error_page.hpp` — heading, detail, failing URL, all sanitized and HTML-escaped, plus a Try-again action that re-navigates the failing URL); GTK fills the failed load through `webkit_web_view_load_alternate_html`, Windows through `WKPageLoadHTMLString`; cancellations are skipped, and the tab skips history/perf recording for the error page itself. (done)
- Bookmarks in the core (`bookmarks.hpp`): a `BookmarkStore` in the session/history style — TSV file with escaped fields in the profile, `add`/`remove`/`contains` with URL de-duplication and title refresh; unit-tested including a persistence round-trip. (done)
- Bookmarks UI on both ports: toolbar star that reflects the current tab (Ctrl+D toggles, click toggles) and a bookmarks window (Ctrl+Shift+O) with open on double-click, open and remove actions — GTK tree view, Windows list box; private tabs may bookmark but bookmarks are the user's explicit data. (done)
- Favicons on GTK: the profile enables the WebKit favicon database (`webkit_web_context_set_favicon_database_directory`), tabs show the site icon through `notify::favicon` + `webkit_web_view_get_favicon`, and non-private tabs persist it as a PNG under `profiles/default/favicons/<host>.png`. (done, GTK-only)
- Honest limits: the pinned WinCairo C API exposes no favicon surface, so Windows tabs and history stay text-only (tracked in [GAPS.md](GAPS.md)); on Windows the inspector is one-way (no `WKInspectorShow`/`IsVisible` in the pin); reloading an error page keeps history silent for that retry.

## M10 — Platform dressing II (done)

- Web notifications on GTK: the `show-notification` signal renders each notification through `GNotification` on a registered `GApplication` (`G_APPLICATION_NON_UNIQUE`, no D-Bus name ownership); titles and bodies pass `sanitize_dialog_text` before display, the permission gate stays the M5 policy (`permissions.notifications` + per-site rule), and without a session bus the registration fails quietly and notifications are dropped instead of shown. (done, GTK-only — the pinned WinCairo C API has no notification display surface)
- Printing on GTK: Ctrl+P opens the GTK print dialog through `WebKitPrintOperation` (`webkit_print_operation_new` + `run_dialog` against the active view). (done, GTK-only)
- Screenshots on GTK: Ctrl+Alt+S captures the visible region through `webkit_web_view_get_snapshot` and writes `mustela-screenshot-YYYYMMDD-HHMMSS.png` into the downloads directory (`downloads.dir` or the user Downloads folder), confirmed in the status bar. (done, GTK-only)
- Full public-suffix data in the core: the compact table in `psl.cpp` is replaced by the official PSL — 10,080 exact, 311 wildcard and 8 exception rules, IDN rules converted to Punycode at generation time, version stamped as `kPslVersion` and exposed through `psl_version()`; the lookup runs binary search over the sorted tables; `scripts/gen_psl.py` regenerates `psl_data.inc` from publicsuffix.org. (done)
- Honest limits: the real data tightened semantics — under `*.kobe.jp` + `!city.kobe.jp` the domain `city.kobe.jp` is now registrable (the compact table used to treat it as an exact suffix); save page (MHTML) and spellchecking stay GTK candidates (closed in M10.5); printing and screenshots are GTK-only (no print/snapshot surface in the pinned C API).
- Test suite 513 → 532 checks: full-PSL behavior (private-section rules like `blogspot.com`/`cloudfront.net`, wildcard bases like `*.compute.amazonaws.com`, Japanese city exceptions, Punycode `рф`, `in-addr.arpa`, registrable-domain chains). (done)

## M10.5 — GTK leftovers (done)

- Save page as MHTML on GTK: Ctrl+S opens a native save dialog (`GtkFileChooserNative`, overwrite confirmation, MHTML filter, default folder from `downloads.dir` or the user Downloads folder) with the name from the sanitized page title — fallback to the host, then `page.mhtml` — and writes the page through `webkit_web_view_save_to_file` in `WEBKIT_SAVE_MODE_MHTML`; the async result is confirmed in the status bar. (done, GTK-only — no save surface in the pinned WinCairo C API)
- Spellchecking on GTK: `shell.spellcheck` (on by default) enables WebKit's spell checker on both the normal and the private context through `webkit_web_context_set_spell_checking_enabled`; `shell.spellcheck_languages` (empty = the locale default) takes a comma-separated tag list like `en_US, ru_RU`, applied through `webkit_web_context_set_spell_checking_languages`; both re-apply on settings hot-reload. WebKit's own spelling context-menu items (guesses, ignore, learn) surface through the standard editing section for editable content. (done, GTK-only)
- Honest limits: dictionaries come from the system enchant setup (e.g. `hunspell-en-us`, `hunspell-ru`) — with none installed the switch is a silent no-op; MHTML captures the current DOM without scripts and is a snapshot, not a live page; the WinCairo C API has neither surface in the pin. (GTK-only)
- Test suite 532 → 538 checks: spellcheck settings parsing (round-trip, partial file, defaults). (done)

## K1 — Core public surface (done)

- Declared surface under `include/mustela/`: an umbrella `mustela.hpp` over `settings` (the `config.hpp` TOML module), `privacy`, `permissions`, `site_rules`, `fingerprint`, `browsing_data`, `perf`, `history`, `url`, `bookmarks` and `profile`, plus a self-contained `version.hpp` with `MUSTELA_CORE_API_VERSION` — the surface version moves independently of the engine release number. Forwarding headers keep the implementation in `src/core` as the single source of truth; consuming needs only `-Iinclude` and a link against the static core, and `cmake --install` ships `libmustela_core.a` with the headers. (done)
- `Profile` handle in the core (`profile.hpp`): a root directory expands into the standard layout (`history.dat`, `bookmarks.tsv`, `session.json`, `perf.jsonl`, `filters/`, `favicons/`), carries its own `PrivacyPolicy` copy, and manages its file-backed stores through `ensure_dirs` / `has` / `clear` per `ProfileStore`; `default_profile_root()` computes the platform default (`XDG_DATA_HOME`/`$HOME/.local/share` on Linux, `%LOCALAPPDATA%` on Windows). Cookies, cache and web storage stay port-owned — WebKit data managers and the existing `ClearRequest` mapping cover those. (done)
- Per-site dressing next to the policy: `SitePolicy` grows `zoom` (0.3–3.0, clamped on load and on `set`; 0 = inherit), `user_agent` and `css` (single-line quoted strings) — parsed from and saved to the same `sites.toml`, resolved with the existing wholesale closest-rule semantics, and guarded by the same public-suffix rules. (done)
- Harness adoption and limits: the GTK shell builds every profile path through `Profile` (same on-disk layout, `settings.toml`/`sites.toml` stay at the config root); the Windows shell keeps its own path plumbing for now; no port applies per-site zoom/UA/CSS yet — that lands with the Kiwi embedder, which is the API's primary consumer. (done, ports follow)
- Test suite 538 → 588 checks: site-rule dressing keys (parse, clamp, save round-trip, empty-erasure), the `Profile` handle (paths, ensure/has/clear, policy holder, default root) and the public surface (umbrella include, API version). (done)

## M11 — Remaining GAPS leftovers (done, v0.17.0)

- Search engines over the settings surface: `SearchEngine` entries (`keyword`, `name`, `url_template`) parsed from `[search.engines]` dotted keys and saved back, `search.default` picks the fallback engine (unknown keyword falls back to the first entry; no engines at all falls back to the legacy `shell.search_template`). `resolve_entry(text, settings)` becomes the single entry-pipeline function — keyword shortcuts (`!g query`), `://` passthrough, host-like text and percent-encoded queries against the default engine — replacing `normalize_url` in both the GTK URL bar and the Windows `load_url_text`; `normalize_url` itself stays in the public API for embedders. (done)
- Profile registry: `ProfileRegistry` (entries of id/name/root, `default_id`, `find`/`upsert`/`remove`) reads and writes `profiles.toml` in the config root; a missing file falls back to the single `default` entry with the historical `profiles/default` layout, so existing installs keep their data. The GTK harness resolves its data dir through the registry (`shell.profile` selects the entry, relative roots resolve against the data base); the Windows shell keeps its direct path plumbing. (done)
- Form autofill storage: `AutofillStore` keeps domain + field + value entries with use counters and last-used timestamps in the profile (`autofill.tsv`, TSV with the shared escape conventions); `find` matches exact domains and subdomains, ordered by use then recency; `upsert`/`remove`/`clear` round out the API and `ProfileStore::Autofill` wires it into the profile layout and clearing. The filling UI is a Kiwi-side feature; passwords stay out of the core on purpose (OS keyring is the right home for secrets). (done, Kiwi UI pending)
- GTK drag & drop: the shell window accepts `text/uri-list` and `text/plain` drops and routes them through `resolve_entry` into the active tab; notebook tabs are drag-reorderable (`gtk_notebook_set_tab_reorderable` with the tab vector resynced on `page-reordered`). Drops onto the page area remain owned by WebKit's internal DnD so editable-field drags keep working. The WinCairo pin has no drag surface. (done, GTK-only)
- Download pause/resume: re-verified against the pinned 2.54 header set — no `pause`/`resume` surface on WebKitGTK 4.1 and no `WKDownloadResume` in the C API pin; the GAPS entry stays an accepted upstream limitation. (upstream-dependent, unchanged)
- Public surface: `search.hpp` and `autofill.hpp` join the umbrella, `MUSTELA_CORE_API_VERSION` moves 1 → 2 (additive: `ProfilePaths`/`ProfileStore`/`Settings` grew). Test suite 588 → 680 checks: engine parsing and resolution (keywords, defaults, encoding, round-trip, malformed keys), the registry (missing-file fallback, round-trip, find/upsert/remove, root resolution) and the autofill store (upsert counters, ordering, subdomain match, escaping, profile integration). (done)

## M12 — CI levers and harness closure (done, v0.18.0)

- Sanitizer track in CI: the core test suite builds with `-fsanitize=address,undefined -fno-omit-frame-pointer` (Debug, shell and bench off) and runs strict — leak detection on, halt on first error; verified locally 702/702 clean before landing. (done)
- Static analysis track: clang-tidy over `src/core` with a curated check set (`.clang-tidy`: `clang-analyzer-*`, `performance-*`, `bugprone-use-after-move`, `modernize-use-nullptr`, `readability-container-size-empty`) — advisory (`continue-on-error`) until findings are burned down; the compile database comes from a CMake configure with the shell off. (done, advisory)
- Release packaging: a tag-triggered job (`v*`) runs after all four check tracks, builds the full GTK tree with `MUSTELA_WEBEXT_DIR=/usr/lib/mustela/webext`, runs ctest, and packages through CPack — a deb with shlib-deps-generated dependencies (shell, bench, web extension, static core + headers) and a portable tarball — then attaches both to the GitHub Release with generated notes. The Windows bundle stays unbundled until a WinCairo runtime source is pinned (header pin alone does not make an exe runnable); the release notes state this. (done, Linux)
- CMake gained install rules for `mustela`, `mustela-bench` and the web extension module, a `MUSTELA_WEBEXT_DIR` cache path (defaults to the build directory for dev runs), and a CPack block (package name, license, deb metadata). (done)
- Per-site dressing applied by the harness: `SiteDecision` now carries `zoom`/`user_agent`/`css` out of `resolve_site`; the GTK shell applies them at commit — zoom overrides the tab level only when a rule sets one, the user-agent overrides with fallback to the captured per-tab default, and CSS lands as a user style sheet (`webkit_user_style_sheet_new`) alongside the script stack. The site-rules window grew a Zoom column and a dressing row (zoom / user agent / CSS entries with an Apply dressing button, prefilled from the selected rule). The Kiwi embedder gets the identical one-call surface. (done)
- Autofill loop closed in the reference harness: `shell.autofill` (fill, off by default) runs the core fill script through `webkit_web_view_evaluate_javascript` after a non-private page finishes loading (matches by name/id, only empty visible fields, fires input/change events, reports the count in the status bar); `shell.autofill_save` (capture, off by default) injects the capture script and receives submits through a `mustelaForm` script-message handler — non-password, non-hidden fields capped at 200 chars, up to 32 per submit, saved into `AutofillStore` with use counters. Private tabs never fill or capture; the core gained `AutofillStore::by_domain`, `autofill_fill_script` and `autofill_capture_script`. (done)
- Public surface: `MUSTELA_CORE_API_VERSION` moves 2 → 3 (additive: `SiteDecision` dressing, autofill script builders, `Settings` autofill keys). Test suite 680 → 702 checks: `resolve_site` dressing passthrough, `by_domain` ordering/limits, fill-script escaping (quotes, backslashes, newlines, control chars), capture-script shape, autofill settings round-trip. (done)
- Honest limits: the fill runs in the main frame only; capture listens to `submit` events (programmatic submits without events are missed); the Windows shell has no dressing application or autofill UI (core storage and `sites.toml` hot-reload still work); clang-tidy is advisory.

## V1 — Embedding view surface (done, v0.19.0)

- Port-neutral public header `include/mustela/view.hpp` (`MUSTELA_VIEW_API_VERSION = 1`, self-contained like the core `version.hpp`): no WebKit types anywhere — `ViewConfig` (settings, privacy policy, site rules, profile, web-extension dir, downloads dir), `ViewSession` (one browser context pair), `View` (one web view), `SessionCallbacks` (download destinations and progress) and `ViewCallbacks` (navigation, loading, commits, error pages, favicons, link hover, process termination, JS dialogs, permissions, certificates, auth, popups, close). The widget surfaces to the embedder as `native_widget()` — a `GtkWidget*` on the GTK port. `zoom_step(current, direction)` ships as a pure function.
- GTK implementation `src/view/gtk/view_gtk.cpp` behind `libmustela_view_gtk.a`, built outside the shell harness (`MUSTELA_BUILD_VIEW`, on by default) so embedders consume the engine without the harness. A session owns the normal and ephemeral private contexts, the favicon database directory, the user-content filter store (remote-fonts filter plus the compiled tracker blocklist), the local/upstream proxy with its DoH transport, TLS policy, the process model, cookie policy, ITP, the Sec-GPC web extension wiring and download tracking. A view wires the full WebKitGTK signal set: hardening, fingerprint payloads, GPC script and per-site dressing (JS, zoom, user-agent, CSS) applied at commit, branded error pages on load failure, per-origin cookie wipe on denied origins, crash recovery with two guarded reloads, favicon persistence into the profile, popup creation as related views handed to the embedder.
- The shell harness stays as-is: `mustela-shell` remains the engine's test playground; the view library is the product path Kiwi consumes.
- Honest limits (first slice): no find-in-page, notifications display, devtools, context menu, fullscreen, MHTML/print/screenshot surfaces, session restore or autofill wiring in the view API yet — each lands in the engine when Kiwi's roadmap reaches it; Windows has no view library yet (the WinCairo C API port remains harness-only).
- CI: the Linux track runs the whole ctest suite under xvfb (the new `mustela_view_smoke` — session + view + data: URL + title/commit/zoom checks — needs a display); sanitizers and clang-tidy configure with the view off (WebKit stays out of the sanitizer run).
- Test suite stays 702 core checks; the view smoke adds 12 runtime checks under ctest.

## M13 — Tab discarding (done, v0.22.0)

- Core policy (`src/core/discard.hpp`, pure and unit-tested): `pick_discard_candidates` walks the tab list and returns the victims for one pass — never the active tab, never a loading tab, never an already-discarded one, only background tabs idle for 10+ minutes (configurable `min_idle_ms`), ordered oldest-idle first, capped by `max_per_pass` and a `min_alive` floor. The trigger is `perf.memory_action = "discard"` plus `ceiling_exceeded` over the sampled process-tree RSS; the default `warn` keeps today's status-bar-only behavior. (done)
- GTK port: the memory timer builds the candidate list (active page, load state, idle time since the last activation), and a picked tab is discarded — session state serialized to base64 into the tab record, the notebook page replaced by a "Tab unloaded — switch to it to restore" placeholder. Returning to the tab schedules an idle undiscard (page mutation never happens inside the GTK `switch-page` emission): a fresh view is created through the normal path, the captured `WebKitWebViewSessionState` is restored with full back/forward history, and the placeholder page is swapped out at the same position. Session save/restore writes discarded tabs as plain URLs, so they survive a restart. (done)
- Windows port: the same policy over the same core call — `discard_tab_index` releases the WKUserContentController/WKPreferences/WKView references after saving uri/title/scale, keeps the tab strip entry, and `undiscard_tab_index` rebuilds through `make_tab` with the saved URL (tab-URL restore, matching the port's session-restore honesty); `switch_tab` restores synchronously before touching the view. (done)
- Settings: `perf.memory_action = "warn" | "discard"` (default `warn`), validated on load like the other enum keys and round-tripped by `save_settings`. (done)
- Honest limits: per-view suspend stays impossible upstream — discard is destroy-and-recreate; a discarded private tab loses its in-memory state by design (its store is ephemeral); on Windows a restored tab reloads its URL instead of full session state (no C API session-state surface, same as session restore there).
- Test suite 702 → 730 checks: memory-action parsing and round-trip, warn/discard gating, ceiling gating, victim selection order, loading/discarded/active/min-idle exclusions, per-pass and min-alive caps. (done)

## V4 — View slice 3: fullscreen, notifications, MHTML (done, v0.23.0)

- View API 3 → 4 (`MUSTELA_VIEW_API_VERSION`, additive): three surfaces Kiwi needs next move from the GTK harness into the port-neutral header. `ViewCallbacks::enter_fullscreen`/`leave_fullscreen` forward the HTML5 element fullscreen requests (the embedder fullscreens its own window — the engine never touches window state); `ViewCallbacks::notification(title, body)` renders `show-notification` with both strings through `sanitize_dialog_text` (256/512 caps, same as the harness); `View::save_mhtml(path, done)` writes the current page through `webkit_web_view_save_to_file` in `WEBKIT_SAVE_MODE_MHTML` and reports success through a `SaveCallback`. `suggested_save_name(title, uri)` builds the default file name from the sanitized page title with host and `page` fallbacks (the harness dialog keeps its own copy — the helper treats the storage-layer `download` sentinel as a missing name). (done)
- GTK implementation in `src/view/gtk/view_gtk.cpp`: `enter-fullscreen`/`leave-fullscreen` handlers match the upstream vtable (`gboolean`, FALSE — the embedder decides what fullscreen means); `show-notification` returns TRUE once the embedder callback consumed the notification; the save context mirrors the session-data async pattern (`GFile` + `save_to_file_finish`). (done)
- Smoke coverage extended: fullscreen enter/leave and notification callbacks fire from signal emission, the save round-trip writes a non-empty MHTML container (`MIME-Version`/`multipart/related` markers), `suggested_save_name` covers title, extension-skip, host and page fallbacks; API version check asserts ≥ 4.
- Honest limits: MHTML is save-only (WebKitGTK has no public MHTML load path), captures the current DOM without scripts — a snapshot, not a live page; notifications still require the permission flow (`permissions.notifications` + per-site rule) before `show-notification` fires; Windows has no view library yet, so none of the three surfaces exist over the WinCairo C API (fullscreen and notifications have no production C API client there, MHTML has no save call).

## M14 — Windows runtime story (done, v0.24.0)

- **Windows view library** (`src/view/win/view_win.cpp`) implements `include/mustela/view.hpp` over the pinned WinCairo C API — the same split as the GTK port, reusing the shell's embedding patterns (state/navigation/UI/find/context-menu clients, data stores, find/download/permission plumbing). `ViewConfig.parent_window` (view API 4 → 5) carries the embedder's HWND. Honest surface parity: fullscreen, notifications, MHTML saving, JS evaluation, edit commands, devtools docking, per-origin cookie wipes and website-data records have no production C API surface in the pin — the Windows view degrades honestly (no-ops, `save_mhtml` reports failure, `fetch_website_data` returns an empty list, loading state is proxied through estimated progress).
- **Windows release bundle**: the tag-triggered `windows-bundle` job runs `scripts/fetch-win-runtime.ps1` (pinned WebKitRequirements release + latest green `Windows-64-bit-Release-Build` build), generates import libraries from the runtime DLLs (`scripts/make-win-import-libs.ps1` — dumpbin export tables → .def → lib.exe), links `mustela.exe` and `mustela_view_win.lib` with MSVC and attaches an experimental zip to the Release with a README pointing at the runtime pair. GitHub runners compile and link; first execution belongs to a manual pass or a self-hosted runner.
- The runtime pin (WebKit 8ece203 headers + requirements release + buildbot build number) stays a three-way lock recorded in `docs/BUILDING.md` while the bundle ships experimental.

## Kiwi — product browser

- The product browser lives in the separate [Kiwi](https://github.com/mel0k1/Kiwi) repository; mustela-shell stays the engine's test harness.
- Engine capabilities it consumes: stable settings API, privacy policy with per-site rules, profiles, packaging hooks.
