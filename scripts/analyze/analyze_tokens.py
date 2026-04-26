#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Token usage analyzer for Claude Code stream-json logs.

Parses oj_eval_*.log files produced by run_claude_code.sh (which uses
`claude -p ... --output-format stream-json --verbose`) and aggregates token
usage per run: input, output, cache_read, cache_creation.

The log files mix shell stdout and stream-json events on the same stream.
This parser is robust: each line is tested as JSON; non-JSON lines and JSON
without `usage` fields are silently skipped.

Usage:
  # One run (single log file)
  python3 scripts/analyze/analyze_tokens.py --log path/to/oj_eval_X.log

  # All runs under a logs directory (default: logs/)
  python3 scripts/analyze/analyze_tokens.py --logs-dir logs

  # Filter by model name (matches the LOG_MODEL_NAME folder, e.g. "sonnet-4.5-custom")
  python3 scripts/analyze/analyze_tokens.py --logs-dir logs --model-name sonnet-4.5-custom

Output: CSV on stdout with columns
  agent,model,problem,timestamp,input,output,cache_read,cache_creation,total_billable
where total_billable = input + output + cache_creation (cache_read is not billed at full price).
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Iterator


LOG_FILENAME_RE = re.compile(
    r"oj_eval_(?P<agent>.+?)_(?P<model>.+?)_(?P<problem>\d+)_(?P<timestamp>\d{14})\.log$"
)


@dataclass
class RunUsage:
    agent: str
    model: str
    problem: str
    timestamp: str
    log_path: str
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_input_tokens: int = 0
    cache_creation_input_tokens: int = 0
    event_count: int = 0
    parse_errors: int = 0

    @property
    def total_billable(self) -> int:
        return self.input_tokens + self.output_tokens + self.cache_creation_input_tokens

    def to_row(self) -> list:
        return [
            self.agent,
            self.model,
            self.problem,
            self.timestamp,
            self.input_tokens,
            self.output_tokens,
            self.cache_read_input_tokens,
            self.cache_creation_input_tokens,
            self.total_billable,
        ]


CSV_HEADER = [
    "agent",
    "model",
    "problem",
    "timestamp",
    "input",
    "output",
    "cache_read",
    "cache_creation",
    "total_billable",
]


def parse_log_filename(path: Path) -> tuple[str, str, str, str] | None:
    m = LOG_FILENAME_RE.search(path.name)
    if not m:
        return None
    return m.group("agent"), m.group("model"), m.group("problem"), m.group("timestamp")


def extract_usage(obj) -> dict | None:
    """Find a `usage` block in a stream-json event; supports both top-level and nested."""
    if not isinstance(obj, dict):
        return None
    if isinstance(obj.get("usage"), dict):
        return obj["usage"]
    msg = obj.get("message")
    if isinstance(msg, dict) and isinstance(msg.get("usage"), dict):
        return msg["usage"]
    return None


def aggregate_log(path: Path) -> RunUsage | None:
    parsed = parse_log_filename(path)
    if not parsed:
        return None
    agent, model, problem, ts = parsed
    usage = RunUsage(
        agent=agent, model=model, problem=problem, timestamp=ts, log_path=str(path)
    )
    try:
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                line = line.strip()
                if not line or line[0] not in "{[":
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    usage.parse_errors += 1
                    continue
                u = extract_usage(obj)
                if not u:
                    continue
                usage.event_count += 1
                usage.input_tokens += int(u.get("input_tokens", 0) or 0)
                usage.output_tokens += int(u.get("output_tokens", 0) or 0)
                usage.cache_read_input_tokens += int(
                    u.get("cache_read_input_tokens", 0) or 0
                )
                usage.cache_creation_input_tokens += int(
                    u.get("cache_creation_input_tokens", 0) or 0
                )
    except OSError as exc:
        sys.stderr.write(f"[WARN] Could not read {path}: {exc}\n")
        return None
    return usage


def discover_logs(
    logs_dir: Path, model_filter: str | None, problem_filter: str | None
) -> Iterator[Path]:
    if not logs_dir.is_dir():
        return iter(())
    pattern = "**/oj_eval_*.log"
    for path in logs_dir.glob(pattern):
        meta = parse_log_filename(path)
        if not meta:
            continue
        _, model, problem, _ = meta
        if model_filter and model != model_filter:
            continue
        if problem_filter and problem != problem_filter:
            continue
        yield path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group(required=False)
    src.add_argument("--log", help="Path to a single oj_eval_*.log file")
    src.add_argument(
        "--logs-dir",
        default="logs",
        help="Root directory holding logs/<agent>/<model>/<problem>/oj_eval_*.log (default: logs)",
    )
    p.add_argument("--model-name", help="Filter by LOG_MODEL_NAME (e.g. sonnet-4.5-custom)")
    p.add_argument("--problem", help="Filter by problem id (e.g. 001)")
    p.add_argument(
        "--format",
        choices=("csv", "json"),
        default="csv",
        help="Output format (default: csv)",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if args.log:
        logs: Iterable[Path] = [Path(args.log)]
    else:
        logs = discover_logs(Path(args.logs_dir), args.model_name, args.problem)

    results: list[RunUsage] = []
    for path in logs:
        u = aggregate_log(path)
        if u is None:
            continue
        results.append(u)

    if not results:
        sys.stderr.write("[INFO] No matching logs found.\n")
        return 1

    results.sort(key=lambda r: (r.agent, r.model, r.problem, r.timestamp))

    if args.format == "json":
        json.dump(
            [
                {
                    "agent": r.agent,
                    "model": r.model,
                    "problem": r.problem,
                    "timestamp": r.timestamp,
                    "input": r.input_tokens,
                    "output": r.output_tokens,
                    "cache_read": r.cache_read_input_tokens,
                    "cache_creation": r.cache_creation_input_tokens,
                    "total_billable": r.total_billable,
                    "events": r.event_count,
                    "parse_errors": r.parse_errors,
                    "log_path": r.log_path,
                }
                for r in results
            ],
            sys.stdout,
            indent=2,
        )
        sys.stdout.write("\n")
    else:
        writer = csv.writer(sys.stdout)
        writer.writerow(CSV_HEADER)
        for r in results:
            writer.writerow(r.to_row())

    # A small summary on stderr
    total_runs = len(results)
    total_billable = sum(r.total_billable for r in results)
    total_events = sum(r.event_count for r in results)
    sys.stderr.write(
        f"[OK] {total_runs} run(s) parsed, {total_events} usage event(s), "
        f"{total_billable:,} billable tokens total.\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
