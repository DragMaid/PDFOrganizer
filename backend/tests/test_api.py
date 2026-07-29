"""End-to-end coverage of the authorization and concurrency rules.

The rules under test, in the words they were specified in:

* only actions performed by authorized users within the group
* allow only the creator of a note to edit or delete it, not others
* do not do this for tags
* handle the race conditions for tags gracefully — if a person wants to add a
  tag that has been already added by another person then just leave them be
"""

from __future__ import annotations

import hashlib

HASH_A = hashlib.sha256(b"pdf-a").hexdigest()
HASH_B = hashlib.sha256(b"pdf-b").hexdigest()


def shared_group(owner, *members):
    """Create a group owned by ``owner`` with ``members`` invited."""
    response = owner.post("/groups", json={"name": "Research"})
    assert response.status_code == 201, response.text
    group_id = response.json()["id"]
    for member in members:
        invited = owner.post(f"/groups/{group_id}/members", json={"email": member.email})
        assert invited.status_code == 200, invited.text
    return group_id


def register_file(account, group_id, content_hash=HASH_A, name="paper.pdf"):
    response = account.post(
        f"/groups/{group_id}/files",
        json={
            "content_hash": content_hash,
            "file_name": name,
            "file_size_bytes": 1234,
            "page_count": 10,
        },
    )
    assert response.status_code == 200, response.text
    return response.json()


# ── Accounts and personal groups ──────────────────────────────────────────────


def test_signup_creates_a_personal_group(alice):
    groups = alice.get("/groups").json()
    assert len(groups) == 1
    assert groups[0]["is_personal"] is True
    assert groups[0]["my_role"] == "owner"


def test_duplicate_email_is_rejected(client, alice):
    response = client.post(
        "/auth/register",
        json={
            "email": alice.email,
            "password": "hunter2hunter2",
            "display_name": "Impostor",
        },
    )
    assert response.status_code == 409
    assert response.json()["code"] == "email_taken"


def test_login_and_refresh(client, alice):
    login = client.post(
        "/auth/login", json={"email": alice.email, "password": "hunter2hunter2"}
    )
    assert login.status_code == 200

    bad = client.post(
        "/auth/login", json={"email": alice.email, "password": "wrong-password"}
    )
    assert bad.status_code == 401

    refreshed = client.post(
        "/auth/refresh", json={"refresh_token": alice.refresh_token}
    )
    assert refreshed.status_code == 200
    assert refreshed.json()["user"]["id"] == alice.id


def test_requests_without_a_token_are_rejected(client):
    assert client.get("/groups").status_code == 401


# ── Group authorization ───────────────────────────────────────────────────────


def test_non_members_cannot_see_or_touch_a_group(alice, bob):
    group_id = shared_group(alice)

    assert bob.get(f"/groups/{group_id}").status_code == 404
    assert bob.get(f"/groups/{group_id}/files").status_code == 404
    assert bob.get(f"/groups/{group_id}/tags").status_code == 404
    assert (
        bob.post(f"/groups/{group_id}/tags", json={"name": "sneaky"}).status_code == 404
    )


def test_members_can_write_content_but_not_administer(alice, bob):
    group_id = shared_group(alice, bob)

    # Bob is a member: content operations work.
    file = register_file(bob, group_id)
    assert bob.post(
        f"/groups/{group_id}/files/{file['id']}/tags", json={"name": "physics"}
    ).status_code == 200

    # ...but administration is the owner's alone.
    assert bob.patch(f"/groups/{group_id}", json={"name": "Bob's"}).status_code == 403
    assert bob.delete(f"/groups/{group_id}").status_code == 403
    assert (
        bob.post(
            f"/groups/{group_id}/members", json={"email": "carol@example.com"}
        ).status_code
        == 403
    )


