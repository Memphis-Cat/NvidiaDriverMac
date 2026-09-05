#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../.."

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "xcodegen is required (brew install xcodegen)" >&2
  exit 1
fi

xcodegen generate

if [[ "${1:-}" == "--unsigned" ]]; then
  xcodebuild \
    -project NvidiaDriverMac.xcodeproj \
    -target RTXMac \
    -configuration Debug \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build
  exit 0
fi

if [[ -z "${XCODE_TEAM_ID:-}" ]]; then
  echo "Set XCODE_TEAM_ID to your Apple Developer Team ID for a signed host build." >&2
  echo "For compile-only validation use: scripts/macos/build.sh --unsigned" >&2
  exit 2
fi

xcodebuild \
  -project NvidiaDriverMac.xcodeproj \
  -scheme RTXMacHost \
  -configuration Debug \
  DEVELOPMENT_TEAM="$XCODE_TEAM_ID" \
  build
