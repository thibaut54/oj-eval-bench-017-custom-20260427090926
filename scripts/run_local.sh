#!/bin/bash
# scripts/run_local.sh — exécute le bench en local sans Docker.
#
# Crée une branche dédiée + un git worktree, copie les fichiers du problème,
# puis lance "claude -p /bench-solve" en mode unattended (skip-permissions).
# Capture le log stream-json et extrait les tokens dans TOKENS.md a la fin.
#
# Usage :
#   ./scripts/run_local.sh <problem_id>
#
# Variables d'env optionnelles :
#   MODEL          — alias court (Sonnet|Opus|Haiku) ou ID API direct (defaut: Sonnet)
#   CLAUDE_EFFORT  — niveau d'effort (low|medium|high|xhigh|max)
#
# Exemple :
#   MODEL=Opus CLAUDE_EFFORT=high ./scripts/run_local.sh 001
#
# Pre-requis :
#   - claude CLI installe localement (npm install -g @anthropic-ai/claude-code)
#   - ANTHROPIC_API_KEY dans config/environment.env (claude -p ne supporte pas OAuth)
#   - jq, python3, git installes

set -e

PROBLEM_ID="${1:?Usage: $0 <problem_id>}"
MODEL_NAME="${MODEL:-Sonnet}"
TIMESTAMP=$(date +%Y%m%d%H%M%S)
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

cd "$PROJECT_ROOT"

# --- Pre-flight checks -------------------------------------------------------
command -v claude >/dev/null  || { echo "❌ claude CLI introuvable. Installer: npm install -g @anthropic-ai/claude-code"; exit 1; }
command -v jq >/dev/null      || { echo "❌ jq introuvable. sudo apt install jq"; exit 1; }
command -v python3 >/dev/null || { echo "❌ python3 introuvable"; exit 1; }
[ -d "problem/$PROBLEM_ID" ]  || { echo "❌ problem/$PROBLEM_ID introuvable"; exit 1; }
[ -f config/problem_registry.json ]  || { echo "❌ config/problem_registry.json manquant"; exit 1; }
[ -f config/environment.env ]        || { echo "❌ config/environment.env manquant"; exit 1; }

# --- Resolution model id + log dir -------------------------------------------
case "$MODEL_NAME" in
  Sonnet|"Claude Sonnet 4.5") API_MODEL_ID="claude-sonnet-4-5"; LOG_MODEL="sonnet-4.5" ;;
  Opus|"Claude Opus 4.7")     API_MODEL_ID="claude-opus-4-7";   LOG_MODEL="opus-4-7" ;;
  Haiku|"Claude Haiku 4.5")   API_MODEL_ID="claude-haiku-4-5";  LOG_MODEL="haiku-4-5" ;;
  *)                          API_MODEL_ID="$MODEL_NAME";        LOG_MODEL="${MODEL_NAME// /-}" ;;
esac
LOG_MODEL="${LOG_MODEL}-local"
[ -n "$CLAUDE_EFFORT" ] && LOG_MODEL="${LOG_MODEL}-${CLAUDE_EFFORT}"

# --- Lecture registry --------------------------------------------------------
ACMOJ_ID=$(jq -r ".problems.\"$PROBLEM_ID\".acmoj_id" config/problem_registry.json)
PROBLEM_NAME=$(jq -r ".problems.\"$PROBLEM_ID\".name" config/problem_registry.json)
MAX_SUBMISSIONS=$(jq -r ".problems.\"$PROBLEM_ID\".max_submissions // .default.max_submissions // 3" config/problem_registry.json)
[ "$ACMOJ_ID" = "null" ] && { echo "❌ Problem $PROBLEM_ID absent du registry"; exit 1; }

# --- Source env (ACMOJ_TOKEN, ANTHROPIC_API_KEY, GITHUB_*) -------------------
set -a
# shellcheck disable=SC1091
source config/environment.env
set +a

if [ -z "$ANTHROPIC_API_KEY" ] || [ "$ANTHROPIC_API_KEY" = "your_anthropic_api_key" ]; then
  echo "❌ ANTHROPIC_API_KEY manquante — claude -p ne supporte pas OAuth, il faut une cle API."
  exit 1
fi
if [ -z "$ACMOJ_TOKEN" ] || [ "$ACMOJ_TOKEN" = "your_acmoj_token" ]; then
  echo "⚠️  ACMOJ_TOKEN absent — l'agent ne pourra pas soumettre, mais peut quand meme coder/tester."
fi

# --- Worktree + branche ------------------------------------------------------
BRANCH="bench/${PROBLEM_ID}-${TIMESTAMP}"
WORKTREE="$(realpath "$PROJECT_ROOT/..")/projdevbench-bench-${PROBLEM_ID}-${TIMESTAMP}"

