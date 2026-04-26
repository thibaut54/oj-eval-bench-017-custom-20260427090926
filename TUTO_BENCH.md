# Tuto — Bencher Claude Code (vanilla vs custom) avec ProjDevBench

But : sur le **problème 001** (smoke test), exécuter Claude Code deux fois — une en config par défaut (`vanilla`), une avec ta config complète (`custom`) — puis comparer scores et tokens.

> Toutes les commandes sont à lancer **dans WSL** (Ubuntu) depuis le dossier projet. Les chemins Windows sont accessibles via `/mnt/c/...`.

---

## 0. Prérequis (à faire une seule fois)

### 0.1 — Docker Desktop avec intégration WSL

Ouvre Docker Desktop → *Settings* → *Resources* → *WSL Integration* → coche ta distro Ubuntu. Vérifie depuis WSL :
```bash
docker ps          # doit lister les containers (vide est OK)
```

### 0.2 — `jq` dans WSL

```bash
sudo apt update && sudo apt install -y jq
jq --version       # >= 1.6
```

### 0.3 — Image Docker du bench

L'image `prlu/ojbench-agent-runner:latest` est tirée automatiquement au premier `docker run`. Pour la pré-tirer maintenant (~plusieurs Go) :
```bash
docker pull prlu/ojbench-agent-runner:latest
```

### 0.4 — Variables d'environnement (`config/environment.env`)

```bash
cd /mnt/c/Users/ThibautVuillaume/Workspace/toolbox/projdevbench
cp config/environment.env.example config/environment.env  # si le template existe
nano config/environment.env
```

Renseigne **au minimum** :
- `GITHUB_TOKEN` — token GitHub avec scopes `repo` (le bench crée un repo public par run)
- `GITHUB_USER` — ton handle GitHub
- `ACMOJ_TOKEN` — token sur l'OJ ACM
- `ANTHROPIC_API_KEY` — ta clé Anthropic
- `ANTHROPIC_BASE_URL` — vide ou par défaut sauf si proxy

> ⚠️ Les valeurs `your_*_here` sont vérifiées par `run_evaluation.sh` et le run échoue tôt si oubliées.

### 0.5 — Vérifier la config custom locale

Le mode custom va bind-mounter `~/.claude/` depuis Windows. Confirmer qu'elle est accessible depuis WSL :
```bash
ls -la /mnt/c/Users/ThibautVuillaume/.claude/agents/  | head
ls -la /mnt/c/Users/ThibautVuillaume/.claude/skills/  | head
ls -la /mnt/c/Users/ThibautVuillaume/.claude/plugins/ | head
test -f /mnt/c/Users/ThibautVuillaume/.claude/CLAUDE.md && echo "CLAUDE.md OK"
test -f /mnt/c/Users/ThibautVuillaume/.claude/settings.json && echo "settings.json OK"
```

> Le script utilise `$HOME/.claude` par défaut. Sous WSL, `$HOME` pointe vers `/home/<utilisateur>/`. Pour cibler ta config Windows, tu peux soit :
> - **(a)** Faire un symlink `ln -s /mnt/c/Users/ThibautVuillaume/.claude ~/.claude` une fois pour toutes
> - **(b)** Override à chaque run via `HOST_CLAUDE_DIR=/mnt/c/Users/ThibautVuillaume/.claude`
>
> L'option **(b)** est plus explicite, on l'utilise dans la suite.

---

## 1. Smoke test : problème 001 en VANILLA

```bash
cd /mnt/c/Users/ThibautVuillaume/Workspace/toolbox/projdevbench

AGENT=claude-code \
MODEL=Sonnet \
PROBLEMS="001" \
CLAUDE_CONFIG_MODE=vanilla \
./scripts/run_all_problem.sh
```

À surveiller pendant le run :
- Banner "Claude Config Mode: vanilla"
- Dans le log, "✅ Vanilla mode: writing minimal settings.json"
- Le run dure ~5–10 min selon la complexité du problème

À la fin, les logs sont dans :
```
logs/claude-code/sonnet-4.5-vanilla/001/
├── oj_eval_claude-code_sonnet-4.5-vanilla_001_<TIMESTAMP>.log
└── submission_ids_001_<TIMESTAMP>.log
```

---

## 2. Smoke test : problème 001 en CUSTOM

```bash
AGENT=claude-code \
MODEL=Sonnet \
PROBLEMS="001" \
CLAUDE_CONFIG_MODE=custom \
HOST_CLAUDE_DIR=/mnt/c/Users/ThibautVuillaume/.claude \
./scripts/run_all_problem.sh
```

À surveiller :
- Banner "Claude Config Mode: custom"
- Dans le log, "📦 Custom Claude config mode: mounting from /mnt/c/Users/ThibautVuillaume/.claude"
- Dans le log container, "✅ Custom mode: merging host settings.json"
- Inventaire qui suit (`agents/ (NN entries)`, `skills/`, `plugins/`, etc.) — si une ligne dit MISSING, le mount n'a pas marché.

