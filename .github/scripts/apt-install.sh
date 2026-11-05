#!/bin/sh
# apt-get install for CI: no recommends, stalled mirror downloads time out,
# and the whole update + install is tried up to three times (a mirror once
# held a job 20 min; the next run took 1).
#
# With APT_CACHE set (a folder restored by actions/cache), apt keeps its
# package lists and downloaded .debs there. When the folder already holds
# them, the install runs from it without the mirror; only a miss (a new
# package, a version the lists no longer name) goes to the network.
# Usage: [APT_CACHE=DIR] .github/scripts/apt-install.sh PACKAGE...
apt_opts="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"
if [ -n "$APT_CACHE" ]; then
  mkdir -p "$APT_CACHE/archives/partial" "$APT_CACHE/lists/partial"
  apt_opts="$apt_opts -o Dir::Cache::Archives=$APT_CACHE/archives -o Dir::State::Lists=$APT_CACHE/lists"
  apt_opts="$apt_opts -o APT::Keep-Downloaded-Packages=true -o Binary::apt::APT::Keep-Downloaded-Packages=true"
fi
# The cache folder goes back to the runner user so actions/cache can save it.
done_ok() {
  [ -n "$APT_CACHE" ] && sudo chown -R "$(id -u):$(id -g)" "$APT_CACHE"
  exit 0
}
if [ -n "$APT_CACHE" ] && ls "$APT_CACHE"/lists/*_Packages* > /dev/null 2>&1; then
  sudo DEBIAN_FRONTEND=noninteractive timeout 120 apt-get $apt_opts install -y --no-install-recommends "$@" &&
    done_ok
  echo "::warning::install from the apt cache failed, updating"
fi
# Three attempts of at most 2 + 2 min fit the steps' 12 min limit.
for attempt in 1 2 3; do
  sudo timeout 120 apt-get $apt_opts update &&
    sudo DEBIAN_FRONTEND=noninteractive timeout 120 apt-get $apt_opts install -y --no-install-recommends "$@" &&
    done_ok
  echo "::warning::apt attempt $attempt failed, retrying"
done
exit 1
