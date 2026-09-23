#!/usr/bin/env bash
# Rolls the stack at /opt/pdforganizer forward to the IMAGE_TAG in .env. Run by the deploy
# workflow over SSH, after it has uploaded docker-compose.prod.yml and a fresh .env.
#
#   Usage: deploy.sh <ghcr-user>     (a registry token is read from stdin; empty = no login)
#
# On a failed health check it puts the previous tag back and exits non-zero, so the
# workflow run goes red while the site keeps serving the last good release. The schema step
# is not reversed by that, which is why a database dump is taken before every rollout.

set -euo pipefail

cd "$(dirname "$0")"

compose=(docker compose -f docker-compose.prod.yml)
ghcr_user="${1:-}"

env_value() {
    grep -E "^$1=" .env | tail -n1 | cut -d= -f2-
}

new_tag="$(env_value IMAGE_TAG)"
previous_tag="$(cat .current-tag 2>/dev/null || true)"
site_domain="$(env_value SITE_DOMAIN)"

echo "==> Deploying ${new_tag} (previous: ${previous_tag:-none})"

# The token arrives on stdin so it never shows in `ps` or the workflow log.
token="$(cat)"
if [[ -n "$token" ]]; then
    echo "$token" | docker login ghcr.io -u "$ghcr_user" --password-stdin >/dev/null
fi

pull_status=0
"${compose[@]}" pull --quiet || pull_status=$?
[[ -n "$token" ]] && docker logout ghcr.io >/dev/null
(( pull_status == 0 )) || { echo "!! pull failed"; exit "$pull_status"; }

# Dump before anything touches the schema. Skipped on the very first deploy, when there is
# no database yet.
if [[ -n "$("${compose[@]}" ps --status running --quiet db)" ]]; then
    ./backup.sh pre-deploy
fi

# Goes through Traefik on this host, so the routing labels are checked too; -k because a
# certificate may not be issued yet (or is self-signed), and it is our own loopback anyway.
probe() {
    curl -fsSk --max-time 5 --resolve "$1:443:127.0.0.1" "https://$1$2"
}

healthy() {
    for _ in $(seq 1 30); do
        # /health answers 200 even when the database is down ("degraded"), so the body
        # is what says whether this release actually works.
        if probe "api.${site_domain}" /health | grep -q '"status":"ok"' \
            && probe "$site_domain" / >/dev/null; then
            return 0
        fi
        sleep 2
    done
    return 1
}

rollback() {
    echo "!! Release ${new_tag} failed"
    "${compose[@]}" logs --tail 80 migrate api site || true

    if [[ -z "$previous_tag" || "$previous_tag" == "$new_tag" ]]; then
        echo "!! No previous release to roll back to"
        exit 1
    fi

    echo "==> Rolling back to ${previous_tag}"
    sed -i "s/^IMAGE_TAG=.*/IMAGE_TAG=${previous_tag}/" .env
    "${compose[@]}" up -d --remove-orphans
    exit 1
}

"${compose[@]}" up -d --remove-orphans || rollback
healthy || rollback

echo "$new_tag" > .current-tag

# Only this stack's images (labelled at build time) and only ones no container uses, so
# the other sites' images are never touched.
docker image prune --all --force \
    --filter "label=org.opencontainers.image.vendor=pdforganizer" \
    --filter "until=240h" >/dev/null

echo "==> ${new_tag} is live"
