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

database_url="$(sed -n 's/^PDFORG_DATABASE_URL=//p' .env | tail -n1)"

# Custom format: compressed, and pg_restore can pick single tables back out of it.
if [[ -n "$database_url" ]]; then
    # External database (e.g. Supabase): a throwaway client container, since there is no
    # `db` one. libpq does not know SQLAlchemy's `+psycopg` driver suffix. Only `public` is
    # dumped; Supabase's own schemas (auth, storage, ...) are not ours to restore. The URL
    # goes in through the environment so it never shows in `ps`. pg_dump refuses servers
    # newer than itself, so this image must be at least the server's major version.
    export DUMP_URL="${database_url/postgresql+psycopg:/postgresql:}"
    docker run --rm -e DUMP_URL postgres:17 \
        sh -c 'exec pg_dump -d "$DUMP_URL" --schema=public --no-owner --no-privileges --format=custom' \
        > "${target}.partial"
else
    docker compose -f docker-compose.prod.yml exec -T db \
        pg_dump -U pdforg -d pdforg --format=custom > "${target}.partial"
fi
mv "${target}.partial" "$target"

find backups -name 'pdforg-*.dump' -mtime +"$keep_days" -delete

echo "==> Backup written: $target"
