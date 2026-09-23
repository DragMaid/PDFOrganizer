#!/usr/bin/env bash
# Dumps the pdforganizer database to /opt/pdforganizer/backups. Run nightly by cron (installed
# by the Ansible playbook) and by deploy.sh before every rollout.
#
#   Usage: backup.sh [label]    e.g. backup.sh pre-deploy
#
# Only Postgres is dumped. The PDFs themselves live in Backblaze B2; use B2's own lifecycle
# rules / versioning for those.

set -euo pipefail

cd "$(dirname "$0")"

label="${1:-nightly}"
keep_days="${BACKUP_KEEP_DAYS:-14}"
target="backups/pdforg-$(date -u +%Y%m%dT%H%M%SZ)-${label}.dump"

mkdir -p backups

# Custom format: compressed, and pg_restore can pick single tables back out of it.
docker compose -f docker-compose.prod.yml exec -T db \
    pg_dump -U pdforg -d pdforg --format=custom > "${target}.partial"
mv "${target}.partial" "$target"

find backups -name 'pdforg-*.dump' -mtime +"$keep_days" -delete

echo "==> Backup written: $target"
