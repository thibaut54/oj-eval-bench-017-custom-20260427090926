# scripts/run_local.ps1 — Bench local sans Docker, natif PowerShell 7+.
#
# Crée une branche dédiée + un git worktree, copie les fichiers du problème,
# puis lance "claude -p /bench-solve --dangerously-skip-permissions" en mode unattended.
# Capture le log stream-json et extrait les tokens dans TOKENS.md a la fin.
#
# Usage :
#   .\scripts\run_local.ps1 -ProblemId 001
#   .\scripts\run_local.ps1 -ProblemId 001 -Model Opus -Effort high
#
# Pre-requis (tous installes coté Windows, pas WSL) :
#   - PowerShell 7+ (pwsh)
#   - claude CLI : npm install -g @anthropic-ai/claude-code
#   - claude login deja effectue (utilise l'OAuth subscription Claude.ai Pro/Max)
#   - git installe
#   - ACMOJ_TOKEN dans config/environment.env si tu veux que l'agent soumette (sinon il code en local seulement)

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$ProblemId,

    [ValidateSet('Sonnet','Opus','Haiku','Claude Sonnet 4.5','Claude Opus 4.7','Claude Haiku 4.5')]
    [string]$Model = 'Opus',

    [ValidateSet('low','medium','high','xhigh','max')]
    [string]$Effort = 'high',

    # -Vanilla : isole l'invocation claude de ta config (no global CLAUDE.md, agents,
    # skills, hooks, plugins). Preserve l'OAuth (credentials.json est copie dans le sandbox).
    # Sans le flag : mode "custom" = ta config quotidienne complete.
    [switch]$Vanilla
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# --- Project root ------------------------------------------------------------
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $ProjectRoot

# --- Pre-flight checks -------------------------------------------------------
function Test-Command([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}
if (-not (Test-Command 'claude')) { throw 'claude CLI introuvable. Installer: npm install -g @anthropic-ai/claude-code' }
if (-not (Test-Command 'git'))    { throw 'git introuvable.' }
if (-not (Test-Path "problem/$ProblemId"))            { throw "problem/$ProblemId introuvable" }
if (-not (Test-Path 'config/problem_registry.json'))  { throw 'config/problem_registry.json manquant' }
if (-not (Test-Path 'config/environment.env'))        { throw 'config/environment.env manquant' }

# --- Resolution model id + log dir ------------------------------------------
$ApiModelId, $LogModel = switch ($Model) {
    { $_ -in 'Sonnet','Claude Sonnet 4.5' } { 'claude-sonnet-4-5', 'sonnet-4.5' }
    { $_ -in 'Opus','Claude Opus 4.7' }     { 'claude-opus-4-7',   'opus-4-7' }
    { $_ -in 'Haiku','Claude Haiku 4.5' }   { 'claude-haiku-4-5',  'haiku-4-5' }
    default                                  { $Model, ($Model -replace ' ','-') }
}
$ConfigMode = if ($Vanilla) { 'vanilla' } else { 'custom' }
$LogModel = "$LogModel-local-$ConfigMode"
if ($Effort) { $LogModel = "$LogModel-$Effort" }

# --- Lecture registry --------------------------------------------------------
$registry = Get-Content 'config/problem_registry.json' -Raw | ConvertFrom-Json
$problemEntry = $registry.problems.$ProblemId
if (-not $problemEntry) { throw "Problem $ProblemId absent du registry" }
$AcmojId         = $problemEntry.acmoj_id
$ProblemName     = $problemEntry.name
$MaxSubmissions  = if ($null -ne $problemEntry.max_submissions) { $problemEntry.max_submissions }
                   elseif ($null -ne $registry.default.max_submissions) { $registry.default.max_submissions }
                   else { 3 }

# --- Source environment.env (parser .env minimal) ----------------------------
Get-Content 'config/environment.env' | ForEach-Object {
    $line = $_
    if ($line -match '^\s*$' -or $line -match '^\s*#') { return }
    if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$') {
        $k = $Matches[1]
        $v = $Matches[2].Trim()
        if ($v -match '^"(.*)"$' -or $v -match "^'(.*)'$") { $v = $Matches[1] }
        Set-Item "env:$k" $v
    }
}

# Note : ANTHROPIC_API_KEY n'est PAS requise — on lance "claude" en interactif,
# qui utilise l'OAuth subscription du `claude login` precedent (~/.claude/.credentials.json).
if (-not $env:ACMOJ_TOKEN -or $env:ACMOJ_TOKEN -eq 'your_acmoj_token') {
    Write-Warning 'ACMOJ_TOKEN absent — l''agent ne pourra pas soumettre, mais peut quand meme coder/tester.'
}

