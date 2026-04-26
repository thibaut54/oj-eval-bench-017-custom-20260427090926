#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Compare two Claude Code config modes (typically vanilla vs custom) on the same problems.

For each (problem_id, config) pair this script aggregates:
  - latest run timestamp (most recent run wins)
  - token usage (input, output, cache_read, cache_creation, total_billable)
  - submission count (lines in submission_ids_*.log)
  - last submission verdict / score grepped from oj_eval_*.log

It then produces a markdown report contrasting the two configs side-by-side.

Usage:
  python3 scripts/analyze/compare_configs.py \\
    --vanilla sonnet-4.5-vanilla --custom sonnet-4.5-custom \\
    [--problems 001,002] [--logs-dir logs] [--agent claude-code]

The two arguments are LOG_MODEL_NAME values, i.e. the folder names under
logs/<agent>/<LOG_MODEL_NAME>/<problem>/.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable

# Reuse the parser from analyze_tokens.py
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from analyze_tokens import RunUsage, aggregate_log, parse_log_filename  # noqa: E402

VERDICT_RE = re.compile(
    r"\b(Accepted|Wrong Answer|Time Limit Exceeded|Memory Limit Exceeded|"
    r"Runtime Error|Compile Error|Bad Problem|Skipped)\b",
    re.IGNORECASE,
)
SCORE_RE = re.compile(r"score['\":\s]+(\d+(?:\.\d+)?)", re.IGNORECASE)


def latest_run_log(logs_dir: Path, agent: str, model: str, problem: str) -> Path | None:
    """Return the most recent oj_eval_*.log for a given (agent,model,problem)."""
    pdir = logs_dir / agent / model / problem
    if not pdir.is_dir():
        return None
    candidates = sorted(pdir.glob("oj_eval_*.log"))
    if not candidates:
        return None
    # filename ends with _<TIMESTAMP>.log so lexical sort == chronological sort
    return candidates[-1]


def submission_count(logs_dir: Path, agent: str, model: str, problem: str) -> int:
    pdir = logs_dir / agent / model / problem
    if not pdir.is_dir():
        return 0
    count = 0
    for sub_log in pdir.glob("submission_ids_*.log"):
        try:
            with sub_log.open("r", encoding="utf-8", errors="replace") as fh:
                count += sum(1 for line in fh if line.strip())
        except OSError:
            continue
    return count


