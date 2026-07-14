// tst_kb_schema — build-time KB validator coverage (change:
// restructure-kb-as-validated-json, task 5.1).
//
// tools/validate_kb.py (stdlib-only Python 3) is the ONLY schema/integrity
// gate for resources/ai/profile_knowledge.json; it is wired into CMake as a
// hard `add_custom_target(validate_kb ALL ...)` build step (see the root
// CMakeLists.txt "Build-time KB validator" block) but until this file
// nothing exercised the validator itself as a *test* — a regression in the
// script (a check silently dropped, an error message that stops naming the
// offending entry) would ship undetected between the rare occasions someone
// hand-edits the KB and hits a violation.
//
// This test shells out to the validator via QProcess (matching how CMake
// invokes it: the same Python3 interpreter, the same script, real argv),
// against (a) the real shipped JSON — must pass — and (b) a representative
// set of deliberately-corrupted single/two-entry fixtures written to a
// QTemporaryDir at runtime, one per hard-gate scenario named in the
// profile-knowledge-base spec's "A build-time validator SHALL fail the
// build..." requirement — must fail with a message naming the offending
// entry/key. Not exhaustive (the spec lists more scenarios than are
// reproduced here — srcArchive-missing, unknown family enum, malformed
// src) — a representative subset per the task's own scoping note.

#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

struct ValidatorResult {
    int exitCode = -1;
    QString output;  // stdout+stderr merged (validate_kb prints WARN/ERROR to stderr)
};

// Invoke the exact interpreter + script CMake's validate_kb build gate
// uses (VALIDATE_KB_PYTHON3 / VALIDATE_KB_SCRIPT come from
// tests/CMakeLists.txt, sourced from the root CMakeLists.txt's
// find_package(Python3) — same interpreter, not a reimplementation).
ValidatorResult runValidator(const QString& kbJsonPath)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(VALIDATE_KB_PYTHON3),
               { QStringLiteral(VALIDATE_KB_SCRIPT), kbJsonPath });

    ValidatorResult r;
    if (!proc.waitForStarted()) {
        r.output = QStringLiteral("failed to start validate_kb.py: ") + proc.errorString();
        return r;
    }
    if (!proc.waitForFinished(30000)) {
        r.output = QStringLiteral("validate_kb.py timed out after 30s");
        proc.kill();
        return r;
    }
    r.exitCode = proc.exitCode();
    r.output = QString::fromUtf8(proc.readAll());
    return r;
}

// A minimal, otherwise-schema-valid entry — every corruption scenario
// clones this and mutates exactly the field under test, so a failure can
// only be attributed to that one deliberate defect.
QJsonObject validEntry(const QString& id, const QString& displayName)
{
    QJsonObject band;
    band["axis"] = QStringLiteral("pressurePeak");
    band["lo"] = 6.0;
    band["hi"] = 9.0;
    band["provenance"] = QStringLiteral("cited");
    band["src"] = QStringLiteral("https://example.com/source");
    band["confidence"] = QStringLiteral("high");
    band["rationale"] = QStringLiteral("Test rationale for validator fixture.");

    QJsonObject e;
    e["id"] = id;
    e["displayName"] = displayName;
    e["family"] = QStringLiteral("manual");
    e["prose"] = QStringLiteral("Test prose describing the fixture profile.");
    e["expertBand"] = band;
    return e;
}

QJsonDocument wrapDoc(const QJsonArray& profiles)
{
    QJsonObject root;
    root["schemaVersion"] = 1;
    root["profiles"] = profiles;
    return QJsonDocument(root);
}

// Write `doc` to a fresh file inside `dir` and return its path.
QString writeKb(QTemporaryDir& dir, const QJsonDocument& doc, const QString& name)
{
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    const bool opened = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    Q_ASSERT(opened);
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return path;
}

} // namespace

