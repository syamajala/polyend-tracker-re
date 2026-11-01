#!/bin/sh
# apt-get install for CI: no recommends, stalled mirror downloads time out,
# and the whole update + install is tried up to three times (a mirror once
# held a job 20 min; the next run took 1).
# Usage: .github/scripts/apt-install.sh PACKAGE...
apt_opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"
for attempt in 1 2 3; do
  sudo timeout 240 apt-get $apt_opts update &&
    sudo DEBIAN_FRONTEND=noninteractive timeout 240 apt-get $apt_opts install -y --no-install-recommends "$@" &&
    exit 0
  echo "::warning::apt attempt $attempt failed, retrying"
done
exit 1