Logs dans :
```
logs/claude-code/sonnet-4.5-custom/001/
```

---

## 3. Vérifier les logs

### 3.1 — Confirmer la séparation des dossiers

```bash
ls logs/claude-code/
# Doit montrer au moins:
#   sonnet-4.5-vanilla/
#   sonnet-4.5-custom/
```

### 3.2 — Sanity check des deux runs (verdict de soumission)

```bash
grep -E "Accepted|Wrong Answer|Time Limit|Runtime|Compile" \
  logs/claude-code/sonnet-4.5-vanilla/001/oj_eval_*.log | tail
grep -E "Accepted|Wrong Answer|Time Limit|Runtime|Compile" \
  logs/claude-code/sonnet-4.5-custom/001/oj_eval_*.log | tail
```

---

## 4. Extraire la conso de tokens

```bash
# Vanilla
python3 scripts/analyze/analyze_tokens.py \
  --logs-dir logs --model-name sonnet-4.5-vanilla --problem 001

# Custom
python3 scripts/analyze/analyze_tokens.py \
  --logs-dir logs --model-name sonnet-4.5-custom --problem 001
```

Sortie attendue (CSV sur stdout) :
```
agent,model,problem,timestamp,input,output,cache_read,cache_creation,total_billable
claude-code,sonnet-4.5-vanilla,001,20260425..., 12345, 6789, 0, 23456, ...
```

Si `input` et `output` sont à zéro → le log ne contient pas les events JSON attendus (vérifier que `--output-format stream-json --verbose` est bien dans la commande container, ce qui doit être le cas par défaut).

---

## 5. Rapport comparatif

```bash
python3 scripts/analyze/compare_configs.py \
  --vanilla sonnet-4.5-vanilla \
  --custom  sonnet-4.5-custom \
  --problems 001 \
  --out comparison_001.md

cat comparison_001.md
```

Lecture :
- **Submissions V/C** : combien de soumissions OJ chaque config a faites (max 3 par problème en général)
- **Verdict V/C** : dernier verdict observé dans le log
- **Input/Output/Cache_creation/Billable V/C** : tokens
- **Δ billable** : diff custom − vanilla. **Custom > vanilla est attendu** (la config ajoute du contexte). Si l'inverse, c'est suspect : audit du parsing.

---

## 6. Si quelque chose cloche

### 6.1 — Le mode custom ne charge pas les agents/skills

Lance un run en mode debug pour entrer dans le container :
```bash
./scripts/run_evaluation.sh 001 1752 claude-code Sonnet debug
```
Une fois dans le container :
```bash
ls /home/agent/.claude/agents/  # doit montrer >100 .md
ls /home/agent/.claude/skills/  # doit montrer plein de dossiers
cat /home/agent/.claude/settings.json | jq .  # doit avoir tes env vars (sans hooks)
exit
```

### 6.2 — `HOST_CLAUDE_DIR` non trouvé

Le check au début de `run_evaluation.sh` retourne :
```
❌ CLAUDE_CONFIG_MODE=custom but host config dir not found: ...
```
→ Fournis le chemin POSIX absolu via `HOST_CLAUDE_DIR=/mnt/c/Users/ThibautVuillaume/.claude`.

### 6.3 — Erreurs `jq` au démarrage du container

Si `jq` casse sur ton settings.json (corruption JSON), le container affichera l'erreur. Vérifie côté hôte :
```bash
jq . /mnt/c/Users/ThibautVuillaume/.claude/settings.json > /dev/null && echo "JSON OK"
```

### 6.4 — Le run veut re-run un problème déjà loggé

Le bench détecte les logs existants et propose skip/force. Pour forcer :
```bash
FORCE=true CLAUDE_CONFIG_MODE=vanilla ... ./scripts/run_all_problem.sh
```

---

## 7. Étendre le bench (après validation du smoke)

Une fois que 001 marche en vanilla et custom :
- Lance plus de problèmes : `PROBLEMS="001,002,005,006,007"` (échantillon mixte)
- Active le parallélisme : `CONCURRENCY=4` (attention aux quotas API et au coût en tokens : ~4.81 M tokens / problème en moyenne d'après le README)
- Pour le full bench (20 problèmes × 2 configs), prévois 6–10 h par config

---

## 8. Ce qui n'est *pas* mesuré par cette comparaison

- Les **hooks** ne tournent pas (RTK, gitnexus, lessons.sh, coderabbit) — leur effet sur la productivité réelle n'est pas dans le delta.
- Le mode `-p` non-interactif tire un seul prompt → les hooks `Stop`, `PostCompact`, `UserPromptSubmit` (au-delà du premier) ne s'activent qu'une fois.
- L'effet "découverte progressive de skills" inhérent à une session multi-tour est partiellement écrasé par le mode print.

Garde ça en tête quand tu interprètes le diff.