def test_owner_manages_membership(alice, bob, carol):
    group_id = shared_group(alice, bob)

    members = alice.get(f"/groups/{group_id}/members").json()
    assert {m["email"] for m in members} == {alice.email, bob.email}

    # Inviting the same person twice is a no-op, not an error.
    again = alice.post(f"/groups/{group_id}/members", json={"email": bob.email})
    assert again.status_code == 200
    assert len(alice.get(f"/groups/{group_id}/members").json()) == 2

    # Unknown addresses are reported clearly.
    missing = alice.post(f"/groups/{group_id}/members", json={"email": "nope@example.com"})
    assert missing.status_code == 404
    assert missing.json()["code"] == "user_not_found"

    # The owner cannot be removed, even by themselves.
    locked = alice.delete(f"/groups/{group_id}/members/{alice.id}")
    assert locked.status_code == 409
    assert locked.json()["code"] == "owner_locked"

    # Removing a member revokes their access immediately.
    assert alice.delete(f"/groups/{group_id}/members/{bob.id}").status_code == 204
    assert bob.get(f"/groups/{group_id}/files").status_code == 404

    # A member may remove themselves.
    alice.post(f"/groups/{group_id}/members", json={"email": carol.email})
    assert carol.delete(f"/groups/{group_id}/members/{carol.id}").status_code == 204


def test_personal_group_is_protected(alice):
    personal = alice.get("/groups").json()[0]
    assert alice.patch(f"/groups/{personal['id']}", json={"name": "x"}).status_code == 400
    assert alice.delete(f"/groups/{personal['id']}").status_code == 400
    assert (
        alice.post(
            f"/groups/{personal['id']}/members", json={"email": "bob@example.com"}
        ).status_code
        == 400
    )


# ── Files ─────────────────────────────────────────────────────────────────────


def test_same_pdf_registered_twice_resolves_to_one_record(alice, bob):
    group_id = shared_group(alice, bob)

    mine = register_file(alice, group_id, name="paper.pdf")
    theirs = register_file(bob, group_id, name="paper-copy.pdf")

    # Same content hash, so the same file, and the group lists it once.
    assert mine["id"] == theirs["id"]
    assert len(alice.get(f"/groups/{group_id}/files").json()) == 1


def test_removing_a_file_from_a_group_leaves_other_groups_alone(alice):
    first = shared_group(alice)
    second = alice.post("/groups", json={"name": "Second"}).json()["id"]

    file = register_file(alice, first)
    register_file(alice, second)

    alice.post(f"/groups/{first}/files/{file['id']}/tags", json={"name": "keep"})
    alice.post(f"/groups/{second}/files/{file['id']}/tags", json={"name": "keep"})

    assert alice.delete(f"/groups/{first}/files/{file['id']}").status_code == 204
    assert alice.get(f"/groups/{first}/files").json() == []
    assert len(alice.get(f"/groups/{second}/files").json()) == 1
    assert len(alice.get(f"/groups/{second}/files/{file['id']}/tags").json()) == 1


# ── Tags: races are resolved by leaving things be ─────────────────────────────


def test_two_people_adding_the_same_tag_both_succeed(alice, bob):
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/tags"

    first = alice.post(url, json={"name": "urgent"})
    second = bob.post(url, json={"name": "urgent"})

    assert first.status_code == 200
    assert second.status_code == 200
    # One tag, not two, and nobody saw an error.
    assert [t["name"] for t in second.json()] == ["urgent"]


def test_tag_names_collide_case_insensitively(alice, bob):
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/tags"

    alice.post(url, json={"name": "Taxes"})
    bob.post(url, json={"name": "taxes"})

    tags = alice.get(f"/groups/{group_id}/tags").json()
    assert len(tags) == 1


def test_creating_an_existing_tag_returns_it_instead_of_conflicting(alice, bob):
    group_id = shared_group(alice, bob)

    mine = alice.post(f"/groups/{group_id}/tags", json={"name": "archive"})
    theirs = bob.post(f"/groups/{group_id}/tags", json={"name": "archive"})

    assert mine.status_code == theirs.status_code == 200
    assert mine.json()["id"] == theirs.json()["id"]


