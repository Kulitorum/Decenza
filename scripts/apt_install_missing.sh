#!/usr/bin/env bash
# Install the apt packages the runner lacks: apt_install_missing.sh [apt options] -- pkg...
# `apt-get update` is the slow part on a GitHub runner (10 s to 4 min, set by the
# mirror), so it runs only when an install against the image's own index fails, as
# it does when the image's index names a superseded .deb that the mirror has dropped.
set -euo pipefail

opts=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do opts+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift

missing=()
for pkg in "$@"; do
  dpkg-query -W -f='${db:Status-Status}' "$pkg" 2>/dev/null | grep -qx installed || missing+=("$pkg")
done

if [ ${#missing[@]} -eq 0 ]; then
  echo "apt: already installed: $*"
  exit 0
fi

echo "apt: installing ${missing[*]}"
sudo apt-get install -y "${opts[@]}" "${missing[@]}" \
  || { sudo apt-get update && sudo apt-get install -y "${opts[@]}" "${missing[@]}"; }
