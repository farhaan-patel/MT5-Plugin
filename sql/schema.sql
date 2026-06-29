-- ===================================================================
--  MT5 Group Series Login Allocator - SQL mirror schema
--
--  The plugin keeps the JSON state file as the source of truth and
--  mirrors each allocation here (best-effort) for reporting/audit.
--  The plugin performs an UPSERT (UPDATE ... else INSERT) keyed on
--  group_name, storing the latest assigned login per group.
--
--  Dialect below is SQL Server (uses GETDATE()). For MySQL/MariaDB
--  replace GETDATE() with NOW() in the plugin's statements, or change
--  the column default and let updated_at default itself (see notes).
-- ===================================================================

-- ---- SQL Server -------------------------------------------------
CREATE TABLE group_account_series (
    id           BIGINT        IDENTITY(1,1) PRIMARY KEY,
    group_name   NVARCHAR(256) NOT NULL UNIQUE,
    start_login  BIGINT        NOT NULL,
    last_login   BIGINT        NOT NULL,
    updated_at   DATETIME      NOT NULL DEFAULT GETDATE()
);
CREATE UNIQUE INDEX ux_group_account_series_group ON group_account_series(group_name);

-- ---- MySQL / MariaDB (alternative) ------------------------------
-- CREATE TABLE group_account_series (
--     id           BIGINT        NOT NULL AUTO_INCREMENT PRIMARY KEY,
--     group_name   VARCHAR(256)  NOT NULL UNIQUE,
--     start_login  BIGINT        NOT NULL,
--     last_login   BIGINT        NOT NULL,
--     updated_at   DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP
--                                ON UPDATE CURRENT_TIMESTAMP
-- );
-- Note: with the ON UPDATE default above you may drop GETDATE() from
-- the plugin's UPDATE statement; for the portable build keep a SQL
-- Server target or adjust SqlMirror.cpp accordingly.

-- Optional: a full per-allocation audit trail (not written by the
-- plugin by default; add an INSERT in SqlMirror::WriteOne if wanted).
-- CREATE TABLE group_account_alloc_log (
--     id          BIGINT IDENTITY(1,1) PRIMARY KEY,
--     group_name  NVARCHAR(256) NOT NULL,
--     login       BIGINT        NOT NULL,
--     allocated_at DATETIME     NOT NULL DEFAULT GETDATE()
-- );
