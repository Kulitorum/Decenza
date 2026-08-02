#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QUuid>

#include "mocks/McpTestFixture.h"
#include "history/shothistorystorage.h"
#include "core/dbutils.h"

// Implemented in src/mcp/mcptools_shots.cpp.
class ShotHistoryStorage;
void registerShotTools(McpToolRegistry* registry, ShotHistoryStorage* shotHistory);

// Exercises the shots_get_debug_log MCP tool's filter/regex/tail additions
// (issue: speeding up MCP debug-log investigation) against a real
// ShotHistoryStorage + temp SQLite DB, end to end through the async tool path.
class tst_McpToolsShotsDebugLog : public QObject {
    Q_OBJECT

    static qint64 insertShotWithDebugLog(QSqlDatabase& db, const QString& debugLog) {
        QSqlQuery q(db);
        q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, debug_log) "
                  "VALUES (:uuid, :ts, 'Test', 30, :log)");
        q.bindValue(":uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
        q.bindValue(":ts", QDateTime::currentSecsSinceEpoch());
        q.bindValue(":log", debugLog);
        if (!q.exec()) {
            qWarning() << "insertShotWithDebugLog failed:" << q.lastError().text();
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void noNewParamsReproducesPriorShape() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed1", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db,
                QStringList{"BLE frame 1", "BLE frame 2", "phase transition: pour"}.join('\n'));
        });
        QVERIFY(shotId > 0);

        QJsonObject result = f.callAsyncTool("shots_get_debug_log", QJsonObject{{"shotId", shotId}});
        QCOMPARE(result["totalLines"].toInt(), 3);
        QCOMPARE(result["returnedLines"].toInt(), 3);
        QVERIFY(!result["hasMore"].toBool());
        QVERIFY(!result.contains("qualifyingLines"));
        QVERIFY(result["log"].toString().contains("phase transition: pour"));
    }

    void substringFilterIsCaseInsensitive() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed2", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db,
                QStringList{"connecting to R2", "scale ready", "r2 error 0/2"}.join('\n'));
        });

        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"filter", "R2"}});
        QCOMPARE(result["qualifyingLines"].toInt(), 2);
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].toObject()["line"].toInt(), 0);
        QCOMPARE(lines[1].toObject()["line"].toInt(), 2);
    }

    void regexFilter() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed3", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db,
                QStringList{"SAW trigger at 34g", "nothing here", "SAW trigger at 36g"}.join('\n'));
        });

        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"filter", "SAW.*trigger"}, {"regex", true}});
        QCOMPARE(result["qualifyingLines"].toInt(), 2);
    }

    void tailReturnsLastNAndOverridesOffset() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        QStringList logLines;
        for (int i = 0; i < 10; ++i)
            logLines << QString("line %1").arg(i);
        const QString log = logLines.join('\n');
        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed4", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db, log);
        });

        // offset supplied alongside tail — tail must win.
        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"offset", 2}, {"tail", 3}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].toObject()["line"].toInt(), 7);
        QCOMPARE(lines[2].toObject()["line"].toInt(), 9);
        QVERIFY(!result["hasMore"].toBool());
    }

    void explicitTailZeroDoesNotForceHasMoreFalse() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        QStringList logLines;
        for (int i = 0; i < 10; ++i)
            logLines << QString("line %1").arg(i);
        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed8", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db, logLines.join('\n'));
        });

        // tail:0 must mean "no tail" — 10 lines total, only 3 fit under this
        // limit, so hasMore must stay true, not be forced false.
        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"tail", 0}, {"limit", 3}});
        QCOMPARE(result["returnedLines"].toInt(), 3);
        QVERIFY(result["hasMore"].toBool());
    }

    void minLevelAcceptedButIgnored() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed5", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db, QStringList{"line one", "line two"}.join('\n'));
        });

        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"minLevel", "ERROR"}});
        QVERIFY(!result.contains("error"));
        QCOMPARE(result["totalLines"].toInt(), 2);
        QCOMPARE(result["returnedLines"].toInt(), 2);
    }

    void dedupeCollapsesRepeatedBurst() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed6", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db,
                QStringList{"BLE frame ack", "BLE frame ack", "BLE frame ack", "BLE frame nack"}.join('\n'));
        });

        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"filter", "BLE frame ack"}, {"dedupe", true}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 1);
        QJsonObject entry = lines[0].toObject();
        QCOMPARE(entry["line"].toInt(), 0);
        QCOMPARE(entry["count"].toInt(), 3);
        QCOMPARE(entry["lastLine"].toInt(), 2);
    }

    void noDedupeReproducesPriorShape() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        qint64 shotId = -1;
        withTempDb(storage.databasePath(), "debuglog_seed7", [&](QSqlDatabase& db) {
            shotId = insertShotWithDebugLog(db, QStringList{"repeat", "repeat"}.join('\n'));
        });

        QJsonObject result = f.callAsyncTool("shots_get_debug_log",
            QJsonObject{{"shotId", shotId}, {"filter", "repeat"}});
        QJsonArray lines = result["lines"].toArray();
        QCOMPARE(lines.size(), 2);
        for (const auto& l : lines) {
            QVERIFY(!l.toObject().contains("count"));
            QVERIFY(!l.toObject().contains("lastLine"));
        }
    }
    // shots_list had NO test at all, which is why a green suite meant nothing
    // when the recipes join broke it: `profile_json` exists on both `shots` and
    // `recipes`, the unqualified name made the statement ambiguous, SQLite
    // rejected it, the bare `if (exec())` swallowed the failure, and the count
    // query — which does not join — kept returning the true total. The tool
    // answered `shots: []` beside a non-zero `total` on every call.
    //
    // The rows-vs-total assertion below is the one that catches that shape.
    // Asserting only `total`, or only that the call did not error, would have
    // passed against the broken build.
    void shotsListReturnsRowsAndCarriesRecipeIdentity() {
        McpTestFixture f;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(f.tempDir.filePath("shots.db")));
        registerShotTools(&f.registry, &storage);

        withTempDb(storage.databasePath(), "shots_list_seed", [&](QSqlDatabase& db) {
            QSqlQuery r(db);
            r.prepare("INSERT INTO recipes (name, profile_title, drink_type, archived) "
                      "VALUES ('Dad Monday', 'Test', 'latte', 0)");
            QVERIFY(r.exec());
            const qint64 recipeId = r.lastInsertId().toLongLong();

            QSqlQuery q(db);
            q.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                      "profile_json, recipe_id) VALUES (:uuid, :ts, 'Test', 30, '{}', :rid)");
            q.bindValue(":uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
            q.bindValue(":ts", QDateTime::currentSecsSinceEpoch());
            q.bindValue(":rid", recipeId);
            QVERIFY(q.exec());

            // A second shot with no recipe, to prove the fields are sparse rather
            // than empty-but-present.
            QSqlQuery q2(db);
            q2.prepare("INSERT INTO shots (uuid, timestamp, profile_name, duration_seconds, "
                       "profile_json) VALUES (:uuid, :ts, 'Test', 30, '{}')");
            q2.bindValue(":uuid", QUuid::createUuid().toString(QUuid::WithoutBraces));
            q2.bindValue(":ts", QDateTime::currentSecsSinceEpoch() - 60);
            QVERIFY(q2.exec());
        });

        const QJsonObject result = f.callAsyncTool("shots_list", QJsonObject{});
        QVERIFY2(!result.contains("error"), qPrintable(result.value("error").toString()));

        const QJsonArray shots = result["shots"].toArray();
        QCOMPARE(result["total"].toInt(), 2);
        QCOMPARE(shots.size(), 2);   // the assertion that goes red on an ambiguous column

        // Newest first: the recipe-driven shot.
        const QJsonObject withRecipe = shots.at(0).toObject();
        QVERIFY(withRecipe.contains("recipeId"));
        QCOMPARE(withRecipe["recipeName"].toString(), QStringLiteral("Dad Monday"));

        // Sparse: presence alone must answer "was this a recipe drink?".
        const QJsonObject without = shots.at(1).toObject();
        QVERIFY(!without.contains("recipeId"));
        QVERIFY(!without.contains("recipeName"));
    }
};

QTEST_GUILESS_MAIN(tst_McpToolsShotsDebugLog)
#include "tst_mcptools_shots_debuglog.moc"
