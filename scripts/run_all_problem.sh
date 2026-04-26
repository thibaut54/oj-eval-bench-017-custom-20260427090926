#!/usr/bin/env bash

# =============================================================================
# ProjDevBench - 批量评估脚本（支持并行执行）
# =============================================================================
# 
# 环境变量:
#   AGENT       - 智能体类型 (如 cursor, claude-code, codex 等)
#   MODEL       - 模型名称 (如 auto, gpt-5, sonnet-4.5 等)
#   PROBLEMS    - 要运行的问题列表，逗号分隔 (如 "001,002,003")
#   CONCURRENCY - 并发数，默认为 1 (顺序执行)
#   SKIP_EXISTING - 是否跳过已有日志的问题 (true/false，默认 false)
#   FORCE       - 是否强制重跑已有日志的问题 (true/false，默认 false)
#
# 示例:
#   # 顺序执行
#   AGENT=cursor MODEL=auto PROBLEMS="001,002" ./scripts/run_all_problem.sh
#
#   # 并行执行 (4 个并发)
#   AGENT=cursor MODEL=auto PROBLEMS="001,002,003,004" CONCURRENCY=4 ./scripts/run_all_problem.sh
#
#   # 并行执行并跳过已有日志
#   AGENT=cursor MODEL=auto CONCURRENCY=4 SKIP_EXISTING=true ./scripts/run_all_problem.sh
#
# =============================================================================

set -e

# 配置文件路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AGENT_MODEL_CONFIG="${PROJECT_ROOT}/config/agent_model_config.json"
PROBLEM_REGISTRY="${PROJECT_ROOT}/config/problem_registry.json"

# 默认并发数为 1
CONCURRENCY=${CONCURRENCY:-1}

# Claude Code config mode: vanilla (default) | custom
# - vanilla: empty ~/.claude in container (current behavior, baseline)
# - custom: bind-mount host ~/.claude (agents, skills, commands, plugins, CLAUDE.md, settings.json)
CLAUDE_CONFIG_MODE=${CLAUDE_CONFIG_MODE:-vanilla}
export CLAUDE_CONFIG_MODE

# Claude Code effort level (optional): low | medium | high | xhigh | max
# Propagated to "claude -p --effort <level>" inside the container. Empty = CLI default.
CLAUDE_EFFORT="${CLAUDE_EFFORT:-}"
export CLAUDE_EFFORT

# 检查配置文件是否存在
if [ ! -f "$AGENT_MODEL_CONFIG" ]; then
    echo "❌ Configuration file not found: ${AGENT_MODEL_CONFIG}"
    exit 1
fi

if [ ! -f "$PROBLEM_REGISTRY" ]; then
    echo "❌ Configuration file not found: ${PROBLEM_REGISTRY}"
    exit 1
fi

# 检查是否安装了 jq
if ! command -v jq &> /dev/null; then
    echo "❌ jq is required but not installed."
    echo "Please install jq:"
    echo "  - On macOS: brew install jq"
    echo "  - On Linux: sudo apt-get install jq (Ubuntu/Debian) or sudo yum install jq (CentOS/RHEL)"
    exit 1
fi

# 从配置文件读取所有可用的 agents
mapfile -t AGENTS < <(jq -r '.agents | keys[]' "$AGENT_MODEL_CONFIG" | sort)

# =============================================================================
# Agent 和 Model 选择
# =============================================================================

if [ -n "$AGENT" ] && [ -n "$MODEL" ]; then
  AGENT_TYPE="$AGENT"
  MODEL_NAME="$MODEL"
  echo "✅ Using environment variables: AGENT=$AGENT_TYPE, MODEL=$MODEL_NAME"
