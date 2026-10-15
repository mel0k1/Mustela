# Privacy model

Privacy in Mustela is a configuration surface, not a slogan. The long-term rule: anything that stores, sends or fingerprints must be explicit, visible and switchable.

## Principles

1. **No silent traffic.** The engine contacts nothing beyond the URIs the user opens. No telemetry, no update pings, no safe-browsing calls unless explicitly enabled.
2. **Ephemeral by option.** `--private` runs a fully ephemeral context: cookies, cache and storage live in memory and die with the process.
3. **Storage is permission.** Cookies, localStorage, IndexedDB, service workers — each becomes an independent switch, per-profile and later per-site.
4. **Fingerprint surface is a budget.** Settings that reduce uniqueness — fonts, WebGL, canvas, timezone exposure — are documented, combinable and measurable.
5. **User data is local.** Profiles, history and settings live on disk in open formats. No cloud sync unless the user builds it themselves.

## Current state (v0.1)

- `--private` flag: ephemeral WebKit context, nothing is persisted.
- Default context writes under the standard WebKitGTK user directory.
- No telemetry exists anywhere in the codebase, and this bullet is enforced by review.

## Next steps

See stage M2 of the [roadmap](ROADMAP.md): per-profile policies, first-party-only mode and the first set of storage switches.
