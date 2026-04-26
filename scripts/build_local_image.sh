#!/bin/bash
# Build l'image Docker locale qui fixe l'image upstream prlu/ojbench-agent-runner:latest
# (claude CLI manquant + /workspace non-writable par agent).
#
# Usage : ./scripts/build_local_image.sh [version_claude]
#   version_claude : optionnel, defaut "latest" (ex: 2.0.37)

set -e

CLAUDE_VERSION="${1:-latest}"
IMAGE_TAG="projdevbench/agent-runner:local"

echo "🔨 Building $IMAGE_TAG (claude-code version: $CLAUDE_VERSION)..."

cd "$(dirname "$0")/.."

docker build \
  -f docker/agent-runner/Dockerfile.local \
  --build-arg CLAUDE_CODE_VERSION="$CLAUDE_VERSION" \
  -t "$IMAGE_TAG" \
  .

echo ""
echo "✅ Image $IMAGE_TAG built successfully"
echo "   Verification :"
docker run --rm --entrypoint sh "$IMAGE_TAG" -c "claude --version && ls -ld /workspace"