else
  # 显示 Agent 选择菜单
  echo "=========================================="
  echo "🤖 Select Agent:"
  echo "=========================================="
  agent_count=${#AGENTS[@]}
  for i in $(seq 0 $((agent_count-1))); do
    echo "$((i+1)). ${AGENTS[$i]}"
  done
  echo "=========================================="
  read -p "Enter agent number (1-$agent_count): " agent_choice
  
  # 验证输入
  if ! [[ "$agent_choice" =~ ^[0-9]+$ ]] || [ "$agent_choice" -lt 1 ] || [ "$agent_choice" -gt "$agent_count" ]; then
    echo "❌ Invalid agent selection!"
    exit 1
  fi
  
  # 获取选中的 agent
  AGENT_TYPE="${AGENTS[$((agent_choice-1))]}"
  echo "✅ Selected agent: $AGENT_TYPE"
  echo ""
  
  # 从配置文件读取该 agent 支持的 models
  model_count=$(jq -r ".agents.\"$AGENT_TYPE\".models | length" "$AGENT_MODEL_CONFIG")
  
  if [ -z "$model_count" ] || [ "$model_count" = "null" ] || [ "$model_count" -eq 0 ]; then
    echo "❌ No models configured for agent: $AGENT_TYPE"
    exit 1
  fi
  
  # 显示 Model 选择菜单
  echo "=========================================="
  echo "🎯 Select Model for $AGENT_TYPE:"
  echo "=========================================="
  for i in $(seq 0 $((model_count-1))); do
    display_name=$(jq -r ".agents.\"$AGENT_TYPE\".models[$i].display_name" "$AGENT_MODEL_CONFIG")
    echo "$((i+1)). ${display_name}"
  done
  echo "=========================================="
  read -p "Enter model number (1-$model_count): " model_choice
  
  # 验证输入
  if ! [[ "$model_choice" =~ ^[0-9]+$ ]] || [ "$model_choice" -lt 1 ] || [ "$model_choice" -gt "$model_count" ]; then
    echo "❌ Invalid model selection!"
    exit 1
  fi
  
  # 获取选中的 model（使用 actual_name）
  MODEL_NAME=$(jq -r ".agents.\"$AGENT_TYPE\".models[$((model_choice-1))].actual_name" "$AGENT_MODEL_CONFIG")
  echo "✅ Selected model: $MODEL_NAME"
  echo ""
fi

# =============================================================================
# 问题列表处理
# =============================================================================

echo "Starting evaluations for all problems with agent: $AGENT_TYPE and model: $MODEL_NAME"
echo ""

# 从 JSON 配置文件读取所有问题 ID（并排序）
all_problem_ids=$(jq -r '.problems | keys[]' "$PROBLEM_REGISTRY" | sort)

# 检查是否通过 PROBLEMS 环境变量指定了问题列表
if [ -n "$PROBLEMS" ]; then
  echo "=========================================="
  echo "📝 使用环境变量指定的问题列表"
  echo "=========================================="
  # 将逗号分隔的列表转换为空格分隔
  problem_ids=$(echo "$PROBLEMS" | tr ',' ' ')
  echo "✅ 将运行指定的问题: $problem_ids"
  echo ""
else
  problem_ids="$all_problem_ids"
  
  # 如果是并行模式，不进行交互式范围选择
  if [ "$CONCURRENCY" -gt 1 ]; then
    echo "✅ 并行模式: 将运行所有题目"
    echo ""
  else
    # 询问是否需要指定题目范围
    echo "=========================================="
    echo "📝 题目范围设置 (Problem Range)"
    echo "=========================================="
    read -p "是否需要指定题目范围？(y/n，默认n运行所有题目): " use_range
    echo ""

    if [[ "$use_range" =~ ^[yY]$ ]]; then
      while true; do
        read -p "请输入起始题号 (例如: 1 或 001): " start_num
        read -p "请输入终止题号 (例如: 10 或 010): " end_num
        
        # 去除前导零并验证输入
        start_num=$((10#$start_num))
        end_num=$((10#$end_num))
        
        if ! [[ "$start_num" =~ ^[0-9]+$ ]] || ! [[ "$end_num" =~ ^[0-9]+$ ]]; then
          echo "❌ 输入无效，请输入数字！"
          continue
        fi
        
        if [ "$start_num" -gt "$end_num" ]; then
          echo "❌ 起始题号不能大于终止题号！"
          continue
        fi
        
        # 格式化为三位数
        START_PROBLEM=$(printf "%03d" "$start_num")
        END_PROBLEM=$(printf "%03d" "$end_num")
        echo "✅ 将运行题目范围: ${START_PROBLEM} 到 ${END_PROBLEM}"
        echo ""
        break
      done
      
      # 过滤问题ID，只保留在范围内的
      filtered_ids=""
      for problem_id in $problem_ids; do
        # 提取问题ID的数字部分进行比较
        if [[ "$problem_id" =~ ^([0-9]+) ]]; then
          id_num=$((10#${BASH_REMATCH[1]}))
          if [ "$id_num" -ge "$start_num" ] && [ "$id_num" -le "$end_num" ]; then
            filtered_ids="$filtered_ids $problem_id"
          fi
        fi
      done
      problem_ids="$filtered_ids"
      
      if [ -z "$problem_ids" ]; then
        echo "❌ 在指定范围内没有找到任何题目！"
        exit 1
      fi
    else
      echo "✅ 将运行所有题目"
      echo ""
    fi
  fi
fi

# =============================================================================
# 生成 LOG_MODEL_NAME 的函数
# =============================================================================
get_log_model_name() {
  local model_name="$1"
  local log_model_name
  case "$model_name" in
    Sonnet|"Claude Sonnet 4.5")  log_model_name="sonnet-4.5" ;;
    Opus|"Claude Opus 4.7")      log_model_name="opus-4-7" ;;
    Haiku|"Claude Haiku 4.5")    log_model_name="haiku-4-5" ;;
    *)
      log_model_name="${model_name// /-}"
      log_model_name="${log_model_name//\//-}"
      ;;
  esac
  # Suffix Claude Code logs with config mode so vanilla and custom runs do not collide
  if [ "$AGENT_TYPE" = "claude-code" ] || [ "$AGENT_TYPE" = "Claude Code" ]; then
    log_model_name="${log_model_name}-${CLAUDE_CONFIG_MODE}"
    # Suffix with effort if explicitly set so distinct effort levels do not collide
    if [ -n "$CLAUDE_EFFORT" ]; then
      log_model_name="${log_model_name}-${CLAUDE_EFFORT}"
    fi
  fi
  echo "$log_model_name"
}

# =============================================================================
# 检查是否需要跳过问题（已有日志）
# =============================================================================
should_skip_problem() {
  local problem_id="$1"
  local log_model_name
  log_model_name=$(get_log_model_name "$MODEL_NAME")
  local log_dir="${PROJECT_ROOT}/logs/${AGENT_TYPE}/${log_model_name}/${problem_id}"
  
  if [ -d "$log_dir" ] && [ "$(ls -A "$log_dir" 2>/dev/null)" ]; then
    return 0  # true, should skip
  fi
  return 1  # false, should not skip
}

# =============================================================================
# 单个问题评估函数
# =============================================================================
run_single_evaluation() {
  local problem_id="$1"
  local problem_name
  local acmoj_id
  
  problem_name=$(jq -r ".problems.\"$problem_id\".name" "$PROBLEM_REGISTRY")
  acmoj_id=$(jq -r ".problems.\"$problem_id\".acmoj_id" "$PROBLEM_REGISTRY")
  
  # 检查问题目录是否存在
  local problem_dir="${PROJECT_ROOT}/problem/${problem_id}"
  if [ ! -d "$problem_dir" ]; then
    echo "[${problem_id}] ⚠️ Skipping - directory not found: ${problem_dir}"
    return 0
  fi
  
  # 检查是否需要跳过（已有日志）
  if [ "${SKIP_EXISTING}" = "true" ]; then
    if should_skip_problem "$problem_id"; then
      local log_model_name
      log_model_name=$(get_log_model_name "$MODEL_NAME")
      echo "[${problem_id}] ⏭️ Skipping - logs exist at: logs/${AGENT_TYPE}/${log_model_name}/${problem_id}"
      return 0
    fi
  fi
  
  echo "[${problem_id}] 🚀 Starting evaluation: ${problem_name} (ACMOJ: ${acmoj_id})"
  
  # 执行评估
  cd "$PROJECT_ROOT"
  ./scripts/run_evaluation.sh "$problem_id" "$acmoj_id" "$AGENT_TYPE" "$MODEL_NAME"
  local exit_code=$?
  
  if [ $exit_code -eq 0 ]; then
    echo "[${problem_id}] ✅ Completed successfully"
  else
    echo "[${problem_id}] ❌ Failed with exit code: $exit_code"
  fi
  
  return $exit_code
}

# 导出函数和变量供并行子进程使用
export -f run_single_evaluation
export -f should_skip_problem
export -f get_log_model_name
export AGENT_TYPE MODEL_NAME PROJECT_ROOT PROBLEM_REGISTRY SKIP_EXISTING FORCE CLAUDE_CONFIG_MODE CLAUDE_EFFORT

# =============================================================================
# 主执行逻辑
# =============================================================================

# 构建最终的问题列表
final_problem_list=()
for problem_id in $problem_ids; do
  # 检查问题目录是否存在
  problem_dir="${PROJECT_ROOT}/problem/${problem_id}"
  if [ ! -d "$problem_dir" ]; then
    echo "⚠️ Skipping problem ${problem_id} - directory not found"
    continue
  fi
  final_problem_list+=("$problem_id")
done

total_problems=${#final_problem_list[@]}

if [ "$total_problems" -eq 0 ]; then
  echo "❌ No valid problems to evaluate!"
  exit 1
fi

echo "=========================================="
echo "📋 Evaluation Summary"
echo "=========================================="
echo "Agent: $AGENT_TYPE"
echo "Model: $MODEL_NAME"
echo "Problems: ${final_problem_list[*]}"
echo "Total: $total_problems problems"
echo "Concurrency: $CONCURRENCY"
echo "Skip Existing: ${SKIP_EXISTING:-false}"
if [ "$AGENT_TYPE" = "claude-code" ] || [ "$AGENT_TYPE" = "Claude Code" ]; then
  echo "Claude Config Mode: ${CLAUDE_CONFIG_MODE}"
fi
echo "=========================================="
echo ""

# =============================================================================
# 根据并发数选择执行模式
# =============================================================================

if [ "$CONCURRENCY" -le 1 ]; then
  # ==========================================================================
  # 顺序执行模式（支持交互式选择）
  # ==========================================================================
  echo "🔄 Running in sequential mode..."
  echo ""
  
  # 全局应用选择："none" | "skip_all" | "force_all"
  ALL_MODE="none"
  
  # 如果设置了 SKIP_EXISTING 或 FORCE，直接应用
  if [ "${SKIP_EXISTING}" = "true" ]; then
    ALL_MODE="skip_all"
  elif [ "${FORCE}" = "true" ]; then
    ALL_MODE="force_all"
  fi
  
  completed=0
  failed=0
  skipped=0
  
  for problem_id in "${final_problem_list[@]}"; do
    problem_name=$(jq -r ".problems.\"$problem_id\".name" "$PROBLEM_REGISTRY")
    acmoj_id=$(jq -r ".problems.\"$problem_id\".acmoj_id" "$PROBLEM_REGISTRY")
    
    echo "=========================================="
    echo "Evaluating Problem ${problem_id}: ${problem_name}"
    echo "ACMOJ ID: ${acmoj_id}"
    echo "Progress: $((completed + failed + skipped + 1))/${total_problems}"
    echo "=========================================="
    
    # 检查是否已有日志
    log_model_name=$(get_log_model_name "$MODEL_NAME")
    LOG_DIR="${PROJECT_ROOT}/logs/${AGENT_TYPE}/${log_model_name}/${problem_id}"
    
    if [ -d "$LOG_DIR" ] && [ "$(ls -A "$LOG_DIR" 2>/dev/null)" ]; then
      if [ "$ALL_MODE" = "skip_all" ]; then
        echo "⏭️ Skipping problem ${problem_id} - logs exist"
        ((skipped++))
        echo ""
        continue
      elif [ "$ALL_MODE" = "force_all" ]; then
        echo "🔁 Forcing re-run for problem ${problem_id}"
      else
        # 交互询问用户
        while true; do
          echo "⚠️ Logs already exist at: ${LOG_DIR}"
          echo "请选择："
          echo "  1) 跳过该题 (Skip)"
          echo "  2) 强制重跑该题 (Force re-run)"
          echo "  3) 跳过该题并对之后所有题目应用相同选择 (Skip all remaining)"
          echo "  4) 强制重跑该题并对之后所有题目应用相同选择 (Force all remaining)"
          read -p "输入 1/2/3/4 并回车: " user_choice
          case "$user_choice" in
            1)
              echo "⏭️ Skipping problem ${problem_id}"
              ((skipped++))
              skip_current=true
              break
              ;;
            2)
              echo "🔁 Forcing re-run for problem ${problem_id}"
              skip_current=false
              break
              ;;
            3)
              echo "⏭️ Skipping problem ${problem_id} and all remaining with existing logs"
              ((skipped++))
              skip_current=true
              ALL_MODE="skip_all"
              break
              ;;
            4)
              echo "🔁 Forcing re-run for problem ${problem_id} and all remaining"
              skip_current=false
              ALL_MODE="force_all"
              break
              ;;
            *)
              echo "无效输入，请输入 1、2、3 或 4。"
              ;;
          esac
        done
        
        if [ "$skip_current" = true ]; then
          echo ""
          continue
        fi
      fi
    fi
    
    # 执行评估
    ./scripts/run_evaluation.sh "$problem_id" "$acmoj_id" "$AGENT_TYPE" "$MODEL_NAME"
    if [ $? -eq 0 ]; then
      ((completed++))
    else
      ((failed++))
    fi
    echo ""
  done

