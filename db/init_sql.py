#!/usr/bin/env python3

"""基础域初始化迁移入口。

作用：
1. 从 chatServer/config/app.json 中读取 PostgreSQL 连接信息；
2. 按固定顺序执行基础域拆分后的 SQL 脚本；
3. 用一个事务包住整批 DDL，避免迁移执行到一半留下半成品结构。

为什么改成 Python：
1. app.json 当前带有 JSONC 风格注释，Python 更适合做解析和校验；
2. 后续如果要继续加参数、日志、迁移选择和错误提示，Python 更容易维护；
3. 仍然保留“一个对象 / 一张表一个 SQL 文件”的结构，但执行入口统一收口在脚本层。

默认行为：
- 默认读取 chatServer/config/app.json；
- 默认读取名为 default 的 db_client；
- 执行 init_sql/ 目录下当前已落地的基础表结构脚本，
  现阶段已覆盖认证域和好友关系域的最小表结构。

可选参数：
- --config：覆盖 app.json 路径；
- --db-client：指定读取哪个 db_client，默认 default。

可选环境变量：
- CHATSERVER_APP_CONFIG：覆盖 app.json 路径；
- CHATSERVER_DB_CLIENT_NAME：指定读取哪个 db_client，默认 default。
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SCRIPT_PATH = Path(__file__).resolve()
MIGRATION_DIR = SCRIPT_PATH.parent / "init_sql"
DEFAULT_APP_CONFIG = SCRIPT_PATH.parents[1] / "config" / "app.json"
DEFAULT_DB_CLIENT_NAME = "default"


@dataclass(frozen=True)
class MigrationStep:
    """描述一个可独立跳过的迁移步骤。"""

    path: Path
    object_kind: str
    object_name: str
    exists_sql: str


MIGRATION_STEPS = (
    MigrationStep(
        path=MIGRATION_DIR / "0000_set_updated_at_function.sql",
        object_kind="function",
        object_name="set_updated_at()",
        exists_sql="SELECT to_regprocedure('set_updated_at()') IS NOT NULL;",
    ),
    MigrationStep(
        path=MIGRATION_DIR / "0001_users.sql",
        object_kind="table",
        object_name="users",
        exists_sql="SELECT to_regclass('users') IS NOT NULL;",
    ),
    MigrationStep(
        path=MIGRATION_DIR / "0002_device_sessions.sql",
        object_kind="table",
        object_name="device_sessions",
        exists_sql="SELECT to_regclass('device_sessions') IS NOT NULL;",
    ),
    MigrationStep(
        path=MIGRATION_DIR / "0003_friend_requests.sql",
        object_kind="table",
        object_name="friend_requests",
        exists_sql="SELECT to_regclass('friend_requests') IS NOT NULL;",
    ),
    MigrationStep(
        path=MIGRATION_DIR / "0004_friendships.sql",
        object_kind="table",
        object_name="friendships",
        exists_sql="SELECT to_regclass('friendships') IS NOT NULL;",
    ),
)


def fail(message: str) -> "NoReturn":
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read chatServer/config/app.json and run base schema migration SQL files.",
    )
    parser.add_argument(
        "--config",
        default=os.environ.get("CHATSERVER_APP_CONFIG", str(DEFAULT_APP_CONFIG)),
        help="Path to app.json. Default: %(default)s",
    )
    parser.add_argument(
        "--db-client",
        default=os.environ.get(
            "CHATSERVER_DB_CLIENT_NAME",
            DEFAULT_DB_CLIENT_NAME,
        ),
        help="db_clients entry name to use. Default: %(default)s",
    )
    return parser.parse_args()


def strip_jsonc(text: str) -> str:
    """去掉 JSONC 风格注释，同时保留字符串中的原始内容。"""

    result: list[str] = []
    index = 0
    in_string = False
    in_single_line_comment = False
    in_multi_line_comment = False
    escape = False

    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""

        if in_single_line_comment:
            if char == "\n":
                in_single_line_comment = False
                result.append(char)
            index += 1
            continue

        if in_multi_line_comment:
            if char == "*" and next_char == "/":
                in_multi_line_comment = False
                index += 2
            else:
                index += 1
            continue

        if in_string:
            result.append(char)
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == '"':
            in_string = True
            result.append(char)
            index += 1
            continue

        if char == "/" and next_char == "/":
            in_single_line_comment = True
            index += 2
            continue

        if char == "/" and next_char == "*":
            in_multi_line_comment = True
            index += 2
            continue

        result.append(char)
        index += 1

    return "".join(result)


def load_jsonc(path: Path) -> dict[str, Any]:
    if not path.exists():
        fail(f"app.json not found: {path}")

    try:
        content = path.read_text(encoding="utf-8")
        return json.loads(strip_jsonc(content))
    except json.JSONDecodeError as exc:
        fail(f"failed to parse app.json: {exc}")


def pick_db_client(config: dict[str, Any], client_name: str) -> dict[str, Any]:
    db_clients = config.get("db_clients") or []
    if not isinstance(db_clients, list) or not db_clients:
        fail("no db_clients found in app.json")

    for db_client in db_clients:
        if db_client.get("name") == client_name:
            return db_client

    return db_clients[0]


def validate_migration_files() -> None:
    if not MIGRATION_DIR.is_dir():
        fail(f"migration directory not found: {MIGRATION_DIR}")

    missing_files = [
        str(step.path)
        for step in MIGRATION_STEPS
        if not step.path.is_file()
    ]
    if missing_files:
        fail(f"migration files not found: {', '.join(missing_files)}")


def build_psql_env(db_client: dict[str, Any]) -> tuple[dict[str, str], str]:
    env = os.environ.copy()

    db_name = str(db_client.get("dbname") or "").strip()
    if not db_name:
        fail(
            "dbname is empty in app.json "
            f"db_client[{db_client.get('name', DEFAULT_DB_CLIENT_NAME)}]",
        )

    env["PGDATABASE"] = db_name

    host = str(db_client.get("host") or "").strip()
    port = str(db_client.get("port") or "").strip()
    user = str(db_client.get("user") or "").strip()
    password = str(db_client.get("passwd") or "")

    if host:
        env["PGHOST"] = host
    if port:
        env["PGPORT"] = port
    if user:
        env["PGUSER"] = user
    if password:
        env["PGPASSWORD"] = password

    return env, db_name


def run_psql_query(env: dict[str, str], sql: str) -> str:
    """执行一条简单查询，并返回去掉首尾空白后的文本结果。"""

    try:
        completed = subprocess.run(
            ["psql", "-tA", "-c", sql],
            check=True,
            capture_output=True,
            text=True,
            env=env,
        )
    except FileNotFoundError:
        fail("required command not found: psql")
    except subprocess.CalledProcessError as exc:
        stderr = exc.stderr.strip()
        if stderr:
            print(stderr, file=sys.stderr)
        raise SystemExit(exc.returncode) from exc

    return completed.stdout.strip()


def collect_pending_steps(env: dict[str, str]) -> list[MigrationStep]:
    """筛出本次真正需要执行的迁移步骤。

    规则：
    1. 目标对象已存在，则跳过对应 SQL 文件；
    2. 目标对象不存在，则加入本次待执行列表；
    3. 只对待执行列表再开启事务，保证剩余步骤仍然原子提交。
    """

    pending_steps: list[MigrationStep] = []

    for step in MIGRATION_STEPS:
        exists_result = run_psql_query(env, step.exists_sql).lower()
        exists = exists_result in {"t", "true", "1", "on", "yes"}
        if exists:
            print(
                f"Skip {step.object_kind} [{step.object_name}] "
                f"because it already exists.",
                flush=True,
            )
            continue

        print(
            f"Queue {step.object_kind} [{step.object_name}] "
            f"from [{step.path.name}].",
            flush=True,
        )
        pending_steps.append(step)

    return pending_steps


def build_psql_script(steps: list[MigrationStep]) -> str:
    lines = ["\\set ON_ERROR_STOP on", "BEGIN;"]
    for step in steps:
        lines.append(f"\\i {step.path}")
    lines.append("COMMIT;")
    lines.append("")
    return "\n".join(lines)


def run_migrations(env: dict[str, str], steps: list[MigrationStep]) -> None:
    if not steps:
        print("No pending migration steps. Database schema is already up to date.", flush=True)
        return

    try:
        subprocess.run(
            ["psql", "-v", "ON_ERROR_STOP=1"],
            input=build_psql_script(steps),
            text=True,
            check=True,
            env=env,
        )
    except FileNotFoundError:
        fail("required command not found: psql")
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode) from exc


def main() -> int:
    args = parse_args()
    validate_migration_files()

    app_config = load_jsonc(Path(args.config).resolve())
    db_client = pick_db_client(app_config, args.db_client)
    env, db_name = build_psql_env(db_client)
    db_client_name = str(db_client.get("name") or DEFAULT_DB_CLIENT_NAME)

    print(
        "Running base schema migrations with "
        f"db_client[{db_client_name}] on database [{db_name}].",
        flush=True,
    )
    pending_steps = collect_pending_steps(env)
    run_migrations(env, pending_steps)
    print("Base schema migrations completed successfully.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
