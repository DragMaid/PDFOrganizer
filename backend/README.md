# PDF Organizer — Backend

FastAPI service that owns everything shared: Postgres, Backblaze B2, and the
rules about who may do what. The Qt client talks only to this API; it never
holds a database connection or a storage credential.

## Running it

```bash
cd backend
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt

cp .env.example .env       # then fill in JWT secret + B2 keys
python init_db.py          # create the schema (idempotent)

uvicorn app.main:app --reload --port 8000
```

Interactive docs at <http://localhost:8000/docs>.

A Postgres to point at:

```bash
docker run -d --name pdforg-pg \
  -e POSTGRES_USER=pdforg -e POSTGRES_PASSWORD=pdforg -e POSTGRES_DB=pdforg \
  -p 5432:5432 postgres:16-alpine
```

## Tests

```bash
pip install -r requirements-dev.txt
docker run -d --name pdforg-test-pg \
  -e POSTGRES_USER=pdforg -e POSTGRES_PASSWORD=pdforg -e POSTGRES_DB=pdforg \
  -p 55432:5432 postgres:16-alpine
pytest
```

The suite drops and recreates the schema per test, so give it a database you
don't care about. It runs against real Postgres on purpose — the concurrency
guarantees below are implemented with `ON CONFLICT DO NOTHING` and would not be
exercised by SQLite.

## Data model

| Table | Purpose |
|---|---|
| `users` | Accounts. Passwords are SHA-256-prehashed then bcrypt-hashed. |
| `groups` | Sharing unit. Every user gets a private `is_personal` group at signup. |
| `group_members` | `owner` or `member`. |
| `files` | One row per **content hash**, not per path. |
| `group_files` | Which groups hold a file, plus that group's display name for it. |
| `tags` / `file_tags` | Tag vocabulary and assignments, scoped per group. |
| `notes` | Comments, scoped per group, soft-deleted. |

Two things follow from keying files by SHA-256 rather than by path:

- Two members holding the same PDF at different paths on different machines
  land on the same record, so they share its tags and notes automatically.
- A blob is uploaded to B2 exactly once no matter how many people or groups
  hold it.

Tags and notes carry an explicit `group_id` because one file may live in
several groups, and a note written for one team must not surface in another.

## Authorization

Every content endpoint reduces to *"is the caller a member of this group"*.

| Action | Who |
|---|---|
| Read files, tags, notes | Any member |
| Add files, add/remove tags, write notes | Any member |
| Edit or delete a note | **Its author only** — the group owner is not exempt |
| Rename/delete group, add/remove members | Owner only |
| Leave a group | Yourself |

A non-member gets `404`, not `403`, so group ids cannot be probed.

## Concurrency

The two policies are deliberately opposite, matching how the objects are used.

**Tags — never fight.** Two people tagging the same file at the same moment
both get `200` and the same result:

- creating a tag that exists returns the existing tag,
- assigning a tag already on the file leaves it be,
- removing a tag someone else already removed still succeeds.

Names collide case-insensitively (`Taxes` == `taxes`) via a unique index on a
lower-cased column, so the dedup happens in the database rather than in a
read-then-write window. The single exception is *renaming* a tag onto a name
that already exists: silently merging would lose assignments, so that returns
`409 tag_exists`.

**Notes — protect authorship.** Only the author may edit or delete. Edits carry
the `version` the client last read; if the note moved on in between the write
is rejected with `409 stale_note` and the response includes `current_body`, so
the client can show what it would have overwritten instead of losing it.

## Errors

Every non-2xx body has the same shape, and `message` is written to be shown to
a person:

```json
{ "code": "stale_note", "message": "This note was changed somewhere else…", "detail": {…} }
```

The Qt client puts `message` straight into a modal.

## Storage

B2 credentials live only in `PDFORG_B2_*` environment variables. Clients POST
bytes to `/groups/{id}/files/{id}/upload` and the server does the B2
conversation. The server verifies the uploaded bytes hash to the registered
`content_hash` before storing them, and refuses uploads above
`PDFORG_MAX_UPLOAD_BYTES` (default 200 MB) rather than buffering unbounded
request bodies. Only B2's single-shot upload path is implemented; large-file
multipart would be the next step if you start storing bigger PDFs.

If the B2 variables are unset, uploads fail with `503 storage_unconfigured`
and a message saying so — everything else keeps working.
