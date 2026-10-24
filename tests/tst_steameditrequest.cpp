#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "network/sessionactionrequest.h"
#include "network/steameditrequest.h"

namespace {
constexpr auto kPackageId = "package-17";
constexpr auto kHostId = "host-abc";
constexpr auto kRequestId = "123e4567-e89b-12d3-a456-426614174000";
constexpr auto kContextId = "f7f63b52-2f01-4d1f-aea9-9f53660cf1b3";

SteamPitcherSnapshot pitchers()
{
    return SteamPitcherSnapshot::project({
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Small")},
                    {QStringLiteral("duration"), 30}, {QStringLiteral("flow"), 150},
                    {QStringLiteral("temperature"), 160.0},
                    {QStringLiteral("pitcherWeightG"), 0.0},
                    {QStringLiteral("calibMilkG"), 0.0},
                    {QStringLiteral("disabled"), false}, {QStringLiteral("builtin"), false}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Heater off")},
                    {QStringLiteral("duration"), 0}, {QStringLiteral("flow"), 0},
                    {QStringLiteral("temperature"), 0.0},
                    {QStringLiteral("pitcherWeightG"), 0.0},
                    {QStringLiteral("calibMilkG"), 0.0},
                    {QStringLiteral("disabled"), true}, {QStringLiteral("builtin"), true}}
    }, 0);
}