def extract_verdict(log: Path) -> str:
    """Best-effort: return the LAST verdict-looking string found in the log."""
    last = "—"
    try:
        with log.open("r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = VERDICT_RE.search(line)
                if m:
                    last = m.group(0)
    except OSError:
        return last
    return last


def discover_problems(logs_dir: Path, agent: str, model: str) -> set[str]:
    base = logs_dir / agent / model
    if not base.is_dir():
        return set()
    return {p.name for p in base.iterdir() if p.is_dir() and p.name.isdigit()}


def fmt_int(n: int) -> str:
    return f"{n:,}"


def diff(a: int, b: int) -> str:
    """Return a signed integer diff (b - a) with thousands separator."""
    d = b - a
    sign = "+" if d > 0 else ""
    return f"{sign}{d:,}"


def pct(a: int, b: int) -> str:
    if a == 0:
        return "n/a"
    return f"{(b - a) / a * 100:+.1f}%"


def collect(logs_dir: Path, agent: str, model: str, problem: str) -> dict:
    log = latest_run_log(logs_dir, agent, model, problem)
    info: dict = {
        "model": model,
        "problem": problem,
        "log_path": str(log) if log else None,
        "submissions": submission_count(logs_dir, agent, model, problem),
    }
    if not log:
        info["found"] = False
        return info
    info["found"] = True
    usage: RunUsage | None = aggregate_log(log)
    if usage is None:
        info["usage"] = None
    else:
        info["usage"] = usage
    info["verdict"] = extract_verdict(log)
    return info


def render_markdown(
    vanilla_model: str,
    custom_model: str,
    problems: list[str],
    rows: list[tuple[dict, dict]],
) -> str:
    lines: list[str] = []
    lines.append(f"# Comparaison `{vanilla_model}` vs `{custom_model}`\n")
    lines.append(
        "Tokens 'billable' = input + output + cache_creation. Cache_read est listé séparément.\n"
    )

    lines.append("## Per-problem\n")
    lines.append(
        "| Problem | Submissions (V/C) | Verdict V | Verdict C | "
        "Input V/C | Output V/C | Cache_creation V/C | Billable V/C | Δ billable |"
    )
    lines.append("|---|---|---|---|---|---|---|---|---|")
    sum_v = sum_c = 0
    for vinfo, cinfo in rows:
        problem = vinfo["problem"]
        vu = vinfo.get("usage")
        cu = cinfo.get("usage")
        if vu and cu:
            v_in, c_in = vu.input_tokens, cu.input_tokens
            v_out, c_out = vu.output_tokens, cu.output_tokens
            v_cc, c_cc = vu.cache_creation_input_tokens, cu.cache_creation_input_tokens
            v_bill, c_bill = vu.total_billable, cu.total_billable
            sum_v += v_bill
            sum_c += c_bill
            tokens_cell = (
                f"{fmt_int(v_in)} / {fmt_int(c_in)} | "
                f"{fmt_int(v_out)} / {fmt_int(c_out)} | "
                f"{fmt_int(v_cc)} / {fmt_int(c_cc)} | "
                f"{fmt_int(v_bill)} / {fmt_int(c_bill)} | {diff(v_bill, c_bill)} ({pct(v_bill, c_bill)})"
            )
        else:
            tokens_cell = "missing | missing | missing | missing | n/a"
        lines.append(
            f"| {problem} | "
            f"{vinfo['submissions']} / {cinfo['submissions']} | "
            f"{vinfo.get('verdict', '—')} | {cinfo.get('verdict', '—')} | "
            f"{tokens_cell} |"
        )

    lines.append("")
    lines.append("## Aggregate\n")
    lines.append(f"- Vanilla billable total: **{fmt_int(sum_v)}**")
    lines.append(f"- Custom  billable total: **{fmt_int(sum_c)}**")
    lines.append(f"- Δ : **{diff(sum_v, sum_c)}** ({pct(sum_v, sum_c)})")

    lines.append("")
    lines.append("## Notes\n")
    lines.append(
        "- Verdict est extrait grossièrement (dernière occurrence d'un verdict dans le log). "
        "Pour le score OJ exact, lance `scripts/analyze/analyze_exec_score.py` séparément."
    )
    lines.append(
        "- Si custom utilise *moins* de tokens que vanilla, c'est suspect : la config custom "
        "ajoute du contexte (CLAUDE.md, agents, skills) donc on s'attend à plus de tokens "
        "input. Vérifier le parsing."
    )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--vanilla", required=True, help="LOG_MODEL_NAME for the vanilla run (e.g. sonnet-4.5-vanilla)")
    p.add_argument("--custom", required=True, help="LOG_MODEL_NAME for the custom run (e.g. sonnet-4.5-custom)")
    p.add_argument("--logs-dir", default="logs", help="Logs root (default: logs)")
    p.add_argument("--agent", default="claude-code", help="Agent type folder (default: claude-code)")
    p.add_argument(
        "--problems",
        help="Comma-separated problem IDs to include. Default: intersection of problems present in both configs.",
    )
    p.add_argument(
        "--out",
        help="Write the markdown report to this file in addition to printing to stdout.",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    logs_dir = Path(args.logs_dir)

    if args.problems:
        problems = sorted(p.strip() for p in args.problems.split(",") if p.strip())
    else:
        v_problems = discover_problems(logs_dir, args.agent, args.vanilla)
        c_problems = discover_problems(logs_dir, args.agent, args.custom)
        problems = sorted(v_problems & c_problems)
        if not problems:
            sys.stderr.write(
                f"[ERROR] No common problem folders under "
                f"{logs_dir}/{args.agent}/{{{args.vanilla},{args.custom}}}/\n"
            )
            return 1

    rows: list[tuple[dict, dict]] = []
    for problem in problems:
        v = collect(logs_dir, args.agent, args.vanilla, problem)
        c = collect(logs_dir, args.agent, args.custom, problem)
        rows.append((v, c))
        if not v["found"]:
            sys.stderr.write(f"[WARN] vanilla missing for problem {problem}\n")
        if not c["found"]:
            sys.stderr.write(f"[WARN] custom missing for problem {problem}\n")

    md = render_markdown(args.vanilla, args.custom, problems, rows)
    print(md)
    if args.out:
        Path(args.out).write_text(md, encoding="utf-8")
        sys.stderr.write(f"[OK] Report written to {args.out}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
