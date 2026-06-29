# Design notes

## Requirement → SDK mapping

| Spec requirement | Mechanism |
|---|---|
| §2 Intercept account creation before save | `IMTUserSink::HookUserAdd(IMTUser*)` (mutable user, pre-commit) |
| §3 Read assigned group | `IMTUser::Group()` |
| §4 Generate next login | `CSeriesAllocator::Allocate` (`last+1` or `start`) |
| §5 Assign login | `IMTUser::Login(UINT64)` |
| §6 Persist counter | atomic JSON state file + SQL mirror |
| §7 Prevent duplicates | single `std::mutex` around read-increment-persist (the plugin is one process; all `UserAdd` paths funnel through it) + `UserGet` existence skip |
| §8 Validate range | `max_login`; returns `MT_RET_USR_LOGIN_EXHAUSTED` |
| §9 Logging | `IMTServerAPI::LoggerOut` |
| Errors: unknown group | pass-through, or `MT_RET_ERR_NOTFOUND` if `reject_unconfigured_groups` |
| Errors: duplicate login | existence scan skips taken logins (`MT_RET_USR_LOGIN_EXIST` not needed — we avoid it) |

The hook receives a **mutable** `IMTUser*` and its `MTAPIRES` return value can
**abort** the creation, which is exactly what the range/unknown-group rejection
paths need.

## Why a single in-process mutex is enough

A server plugin runs inside the single MT5 trade-server process. Every account
creation — dealer, Manager API, Web API, automation — ends up calling
`UserAdd`, which fires `HookUserAdd` on our sink in that same process. So a
process-local critical section fully serialises allocation. Distributed locks
(Redis/SQL row locks) would only be needed if multiple independent servers
shared one user database, which is not the standard MT5 topology.

## Reserve-before-commit

`HookUserAdd` fires before the record is committed. We advance and persist the
counter at assignment time (reserve-ahead) rather than waiting for a post-commit
event. This guarantees no two concurrent creations receive the same login. The
trade-off is that a creation that fails *after* the hook leaves a gap in the
sequence. Gaps are acceptable; duplicates are not (§7 is the critical
requirement).

## Storage model

- **Source of truth:** `SeriesPlugin.state.json`, written atomically
  (`write tmp` → `MoveFileEx REPLACE_EXISTING`). Holds `{ counters: { group: last_login } }`.
- **SQL mirror:** best-effort, on a dedicated worker thread with its own queue
  and reconnect/backoff. Allocation never waits on it. The DB is for reporting
  and cross-system visibility, not correctness.

## Matching

Group rules match either exactly or, with a trailing `*`, by prefix
(case-insensitive, matching MT5 group semantics). When several rules match, the
one with the longest `match` string wins (most specific). This lets you set a
broad default (`real\*`) and override specific sub-groups.

## Files

```
SeriesPlugin/
  SeriesPlugin.cpp     entry points (MTServerAbout / MTServerCreate), DllMain
  PluginInstance.*     IMTServerPlugin + IMTUserSink, HookUserAdd
  SeriesAllocator.*    config load, counters, lock, allocation, state file
  SqlMirror.*          ODBC background mirror
  Json.h               minimal JSON parser/writer (no deps)
  Util.h               UTF-8<->wide, atomic file IO, module dir
config/SeriesPlugin.config.json   sample config (deploy beside the DLL)
sql/schema.sql                    SQL mirror table
```

## Known risk / open validation

Whether MT5 honours a login written in `HookUserAdd` — and the requirement that
series numbers lie within the server's configured login range — cannot be
confirmed from the SDK headers alone and must be verified on a real server. See
the README "Validate on a test server first" section.