QJsonObject validFields(const QString& operation)
{
    if (operation == QLatin1String("set-preset-duration"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")},
                {QStringLiteral("durationSec"), 30}};
    if (operation == QLatin1String("set-preset-flow"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")},
                {QStringLiteral("flow"), 150}};
    if (operation == QLatin1String("set-preset-temperature"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")},
                {QStringLiteral("temperatureC"), 160.0}};
    if (operation == QLatin1String("rename-preset"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")},
                {QStringLiteral("newName"), QStringLiteral("Medium")}};
    if (operation == QLatin1String("create-preset"))
        return {{QStringLiteral("name"), QStringLiteral("Large")}};
    if (operation == QLatin1String("delete-preset"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")}};
    if (operation == QLatin1String("move-preset"))
        return {{QStringLiteral("fromIndex"), 0}, {QStringLiteral("expectedFromName"), QStringLiteral("Small")},
                {QStringLiteral("toIndex"), 1}, {QStringLiteral("expectedToName"), QStringLiteral("Medium")}};
    if (operation == QLatin1String("set-pitcher-weight"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")},
                {QStringLiteral("weightG"), 125.5}};
    if (operation == QLatin1String("store-current-pitcher-weight")
        || operation == QLatin1String("clear-pitcher-weight"))
        return {{QStringLiteral("index"), 0}, {QStringLiteral("expectedName"), QStringLiteral("Small")}};
    if (operation == QLatin1String("set-milk-auto-capture"))
        return {{QStringLiteral("enabled"), true}};
    if (operation == QLatin1String("set-steam-seconds-per-gram"))
        return {{QStringLiteral("value"), 0.15}};
    return {};
}

QByteArray actionBody(const QString& operation, const QJsonObject& fields)
{
    QJsonObject object{
        {QStringLiteral("packageId"), QString::fromLatin1(kPackageId)},
        {QStringLiteral("hostId"), QString::fromLatin1(kHostId)},
        {QStringLiteral("requestId"), QString::fromLatin1(kRequestId)},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("expectedRevision"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("controlContextId"), QString::fromLatin1(kContextId)}
    };
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        object.insert(it.key(), it.value());
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
} // namespace

class tst_SteamEditRequest : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void editorRevisionCoversTheCompleteEditableSnapshot()
    {
        SteamPitcherSnapshot sourcePitchers = pitchers();
        MachineUiSnapshot sourceUi;
        const QString initial = SteamEditRequest::editorRevision(sourcePitchers, sourceUi);
        QVERIFY(SteamEditRequest::isRevision(initial));

        auto changedPitchers = sourcePitchers;
        changedPitchers.selectedIndex = -1;
        QVERIFY(initial != SteamEditRequest::editorRevision(changedPitchers, sourceUi));
        changedPitchers = sourcePitchers;
        auto row = changedPitchers.presets.first().toMap();
        row.insert(QStringLiteral("pitcherWeightG"), 120.0);
        changedPitchers.presets[0] = row;
        QVERIFY(initial != SteamEditRequest::editorRevision(changedPitchers, sourceUi));

        const auto changeUi = [&sourceUi](auto setter) {
            MachineUiSnapshot changed = sourceUi;
            setter(changed);
            return changed;
        };
        QVERIFY(initial != SteamEditRequest::editorRevision(sourcePitchers,
            changeUi([](MachineUiSnapshot& ui) { ++ui.brewSteamTimeout; })));
        QVERIFY(initial != SteamEditRequest::editorRevision(sourcePitchers,
            changeUi([](MachineUiSnapshot& ui) { ++ui.brewSteamFlow; })));
        QVERIFY(initial != SteamEditRequest::editorRevision(sourcePitchers,
            changeUi([](MachineUiSnapshot& ui) { ui.brewSteamTemperature += 0.5; })));
        QVERIFY(initial != SteamEditRequest::editorRevision(sourcePitchers,
            changeUi([](MachineUiSnapshot& ui) { ui.brewMilkAutoCaptureEnabled = true; })));
        QVERIFY(initial != SteamEditRequest::editorRevision(sourcePitchers,
            changeUi([](MachineUiSnapshot& ui) { ui.brewSteamSecondsPerGram = 0.25; })));

        MachineUiSnapshot unrelated = sourceUi;
        unrelated.temperature = 99.0;
        QCOMPARE(initial, SteamEditRequest::editorRevision(sourcePitchers, unrelated));
    }

    void operationTableValidatesAndExtractsTypedCommands()
    {
        const QString operations[] = {
            QStringLiteral("set-preset-duration"), QStringLiteral("set-preset-flow"),
            QStringLiteral("set-preset-temperature"), QStringLiteral("rename-preset"),
            QStringLiteral("create-preset"), QStringLiteral("delete-preset"),
            QStringLiteral("move-preset"), QStringLiteral("set-pitcher-weight"),
            QStringLiteral("store-current-pitcher-weight"), QStringLiteral("clear-pitcher-weight"),
            QStringLiteral("set-milk-auto-capture"), QStringLiteral("set-steam-seconds-per-gram"),
            QStringLiteral("calibrate-from-last-steam")
        };
        for (const QString& name : operations) {
            const auto parsed = SteamEditRequest::validateFields(name, validFields(name));
            QVERIFY2(parsed.valid, qPrintable(name + QStringLiteral(": ") + parsed.error));
            QCOMPARE(parsed.operationName, name);
            QCOMPARE(SteamEditRequest::operationName(parsed.operation), name);
        }

        const auto duration = SteamEditRequest::validateFields(
            QStringLiteral("set-preset-duration"), validFields(QStringLiteral("set-preset-duration")));
        QCOMPARE(duration.command.index, 0);
        QCOMPARE(duration.command.expectedName, QStringLiteral("Small"));
        QCOMPARE(duration.command.durationSec, 30);
        const auto create = SteamEditRequest::validateFields(
            QStringLiteral("create-preset"), validFields(QStringLiteral("create-preset")));
        QCOMPARE(create.command.newName, QStringLiteral("Large"));
        const auto weight = SteamEditRequest::validateFields(
            QStringLiteral("set-pitcher-weight"), validFields(QStringLiteral("set-pitcher-weight")));
        QCOMPARE(weight.command.weightG, 125.5);
    }

    void rejectsUnknownFieldsAndOutOfRangeValues()
    {
        QJsonObject fields = validFields(QStringLiteral("set-preset-flow"));
        fields.insert(QStringLiteral("unexpected"), 1);
        QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("set-preset-flow"), fields).valid);

        const QJsonValue badFlow[] = {QJsonValue(42), QJsonValue(true), QJsonValue(300)};
        for (const QJsonValue& value : badFlow) {
            fields = validFields(QStringLiteral("set-preset-flow"));
            fields.insert(QStringLiteral("flow"), value);
            QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("set-preset-flow"), fields).valid);
        }

        fields = validFields(QStringLiteral("set-preset-temperature"));
        fields.insert(QStringLiteral("temperatureC"), 1000.0);
        QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("set-preset-temperature"), fields).valid);
        fields = validFields(QStringLiteral("create-preset"));
        fields.insert(QStringLiteral("name"), QString(129, QLatin1Char('x')));
        QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("create-preset"), fields).valid);
        fields.insert(QStringLiteral("name"), QStringLiteral("  Large  "));
        QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("create-preset"), fields).valid);
        QVERIFY(!SteamEditRequest::validateFields(QStringLiteral("unknown"), {}).valid);
    }

    void sessionActionUsesTheSameStrictEditorSchema()
    {
        const QString path = QStringLiteral("/api/session/steam/editor");
        const QString operation = QStringLiteral("set-preset-flow");
        const auto parsed = SessionActionRequest::validate(
            QStringLiteral("POST"), path, actionBody(operation, validFields(operation)),
            QString::fromLatin1(kPackageId), QString::fromLatin1(kHostId));
        QVERIFY2(parsed.valid, qPrintable(parsed.error));
        QCOMPARE(parsed.action, SessionActionRequest::Action::SteamEditor);
        QCOMPARE(parsed.actionName, QStringLiteral("steam-editor"));
        QCOMPARE(parsed.expectedRevision, QString(64, QLatin1Char('a')));
        QCOMPARE(parsed.controlContextId, QString::fromLatin1(kContextId));
        QCOMPARE(parsed.steamEditor.operation, SteamEditRequest::Operation::SetPresetFlow);
        QCOMPARE(parsed.steamEditor.command.flow, 150);

        QJsonObject invalidFields = validFields(operation);
        invalidFields.insert(QStringLiteral("expectedRevision"), QStringLiteral("extra"));
        const auto invalid = SessionActionRequest::validate(
            QStringLiteral("POST"), path, actionBody(operation, invalidFields),
            QString::fromLatin1(kPackageId), QString::fromLatin1(kHostId));
        QVERIFY(!invalid.valid);

        const auto wrongMethod = SessionActionRequest::validate(
            QStringLiteral("GET"), path, actionBody(operation, validFields(operation)),
            QString::fromLatin1(kPackageId), QString::fromLatin1(kHostId));
        QVERIFY(!wrongMethod.valid);
        QCOMPARE(wrongMethod.httpStatus, 405);
    }
};

QTEST_GUILESS_MAIN(tst_SteamEditRequest)
#include "tst_steameditrequest.moc"
