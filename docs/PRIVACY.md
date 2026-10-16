# Privacy model

Privacy in Mustela is a configuration surface, not a slogan. The long-term rule: anything that stores, sends or fingerprints must be explicit, visible and switchable.

## Platform parity (v0.7)

The privacy core is one codebase; ports only translate it into WebKit calls. The GTK port (WebKitGTK) is the reference implementation. The Windows port (WinCairo over the WebKit C API) applies the same `PrivacyPolicy`, the same `sites.toml` and the same script payloads:

| Surface | GTK port | Windows port |
|---------|----------|--------------|
| Persistent profile | `~/.local/share/mustela/profiles/default` | `%APPDATA%\mustela\profiles\default` with explicit per-kind directories |
| Private tabs | shared ephemeral context | per-tab non-persistent `WKWebsiteDataStore` |
| Cookie accept policy | `WebKitCookieManager` on both contexts | `WKHTTPCookieStoreSetHTTPCookieAcceptPolicy` on every store |
| ITP | before context creation | `WKWebsiteDataStoreSetResourceLoadStatisticsEnabled` on every store |
| Hardening switches | `WebKitSettings` properties | `WKPreferences` setters (hyperlink auditing and DNS prefetching through deprecated-but-functional C API entries) |
| Per-site JavaScript and fingerprint | `resolve_site` + user content manager | same `resolve_site` + `WKUserContentController` |
| Fingerprint payloads | `WebKitUserScript` + compiled filter | `WKUserScript` + `WKUserContentExtensionStore` filter — same JSON, same scripts |
| Per-site cookie denial | origin wipe on load | global accept policy only — the C API has no per-origin cookie wipe; noted as a known gap |
| Clear browsing data | full kind mapping | cookies and storage; disk cache not reachable through the C API yet |
| History, settings, sites, hot-reload | GFileMonitor | `ReadDirectoryChangesW` |
| Popups and dialogs | related view inherits the ephemeral context; GTK message dialogs | new tab inherits the originator's data store; message boxes and a modal prompt |
| UI text from the web | dialog bodies and find queries sanitized in the core (`ui_text`) before display | same |

Popups are privacy-relevant by construction: a `target=_blank` tab opened from a private tab stays on the private (ephemeral) context or data store on both ports, and per-site policies are resolved for the popup URL exactly like for any other tab. Dialog text and find queries never pass raw from the page to the UI: `mustela::ui_text` strips control characters, normalizes line endings and caps length on UTF-8 boundaries before anything is shown.

Everything above the translation layer — what counts as private, what gets recorded, what a site is allowed to do — is decided once, in the core, and both ports inherit it. New privacy switches land in the core first; ports follow.

## Principles

1. **No silent traffic.** The engine contacts nothing beyond the URIs the user opens. No telemetry, no update pings, no safe-browsing calls unless explicitly enabled.
2. **Ephemeral by option.** `--private` runs a fully ephemeral context: cookies, cache and storage live in memory and die with the process.
3. **Storage is permission.** Cookies, localStorage, IndexedDB, service workers — each becomes an independent switch, per-profile and later per-site.
4. **Fingerprint surface is a budget.** Settings that reduce uniqueness — fonts, WebGL, canvas, timezone exposure — are documented, combinable and measurable.
5. **User data is local.** Profiles, history and settings live on disk in open formats. No cloud sync unless the user builds it themselves.

## Current state (v0.6)

- Default mode is normal browsing: privacy is off unless asked for.
- The default profile is Mustela-owned and persistent under `~/.local/share/mustela/profiles/default`, never in WebKit's system location.
- Private tabs are opt-in: `Ctrl+Shift+P` opens one, `--private` on the command line makes the first tab private. Private tabs run in a shared ephemeral WebKit context — cookies, cache and storage stay in memory and die with the process.
- The policy lives in `~/.config/mustela/settings.toml` and is read through `PrivacyPolicy` in the core; the shell only asks the policy, never decides on its own.

### Cookies

The cookie accept policy is a single setting applied to every context, normal and ephemeral alike:

| `privacy.cookie_policy` | Behavior |
|--------------------------|----------|
| `always` | accept all cookies, first-party and third-party |
| `no-third-party` | accept first-party, reject third-party (default) |
| `never` | accept nothing; sessions do not survive a reload |

### Tracking prevention

`privacy.tracking_prevention = true` (default) enables WebKit's Intelligent Tracking Prevention on the persistent profile before any context uses it.

### Hardening switches

Applied to every web view at creation time:

| Setting | Default | WebKit surface |
|---------|---------|----------------|
| `privacy.javascript` | true | `enable-javascript` |
| `privacy.hyperlink_auditing` | false | `enable-hyperlink-auditing` — `<a ping>` traffic stays off |
| `privacy.dns_prefetching` | false | `enable-dns-prefetching` — no speculative DNS traffic |
| `privacy.block_autoplay` | true | `media-playback-requires-user-gesture` |

### Fingerprint budget

Fingerprinting reads what a browser exposes: which rendering APIs answer, which fonts arrive, what a canvas round-trip returns, which URL the previous page leaked. Each switch below removes one entropy source and costs one class of compatibility — the budget is this trade-off table, not a marketing number.

| Setting | Default | Removes | Costs |
|---------|---------|---------|-------|
| `privacy.fingerprint_webgl` | false | `canvas.getContext('webgl*')` returns null — no GPU model, no renderer strings, no WebGL canvas hashing | WebGL maps, games and 3D viewers break |
| `privacy.fingerprint_canvas` | false | canvas readbacks return blank data: `toDataURL`/`toBlob` render an empty canvas of the same size, `getImageData` and `readPixels` come back zeroed | canvas exports become empty; page pixels are not recoverable |
| `privacy.fingerprint_fonts` | false | every remote font download is blocked (compiled WebKit content filter, resource type `font`) | typography falls back to system fonts and pages look different |
| `privacy.fingerprint_referrer` | `same-origin` | cross-origin navigations and subresources carry no Referer; `no-referrer` removes it everywhere including same-site | hotlink-protected images and referrer-checked flows may break |

Reference presets, also exposed in the core as `fingerprint_preset()` for a future UI:

| Preset | Meaning |
|--------|---------|
| `off` | all four switches off |
| `standard` | referrer trimming only — the default |
| `strict` | everything blocked, referrer `no-referrer` |

Mechanics, without WebKit patches:

- WebGL and canvas defenses are user scripts injected at document start into every frame through `WebKitUserContentManager` — the same injection path a content blocker uses. Patched functions report themselves as native (`function toDataURL() { [native code] }`), but a determined script can still detect the patch; masking is best-effort, not invisibility.
- Remote fonts are blocked with a compiled `WebKitUserContentFilter`, not a CSS hack — the request never happens.
- Referrer trimming injects a `meta name="referrer"` tag ahead of the page's own head, so it wins tree-order against the page's meta; a `Content-Security-Policy: referrer` directive on the page still beats meta injection.
- Blocking remote fonts hides font downloads, not the list of installed fonts: text-metric probing still measures system fonts. The budget is additive, not absolute — stacking switches removes sources, not the whole fingerprint.

Per-site rules extend the budget: `fingerprint = "allow"` exempts a host (for maps, games, canvas apps that legitimately need the surface) and `fingerprint = "deny"` forces the strict set on it regardless of the global switches.

```toml
[maps.example.com]
fingerprint = "allow"

[suspicious.example]
fingerprint = "deny"
```

The status label shows `fp` whenever any fingerprint defense is active globally.

### Clearing browsing data

`Ctrl+Shift+Del` clears cookies, memory and disk cache, local/session storage and IndexedDB for both contexts at once. The kinds live in the core as `DataKind` flags and `ClearRequest`, so a future UI can offer granular clear with a time range.

### Per-site overrides