else
  # ==========================================================================
  # 并行执行模式
  # ==========================================================================
  echo "🚀 Running in parallel mode with $CONCURRENCY concurrent jobs..."
  echo ""
  
  # 如果没有设置 SKIP_EXISTING 或 FORCE，默认跳过已有日志
  if [ -z "${SKIP_EXISTING}" ] && [ -z "${FORCE}" ]; then
    echo "⚠️ Parallel mode: defaulting to SKIP_EXISTING=true"
    echo "   Use FORCE=true to re-run problems with existing logs"
    echo ""
    export SKIP_EXISTING=true
  fi
  
  # 创建临时文件存储问题列表
  TEMP_PROBLEMS=$(mktemp)
  printf '%s\n' "${final_problem_list[@]}" > "$TEMP_PROBLEMS"
  
  # 创建临时目录存储结果
  RESULTS_DIR=$(mktemp -d)
  
  # 定义并行执行的 worker 函数
  worker() {
    local problem_id="$1"
    local result_file="${RESULTS_DIR}/${problem_id}.result"
    
    # 执行评估并记录结果
    if run_single_evaluation "$problem_id"; then
      echo "success" > "$result_file"
    else
      echo "failed" > "$result_file"
    fi
  }
  export -f worker
  export RESULTS_DIR
  
  # 使用 xargs 进行并行执行
  cat "$TEMP_PROBLEMS" | xargs -P "$CONCURRENCY" -I {} bash -c 'worker "$@"' _ {}
  
  # 统计结果
  completed=0
  failed=0
  skipped=0
  
  for problem_id in "${final_problem_list[@]}"; do
    result_file="${RESULTS_DIR}/${problem_id}.result"
    if [ -f "$result_file" ]; then
      result=$(cat "$result_file")
      if [ "$result" = "success" ]; then
        ((completed++))
      else
        ((failed++))
      fi
    else
      # 如果没有结果文件，说明被跳过了
      ((skipped++))
    fi
  done
  
  # 清理临时文件
  rm -f "$TEMP_PROBLEMS"
  rm -rf "$RESULTS_DIR"
fi

# =============================================================================
# 输出总结
# =============================================================================

echo "=========================================="
echo "📊 Evaluation Summary"
echo "=========================================="
echo "Total problems: $total_problems"
echo "Completed: $completed"
echo "Failed: $failed"
echo "Skipped: $skipped"
echo "=========================================="
echo "All evaluations completed!"
echo "=========================================="