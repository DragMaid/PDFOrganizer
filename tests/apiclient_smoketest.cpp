/**
 * Integration check for ApiClient against a live backend.
 *
 * The Python suite proves the backend's rules; this proves the C++ client
 * speaks the same wire format — that request bodies land where the server
 * expects and that replies parse back into the DTOs correctly.
 *
 *   cmake -S . -B build -DBUILD_API_SMOKETEST=ON
 *   cmake --build build --target apiclient_smoketest
 *   ./build/apiclient_smoketest http://localhost:8000
 */

#include "api/apiclient.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUuid>

static int g_failures = 0;
static QTextStream out(stdout);

static void check(bool condition, const QString& what)
{
    out << (condition ? "  ok    " : "  FAIL  ") << what << Qt::endl;
    if (!condition)
        ++g_failures;
}

/// Run @p work and block until it calls done(). Fails the test on timeout.
template <typename Work>
static void await(const QString& label, Work work)
{
    QEventLoop loop;
    bool finished = false;

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(20000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        out << "  FAIL  " << label << " (timed out)" << Qt::endl;
        ++g_failures;
        loop.quit();
    });
    timeout.start();

    work([&]() {
        finished = true;
        loop.quit();
    });

    if (!finished)
        loop.exec();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString baseUrl = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("http://localhost:8000");
    const QString stamp = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    ApiClient alice;
    ApiClient bob;
    alice.setBaseUrl(QUrl(baseUrl));
    bob.setBaseUrl(QUrl(baseUrl));

    // Unhandled failures would otherwise be silent in a console program.
    QObject::connect(&alice, &ApiClient::errorOccurred,
                     [](const ApiError& e) {
                         out << "  !! alice unhandled: " << e.code << " — "
                             << e.message << Qt::endl;
                         ++g_failures;
                     });
    QObject::connect(&bob, &ApiClient::errorOccurred, [](const ApiError& e) {
        out << "  !! bob unhandled: " << e.code << " — " << e.message << Qt::endl;
        ++g_failures;
    });

    out << "Backend: " << baseUrl << Qt::endl << Qt::endl;

    // ── Health ────────────────────────────────────────────────────────────────
    out << "health" << Qt::endl;
    await("server reachable", [&](auto done) {
        alice.health(
            [&, done](const ApiServerInfo& info) {
                check(info.databaseOk, "database reports healthy");
                check(!info.version.isEmpty(), "version parsed");
                done();
            },
            [&, done](const ApiError& e) {
                check(false, "health: " + e.message);
                done();
            });
    });
    if (g_failures) {
        out << Qt::endl << "Backend unreachable — is it running?" << Qt::endl;
        return 1;
    }

    // ── Accounts ──────────────────────────────────────────────────────────────
    out << Qt::endl << "accounts" << Qt::endl;
    const QString aliceEmail = QStringLiteral("alice-%1@example.com").arg(stamp);
    const QString bobEmail   = QStringLiteral("bob-%1@example.com").arg(stamp);

    await("alice registers", [&](auto done) {
        alice.registerAccount(aliceEmail, QStringLiteral("hunter2hunter2"),
                              QStringLiteral("Alice"),
                              [&, done](const ApiUser& user) {
                                  check(user.isValid(), "alice has a user id");
                                  check(user.displayName == QLatin1String("Alice"),
                                        "display name round-trips");
                                  done();
                              });
    });
    await("bob registers", [&](auto done) {
        bob.registerAccount(bobEmail, QStringLiteral("hunter2hunter2"),
                            QStringLiteral("Bob"),
                            [&, done](const ApiUser&) { done(); });
    });

    check(alice.isAuthenticated(), "alice is authenticated");
    check(!alice.refreshToken().isEmpty(), "refresh token stored");

    // Long-lived: destroying an ApiClient cancels its in-flight replies, so a
    // stack local inside the lambda below would never see its callback.
    ApiClient dupe;
    dupe.setBaseUrl(QUrl(baseUrl));

    await("duplicate email is refused", [&](auto done) {
        dupe.registerAccount(
            aliceEmail, QStringLiteral("hunter2hunter2"),
            QStringLiteral("Impostor"),
            [&, done](const ApiUser&) {
                check(false, "duplicate email should not succeed");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.code == QLatin1String(ApiError::EmailTaken),
                      "error code is email_taken");
                check(!e.message.isEmpty(), "error carries a readable message");
                done();
            });
    });

    // ── Groups ────────────────────────────────────────────────────────────────
    out << Qt::endl << "groups" << Qt::endl;
    int personalId = -1;
    await("signup created a personal group", [&](auto done) {
        alice.listGroups([&, done](const QList<ApiGroup>& groups) {
            check(groups.size() == 1, "exactly one group after signup");
            if (!groups.isEmpty()) {
                check(groups.first().isPersonal, "it is the personal group");
                check(groups.first().isOwner(), "alice owns it");
                personalId = groups.first().id;
            }
            done();
        });
    });

    int groupId = -1;
    await("alice creates a shared group", [&](auto done) {
        alice.createGroup(QStringLiteral("Research"),
                          [&, done](const ApiGroup& group) {
                              check(group.isValid(), "group has an id");
                              check(group.isOwner(), "creator is the owner");
                              groupId = group.id;
                              done();
                          });
    });

    await("alice invites bob", [&](auto done) {
        alice.addMember(groupId, bobEmail, [&, done](const ApiMember& member) {
            check(member.role == QLatin1String("member"), "bob joins as member");
            done();
        });
    });

    await("bob cannot rename the group", [&](auto done) {
        bob.renameGroup(
            groupId, QStringLiteral("Bob's"),
            [&, done](const ApiGroup&) {
                check(false, "a member should not be able to rename");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 403, "member rename is 403");
                done();
            });
    });

    // ── Files ─────────────────────────────────────────────────────────────────
    out << Qt::endl << "files" << Qt::endl;
    QTemporaryDir tmp;
    const QString pdfPath = tmp.filePath(QStringLiteral("paper.pdf"));
    {
        QFile file(pdfPath);
        check(file.open(QIODevice::WriteOnly), "temp pdf created");
        file.write("%PDF-1.4\nnot really a pdf, but it hashes fine\n");
    }
    const QString hash = ApiClient::hashFile(pdfPath);
    check(hash.size() == 64, "hashFile returns a 64-char digest");

    int fileId = -1;
    await("alice registers the file", [&](auto done) {
        alice.registerFile(groupId, hash, QStringLiteral("paper.pdf"), 42, 3,
                           [&, done](const ApiFile& file) {
                               check(file.isValid(), "file has an id");
                               check(file.contentHash == hash, "hash round-trips");
                               check(!file.uploaded, "not uploaded yet");
                               fileId = file.id;
                               done();
                           });
    });

    await("bob registering the same content is a no-op", [&](auto done) {
        bob.registerFile(groupId, hash, QStringLiteral("copy.pdf"), 42, 3,
                         [&, done](const ApiFile& file) {
                             check(file.id == fileId,
                                   "same content resolves to the same record");
                             done();
                         });
    });

    // ── Tags: concurrent adds must not fight ──────────────────────────────────
    out << Qt::endl << "tags" << Qt::endl;
    await("alice tags the file", [&](auto done) {
        alice.addFileTag(groupId, fileId, QStringLiteral("urgent"),
                         [&, done](const QList<ApiTag>& tags) {
                             check(tags.size() == 1, "one tag on the file");
                             done();
                         });
    });

    await("bob adding the same tag succeeds", [&](auto done) {
        bob.addFileTag(groupId, fileId, QStringLiteral("urgent"),
                       [&, done](const QList<ApiTag>& tags) {
                           check(tags.size() == 1,
                                 "still one tag — the duplicate was left be");
                           done();
                       });
    });

    await("case-insensitive duplicates collapse", [&](auto done) {
        bob.addFileTag(groupId, fileId, QStringLiteral("URGENT"),
                       [&, done](const QList<ApiTag>& tags) {
                           check(tags.size() == 1, "'URGENT' matched 'urgent'");
                           done();
                       });
    });

    await("setFileTags replaces the set", [&](auto done) {
        alice.setFileTags(groupId, fileId,
                          {QStringLiteral("alpha"), QStringLiteral("beta")},
                          [&, done](const QList<ApiTag>& tags) {
                              check(tags.size() == 2, "two tags after replace");
                              done();
                          });
    });

    await("vocabulary is visible to both members", [&](auto done) {
        bob.listAllTags([&, done](const QList<ApiTag>& tags) {
            check(tags.size() >= 2, "bob sees the shared vocabulary");
            done();
        });
    });

    // ── Notes: only the author may edit or delete ─────────────────────────────
    out << Qt::endl << "notes" << Qt::endl;
    ApiNote alicesNote;
    await("alice writes a note", [&](auto done) {
        alice.createNote(groupId, fileId, QStringLiteral("Check section 3."),
                         [&, done](const ApiNote& note) {
                             check(note.isValid(), "note has an id");
                             check(note.version == 1, "starts at version 1");
                             check(note.editable, "author may edit it");
                             alicesNote = note;
                             done();
                         });
    });

    await("bob sees it but cannot edit it", [&](auto done) {
        bob.listNotes(groupId, fileId, [&, done](const QList<ApiNote>& notes) {
            check(notes.size() == 1, "bob sees alice's note");
            if (!notes.isEmpty()) {
                check(!notes.first().editable,
                      "editable is false for someone else's note");
                check(notes.first().authorName == QLatin1String("Alice"),
                      "author name is shown");
            }
            done();
        });
    });

    await("bob's edit is refused", [&](auto done) {
        bob.updateNote(
            alicesNote.id, QStringLiteral("Bob was here"), alicesNote.version,
            [&, done](const ApiNote&) {
                check(false, "a non-author must not be able to edit");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 403, "non-author edit is 403");
                check(e.message.contains(QLatin1String("Alice")),
                      "message names the author");
                done();
            });
    });

    await("bob's delete is refused", [&](auto done) {
        bob.deleteNote(
            alicesNote.id,
            [&, done]() {
                check(false, "a non-author must not be able to delete");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 403, "non-author delete is 403");
                done();
            });
    });

    await("alice edits her own note", [&](auto done) {
        alice.updateNote(alicesNote.id, QStringLiteral("Check section 4."),
                         alicesNote.version, [&, done](const ApiNote& note) {
                             check(note.version == 2, "version was bumped");
                             check(note.body == QLatin1String("Check section 4."),
                                   "body was updated");
                             done();
                         });
    });

    await("a stale edit is rejected with the current text", [&](auto done) {
        // alicesNote still carries version 1 — the state a second machine
        // would have.
        alice.updateNote(
            alicesNote.id, QStringLiteral("From my other laptop"),
            alicesNote.version,
            [&, done](const ApiNote&) {
                check(false, "a stale write must not go through");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.code == QLatin1String(ApiError::StaleNote),
                      "error code is stale_note");
                check(e.detail.value(QStringLiteral("current_body")).toString() ==
                          QLatin1String("Check section 4."),
                      "detail carries the text that would have been lost");
                done();
            });
    });

    // ── Sync ──────────────────────────────────────────────────────────────────
    out << Qt::endl << "sync" << Qt::endl;
    await("sync status lists the pending file", [&](auto done) {
        alice.syncStatus(groupId, [&, done](const ApiSyncStatus& status) {
            check(status.totalFiles == 1, "one file in the group");
            check(status.uploadedFiles == 0, "none uploaded yet");
            check(status.pending.size() == 1, "one pending upload");
            // The download half of a sync is worked out client-side, so the
            // whole list — with hashes — has to come back too.
            check(status.files.size() == 1, "the full file list rides along");
            check(status.files.first().contentHash == hash,
                  "each entry carries the hash the client compares against");
            done();
        });
    });

    // Whether the bytes actually reached storage decides how much of the
    // download half below can be exercised.
    bool blobStored = false;
    await("upload reports storage state honestly", [&](auto done) {
        alice.uploadFile(
            groupId, fileId, pdfPath,
            [&, done](const ApiUploadResult& result) {
                check(result.uploaded || result.alreadyPresent,
                      "upload succeeded (B2 is configured)");
                blobStored = true;
                done();
            },
            [&, done](const ApiError& e) {
                // Without PDFORG_B2_* set this is the correct answer.
                check(e.code == QLatin1String(ApiError::StorageUnconfigured),
                      "clear 'storage not configured' error, not a crash");
                done();
            });
    });

    // ── Share codes: joining a group and pulling its folder down ──────────────
    out << Qt::endl << "share codes" << Qt::endl;

    ApiClient carol;
    ApiClient dave;
    carol.setBaseUrl(QUrl(baseUrl));
    dave.setBaseUrl(QUrl(baseUrl));
    const QString carolEmail = QStringLiteral("carol-%1@example.com").arg(stamp);
    const QString daveEmail  = QStringLiteral("dave-%1@example.com").arg(stamp);

    QObject::connect(&carol, &ApiClient::errorOccurred, [](const ApiError& e) {
        out << "  !! carol unhandled: " << e.code << " — " << e.message
            << Qt::endl;
        ++g_failures;
    });
    QObject::connect(&dave, &ApiClient::errorOccurred, [](const ApiError& e) {
        out << "  !! dave unhandled: " << e.code << " — " << e.message << Qt::endl;
        ++g_failures;
    });

    await("carol registers", [&](auto done) {
        carol.registerAccount(carolEmail, QStringLiteral("hunter2hunter2"),
                              QStringLiteral("Carol"),
                              [&, done](const ApiUser&) { done(); });
    });
    await("dave registers", [&](auto done) {
        dave.registerAccount(daveEmail, QStringLiteral("hunter2hunter2"),
                             QStringLiteral("Dave"),
                             [&, done](const ApiUser&) { done(); });
    });

    int sharedId = -1;
    QString shareCode;
    await("a new group carries a share code", [&](auto done) {
        alice.createGroup(QStringLiteral("Shared By Code"),
                          [&, done](const ApiGroup& group) {
                              check(group.isShareable(), "group is shareable");
                              check(group.shareCode.startsWith(
                                        QLatin1String("PDFORG-")),
                                    "code carries the expected prefix");
                              sharedId  = group.id;
                              shareCode = group.shareCode;
                              done();
                          });
    });

    await("the personal group has no share code", [&](auto done) {
        alice.listGroups([&, done](const QList<ApiGroup>& groups) {
            for (const ApiGroup& group : groups) {
                if (group.isPersonal) {
                    check(group.shareCode.isEmpty(),
                          "a personal group's code is withheld");
                    check(!group.isShareable(), "and it is not shareable");
                }
            }
            done();
        });
    });

    await("the group is invisible before the code is used", [&](auto done) {
        carol.listFiles(
            sharedId,
            [&, done](const QList<ApiFile>&) {
                check(false, "a non-member must not read the group");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 404, "non-member gets 404");
                done();
            });
    });

    await("a malformed code is refused", [&](auto done) {
        carol.joinGroup(
            QStringLiteral("nonsense"),
            [&, done](const ApiGroup&) {
                check(false, "garbage should not join anything");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 400, "malformed code is a 400");
                done();
            });
    });

    await("a well-formed but unknown code is refused", [&](auto done) {
        carol.joinGroup(
            QStringLiteral("PDFORG-2345-6789-ABCD"),
            [&, done](const ApiGroup&) {
                check(false, "an unknown code should not join anything");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.code == QLatin1String(ApiError::ShareCodeNotFound),
                      "unknown code reports share_code_not_found");
                done();
            });
    });

    await("carol joins with the code", [&](auto done) {
        carol.joinGroup(shareCode, [&, done](const ApiGroup& group) {
            check(group.id == sharedId, "joined the right group");
            check(!group.isOwner(), "joins as a member, not an owner");
            check(group.name == QLatin1String("Shared By Code"),
                  "the group's name comes back to name the local folder");
            done();
        });
    });

    await("the code survives being retyped", [&](auto done) {
        // Lower-cased and unhyphenated, as it arrives out of a chat window.
        QString mangled = shareCode.toLower();
        mangled.remove(QLatin1Char('-'));
        carol.joinGroup(mangled, [&, done](const ApiGroup& group) {
            check(group.id == sharedId, "same group, however the code was typed");
            check(group.memberCount == 2, "rejoining did not duplicate anyone");
            done();
        });
    });

    // The blob is keyed by content, so registering the same PDF in a second
    // group finds it already stored — nothing to upload before downloading.
    int sharedFileId = -1;
    await("the same content in a new group reuses the stored blob", [&](auto done) {
        alice.registerFile(sharedId, hash, QStringLiteral("paper.pdf"), 42, 3,
                           [&, done](const ApiFile& file) {
                               check(file.isValid(), "file registered");
                               check(file.uploaded == blobStored,
                                     "storage state carried over by content hash");
                               sharedFileId = file.id;
                               done();
                           });
    });

    const QString downloadPath = tmp.filePath(QStringLiteral("downloaded.pdf"));
    if (blobStored) {
        await("carol downloads bytes she never had", [&](auto done) {
            carol.downloadFile(sharedId, sharedFileId, downloadPath,
                               [&, done]() {
                                   check(QFile::exists(downloadPath),
                                         "the file landed on disk");
                                   check(ApiClient::hashFile(downloadPath) == hash,
                                         "downloaded bytes match the original");
                                   done();
                               });
        });
    } else {
        out << "  skip  download (B2 not configured on this server)" << Qt::endl;
    }

    await("a non-member cannot download", [&](auto done) {
        dave.downloadFile(
            sharedId, sharedFileId, tmp.filePath(QStringLiteral("nope.pdf")),
            [&, done]() {
                check(false, "a non-member must not get the bytes");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 404, "non-member download is a 404");
                check(!QFile::exists(tmp.filePath(QStringLiteral("nope.pdf"))),
                      "and nothing was written to disk");
                done();
            });
    });

    await("an unsynced file reports itself rather than failing opaquely",
          [&](auto done) {
              const QString otherHash = QCryptographicHash::hash(
                                            QByteArrayLiteral("never uploaded"),
                                            QCryptographicHash::Sha256)
                                            .toHex();
              alice.registerFile(
                  sharedId, QString::fromLatin1(otherHash.toLatin1()),
                  QStringLiteral("missing.pdf"), 9, 1,
                  [&, done](const ApiFile& file) {
                      carol.downloadFile(
                          sharedId, file.id,
                          tmp.filePath(QStringLiteral("missing.pdf")),
                          [&, done]() {
                              check(false, "there are no bytes to download");
                              done();
                          },
                          [&, done](const ApiError& e) {
                              check(e.code == QLatin1String(ApiError::NotUploaded),
                                    "reports not_uploaded, so the client can skip it");
                              done();
                          });
                  });
          });

    await("a member cannot rotate the code", [&](auto done) {
        carol.rotateShareCode(
            sharedId,
            [&, done](const ApiGroup&) {
                check(false, "only the owner may rotate");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 403, "member rotate is 403");
                done();
            });
    });

    await("rotating locks out the old code", [&](auto done) {
        alice.rotateShareCode(sharedId, [&, done](const ApiGroup& updated) {
            check(updated.shareCode != shareCode, "the code changed");
            check(updated.isShareable(), "and the group is still shareable");

            dave.joinGroup(
                shareCode,
                [&, done](const ApiGroup&) {
                    check(false, "the retired code must not work");
                    done();
                },
                [&, done](const ApiError& e) {
                    check(e.code == QLatin1String(ApiError::ShareCodeNotFound),
                          "the retired code is no longer recognised");
                    done();
                });
        });
    });

    // ── Removing a file ───────────────────────────────────────────────────────
    // Detaching a file from a group and destroying the stored copy are separate
    // acts, and the client has to be able to tell which one happened.
    out << Qt::endl << "removal" << Qt::endl;

    await("a member cannot delete the stored copy", [&](auto done) {
        carol.removeFile(
            sharedId, sharedFileId, /*deleteStoredCopy=*/true,
            [&, done](const ApiFileRemoval&) {
                check(false, "only the owner may destroy the stored copy");
                done();
            },
            [&, done](const ApiError& e) {
                check(e.httpStatus == 403, "a member's purge is a 403");
                done();
            });
    });

    await("the refused purge left the file in the group", [&](auto done) {
        alice.listFiles(sharedId, [&, done](const QList<ApiFile>& files) {
            bool present = false;
            for (const ApiFile& file : files)
                present = present || file.id == sharedFileId;
            check(present, "nothing was removed by the refused call");
            done();
        });
    });

    await("a purge spares a blob another group still shares", [&](auto done) {
        // The same content is registered in alice's first group, so destroying
        // the blob here would pull it out from under that group too.
        alice.removeFile(sharedId, sharedFileId, /*deleteStoredCopy=*/true,
                         [&, done](const ApiFileRemoval& result) {
                             check(result.detached, "the group lost the file");
                             check(!result.purged,
                                   "but the stored copy was kept");
                             check(result.stillReferenced,
                                   "and the client is told why");
                             done();
                         });
    });

    await("the other group's copy is untouched", [&](auto done) {
        alice.listFiles(groupId, [&, done](const QList<ApiFile>& files) {
            check(files.size() == 1, "the first group still lists the file");
            check(files.first().uploaded == blobStored,
                  "and its storage state did not change");
            done();
        });
    });

    await("the owner's purge takes the last copy with it", [&](auto done) {
        alice.removeFile(groupId, fileId, /*deleteStoredCopy=*/true,
                         [&, done](const ApiFileRemoval& result) {
                             check(result.detached, "the group lost the file");
                             check(!result.stillReferenced,
                                   "no other group was holding it");
                             // Only a file that reached storage can leave it;
                             // without B2 configured there is nothing to purge.
                             check(result.purged == blobStored,
                                   "the stored copy went exactly when there was one");
                             done();
                         });
    });

    await("the purged file is gone from the group", [&](auto done) {
        alice.listFiles(groupId, [&, done](const QList<ApiFile>& files) {
            check(files.isEmpty(), "the group lists nothing now");
            done();
        });
    });

    // ── Access revocation ─────────────────────────────────────────────────────
    out << Qt::endl << "access" << Qt::endl;
    await("removing bob revokes his access", [&](auto done) {
        int bobId = -1;
        alice.listMembers(groupId, [&, done](const QList<ApiMember>& members) {
            for (const ApiMember& member : members)
                if (member.email == bobEmail)
                    bobId = member.userId;
            check(bobId >= 0, "bob found in the member list");

            alice.removeMember(groupId, bobId, [&, done]() {
                bob.listNotes(
                    groupId, fileId,
                    [&, done](const QList<ApiNote>&) {
                        check(false, "a removed member must lose access");
                        done();
                    },
                    [&, done](const ApiError& e) {
                        check(e.httpStatus == 404,
                              "removed member gets 404, not a leak");
                        done();
                    });
            });
        });
    });

    // ── Session refresh ───────────────────────────────────────────────────────
    out << Qt::endl << "session" << Qt::endl;
    ApiClient restored;
    restored.setBaseUrl(QUrl(baseUrl));

    await("refresh token yields a working session", [&](auto done) {
        restored.restoreSession(alice.refreshToken(), alice.currentUser());
        restored.refreshSession([&, done]() {
            check(restored.isAuthenticated(), "session restored from token");
            restored.listGroups([&, done](const QList<ApiGroup>& groups) {
                check(groups.size() == 2, "restored session can read groups");
                done();
            });
        });
    });

    out << Qt::endl
        << (g_failures == 0 ? QStringLiteral("All checks passed.")
                            : QStringLiteral("%1 check(s) failed.").arg(g_failures))
        << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}