class TstKbSchema : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    // The precondition every corruption test leans on: the validator must
    // actually be runnable and must accept well-formed input. If this
    // fails, every "fails as expected" test below is meaningless (a
    // validator that always exits non-zero would make every corruption
    // scenario a false positive).
    void realShippedKbPasses()
    {
        const ValidatorResult r = runValidator(QStringLiteral(VALIDATE_KB_REAL_JSON));
        QVERIFY2(r.exitCode == 0,
                 qPrintable(QStringLiteral("validate_kb.py rejected the real shipped "
                     "resources/ai/profile_knowledge.json:\n") + r.output));
    }

    // Scenario: "Typo'd field key fails the build" — an authored entry
    // contains "usg" instead of "ugs" (or any key outside the schema). The
    // validator must exit non-zero and name both the entry and the bad key.
    void corruptedKb_typoedFieldKey_failsWithEntryAndKeyNamed()
    {
        QJsonObject bad = validEntry(QStringLiteral("typo-key-test"),
                                     QStringLiteral("Typo Key Test"));
        QJsonObject usg;
        usg["value"] = 1.0;
        usg["inferred"] = false;
        bad["usg"] = usg;  // typo of "ugs" — not in ENTRY_KEYS

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeKb(dir, wrapDoc({ bad }), QStringLiteral("typo_key.json"));

        const ValidatorResult r = runValidator(path);
        QVERIFY2(r.exitCode != 0,
                 "validate_kb.py must reject an entry with an unknown key ('usg')");
        QVERIFY2(r.output.contains(QStringLiteral("unknown key")),
                 qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("usg")),
                 qPrintable(QStringLiteral("error must name the offending key 'usg': ") + r.output));
        QVERIFY2(r.output.contains(QStringLiteral("typo-key-test")),
                 qPrintable(QStringLiteral("error must name the offending entry: ") + r.output));
    }

    // Scenario: "Out-of-range expert band fails the build" — expertBand
    // with lo >= hi. Must fail identifying the entry and the invariant.
    void corruptedKb_outOfRangeExpertBand_failsWithEntryNamed()
    {
        QJsonObject bad = validEntry(QStringLiteral("range-test"),
                                     QStringLiteral("Range Test"));
        QJsonObject band = bad["expertBand"].toObject();
        band["lo"] = 9.0;
        band["hi"] = 6.0;  // lo >= hi — invalid
        bad["expertBand"] = band;

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeKb(dir, wrapDoc({ bad }), QStringLiteral("bad_range.json"));

        const ValidatorResult r = runValidator(path);
        QVERIFY2(r.exitCode != 0,
                 "validate_kb.py must reject expertBand.lo >= expertBand.hi");
        QVERIFY2(r.output.contains(QStringLiteral("must be < hi")),
                 qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("range-test")),
                 qPrintable(QStringLiteral("error must name the offending entry: ") + r.output));
    }

    // Scenario: "Duplicate or orphaned alias fails the build" (duplicate
    // id half) — two entries sharing an `id`.
    void corruptedKb_duplicateId_failsWithIdNamed()
    {
        QJsonObject a = validEntry(QStringLiteral("dup-id"), QStringLiteral("Dup A"));
        QJsonObject b = validEntry(QStringLiteral("dup-id"), QStringLiteral("Dup B"));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeKb(dir, wrapDoc({ a, b }), QStringLiteral("dup_id.json"));

        const ValidatorResult r = runValidator(path);
        QVERIFY2(r.exitCode != 0, "validate_kb.py must reject a duplicate id");
        QVERIFY2(r.output.contains(QStringLiteral("duplicated")), qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("dup-id")),
                 qPrintable(QStringLiteral("error must name the duplicated id: ") + r.output));
    }

    // Scenario: "Duplicate or orphaned alias fails the build" (colliding
    // alias half) — one entry's `alsoMatches` collides with a DIFFERENT
    // entry's `displayName`, so the alias resolves to more than one id.
    void corruptedKb_collidingAlias_failsWithBothEntriesNamed()
    {
        QJsonObject a = validEntry(QStringLiteral("collide-a"), QStringLiteral("Collide Name"));
        QJsonObject b = validEntry(QStringLiteral("collide-b"), QStringLiteral("Other Name"));
        b["alsoMatches"] = QJsonArray{ QStringLiteral("Collide Name") };  // == a's displayName

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeKb(dir, wrapDoc({ a, b }), QStringLiteral("collide_alias.json"));

        const ValidatorResult r = runValidator(path);
        QVERIFY2(r.exitCode != 0,
                 "validate_kb.py must reject an alias that resolves to two different ids");
        QVERIFY2(r.output.contains(QStringLiteral("alias collision")), qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("collide-a")) && r.output.contains(QStringLiteral("collide-b")),
                 qPrintable(QStringLiteral("error must name both conflicting entries: ") + r.output));
    }

    // Scenario: "Provenance/src cross-field violation fails the build" —
    // provenance=cited with no src.
    void corruptedKb_citedProvenanceMissingSrc_failsWithEntryNamed()
    {
        QJsonObject bad = validEntry(QStringLiteral("cited-no-src"),
                                     QStringLiteral("Cited No Src"));
        QJsonObject band = bad["expertBand"].toObject();
        band.remove(QStringLiteral("src"));  // provenance stays "cited"
        bad["expertBand"] = band;

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeKb(dir, wrapDoc({ bad }), QStringLiteral("cited_no_src.json"));

        const ValidatorResult r = runValidator(path);
        QVERIFY2(r.exitCode != 0,
                 "validate_kb.py must reject provenance=cited with no src");
        QVERIFY2(r.output.contains(QStringLiteral("provenance=cited requires")),
                 qPrintable(r.output));
        QVERIFY2(r.output.contains(QStringLiteral("cited-no-src")),
                 qPrintable(QStringLiteral("error must name the offending entry: ") + r.output));
    }
};

QTEST_GUILESS_MAIN(TstKbSchema)
#include "tst_kb_schema.moc"