# --- Worktree + branche ------------------------------------------------------
$Timestamp = Get-Date -Format 'yyyyMMddHHmmss'
$Branch    = "bench/$ProblemId-$ConfigMode-$Timestamp"
$ParentDir = (Resolve-Path (Join-Path $ProjectRoot '..')).Path
$Worktree  = Join-Path $ParentDir "projdevbench-bench-$ProblemId-$ConfigMode-$Timestamp"

Write-Host "🌿 Worktree   : $Worktree"
Write-Host "🌿 Branch     : $Branch"
Write-Host "🎯 Model      : $ApiModelId$(if ($Effort) { " (effort: $Effort)" })"
Write-Host "🎯 Problem    : $ProblemId — $ProblemName (ACMOJ $AcmojId, max $MaxSubmissions submissions)"
Write-Host "🎚️  Config     : $ConfigMode$(if ($Vanilla) { ' (HOME sandbox + project config stripped)' })"
Write-Host ''

git worktree add -b $Branch $Worktree | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'git worktree add a echoue' }

# Copie problem files dans la racine du worktree (sans ecraser .git)
Copy-Item -Path "problem/$ProblemId/*" -Destination $Worktree -Recurse -Force
if (Test-Path "data/$ProblemId") {
    Copy-Item -Path "data/$ProblemId" -Destination (Join-Path $Worktree 'data') -Recurse -Force
}

# Copie .claude/commands/ pour que /bench-solve soit dispo dans le worktree,
# meme si bench-solve.md n'est pas (encore) commite sur le projet.
$WorktreeClaudeDir = Join-Path $Worktree '.claude/commands'
New-Item -ItemType Directory -Path $WorktreeClaudeDir -Force | Out-Null
Copy-Item -Path '.claude/commands/*' -Destination $WorktreeClaudeDir -Recurse -Force

# Settings minimaux pour le worktree (utilise via --settings).
# En mode --bare, claude n'auto-charge pas le CLAUDE.md global ni les hooks.
$BenchSettings = @{
    env = @{
        ANTHROPIC_DEFAULT_HAIKU_MODEL  = $ApiModelId
        ANTHROPIC_DEFAULT_SONNET_MODEL = $ApiModelId
        ANTHROPIC_DEFAULT_OPUS_MODEL   = $ApiModelId
    }
} | ConvertTo-Json -Depth 4
$BenchSettingsPath = Join-Path $Worktree '.bench-settings.json'
Set-Content -Path $BenchSettingsPath -Value $BenchSettings -Encoding utf8

# Contexte pour la slash command
$context = @{
    problem_id        = $ProblemId
    problem_name      = $ProblemName
    acmoj_problem_id  = $AcmojId
    max_submissions   = $MaxSubmissions
    branch            = $Branch
    timestamp         = $Timestamp
    model             = $ApiModelId
    effort            = if ($Effort) { $Effort } else { 'default' }
}
$context | ConvertTo-Json -Depth 4 |
    Set-Content -Path (Join-Path $Worktree '.bench-context.json') -Encoding utf8

