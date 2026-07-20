#!/usr/bin/env python3
"""Validate EmbeddedMQ conformance scenario JSON files."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

STAT_KEYS = frozenset(
    {
        "enqueued",
        "dequeued",
        "acked",
        "nacked",
        "expired",
        "depth",
        "bytes",
        "redelivered",
    }
)

KNOWN_OPS = frozenset(
    {
        "runtime_create",
        "runtime_destroy",
        "queue_create",
        "queue_close",
        "push",
        "pop",
        "try_pop",
        "peek",
        "ack",
        "nack",
        "claim",
        "release_claim",
        "flush",
        "subscribe",
        "unsubscribe",
        "publish",
        "sub_next",
    }
)

STATUS_NAMES = frozenset(
    {
        "ok",
        "invalid",
        "nomem",
        "not_found",
        "full",
        "empty",
        "io",
        "timeout",
        "exists",
        "closed",
        "busy",
        "unsupported",
    }
)


def err(path: Path, msg: str) -> str:
    return f"{path}: {msg}"


def check_stats(path: Path, stats: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(stats, dict):
        return [err(path, "expected_stats must be an object")]
    missing = STAT_KEYS - stats.keys()
    extra = stats.keys() - STAT_KEYS
    if missing:
        errors.append(err(path, f"expected_stats missing keys: {sorted(missing)}"))
    if extra:
        errors.append(err(path, f"expected_stats unknown keys: {sorted(extra)}"))
    for key in STAT_KEYS & stats.keys():
        if not isinstance(stats[key], int) or stats[key] < 0:
            errors.append(err(path, f"expected_stats.{key} must be a non-negative integer"))
    return errors


def check_op(path: Path, index: int, op: Any) -> list[str]:
    errors: list[str] = []
    prefix = f"ops[{index}]"
    if not isinstance(op, dict):
        return [err(path, f"{prefix} must be an object")]
    name = op.get("op")
    if not isinstance(name, str):
        errors.append(err(path, f"{prefix} missing string 'op'"))
        return errors
    if name not in KNOWN_OPS:
        errors.append(err(path, f"{prefix} unknown op '{name}'"))
    status = op.get("expect_status")
    if status is not None and status not in STATUS_NAMES:
        errors.append(err(path, f"{prefix} unknown expect_status '{status}'"))
    for field in ("payload", "capture_id", "message_id"):
        val = op.get(field)
        if val is not None and not isinstance(val, str):
            errors.append(err(path, f"{prefix}.{field} must be a string"))
    for field in ("size", "timeout_ms", "delay_ms"):
        val = op.get(field)
        if val is not None and (not isinstance(val, int) or val < 0):
            errors.append(err(path, f"{prefix}.{field} must be a non-negative integer"))
    return errors


def validate_scenario(path: Path, data: Any) -> list[str]:
    errors: list[str] = []
    if not isinstance(data, dict):
        return [err(path, "root must be an object")]

    name = data.get("name")
    if not isinstance(name, str) or not name:
        errors.append(err(path, "missing non-empty string 'name'"))
    elif path.stem != name:
        errors.append(err(path, f"name '{name}' does not match filename stem '{path.stem}'"))

    ops = data.get("ops")
    if not isinstance(ops, list) or len(ops) == 0:
        errors.append(err(path, "'ops' must be a non-empty array"))
    else:
        for i, op in enumerate(ops):
            errors.extend(check_op(path, i, op))

    errors.extend(check_stats(path, data.get("expected_stats")))
    return errors


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: validate_corpus.py <scenario.json> ...", file=sys.stderr)
        return 2

    total_errors = 0
    for arg in argv[1:]:
        path = Path(arg)
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            print(err(path, "file not found"), file=sys.stderr)
            total_errors += 1
            continue
        except json.JSONDecodeError as exc:
            print(err(path, f"invalid JSON: {exc}"), file=sys.stderr)
            total_errors += 1
            continue

        errors = validate_scenario(path, data)
        if errors:
            total_errors += len(errors)
            for line in errors:
                print(line, file=sys.stderr)
        else:
            print(f"ok: {path}")

    return 1 if total_errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