echo "🌿 Worktree   : $WORKTREE"
echo "🌿 Branch     : $BRANCH"
echo "🎯 Model      : $API_MODEL_ID${CLAUDE_EFFORT:+ (effort: $CLAUDE_EFFORT)}"
echo "🎯 Problem    : $PROBLEM_ID — $PROBLEM_NAME (ACMOJ $ACMOJ_ID, max $MAX_SUBMISSIONS submissions)"
echo ""

git worktree add -b "$BRANCH" "$WORKTREE" >/dev/null

# Copie problem files + data
cp -r "problem/$PROBLEM_ID/." "$WORKTREE/"
chmod -R u+w "$WORKTREE" 2>/dev/null || true
[ -d "data/$PROBLEM_ID" ] && cp -r "data/$PROBLEM_ID" "$WORKTREE/data"

# Contexte pour la slash command
cat > "$WORKTREE/.bench-context.json" <<EOF
{
  "problem_id": "$PROBLEM_ID",
  "problem_name": "$PROBLEM_NAME",
  "acmoj_problem_id": "$ACMOJ_ID",
  "max_submissions": $MAX_SUBMISSIONS,
  "branch": "$BRANCH",
  "timestamp": "$TIMESTAMP",
  "model": "$API_MODEL_ID",
  "effort": "${CLAUDE_EFFORT:-default}"
}
EOF

# Initial commit
( cd "$WORKTREE" && git add -A && git commit -m "Bench setup: problem $PROBLEM_ID" --allow-empty >/dev/null )

# --- Log dir + lancement claude ---------------------------------------------
LOG_DIR="$PROJECT_ROOT/logs/claude-code-local/$LOG_MODEL/$PROBLEM_ID"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/oj_eval_local_${PROBLEM_ID}_${TIMESTAMP}.log"
echo "📝 Log        : $LOG_FILE"

EFFORT_ARGS=()
[ -n "$CLAUDE_EFFORT" ] && EFFORT_ARGS=(--effort "$CLAUDE_EFFORT")

echo ""
echo "🚀 Launching claude -p /bench-solve --dangerously-skip-permissions ..."
echo ""

# Important : cd dans le worktree pour que claude charge .claude/commands/ depuis la
# racine du projet (le worktree est un checkout, donc .claude/ est present).
cd "$WORKTREE"
set +e
claude -p "/bench-solve" \
  --model "$API_MODEL_ID" \
  "${EFFORT_ARGS[@]}" \
  --dangerously-skip-permissions \
  --output-format stream-json \
  --verbose \
  | tee "$LOG_FILE"
CLAUDE_RC=${PIPESTATUS[0]}
set -e
cd "$PROJECT_ROOT"

echo ""
echo "✅ Session terminée (exit $CLAUDE_RC)"
echo ""

# --- Token tracking : parsing du log stream-json -> TOKENS.md ----------------
echo "📊 Extraction des tokens..."
python3 - "$LOG_FILE" "$WORKTREE/TOKENS.md" "$PROBLEM_ID" "$TIMESTAMP" "$API_MODEL_ID" "${CLAUDE_EFFORT:-default}" <<'PY'
import json, sys
log_path, out_path, problem_id, ts, model, effort = sys.argv[1:7]
totals = {"input": 0, "output": 0, "cache_creation": 0, "cache_read": 0}
turns = 0
with open(log_path, encoding="utf-8", errors="replace") as f:
    for line in f:
        try:
            obj = json.loads(line)
        except Exception:
            continue
        usage = (obj.get("message") or {}).get("usage") or obj.get("usage") or {}
        if not usage:
            continue
        any_count = False
        for src, dst in (
            ("input_tokens", "input"),
            ("output_tokens", "output"),
            ("cache_creation_input_tokens", "cache_creation"),
            ("cache_read_input_tokens", "cache_read"),
        ):
            v = usage.get(src) or 0
            if v:
                totals[dst] += v
                any_count = True
        if any_count:
            turns += 1
billable = totals["input"] + totals["output"] + totals["cache_creation"]
md = f"""# Token usage — {problem_id} — {ts}

| Field | Value |
|---|---|
| Model | `{model}` |
| Effort | `{effort}` |
| Turns with usage data | {turns} |

| Metric | Tokens |
|---|---:|
| Input | {totals['input']:,} |
| Output | {totals['output']:,} |
| Cache creation | {totals['cache_creation']:,} |
| Cache read | {totals['cache_read']:,} |
| **Total billable** | **{billable:,}** |

> Cache reads sont facturés à tarif réduit ; ne sont pas inclus dans "Total billable".
"""
with open(out_path, "w", encoding="utf-8") as f:
    f.write(md)
print(f"  → {out_path}")
PY

echo ""
echo "📦 Resultats dans le worktree :"
echo "   $WORKTREE/SESSION.md   (resume narratif par claude)"
echo "   $WORKTREE/TOKENS.md    (decompte tokens)"
echo ""
echo "🧹 Pour nettoyer apres analyse :"
echo "   git worktree remove --force $WORKTREE"
echo "   git branch -D $BRANCH"