# Mode vanilla : strip la config niveau projet pour empecher claude de l'auto-loader
# (CLAUDE.md, AGENTS.md, .claude/agents, .claude/skills, .claude/plugins, .claude/settings*).
# La slash command .claude/commands/bench-solve.md est preservee.
if ($Vanilla) {
    Write-Host '🧹 Vanilla : strip de la config niveau projet du worktree...'
    $strippable = @(
        Join-Path $Worktree 'CLAUDE.md'
        Join-Path $Worktree 'AGENTS.md'
        Join-Path $Worktree '.claude/agents'
        Join-Path $Worktree '.claude/skills'
        Join-Path $Worktree '.claude/plugins'
        Join-Path $Worktree '.claude/settings.json'
        Join-Path $Worktree '.claude/settings.local.json'
        Join-Path $Worktree '.claude/lessons.md'
        Join-Path $Worktree '.gitnexus'
    )
    foreach ($p in $strippable) {
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
}

# Initial commit dans le worktree
Push-Location $Worktree
try {
    git add -A | Out-Null
    $commitMsg = if ($Vanilla) { "Bench setup (vanilla): problem $ProblemId" } else { "Bench setup (custom): problem $ProblemId" }
    git commit -m $commitMsg --allow-empty | Out-Null
}
finally { Pop-Location }

# --- Lancement claude INTERACTIF (utilise ta session OAuth subscription) ----
#
# Pourquoi interactif et pas "-p" :
#   - "claude -p" headless rejette OAuth ("OAuth not supported"), exige une API key.
#   - "claude" interactif accepte OAuth = ton abo Claude.ai Pro/Max.
#   - "--dangerously-skip-permissions" rend la session autonome apres le premier
#     prompt : aucune confirmation ne stoppe l'agent. Tu tapes /bench-solve une fois,
#     tu pars, tu reviens quand c'est fini, /exit pour quitter.

Write-Host ''
Write-Host '🚀 Lancement de claude en interactif (subscription OAuth)...'
Write-Host ''
Write-Host '👉 Quand le TUI s''ouvre :'
Write-Host '   1. Tape :  /bench-solve'
Write-Host '   2. Pars. Skip-permissions est actif, claude bosse tout seul.'
Write-Host '   3. Quand tu reviens, /exit pour quitter et generer TOKENS.md.'
Write-Host ''
Read-Host 'Appuie sur Entree pour lancer claude (ou Ctrl+C pour annuler)' | Out-Null

# IMPORTANT : on PURGE les env vars ANTHROPIC_* heritees du .env source pour ce process.
# Si elles restent, claude code les utilise prioritairement sur ~/.claude/.credentials.json
# et tente d'envoyer l'OAuth token comme une API key -> 401 "OAuth not supported".
# En les retirant, claude retombe sur le credentials.json natif (ton `claude login`) qui
# gere correctement l'OAuth refresh.
$savedKey  = $env:ANTHROPIC_API_KEY
$savedTok  = $env:ANTHROPIC_AUTH_TOKEN
$savedUrl  = $env:ANTHROPIC_BASE_URL
$savedHome = $env:HOME
$savedUserProfile = $env:USERPROFILE
Remove-Item Env:ANTHROPIC_API_KEY    -ErrorAction SilentlyContinue
Remove-Item Env:ANTHROPIC_AUTH_TOKEN -ErrorAction SilentlyContinue
Remove-Item Env:ANTHROPIC_BASE_URL   -ErrorAction SilentlyContinue

# Mode vanilla : sandbox HOME -> ~/.claude/CLAUDE.md, agents, skills, hooks ne se chargent pas.
# On copie .credentials.json pour preserver l'OAuth subscription (sinon claude demanderait /login).
if ($Vanilla) {
    $vanillaHome = Join-Path $Worktree '.vanilla-home'
    $vanillaClaudeDir = Join-Path $vanillaHome '.claude'
    New-Item -ItemType Directory -Path $vanillaClaudeDir -Force | Out-Null
    $realCreds = Join-Path $savedUserProfile '.claude/.credentials.json'
    if (Test-Path $realCreds) {
        Copy-Item $realCreds (Join-Path $vanillaClaudeDir '.credentials.json') -Force
    } else {
        Write-Warning '~/.claude/.credentials.json introuvable — claude pourrait demander /login.'
        Write-Warning 'Si c''est le cas, exit, fais "claude login" puis relance ce script.'
    }
    # settings.json minimal (no hooks, no env, no model overrides — claude utilise --model et --effort directs)
    '{ "env": {} }' | Set-Content (Join-Path $vanillaClaudeDir 'settings.json') -Encoding utf8
    $env:HOME = $vanillaHome
    $env:USERPROFILE = $vanillaHome
    Write-Host "🧪 Vanilla : HOME redirige vers $vanillaHome"
}

$preLaunchTime = Get-Date

Push-Location $Worktree
try {
    & claude --dangerously-skip-permissions --model $ApiModelId --effort $Effort
    $claudeExit = $LASTEXITCODE
}
finally {
    Pop-Location
    # Restauration des env vars
    if ($null -ne $savedKey)         { $env:ANTHROPIC_API_KEY = $savedKey }
    if ($null -ne $savedTok)         { $env:ANTHROPIC_AUTH_TOKEN = $savedTok }
    if ($null -ne $savedUrl)         { $env:ANTHROPIC_BASE_URL = $savedUrl }
    if ($null -ne $savedHome)        { $env:HOME = $savedHome } else { Remove-Item Env:HOME -ErrorAction SilentlyContinue }
    if ($null -ne $savedUserProfile) { $env:USERPROFILE = $savedUserProfile }
}

Write-Host ''
Write-Host "✅ Session claude terminee (exit $claudeExit)"
Write-Host ''

# --- Extraction tokens depuis le session log de Claude Code ------------------
# Claude Code persiste chaque session en JSONL sous %USERPROFILE%\.claude\projects\<encoded-cwd>\<uuid>.jsonl
# L'encodage du chemin est : ":" et "\" remplaces par "-" (donc "C:\Users\..." -> "C--Users-...").
Write-Host '📊 Extraction des tokens depuis le session log...'

$claudeProjectsDir = Join-Path $env:USERPROFILE '.claude/projects'
$encodedCwd = ($Worktree -replace ':', '-') -replace '\\', '-'
$projDir = Join-Path $claudeProjectsDir $encodedCwd

if (-not (Test-Path $projDir)) {
    # Fallback : match par nom de base
    $leaf = Split-Path $Worktree -Leaf
    $projDir = Get-ChildItem $claudeProjectsDir -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*$leaf*" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

$tokensPath = Join-Path $Worktree 'TOKENS.md'

if (-not $projDir -or -not (Test-Path $projDir)) {
    Write-Warning "Session log introuvable sous $claudeProjectsDir."
    Write-Warning "TOKENS.md non genere — verifie a la main avec /context dans une nouvelle session."
} else {
    # Prendre la session JSONL la plus recente (post-launch)
    $sessions = Get-ChildItem $projDir -Filter '*.jsonl' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $preLaunchTime.AddSeconds(-5) } |
        Sort-Object LastWriteTime -Descending
    if (-not $sessions) {
        # Fallback : la plus recente tout court
        $sessions = Get-ChildItem $projDir -Filter '*.jsonl' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
    }
    $sessionFile = $sessions | Select-Object -First 1
    if (-not $sessionFile) {
        Write-Warning "Aucun .jsonl trouve dans $projDir."
    } else {
        Write-Host "  Session : $($sessionFile.FullName)"
        $totals = @{ input = 0; output = 0; cache_creation = 0; cache_read = 0 }
        $turns = 0
        foreach ($line in Get-Content -Path $sessionFile.FullName -Encoding utf8 -ErrorAction SilentlyContinue) {
            if (-not $line.Trim()) { continue }
            try { $obj = $line | ConvertFrom-Json -ErrorAction Stop } catch { continue }
            $usage = $null
            if ($obj.message.usage) { $usage = $obj.message.usage }
            elseif ($obj.usage)      { $usage = $obj.usage }
            if (-not $usage) { continue }
            $hadAny = $false
            foreach ($pair in @(
                @{ src = 'input_tokens';                dst = 'input' },
                @{ src = 'output_tokens';               dst = 'output' },
                @{ src = 'cache_creation_input_tokens'; dst = 'cache_creation' },
                @{ src = 'cache_read_input_tokens';     dst = 'cache_read' }
            )) {
                $val = $usage.($pair.src)
                if ($val -and $val -gt 0) {
                    $totals[$pair.dst] += [int]$val
                    $hadAny = $true
                }
            }
            if ($hadAny) { $turns++ }
        }
        $billable = $totals.input + $totals.output + $totals.cache_creation
        $effortLabel = if ($Effort) { $Effort } else { 'default' }
        $tokensMd = @"
# Token usage — $ProblemId — $Timestamp

| Field | Value |
|---|---|
| Model | ``$ApiModelId`` |
| Effort | ``$effortLabel`` |
| Config mode | ``$ConfigMode`` |
| Auth | OAuth subscription (Claude.ai Pro/Max) |
| Session log | ``$($sessionFile.Name)`` |
| Turns with usage data | $turns |

| Metric | Tokens |
|---|---:|
| Input | $('{0:N0}' -f $totals.input) |
| Output | $('{0:N0}' -f $totals.output) |
| Cache creation | $('{0:N0}' -f $totals.cache_creation) |
| Cache read | $('{0:N0}' -f $totals.cache_read) |
| **Total billable** | **$('{0:N0}' -f $billable)** |

> Cache reads sont factures a tarif reduit ; non inclus dans "Total billable".
> Tokens parses depuis le session log de Claude Code (pas de facturation API ici, c'est ton abo).
"@
        Set-Content -Path $tokensPath -Value $tokensMd -Encoding utf8
        Write-Host "  -> $tokensPath"
    }
}

Write-Host ''
Write-Host '📦 Resultats dans le worktree :'
Write-Host "   $Worktree\SESSION.md   (resume narratif par claude)"
Write-Host "   $Worktree\TOKENS.md    (decompte tokens)"
Write-Host ''
Write-Host '🧹 Pour nettoyer apres analyse :'
Write-Host "   git worktree remove --force `"$Worktree`""
Write-Host "   git branch -D $Branch"
