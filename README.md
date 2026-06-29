# MT5 Group Series Login Allocator

A native **MetaTrader 5 server plugin** that replaces MT5's default global
account numbering with an **independent sequential login series per group**.

> Example: accounts in `real\group_a` are numbered from `786000`, accounts in
> `real\group_b` from `900000`, accounts in `demo\vip` from `5000000` — each
> group keeps its own counter.

## How it works

The plugin subscribes to user events and implements
`IMTUserSink::HookUserAdd(IMTUser* user)` — the one hook the MT5 Server API
fires **before a new account is committed**, with a *mutable* user object.

For every new account, regardless of origin (dealer in the Manager terminal,
Manager API, Web API / CRM, or an automation script):

1. Read `user->Group()`.
2. Find the configured series whose group pattern matches (most specific wins).
3. Under a lock, compute the next login (`last + 1`, or `start` for the first),
   skipping any login that already exists.
4. Write it back with `user->Login(next)` and let creation proceed.
5. Persist the counter and mirror the allocation to SQL (best-effort).

If the group is not configured, the account is left to MT5's default numbering
(unless you set `reject_unconfigured_groups`). If the series is exhausted, the
account creation is rejected with `MT_RET_USR_LOGIN_EXHAUSTED`.

Source map:
- `SeriesPlugin/PluginInstance.cpp` — the plugin + `HookUserAdd` interception
- `SeriesPlugin/SeriesAllocator.cpp` — config, counters, locking, allocation
- `SeriesPlugin/SqlMirror.cpp` — background ODBC mirror
- `SeriesPlugin/Json.h`, `Util.h` — dependency-free JSON + file/string helpers

## Building

Requirements: Visual Studio 2019 (toolset **v142**) with the C++ workload, and
the MetaTrader 5 SDK at `C:\MetaTrader5SDK` (the include path is set in the
project; change `AdditionalIncludeDirectories` if your SDK lives elsewhere).

```powershell
# from a Developer prompt, or with full MSBuild path:
msbuild SeriesPlugin.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
msbuild SeriesPlugin.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

Outputs:

| Platform | DLL                                  |
|----------|--------------------------------------|
| x64      | `bin\x64\Release\SeriesPlugin64.dll` |
| x86      | `bin\Win32\Release\SeriesPlugin.dll` |

Built with the static runtime (`/MT`), so no MSVC redistributable is needed on
the server. Links `odbc32.lib` (present on every Windows install).

> Newer Visual Studio? Right-click the solution → *Retarget Projects*, or set
> `<PlatformToolset>` (v143 for VS2022) in `SeriesPlugin.vcxproj`.

## Installing on your MT5 server

A server plugin runs **on the MT5 server machine** and is managed from the
**MetaTrader 5 Administrator** application (not the Manager terminal). You need:

- Access to the server (to copy files onto it), and
- The **MetaTrader 5 Administrator** with admin rights.

Steps:

1. **Copy the files onto the server.** Put the DLL that matches your server's
   architecture (almost always the 64-bit one, `SeriesPlugin64.dll`) into the
   MT5 server's plugins folder — typically `...\bin\plugins\` under the server
   installation. Copy `SeriesPlugin.config.json` into the **same folder** as the
   DLL.

   > The plugin reads `<dll-folder>\SeriesPlugin.config.json` and writes its
   > counters to `<dll-folder>\SeriesPlugin.state.json`. Both live next to the DLL.

2. **Open the MetaTrader 5 Administrator** and go to the **Plugins** section.
   The `SeriesPlugin64` module should now appear in the list of available plugin
   modules (because the DLL is in the plugins folder).

3. **Add a plugin configuration** that references the `SeriesPlugin64` module,
   choose the server it should run on, and set its **Mode = Enabled**.

4. **Apply.** The server loads the plugin. Open the server **Journal** and
   confirm you see:

   ```
   SeriesPlugin: loaded N group series, sql_mirror=off, reject_unconfigured=off
   SeriesPlugin: started
   ```

5. **Set the server's login range** so it covers your series numbers — see the
   "Validate first" section below. This is the one server setting that can stop
   the plugin from working.

To change group series later: edit `SeriesPlugin.config.json` on the server and
reload the plugin (disable/enable it in the Administrator, or restart the
server). The plugin reads the config at start-up.

### Configuration — how to define a group's series

Edit `SeriesPlugin.config.json` (sitting next to the DLL). Each entry in the
`groups` list defines one group's number series:

```json
"groups": [
  { "group": "real\\group_a", "start_login": 786000,  "max_login": 786999 },
  { "group": "real\\group_b", "start_login": 900000,  "max_login": 0 },
  { "group": "demo\\vip",     "start_login": 5000000, "max_login": 0 }
]
```

- **`group`** — the exact MT5 group path. In JSON, write each backslash as `\\`
  (so `real\group_a` becomes `"real\\group_a"`). A trailing `*` makes it a
  prefix match, e.g. `"real\\vip*"` matches `real\vip`, `real\vip2`, … When a
  group matches more than one entry, the most specific (longest) one wins.
- **`start_login`** — the first login handed out for that group.
- **`max_login`** — the highest login allowed; `0` means unlimited. When a
  capped series runs out, account creation in that group is rejected.

To **add a new group series**: copy one line, change the group name and numbers,
save, and reload the plugin. To change behaviour for *unlisted* groups, see the
flags below.

| Other field | Meaning |
|-------|---------|
| `reject_unconfigured_groups` | `false` (default) = groups not listed above keep MT5's normal numbering; `true` = block account creation in unlisted groups. |
| `override_explicit_login` | `false` (default) = if a login was already entered, keep it; `true` = always assign from the series. |
| `check_existing_logins` | Skip any number that's already taken (recommended `true`). |
| `log_allocations` | Write a Journal line for each assigned login. |
| `sql_mirror.*` | Optional ODBC mirror (see `sql\schema.sql`). |

### SQL mirror (optional)

The JSON state file is always the source of truth. When `sql_mirror.enabled` is
`true`, each allocation is also UPSERTed into `group_account_series` on a
background thread — a dead/slow database can never block or fail account
creation. Create the table with `sql\schema.sql` (SQL Server dialect; MySQL
notes included).

## ⚠️ Validate on a test server first

This plugin sets the login inside `HookUserAdd`. Confirm on a **staging server**
before production:

1. **The server accepts the custom login.** MT5 servers have a configured global
   login range; your series numbers (786000, 900000, 5000000, …) **must fall
   inside that allowed range** or MT5 will refuse them. Adjust the server's
   login range if needed.
2. **Create one account per group** via the Manager terminal and confirm the
   assigned login matches the series and the Journal logs the allocation.
3. **Concurrency:** create several accounts in the same group in quick
   succession and confirm no duplicates and a clean increasing sequence.

## Behaviour notes

- **Counters are reserved before commit.** A number is advanced and persisted
  the moment it is assigned, so two simultaneous creations can never collide.
  If a creation later fails, that number is skipped (a gap) — uniqueness is
  prioritised over a perfectly contiguous sequence.
- **Gaps are normal** and harmless. The series only guarantees *unique,
  increasing* logins within a group, not zero gaps.
- The plugin is a **no-op** until a valid config file is present, so deploying
  the DLL without config cannot disrupt existing account creation.
