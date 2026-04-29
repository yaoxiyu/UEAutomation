#!/usr/bin/env python3
"""
Lightweight UEEditorAutomation regression runner.

Precondition: the UE editor is already running with this plugin loaded.
This script never starts the editor, UBT, UAT, or any build command.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8-sig") as handle:
        return json.load(handle)


def wait_result(task_id: str, result_dir: Path, timeout_seconds: int) -> dict:
    result_path = result_dir / f"{task_id}.result.json"
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if result_path.exists():
            return load_json(result_path)
        time.sleep(0.5)
    raise TimeoutError(f"Timed out waiting for result: {result_path}")


def run_case(task_path: Path, inbox_dir: Path, result_dir: Path, timeout_seconds: int, expected_success: bool) -> tuple[bool, str]:
    task = load_json(task_path)
    task_id = task.get("task_id")
    if not task_id:
        return False, f"{task_path}: missing task_id"

    result_path = result_dir / f"{task_id}.result.json"
    if result_path.exists():
        result_path.unlink()

    inbox_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(task_path, inbox_dir / task_path.name)
    result = wait_result(task_id, result_dir, timeout_seconds)
    actual_success = bool(result.get("success"))
    if actual_success != expected_success:
        errors = result.get("errors", [])
        return False, f"{task_path}: expected success={expected_success}, got {actual_success}; errors={errors}"
    return True, f"{task_path}: success={actual_success}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples-dir", default="Samples", type=Path)
    parser.add_argument("--inbox-dir", default=Path(r"C:\UEAutomation\tasks\inbox"), type=Path)
    parser.add_argument("--result-dir", default=Path(r"C:\UEAutomation\results"), type=Path)
    parser.add_argument("--timeout-seconds", default=300, type=int)
    parser.add_argument("--valid-only", action="store_true")
    parser.add_argument("--invalid-only", action="store_true")
    args = parser.parse_args()

    cases: list[tuple[Path, bool]] = []
    if not args.invalid_only:
        cases.extend((path, True) for path in sorted((args.samples_dir / "valid").glob("*.json")))
    if not args.valid_only:
        cases.extend((path, False) for path in sorted((args.samples_dir / "invalid").glob("*.json")))

    if not cases:
        print("No sample cases found.", file=sys.stderr)
        return 1

    failures: list[str] = []
    for task_path, expected_success in cases:
        try:
            ok, message = run_case(task_path, args.inbox_dir, args.result_dir, args.timeout_seconds, expected_success)
        except Exception as exc:
            ok, message = False, f"{task_path}: {exc}"
        print(message)
        if not ok:
            failures.append(message)

    if failures:
        print("\nFailures:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