`~/.config/mustela/sites.toml` overrides the global policy per host. Hosts are guarded by the bundled public-suffix list: a section for a registry suffix like `[co.uk]` is skipped on load and refused on save, so one entry can never silently cover a whole registry — rules belong to registrable domains (`example.org`, `foo.co.uk`). Rule lookup climbs the host only until the effective top level, and IP addresses plus `localhost` stay addressable for local development.

```toml
[example.org]
javascript = "deny"

[tracker.example.org]
cookies = "deny"

[maps.example.com]
fingerprint = "allow"
```

- JavaScript supports `allow` and `deny`; the site rule wins over `privacy.javascript`.
- Cookies support `deny`: after every load of that host its cookies are removed, so no session survives. `allow` is intentionally not offered — WebKit applies accept policies globally and Mustela does not fake per-domain behavior it cannot enforce.
- A rule for `example.org` also covers `www.example.org` and deeper subdomains; the nearest matching rule wins.
- Resolution lives in the core (`resolve_site`) and is the exact API the Kiwi browser will drive with its own UI. The shell demonstrates it with a site-rules window (`Ctrl+Shift+S`); buttons apply to the selected row, or to the current site when nothing is selected.

### Hot reload

`settings.toml` and `sites.toml` are watched while the shell runs. Edits apply within a fraction of a second: the cookie policy re-applies to both contexts, hardening and per-site rules re-apply to every open tab, turning history on recreates its store. ITP may require a restart to fully apply — WebKit expects it to be configured before the profile is used.

### Process model and isolation

The perf switch `perf.process_model` has privacy consequences and is documented as such. The default `per-view` gives every web view its own web process — the upstream WebKitGTK default and the closest thing to site isolation these ports offer. Setting `shared` puts all views into one shared web process: memory drops, but a renderer compromise or a site reading shared state reaches everything open in that process. Treat `shared` as a battery-and-RAM trade-off for trusted browsing, not a default.

The performance log (`perf.log`) records only local timing and RSS numbers: durations, timestamps, and the URL of the page that finished loading. URLs in `perf.jsonl` are effectively a second history file on disk, so it is gated by the same kind of switch as history — set `perf.log = false` to stop writing it entirely, or clear browsing data habits should include it (it lives in the same profile directory).

### History

History is stored by the core (`HistoryStore`) inside the default profile. Private tabs never reach it — `PrivacyPolicy::records_history` returns false for them — and `privacy.history_enabled = false` turns recording off entirely.

### Permissions, certificates and authentication

Permission requests (geolocation, camera, microphone, notifications) default to `deny` in `settings.toml` — a site that wants your location gets silence unless you flip `permissions.geolocation` (and friends) to `ask` or `allow`. The `ask` mode shows a prompt with Allow / Deny / Always allow / Never allow; the last two write a per-site `permissions` rule into `sites.toml`, which then answers future requests for that host without asking. Private tabs prompt identically but never write rules, so an ephemeral session cannot leave a permission trail. Display capture follows the camera setting on GTK; every other unrecognized permission kind (pointer lock, XR and anything new) is denied without a prompt.

Invalid TLS certificates fail the load by default (`security.tls_strict`); the GTK port then offers an explicit allow-once prompt — the acceptance lives for that single load and is never stored. `security.tls_strict = false` ignores TLS errors entirely and exists for broken local networks, not as a daily-driver setting. HTTP authentication asks for user and password in a native dialog; credentials are created for the session only and are never written to disk. On Windows the C API offers no certificate-acceptance call, so strict TLS is the only mode there and server-trust auth challenges are rejected.

Downloads land in the platform Downloads folder (or `downloads.dir`) under a name sanitized in the core — path separators and control characters stripped — and never overwrite an existing file. The suggested name comes from the server response, so a hostile `Content-Disposition` cannot escape the destination directory.

### What stays constant

- The shell toolbar has a Private toggle that switches the default for new tabs at runtime; it is session-scoped and never persisted.
- The status label in the toolbar shows the active policy: current tab mode, cookie policy, ITP and fingerprint state.
- No telemetry exists anywhere in the codebase, and this bullet is enforced by review.

