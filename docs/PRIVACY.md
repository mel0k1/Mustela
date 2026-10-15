# Privacy model

Privacy in Mustela is a configuration surface, not a slogan. The long-term rule: anything that stores, sends or fingerprints must be explicit, visible and switchable.

## Principles

1. **No silent traffic.** The engine contacts nothing beyond the URIs the user opens. No telemetry, no update pings, no safe-browsing calls unless explicitly enabled.
2. **Ephemeral by option.** `--private` runs a fully ephemeral context: cookies, cache and storage live in memory and die with the process.
3. **Storage is permission.** Cookies, localStorage, IndexedDB, service workers — each becomes an independent switch, per-profile and later per-site.
4. **Fingerprint surface is a budget.** Settings that reduce uniqueness — fonts, WebGL, canvas, timezone exposure — are documented, combinable and measurable.
5. **User data is local.** Profiles, history and settings live on disk in open formats. No cloud sync unless the user builds it themselves.

## Current state (v0.4)

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

### Clearing browsing data

`Ctrl+Shift+Del` clears cookies, memory and disk cache, local/session storage and IndexedDB for both contexts at once. The kinds live in the core as `DataKind` flags and `ClearRequest`, so a future UI can offer granular clear with a time range.

### Per-site overrides

`~/.config/mustela/sites.toml` overrides the global policy per host:

```toml
[example.org]
javascript = "deny"

[tracker.example.org]
cookies = "deny"
```

- JavaScript supports `allow` and `deny`; the site rule wins over `privacy.javascript`.
- Cookies support `deny`: after every load of that host its cookies are removed, so no session survives. `allow` is intentionally not offered — WebKit applies accept policies globally and Mustela does not fake per-domain behavior it cannot enforce.
- A rule for `example.org` also covers `www.example.org` and deeper subdomains; the nearest matching rule wins.
- Resolution lives in the core (`resolve_site`) and is the exact API the Kiwi browser will drive with its own UI. The shell demonstrates it with a site-rules window (`Ctrl+Shift+S`); buttons apply to the selected row, or to the current site when nothing is selected.

### Hot reload

`settings.toml` and `sites.toml` are watched while the shell runs. Edits apply within a fraction of a second: the cookie policy re-applies to both contexts, hardening and per-site rules re-apply to every open tab, turning history on recreates its store. ITP may require a restart to fully apply — WebKit expects it to be configured before the profile is used.

### History

History is stored by the core (`HistoryStore`) inside the default profile. Private tabs never reach it — `PrivacyPolicy::records_history` returns false for them — and `privacy.history_enabled = false` turns recording off entirely.

### What stays constant

- The shell toolbar has a Private toggle that switches the default for new tabs at runtime; it is session-scoped and never persisted.
- The status label in the toolbar shows the active policy: current tab mode, cookie policy and ITP state.
- No telemetry exists anywhere in the codebase, and this bullet is enforced by review.

## Next steps

Fingerprint-surface switches (WebGL, canvas, remote fonts, referrer trimming) with a measurable budget — see stage M2.5 of the [roadmap](ROADMAP.md).
