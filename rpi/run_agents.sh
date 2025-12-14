#!/usr/bin/env bash
set -euo pipefail

# RPi上で FleetWise Edge Agent と micro-ROS Agent をDockerで起動するユーティリティ
#
# Usage:
#   ./rpi/run_agents.sh up
#   ./rpi/run_agents.sh down
#   ./rpi/run_agents.sh logs
#   ./rpi/run_agents.sh ps
#
# Notes:
# - 実行前に rpi/agents.env を用意する
# - 証明書は rpi/cert/ に配置する

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
readonly COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"
readonly ENV_FILE="${SCRIPT_DIR}/agents.env"
readonly CERT_FILE="${SCRIPT_DIR}/cert/certificate.pem"
readonly KEY_FILE="${SCRIPT_DIR}/cert/private.key"

usage() {
  cat <<'EOF'
Usage:
  ./rpi/run_agents.sh <up|down|logs|ps>

Commands:
  up    Start micro-ROS Agent and FleetWise Edge Agent
  down  Stop containers
  logs  Follow logs
  ps    Show status
EOF
}

compose() {
  # docker compose (v2) と docker-compose (v1) の両対応
  if docker compose version >/dev/null 2>&1; then
    docker compose "$@"
    return 0
  fi

  if command -v docker-compose >/dev/null 2>&1; then
    docker-compose "$@"
    return 0
  fi

  echo "ERROR: Docker Compose が見つかりません (docker compose / docker-compose)" >&2
  echo "HINT: Docker Engine + Compose plugin を入れてね" >&2
  exit 1
}

require_file() {
  local path="${1:?path required}"
  local hint="${2:-}"

  if [[ ! -f "${path}" ]]; then
    echo "ERROR: required file not found: ${path}" >&2
    if [[ -n "${hint}" ]]; then
      echo "HINT: ${hint}" >&2
    fi
    exit 1
  fi
}

main() {
  local cmd="${1:-}"

  if [[ -z "${cmd}" ]]; then
    usage
    exit 2
  fi

  if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker が見つかりません" >&2
    echo "HINT: RPiにDockerをインストールしてね" >&2
    exit 1
  fi

  require_file "${COMPOSE_FILE}" "リポジトリの rpi/docker-compose.yml を確認してね"

  if [[ ! -f "${ENV_FILE}" ]]; then
    echo "ERROR: ${ENV_FILE} がありません" >&2
    echo "HINT: cp rpi/agents.env.example rpi/agents.env して値を埋めてね" >&2
    exit 1
  fi

  require_file "${CERT_FILE}" "AWS IoT Device証明書を rpi/cert/certificate.pem に配置"
  require_file "${KEY_FILE}" "AWS IoT Device秘密鍵を rpi/cert/private.key に配置"

  case "${cmd}" in
    up)
      compose --env-file "${ENV_FILE}" -f "${COMPOSE_FILE}" up -d
      ;;
    down)
      compose --env-file "${ENV_FILE}" -f "${COMPOSE_FILE}" down
      ;;
    logs)
      compose --env-file "${ENV_FILE}" -f "${COMPOSE_FILE}" logs -f
      ;;
    ps)
      compose --env-file "${ENV_FILE}" -f "${COMPOSE_FILE}" ps
      ;;
    *)
      usage
      exit 2
      ;;
  esac
}

main "$@"