## GPC, tracker blocklist and secure DNS (v0.10)

Global Privacy Control is on by default (`privacy.gpc`). A document-start user script exposes `navigator.globalPrivacyControl` as a read-only `true` on every frame of both ports; on Linux a small web-process extension (`src/webext/gtk`) additionally appends `Sec-GPC: 1` to every outgoing request header, main frame and subresource alike. The WinCairo C API has no request-header surface, so Windows ships the JS property only — sites reading the signal get it everywhere, sites sniffing headers get it on GTK. The script follows hot-reload; the extension reads its flag at browser launch.

The tracker blocklist (`privacy.blocklist`, on by default) ships ~60 well-known ad and analytics domains as data in the core (`blocklist.hpp`) and compiles them through the same content-filter machinery as the remote-font filter: `WebKitUserContentFilterStore` on GTK, `WKUserContentExtensionStore` on Windows. Matching is suffix-based — a rule for `doubleclick.net` covers every subdomain and never touches `notdoubleclick.net`. Loads from listed domains are blocked on all resource types, including top-level navigations: typing a tracker domain into the address bar shows a blocked page. `privacy.blocklist_file` accepts an extra list, one domain per line with `#` comments; malformed lines are ignored.

Secure DNS ships as groundwork: the core carries an RFC 8484 DoH codec (`doh.hpp` — DNS query build, response parsing with name compression, A/AAAA/CNAME extraction, base64url framing) behind `privacy.dns_over_https`. Neither WebKitGTK nor the WinCairo C API exposes a resolver hook today, so no traffic flows through the setting yet; the codec is unit-tested and waiting for a transport, with a local proxy milestone as the natural place. Until then, encrypted DNS on the host is the platform resolver's job.

## Local proxy network layer (v0.11)

`proxy.mode = "local"` moves the network boundary inside the engine. A loopback-only proxy (`proxy_server.hpp` in the core, bound to 127.0.0.1, never exposed on a network interface) receives every request the WebKit network process makes. Tracker domains from the blocklist are refused at connection setup — a CONNECT to a blocked host (or any of its subdomains) gets a 403 before a single TCP packet leaves the machine, and plain-HTTP requests are additionally matched per request. Nothing is decrypted or intercepted: HTTPS tunnels stay opaque, which means no MITM certificates, no trust-store changes and no readable content in the proxy — the price is that HTTPS blocking is connection-level (by authority) while plain-HTTP blocking is request-level. Content filters from M6 stay active on top, covering subresource URLs inside pages.

When `privacy.dns_over_https` is set, the proxy resolves every host through the configured DoH server: the wire codec encodes the query, the port supplies TLS transport (libsoup on Linux, WinHTTP on Windows), answers are TTL-cached for 30–600 seconds, and IP-literal hosts skip DNS entirely. The policy is strict by design — if the DoH server is unreachable or returns an error, the connection fails with a 502 instead of silently falling back to the system resolver, so a name never leaks through plain DNS while DoH mode is on.

`proxy.mode = "upstream"` routes the engine through an external proxy (`proxy.upstream`, optionally `proxy.user`/`proxy.password` for HTTP Basic auth) without the local engine in the path; in `local` mode the same upstream is chained after the local proxy, so blocking and DoH still apply before anything reaches it. GTK applies proxy settings through `webkit_website_data_manager_set_network_proxy_settings` for both profiles and hot-reloads them; Windows writes `http_proxy`/`https_proxy`/`no_proxy` before `WKContext` creation (the curl backend reads the environment) and re-reads them on restart. Loopback traffic (`localhost`, `127.0.0.1`, `::1`) always bypasses the proxy on both ports. Credentials live in `settings.toml` in plaintext — the profile directory is user-private by convention (`0700` on Linux), and private tabs change nothing here because the proxy is profile-agnostic by design: it sees hosts, never identities, history or cookies.

## Next steps

Performance budgets, per-origin cookie wipe on Windows when the C API grows one, and the Kiwi browser driving this whole surface with a real UI — see the [roadmap](ROADMAP.md).