def test_removing_an_already_removed_tag_still_succeeds(alice, bob):
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/tags"

    tag_id = alice.post(url, json={"name": "draft"}).json()[0]["id"]
    assert alice.delete(f"{url}/{tag_id}").status_code == 204
    # Bob races Alice to the same removal and is not punished for losing.
    assert bob.delete(f"{url}/{tag_id}").status_code == 204


def test_set_file_tags_replaces_only_this_groups_tags(alice):
    group_id = shared_group(alice)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/tags"

    alice.put(url, json={"names": ["alpha", "beta", "gamma"]})
    assert {t["name"] for t in alice.get(url).json()} == {"alpha", "beta", "gamma"}

    alice.put(url, json={"names": ["beta", "delta"]})
    assert {t["name"] for t in alice.get(url).json()} == {"beta", "delta"}

    # Duplicates and blanks in the payload are ignored rather than rejected.
    alice.put(url, json={"names": ["beta", "BETA", "  ", "delta"]})
    assert {t["name"] for t in alice.get(url).json()} == {"beta", "delta"}


def test_renaming_onto_an_existing_tag_is_the_one_tag_conflict(alice):
    group_id = shared_group(alice)
    first = alice.post(f"/groups/{group_id}/tags", json={"name": "one"}).json()
    alice.post(f"/groups/{group_id}/tags", json={"name": "two"})

    clash = alice.patch(
        f"/groups/{group_id}/tags/{first['id']}", json={"name": "two"}
    )
    assert clash.status_code == 409
    assert clash.json()["code"] == "tag_exists"


def test_tag_vocabulary_does_not_leak_across_groups(alice, bob):
    group_id = shared_group(alice, bob)
    alice.post(f"/groups/{group_id}/tags", json={"name": "shared-tag"})

    private = alice.get("/groups").json()
    personal_id = next(g["id"] for g in private if g["is_personal"])
    alice.post(f"/groups/{personal_id}/tags", json={"name": "private-tag"})

    assert {t["name"] for t in alice.get("/tags").json()} == {
        "shared-tag",
        "private-tag",
    }
    # Bob shares only the Research group, so he never sees Alice's private tag.
    assert {t["name"] for t in bob.get("/tags").json()} == {"shared-tag"}


# ── Notes: only the author may edit or delete ─────────────────────────────────


def test_group_members_all_see_notes(alice, bob):
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/notes"

    alice.post(url, json={"body": "Check section 3."})
    bob.post(url, json={"body": "Agreed."})

    seen = bob.get(url).json()
    assert [n["body"] for n in seen] == ["Check section 3.", "Agreed."]
    assert [n["author_name"] for n in seen] == ["Alice", "Bob"]
    # editable marks which ones the client may offer edit/delete on.
    assert [n["editable"] for n in seen] == [False, True]


def test_only_the_author_may_edit_or_delete_a_note(alice, bob):
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/notes"

    note = alice.post(url, json={"body": "Alice's note."}).json()

    blocked_edit = bob.patch(f"/notes/{note['id']}", json={"body": "Bob was here"})
    assert blocked_edit.status_code == 403
    assert "Alice" in blocked_edit.json()["message"]

    blocked_delete = bob.delete(f"/notes/{note['id']}")
    assert blocked_delete.status_code == 403

    # The note is untouched.
    assert alice.get(url).json()[0]["body"] == "Alice's note."

    # The author can do both.
    assert alice.patch(f"/notes/{note['id']}", json={"body": "Edited."}).status_code == 200
    assert alice.delete(f"/notes/{note['id']}").status_code == 204
    assert alice.get(url).json() == []


def test_group_owner_is_not_exempt_from_the_author_rule(alice, bob):
    """Owning the group does not grant authority over other people's words."""
    group_id = shared_group(alice, bob)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/notes"

    bobs_note = bob.post(url, json={"body": "Bob's opinion."}).json()

    assert alice.patch(f"/notes/{bobs_note['id']}", json={"body": "no"}).status_code == 403
    assert alice.delete(f"/notes/{bobs_note['id']}").status_code == 403


