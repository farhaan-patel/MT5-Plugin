MT5 GROUP SERIES LOGIN ALLOCATOR
Server Plugin for MetaTrader 5  ·  Professional Edition
Independent sequential login numbering per MT5 group — full source, zero dependencies.

Overview
A native MetaTrader 5 server plugin that replaces MT5's default global account numbering with an independent sequential login series per group.

Example: accounts in real\group_a are numbered from 786000, accounts in real\group_b from 900000, accounts in demo\vip from 5000000 — each group maintains its own counter.

How It Works
The plugin subscribes to user events and implements IMTUserSink::HookUserAdd(IMTUser* user) — the hook the MT5 Server API fires before a new account is committed, with a mutable user object.

For every new account (regardless of origin — dealer terminal, Manager API, Web API / CRM, or automation script):

1.	Read user->Group().
2.	Find the configured series whose group pattern matches (most specific wins).
3.	Under a lock, compute the next login (last + 1, or start for the first), skipping any login that already exists.
4.	Write it back with user->Login(next) and let creation proceed.
5.	Persist the counter and mirror the allocation to SQL (best-effort).

If the group is not configured, the account falls back to MT5's default numbering (unless reject_unconfigured_groups is set). If a series is exhausted, account creation is rejected with MT_RET_USR_LOGIN_EXHAUSTED.

Source File Map

File	Purpose
SeriesPlugin/PluginInstance.cpp	Plugin entry + HookUserAdd interception
SeriesPlugin/SeriesAllocator.cpp	Config, counters, locking, allocation logic
SeriesPlugin/SqlMirror.cpp	Background ODBC mirror
SeriesPlugin/Json.h, Util.h	Dependency-free JSON + file/string helpers

Building
Compiler	Visual Studio 2019 (toolset v142) with the C++ workload
MT5 SDK	C:\MetaTrader5SDK  (change AdditionalIncludeDirectories in the project if your SDK lives elsewhere)
Runtime	Static (/MT) — no MSVC redistributable required on the server
Extra libs	odbc32.lib (present on every Windows install)

Build command (from a Developer PowerShell or with full MSBuild path):
msbuild SeriesPlugin.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 msbuild SeriesPlugin.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32

Build Outputs
Platform	Output DLL
x64	bin\x64\Release\SeriesPlugin64.dll
x86	bin\Win32\Release\SeriesPlugin.dll

Using a newer Visual Studio? Right-click the solution → Retarget Projects, or set <PlatformToolset> (v143 for VS2022) in SeriesPlugin.vcxproj.

Installing on Your MT5 Server
A server plugin runs on the MT5 server machine and is managed from the MetaTrader 5 Administrator application (not the Manager terminal). You need:
•	Access to the server (to copy files onto it)
•	The MetaTrader 5 Administrator with admin credentials

Installation Steps
6.	Copy the files onto the server. Place the DLL matching your server architecture (almost always the 64-bit SeriesPlugin64.dll) into the MT5 server's plugins folder — typically ...\bin\plugins\ under the server installation. Copy SeriesPlugin.config.json into the same folder as the DLL.

The plugin reads <dll-folder>\SeriesPlugin.config.json and writes counters to <dll-folder>\SeriesPlugin.state.json. Both files live next to the DLL.

7.	Open MetaTrader 5 Administrator and go to the Plugins section. The SeriesPlugin64 module should appear in the list of available plugin modules.
8.	Add a plugin configuration referencing the SeriesPlugin64 module, choose the target server, and set Mode = Enabled.
9.	Apply. The server loads the plugin. Open the server Journal and confirm you see:

SeriesPlugin: loaded N group series, sql_mirror=off, reject_unconfigured=off SeriesPlugin: started

10.	Set the server's login range so it covers your series numbers — see the Validate section below. This is the one server setting that can stop the plugin from working.

To change group series later: edit SeriesPlugin.config.json on the server and reload the plugin (disable/enable in Administrator, or restart the server).

Configuration — Defining a Group's Series
Edit SeriesPlugin.config.json (located next to the DLL). Each entry in the groups list defines one group's number series:
"groups": [   { "group": "real\\group_a", "start_login": 786000,  "max_login": 786999 },   { "group": "real\\group_b", "start_login": 900000,  "max_login": 0      },   { "group": "demo\\vip",     "start_login": 5000000, "max_login": 0      } ]

group	The exact MT5 group path. In JSON, write each backslash as \\ (so real\group_a becomes "real\\group_a"). A trailing * makes it a prefix match, e.g. "real\\vip*" matches real\vip, real\vip2, etc. When multiple entries match, the most specific (longest) one wins.
start_login	The first login handed out for that group.
max_login	The highest login allowed; 0 means unlimited. When a capped series runs out, account creation in that group is rejected.

Additional Configuration Flags
Field	Meaning
reject_unconfigured_groups	false (default) = unlisted groups use MT5 default numbering; true = block account creation in unlisted groups.
override_explicit_login	false (default) = keep manually entered logins; true = always assign from the series.
check_existing_logins	Skip any number already taken (recommended: true).
log_allocations	Write a Journal entry for each assigned login.
sql_mirror.*	Optional ODBC mirror (see sql\schema.sql).

SQL Mirror (Optional)
The JSON state file is always the source of truth. When sql_mirror.enabled is true, each allocation is also UPSERTed into group_account_series on a background thread.

A dead or slow database can never block or fail account creation — the mirror runs independently in the background.

Create the table using sql\schema.sql (SQL Server dialect; MySQL notes are included in the file).

⚠️  Validate on a Test Server First
⚠️  Always validate on a staging server before deploying to production.

11.	The server accepts the custom login. MT5 servers have a configured global login range; your series numbers (786000, 900000, 5000000, …) must fall inside that allowed range or MT5 will refuse them. Adjust the server login range if needed.
12.	Create one account per group via the Manager terminal and confirm the assigned login matches the expected series and the Journal logs the allocation.
13.	Concurrency test: create several accounts in the same group in rapid succession and confirm there are no duplicates and a clean increasing sequence.

Behaviour Notes
•	Counters are reserved before commit. A number is advanced and persisted the moment it is assigned, so two simultaneous creations can never collide. If a creation later fails, that number is skipped (a gap) — uniqueness is prioritised over a perfectly contiguous sequence.
•	Gaps are normal and harmless. The series only guarantees unique, increasing logins within a group, not a zero-gap sequence.
•	The plugin is a no-op until a valid config file is present, so deploying the DLL without config cannot disrupt existing account creation.

🔧  We Build MT5 Plugins — Custom & Ready-Made
This plugin is one of many we develop for MetaTrader 5. Whether you need a custom server-side plugin, a risk management tool, a reporting bridge, a CRM integration, or any other MT5 automation — we build it.
Reach out via any channel below to discuss your requirements.

📬  Get In Touch

📞 Phone / WhatsApp
+91 96243 35237
✉️ Email
farhaanpatel345@gmail.com
💬 Telegram
@saiki6069
🔗 WhatsApp Chat
Click to Chat


We are happy to discuss your MT5 plugin requirements, provide support, or deliver a custom build tailored to your brokerage needs.
