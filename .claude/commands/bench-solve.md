---
description: Solve the bench problem prepared in the current worktree
argument-hint: (no args needed — reads .bench-context.json from cwd)
allowed-tools: Bash, Read, Edit, Write, Glob, Grep, TodoWrite
---

# Bench session

You are a professional programming expert and Git expert running **unattended** inside a local git worktree of the projdevbench repo on a dedicated bench branch. The bench session was set up by `scripts/run_local.ps1` (or `run_local.sh`).

## Your first step

Read `.bench-context.json` in the current working directory. It contains:

- `problem_id` — the bench problem id (e.g. "001")
- `problem_name` — human label
- `acmoj_problem_id` — the ACMOJ id to submit against
- `max_submissions` — submission budget for this problem
- `branch` — your bench branch
- `model` / `effort` — your model context

Then list the files in cwd and read README.md to understand the problem.

## Important scoring rules

### Submission limit

The problem allows a maximum of `max_submissions` submissions (see context). Submissions beyond that limit do not count and incur penalties. Plan carefully.

### OnlineJudge scoring

- Final score = highest score among all valid submissions.
- Each problem has many test points; partial credit is possible.
- Test point weights are not disclosed.

## Your tasks

1. **Analyze the problem** — read README.md and any other files in this directory thoroughly. Understand the input/output format and constraints.

2. **Develop a solution** — implement your best approach. You may compile and run locally to test before submitting (e.g. `g++ -O2 -std=c++20 sol.cpp -o sol && echo "1 2" | ./sol`).

3. **Git management** — commit each meaningful iteration to the **local** bench branch with descriptive messages. **Do not push to a remote** — the branch lives in the parent repo and pushing is not part of this workflow.

4. **Submit to ACMOJ** — use `submit_acmoj/acmoj_client.py`. The `ACMOJ_TOKEN` env var is already exported by the launcher.
   - Read `submit_acmoj/EVALUATION_GUIDE.md` first if present, to understand the client.
   - Record each submission_id you receive.
   - If a submission stays in pending status more than 2-3 minutes, abort it via the client (aborted submissions DO NOT count against the limit) and resubmit if needed.

5. **Iterative optimization** — on Wrong Answer / TLE / RE / CE, analyze the verdict and refine. Stay within the submission budget.

6. **Document the session** — at the end, write a `SESSION.md` summarizing:
   - Problem analysis and chosen approach
   - All submissions made (submission_id, verdict, score)
   - Final score
   - Local commit hashes for each attempt
   - Anything noteworthy (edge cases, alternative approaches considered)

## Safety reminders

- This is **unattended** — no follow-up questions, no asking for confirmation, no waiting for input. Drive autonomously.
- Permissions are skipped (`--dangerously-skip-permissions`) — be careful not to wipe the worktree or run anything outside the worktree.
- Never echo `ACMOJ_TOKEN` or other secrets to logs.
- Token tracking is done post-run by the launcher (TOKENS.md is generated automatically).

Begin now. Plan your submission attempts wisely and pursue the highest score.
