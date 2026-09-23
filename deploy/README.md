# Deploying to the VPS

```
push to main ─► CI suite ─► build api + site images ─► GHCR ─► ssh deploy@vps deploy.sh
                                                                │
                      pull ─► pg_dump ─► init_db.py ─► up -d ─► /health ok? ─┬─ yes: done
                                                                             └─ no: previous tag back up, run fails
```

It runs on the same VPS as the Portfolio stack and shares its Traefik:

| Host | Container | What |
|---|---|---|
| `SITE_DOMAIN` | `site` | the showcase site (`site/`, static files behind nginx) |
| `api.SITE_DOMAIN` | `api` | the FastAPI backend; the desktop client's server address |

- **`docker-compose.prod.yml`** (repo root): the whole stack. Postgres, a one-shot `migrate`
  (`python init_db.py` with the release's own image), the API and the site. It publishes no
  ports; `api` and `site` join the external `traefik` network and route themselves with
  labels. Postgres sits on a private network only this stack can reach.
- **`ansible/`**: one-time (re-runnable) setup on top of the Portfolio playbook, which must
  have run first because it owns Docker and Traefik. This adds the `deploy` user's key for this
  project, `/opt/pdforganizer`, and a nightly dump at 03:47.
- **`deploy.sh` / `backup.sh`**: copied to `/opt/pdforganizer` on every deploy and run there.
- **`.github/workflows/ci.yml`**: backend tests on Postgres, a client build plus the ApiClient
  smoke test against a live backend, and site validation. Runs on PRs, and before every
  deploy and release.
- **`.github/workflows/deploy.yml`**: the pipeline.

## First-time setup

1. **DNS**: point `pdforganizer.<your-domain>` and `api.pdforganizer.<your-domain>` at the VPS.
   Traefik issues their certificates on first request (HTTP challenge), provided the
   Portfolio inventory has `tls_mode=letsencrypt`.
2. **Deploy key** (its own, so it can be revoked without touching the portfolio's):
   `ssh-keygen -t ed25519 -f ~/.ssh/pdforganizer-deploy -C pdforganizer-deploy`
3. **Provision**:
   ```sh
   cd deploy/ansible
   cp inventory.example.ini inventory.ini   # fill in host and key path
   ansible-galaxy collection install -r requirements.yml
   ansible-playbook -i inventory.ini playbook.yml
   ```
4. **GitHub** → Settings → Environments → `production`:

   | kind   | name              | value                                                     |
   |--------|-------------------|-----------------------------------------------------------|
   | secret | `VPS_SSH_KEY`     | contents of `~/.ssh/pdforganizer-deploy`                  |
   | secret | `VPS_KNOWN_HOSTS` | `ssh-keyscan -p 22 <host>`; compare to the server's own fingerprint |
   | secret | `PROD_ENV_FILE`   | `deploy/.env.example`, filled in                          |
   | var    | `VPS_HOST`        | host or IP                                                |
   | var    | `VPS_PORT`        | optional, default 22                                      |

5. Push to `main` (or run **Deploy** by hand). The first run creates the database from scratch.
6. Point the desktop client at `https://api.<SITE_DOMAIN>` under **Settings ▸ Account**.

### Moving off Render

The client currently talks to the Render deployment. This stack starts with an empty
database; to carry accounts and groups over, dump the old database and restore it before the
client switches:

```sh
pg_dump --format=custom "$OLD_DATABASE_URL" > render.dump
scp render.dump deploy@vps:/opt/pdforganizer/backups/
ssh deploy@vps 'cd /opt/pdforganizer && docker compose -f docker-compose.prod.yml exec -T db \
  pg_restore -U pdforg -d pdforg --clean --if-exists --no-owner < backups/render.dump'
```

The PDFs themselves are in B2, and stay there if `PDFORG_B2_*` point at the same bucket.

## Day to day

- **Release the server and site**: merge to `main`. Nothing else.
- **Release the desktop app**: push a `v*` tag; `release.yml` runs CI, then builds and
  publishes the installers. The site's download buttons pick up the newest release on their
  own (via the GitHub API, falling back to the Releases page).
- **Roll back**: Actions → Deploy → Run workflow → `image_tag` = an earlier commit SHA. No
  rebuild; it redeploys what is already in GHCR. The schema is *not* reversed: `init_db.py`
  only adds tables and columns, but when it rebuilds tables for a new shape, restore the
  pre-deploy dump.
- **Change a secret/setting**: edit `PROD_ENV_FILE`, re-run the latest Deploy.
- **Logs**: `ssh deploy@vps 'cd /opt/pdforganizer && docker compose -f docker-compose.prod.yml logs -f api'`
- **Restore a dump**:
  ```sh
  cd /opt/pdforganizer
  docker compose -f docker-compose.prod.yml exec -T db \
    pg_restore -U pdforg -d pdforg --clean --if-exists < backups/<file>.dump
  ```

## Not automatic, on purpose

- **Postgres major versions** (`postgres:17`): a major bump needs a dump and restore.
- **Backups are local to the VPS.** Copy `/opt/pdforganizer/backups` off the box. A dead disk
  takes the dumps with it.
- **`PDFORG_JWT_SECRET`**: changing it signs every user out; that is the only effect.
- **Upload size**: uploads up to `PDFORG_MAX_UPLOAD_BYTES` pass through Traefik, whose
  timeouts (300s) are set in the Portfolio playbook's `traefik.yml`.