def test_a_stale_edit_is_rejected_with_the_current_text(alice):
    group_id = shared_group(alice)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/notes"

    note = alice.post(url, json={"body": "v1"}).json()
    assert note["version"] == 1

    # Alice edits from her laptop.
    updated = alice.patch(f"/notes/{note['id']}", json={"body": "v2", "version": 1})
    assert updated.status_code == 200
    assert updated.json()["version"] == 2

    # Her desktop still thinks the note is at v1.
    stale = alice.patch(f"/notes/{note['id']}", json={"body": "v2-other", "version": 1})
    assert stale.status_code == 409
    assert stale.json()["code"] == "stale_note"
    assert stale.json()["detail"]["current_body"] == "v2"

    # Nothing was lost.
    assert alice.get(url).json()[0]["body"] == "v2"


def test_non_members_cannot_read_or_write_notes(alice, bob):
    group_id = shared_group(alice)
    file = register_file(alice, group_id)
    url = f"/groups/{group_id}/files/{file['id']}/notes"

    note = alice.post(url, json={"body": "private"}).json()

    assert bob.get(url).status_code == 404
    assert bob.post(url, json={"body": "intruding"}).status_code == 404
    assert bob.patch(f"/notes/{note['id']}", json={"body": "x"}).status_code == 404
    assert bob.delete(f"/notes/{note['id']}").status_code == 404


def test_notes_do_not_leak_between_groups_sharing_a_file(alice, bob):
    """The same PDF in two groups keeps two separate conversations."""
    shared = shared_group(alice, bob)
    personal_id = next(g["id"] for g in alice.get("/groups").json() if g["is_personal"])

    register_file(alice, shared)
    file = register_file(alice, personal_id)

    alice.post(
        f"/groups/{personal_id}/files/{file['id']}/notes", json={"body": "just for me"}
    )
    alice.post(
        f"/groups/{shared}/files/{file['id']}/notes", json={"body": "for the team"}
    )

    team_view = bob.get(f"/groups/{shared}/files/{file['id']}/notes").json()
    assert [n["body"] for n in team_view] == ["for the team"]


# ── Sync ──────────────────────────────────────────────────────────────────────


def test_sync_status_lists_files_awaiting_upload(alice):
    group_id = shared_group(alice)
    register_file(alice, group_id, HASH_A, "a.pdf")
    register_file(alice, group_id, HASH_B, "b.pdf")

    status = alice.get(f"/groups/{group_id}/sync-status").json()
    assert status["total_files"] == 2
    assert status["uploaded_files"] == 0
    assert len(status["pending"]) == 2


def test_upload_rejects_bytes_that_do_not_match_the_registration(alice):
    group_id = shared_group(alice)
    file = register_file(alice, group_id, HASH_A)

    response = alice.post(
        f"/groups/{group_id}/files/{file['id']}/upload",
        files={"content": ("a.pdf", b"different bytes", "application/pdf")},
    )
    assert response.status_code == 400
    assert "changed on disk" in response.json()["message"]


def test_upload_reports_missing_storage_configuration_clearly(alice):
    group_id = shared_group(alice)
    file = register_file(alice, group_id, HASH_A)

    response = alice.post(
        f"/groups/{group_id}/files/{file['id']}/upload",
        files={"content": ("a.pdf", b"pdf-a", "application/pdf")},
    )
    # No PDFORG_B2_* set in the test environment, so this is the honest answer.
    assert response.status_code == 503
    assert response.json()["code"] == "storage_unconfigured"


# ── Error shape ───────────────────────────────────────────────────────────────


def test_every_error_carries_code_and_message(client, alice):
    for response in (
        client.get("/groups"),
        alice.get("/groups/99999"),
        client.post("/auth/register", json={"email": "bad", "password": "x"}),
    ):
        body = response.json()
        assert set(body) >= {"code", "message"}
        assert isinstance(body["message"], str) and body["message"]
