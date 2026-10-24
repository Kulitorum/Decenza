#include "beanuiservicesource.h"
#include "network/beanbaseclient.h"
#include "network/communitysettingsclient.h"
#include "core/settings_visualizer.h"
#include "servicevalue.h"
#include "network/baglinkcontroller.h"
#include "network/visualizershotsync.h"
#include "history/coffeebagstorage.h"
#include "ai/aimanager.h"
#include "core/settings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <cmath>

namespace {
bool validHttpUrl(const QString& text)
{
    const QUrl url(text);
    return text.size() <= 4096 && url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

bool validBagId(const QVariant& value)
{
    const int type = value.metaType().id();
    if (type == QMetaType::Bool || (type != QMetaType::Int && type != QMetaType::UInt
        && type != QMetaType::LongLong && type != QMetaType::ULongLong)) return false;
    bool ok = false;
    const qlonglong id = value.toLongLong(&ok);
    return ok && id >= 0 && id <= 9007199254740991LL;
}

bool validText(const QVariantMap& args, const char* key, qsizetype max, bool allowEmpty = true)
{
    const auto value = args.value(QLatin1String(key));
    if (value.metaType().id() != QMetaType::QString) return false;
    const auto text = value.toString();
    return text.size() <= max && (allowEmpty || !text.trimmed().isEmpty());
}

bool boundedNumber(const QVariant& value, double minimum, double maximum, bool integer = false)
{
    double number = 0;
    return ServiceValue::finiteNumber(value, &number) && number >= minimum && number <= maximum
        && (!integer || std::floor(number) == number);
}

bool validCommunityText(const QVariantMap& args, const char* key, bool allowEmpty = true)
{
    return validText(args, key, 512, allowEmpty)
        && !args.value(QLatin1String(key)).toString().contains(QLatin1Char('\n'))
        && !args.value(QLatin1String(key)).toString().contains(QLatin1Char('\r'));
}

bool validShotId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}

bool validCommunitySuggestion(const QVariantMap& s)
{
    static const QStringList required{
        "key", "title", "topShotId", "profileJson", "doseG", "ratio", "tempC", "grind",
        "grinderMatched", "bagId", "roaster", "coffee", "beanBaseId"};
    if (s.size() != required.size() + (s.contains(QStringLiteral("select")) ? 1 : 0)) return false;
    for (const auto& key : required) if (!s.contains(key)) return false;
    for (const char* key : {"key", "title", "topShotId", "grind", "roaster", "coffee", "beanBaseId"})
        if (!validText(s, key, QLatin1String(key) == QLatin1String("key") ? 1600 : 512)) return false;
    if (s.value(QStringLiteral("title")).toString().trimmed().isEmpty()
        || !validText(s, "profileJson", 256 * 1024)
        || !boundedNumber(s.value(QStringLiteral("doseG")), 0, 1000)
        || !boundedNumber(s.value(QStringLiteral("ratio")), 0, 100)
        || !boundedNumber(s.value(QStringLiteral("tempC")), -1, 200)
        || !boundedNumber(s.value(QStringLiteral("bagId")), 0, 9007199254740991.0, true)
        || s.value(QStringLiteral("grinderMatched")).metaType().id() != QMetaType::Bool
        || (s.contains(QStringLiteral("select"))
            && s.value(QStringLiteral("select")).metaType().id() != QMetaType::Bool))
        return false;
    const QString profileJson = s.value(QStringLiteral("profileJson")).toString();
    return profileJson.isEmpty() || QJsonDocument::fromJson(profileJson.toUtf8()).isObject();
}

bool validKind(const QString& kind)
{
    return kind == QLatin1String("coffee") || kind == QLatin1String("tea");
}

bool validJsonValue(const QVariant& value, int depth = 0)
{
    if (depth > 8) return false;
    const int type = value.metaType().id();
    if (type == QMetaType::QString) return value.toString().size() <= 8192;
    if (type == QMetaType::Bool || type == QMetaType::Int || type == QMetaType::UInt) return true;
    if (type == QMetaType::LongLong)
        return std::llabs(value.toLongLong()) <= 9007199254740991LL;
    if (type == QMetaType::ULongLong)
        return value.toULongLong() <= 9007199254740991ULL;
    if (type == QMetaType::Double || type == QMetaType::Float)
        return std::isfinite(value.toDouble());
    if (type == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        if (map.size() > 256) return false;
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (it.key().size() > 128 || !validJsonValue(it.value(), depth + 1)) return false;
        return true;
    }
    if (type == QMetaType::QVariantList) {
        const auto list = value.toList();
        if (list.size() > 256) return false;
        for (const auto& item : list)
            if (!validJsonValue(item, depth + 1)) return false;
        return true;
    }
    return false;
}
}

NativeBeanUiServiceSource::NativeBeanUiServiceSource(BeanBaseClient* beans, AIManager* ai,
    Settings* settings, VisualizerShotSync* visualizer, CoffeeBagStorage* bags, QObject* parent)
    : BeanUiStateSimpleSource(parent), m_beans(beans), m_ai(ai), m_settings(settings), m_visualizer(visualizer)
{
    if (bags && beans && settings)
        m_bagLinks = new BagLinkController(bags, beans, settings->dye(), visualizer, this);
    if (m_bagLinks) {
        connect(m_bagLinks, &BagLinkController::inspectionFinished, this,
            [this](const QString& id, bool success, const QString& reason) {
                finish(id, {}, success ? QString() : reason);
            });
    }
    if (beans) {
        connect(beans, &BeanBaseClient::bagImageReady, this, [this](const QString& key,const QString& path) {
            emit imageReady(key,QUrl::fromLocalFile(path).toString());
        });
        connect(beans, &BeanBaseClient::linkStateResolved,this,&BeanUiStateSource::linkResolved);
        connect(beans, &BeanBaseClient::canonicalDetails,this,&BeanUiStateSource::canonicalReady);
        connect(beans, &BeanBaseClient::pageTextReady,this,[this](const QString&,const QString& text,const QString& diagnostic) {
            const auto id=m_diagnostics.key(diagnostic);
            if (!id.isEmpty()) finish(id,{{"text",text}});
        }, Qt::QueuedConnection);
        connect(beans, &BeanBaseClient::pageTextFailed,this,[this](const QString&,const QString& error,const QString& diagnostic) {
            const auto id=m_diagnostics.key(diagnostic);
            if (!id.isEmpty()) finish(id,{},error);
        }, Qt::QueuedConnection);
    }
    if (ai) {
        connect(ai,&AIManager::configurationChanged,this,&NativeBeanUiServiceSource::publishCapabilities);
        connect(ai,&AIManager::providerChanged,this,&NativeBeanUiServiceSource::publishCapabilities);
        connect(ai,&AIManager::bagDetailsExtracted,this,[this](const QString& id,const QVariantMap& fields){finish(id,fields);});
        connect(ai,&AIManager::bagDetailsExtractionFailed,this,[this](const QString& id,const QString& error){finish(id,{},error);});
        connect(ai,&AIManager::productPageFound,this,[this](const QString& id,const QString& url){finish(id,{{"url",url}});});
        connect(ai,&AIManager::productPageSearchFailed,this,[this](const QString& id,const QString& error){finish(id,{},error);});
        connect(ai, &AIManager::beanResearchReady, this,
            [this](const QString& id, const QString& advice) { finish(id, {{"advice", advice}}); });
        connect(ai, &AIManager::beanResearchFailed, this,
            [this](const QString& id, const QString& reason) { finish(id, {}, reason); });
    }
    if (settings) {
        auto* visualizerSettings = settings->visualizer();
        connect(visualizerSettings, &SettingsVisualizer::communityApiBaseUrlChanged,
                this, &NativeBeanUiServiceSource::publishCapabilities);
        connect(visualizerSettings, &SettingsVisualizer::communityMinEnjoymentChanged,
                this, &NativeBeanUiServiceSource::publishCapabilities);
    }
    publishCapabilities();
}

void NativeBeanUiServiceSource::setCommunitySettings(CommunitySettingsClient* client)
{
    if (m_community == client && client) return;
    if (m_community) disconnect(m_community, nullptr, this, nullptr);
    const auto pendingSearches = m_communityReads.keys();
    const auto pendingProfiles = m_profileReads.keys();
    for (const auto& id : pendingSearches) finish(id, {}, QStringLiteral("disconnected"));
    for (const auto& id : pendingProfiles) finish(id, {}, QStringLiteral("disconnected"));
    m_community = client;
    if (client) {
        connect(client, &CommunitySettingsClient::apiAvailableChanged,
                this, &NativeBeanUiServiceSource::publishCapabilities);
        connect(client, &CommunitySettingsClient::resultsReady, this,
            [this](const QString& key, const QVariantMap& result) { finishCommunityRead(false, key, result); });
        connect(client, &CommunitySettingsClient::searchFailed, this,
            [this](const QString& key, const QString& reason) { finishCommunityRead(false, key, {}, reason); });
        connect(client, &CommunitySettingsClient::shotProfileReady, this,
            [this](const QString& id, const QVariantMap& result) { finishCommunityRead(true, id, result); });
        connect(client, &CommunitySettingsClient::shotProfileFailed, this,
            [this](const QString& id, const QString& reason) { finishCommunityRead(true, id, {}, reason); });
        connect(client, &QObject::destroyed, this, [this] {
            setCommunitySettings(nullptr);
            publishCapabilities();
        });
    }
    publishCapabilities();
}

void NativeBeanUiServiceSource::finishCommunityRead(
    bool profile, const QString& key, const QVariantMap& values, const QString& reason)
{
    const auto pending = profile ? m_profileReads : m_communityReads;
    const QString starting = profile ? m_startingProfileRequest : m_startingCommunityRequest;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        // The client can synchronously supersede an older MCP-owned request for
        // the same key while starting ours; that failure belongs to the old fetch.
        if (reason == QLatin1String("superseded") && it.key() == starting) continue;
        if (it.value() == key) finish(it.key(), values, reason);
    }
}

void NativeBeanUiServiceSource::completeCommunitySuggestion(
    const QString& requestId, bool ok, const QString& recipeName, bool selected)
{
    if (m_operations.value(requestId) != QLatin1String("applyCommunity")) return;
    finish(requestId, {{"ok", ok}, {"recipeName", recipeName}, {"selected", selected}});
}

void NativeBeanUiServiceSource::publishCapabilities() {
    setCapabilities({{"configured",m_ai && m_ai->isConfigured()},
        {"provider",m_ai ? m_ai->selectedProvider() : QString()},
        {"search",m_ai && m_ai->supportsProductPageSearch()},
        {"extractUrl",m_ai && m_ai->supportsUrlExtraction()},
        {"webSearch", m_ai && m_ai->currentProviderSupportsWebSearch()},
        {"communityApiAvailable", m_community && m_community->apiAvailable()},
        {"communityBaseUrl", m_settings ? m_settings->visualizer()->communityApiBaseUrl() : QString()},
        {"communityMinEnjoyment", m_settings ? m_settings->visualizer()->communityMinEnjoyment() : 0}});
}
void NativeBeanUiServiceSource::finish(const QString& id,const QVariantMap& values,const QString& reason) {
    if (!m_requests.finish(id)) return;
    const auto operation=m_operations.take(id);
    m_communityReads.remove(id);
    m_profileReads.remove(id);
    if (auto* search=m_searches.take(id).data()) search->deleteLater();
    if (operation == QLatin1String("findPage") || operation == QLatin1String("researchBean")
        || (operation == QLatin1String("page") && !reason.isEmpty()))
        removeDiagnostic(id);
    if (operation == QLatin1String("extract") || operation == QLatin1String("extractUrl")) {
        const auto pageId = m_extractionPages.take(id);
        if (!pageId.isEmpty()) removeDiagnostic(pageId);
    }
    emit result(id,operation,values,reason);
}
void NativeBeanUiServiceSource::removeDiagnostic(const QString& requestId)
{
    m_diagnostics.remove(requestId);
}
void NativeBeanUiServiceSource::cancel(QString id) {
    if (auto* search=m_searches.take(id).data()) search->deleteLater();
    QString pageId = id;
    if (m_extractionPages.contains(id)) pageId = m_extractionPages.take(id);
    const auto diagnostic=m_diagnostics.take(pageId);
    m_operations.remove(id);
    m_communityReads.remove(id);
    m_profileReads.remove(id);
    m_requests.finish(id);
    if (!diagnostic.isEmpty()) {
        if (m_beans) m_beans->abandonPageOperation(diagnostic);
        if (m_ai) m_ai->abandonDiagnosticOperation(diagnostic);
    }
}
void NativeBeanUiServiceSource::invoke(QString id,QString op,QVariantMap args) {
    const auto start=m_requests.begin(id);
    if (start==ServiceRequestIds::Start::DuplicateId) return;
    if (start==ServiceRequestIds::Start::InvalidId) return;
    if (start==ServiceRequestIds::Start::LimitReached) { emit result(id,op,{},QStringLiteral("tooManyRequests")); return; }
    m_operations.insert(id,op);
    if (QJsonDocument(QJsonObject::fromVariantMap(args)).toJson(QJsonDocument::Compact).size()>512*1024
        || !validateArguments(op, args)) {
        finish(id,{},QStringLiteral("invalidRequest")); return;
    }
    const auto value=[&args](const char* key){return args.value(QLatin1String(key)).toString();};
    if (op == QLatin1String("communitySearch")) {
        if (!m_community) { finish(id, {}, QStringLiteral("disconnected")); return; }
        const QString key = CommunitySettingsClient::searchKey(value("roaster"), value("coffee"), value("canonicalId"));
        m_communityReads.insert(id, key);
        const QScopedValueRollback<QString> dispatch(m_startingCommunityRequest, id);
        m_community->search(value("roaster"), value("coffee"), value("canonicalId"));
        return;
    }
    if (op == QLatin1String("communityProfile")) {
        if (!m_community) { finish(id, {}, QStringLiteral("disconnected")); return; }
        const QString shotId = value("shotId");
        const bool alreadyPending = m_profileReads.values().contains(shotId);
        m_profileReads.insert(id, shotId);
        if (!alreadyPending) {
            const QScopedValueRollback<QString> dispatch(m_startingProfileRequest, id);
            m_community->fetchShotProfile(shotId);
        }
        return;
    }
    if (op == QLatin1String("researchBean")) {
        if (!m_ai) { finish(id, {}, QStringLiteral("notConfigured")); return; }
        if (m_diagnostics.size() >= 128) { finish(id, {}, QStringLiteral("tooManyDiagnostics")); return; }
        const QString diagnostic = m_ai->researchBean(id, value("roaster"), value("coffee"), value("productUrl"));
        if (m_operations.contains(id)) {
            if (diagnostic.isEmpty()) { finish(id, {}, QStringLiteral("requestFailed")); return; }
            m_diagnostics.insert(id, diagnostic);
        }
        return;
    }
    if (op == QLatin1String("applyCommunity")) {
        if (!m_applyCommunity) { finish(id, {}, QStringLiteral("disconnected")); return; }
        QVariantMap suggestion = args.value(QStringLiteral("suggestion")).toMap();
        suggestion.insert(QStringLiteral("key"), id);
        m_applyCommunity(suggestion);
        return;
    }
    if (op == QLatin1String("inspectBagLinks")) {
        if (!m_bagLinks) { finish(id, {}, QStringLiteral("disconnected")); return; }
        m_bagLinks->inspectBag(args.value(QStringLiteral("bagId")).toLongLong(), id);
        return;
    }
    if (op == QLatin1String("refreshVisualizerBags")) {
        if (!m_bagLinks) { finish(id, {}, QStringLiteral("disconnected")); return; }
        finish(id, {}, m_bagLinks->refreshVisualizerBags()
            ? QString() : QStringLiteral("disconnected"));
        return;
    }
    if (!m_beans) {finish(id,{},QStringLiteral("disconnected"));return;}
    if (op=="search") {
        if (value("query").trimmed().isEmpty()) { finish(id,{{"entries",QVariantList{}}}); return; }
        auto* client=new BeanBaseClient(&m_network,m_settings,this);
        m_searches.insert(id,client);
        connect(client,&BeanBaseClient::searchResults,this,[this,id](const QString&,const QVariantList& rows){finish(id,{{"entries",rows}});});
        connect(client,&BeanBaseClient::searchFailed,this,[this,id](const QString&,const QString& error){finish(id,{},error);});
        client->search(value("query")); return;
    }
    if (op=="canonical") m_beans->fetchCanonicalDetails(args.value("entry").toMap());
    else if (op=="ensureImage") m_beans->ensureBagImage(value("key"),value("name"),value("url"));
    else if (op=="refreshImage") m_beans->refreshBagImage(value("key"),value("name"),value("url"));
    else if (op=="replaceImage") m_beans->replaceBagImageFromUrl(value("key"),value("url"));
    else if (op=="probeLink") m_beans->probeLinkState(value("url"));
    else if (op=="page") {
        if (m_diagnostics.size()>=128) { finish(id,{},QStringLiteral("tooManyDiagnostics")); return; }
        const auto diagnostic=m_beans->fetchPageText(value("url"),args.value("bagId").toLongLong());
        if (m_operations.contains(id)) {
            if (diagnostic.isEmpty()) { finish(id,{},QStringLiteral("requestFailed")); return; }
            m_diagnostics.insert(id,diagnostic);
        }
        return;
    } else if (op=="refreshVisualizer") {
        if (!m_visualizer) {finish(id,{},QStringLiteral("disconnected"));return;}
        m_visualizer->refreshBag(args.value("bagId").toLongLong());
    } else {
        if (!m_ai) {finish(id,{},QStringLiteral("notConfigured"));return;}
        const auto pageId = value("operationId");
        const auto diagnostic=m_diagnostics.value(pageId);
        if (op=="extract") {
            if (!pageId.isEmpty()) m_extractionPages.insert(id, pageId);
            m_ai->extractCoffeeBagDetails(id,value("text"),value("kind"),diagnostic);return;
        }
        if (op=="extractUrl") {
            if (!diagnostic.isEmpty()) m_extractionPages.insert(id, pageId);
            m_ai->extractCoffeeBagDetailsFromUrl(id,value("url"),value("kind"),diagnostic);return;
        }
        if (op=="findPage") {
            if (m_diagnostics.size()>=128) { finish(id,{},QStringLiteral("tooManyDiagnostics")); return; }
            const auto diagnosticId = m_ai->findProductPage(id,value("roaster"),value("coffee"),value("kind"),args.value("bagId").toLongLong());
            if (m_operations.contains(id)) {
                if (diagnosticId.isEmpty()) { finish(id,{},QStringLiteral("requestFailed")); return; }
                m_diagnostics.insert(id, diagnosticId);
            }
            return;
        }
        if (op=="searchDeclined") m_ai->logProductPageSearchDeclined(value("reason"));
    }
    finish(id);
}

bool NativeBeanUiServiceSource::validateArguments(const QString& operation, const QVariantMap& args) const
{
    static const QHash<QString, QStringList> allowed{
        {"search", {"query"}}, {"canonical", {"entry"}},
        {"ensureImage", {"key", "name", "url"}}, {"refreshImage", {"key", "name", "url"}},
        {"replaceImage", {"key", "url"}}, {"probeLink", {"url"}},
        {"page", {"url", "bagId"}}, {"extract", {"text", "kind", "operationId"}},
        {"extractUrl", {"url", "kind", "operationId"}},
        {"findPage", {"roaster", "coffee", "kind", "bagId"}},
        {"searchDeclined", {"reason"}}, {"refreshVisualizer", {"bagId"}},
        {"inspectBagLinks", {"bagId"}}, {"refreshVisualizerBags", {}},
        {"communitySearch", {"roaster", "coffee", "canonicalId"}},
        {"communityProfile", {"shotId"}}, {"researchBean", {"roaster", "coffee", "productUrl"}},
        {"applyCommunity", {"suggestion"}}
    };
    if (!allowed.contains(operation) || args.size() != allowed.value(operation).size()) return false;
    for (auto it = args.cbegin(); it != args.cend(); ++it) {
        if (!allowed.value(operation).contains(it.key())) return false;
        if (it.key() == QLatin1String("bagId")) {
            if (!validBagId(it.value())) return false;
        } else if (it.key() == QLatin1String("entry") || it.key() == QLatin1String("suggestion")) {
            if (it->metaType().id() != QMetaType::QVariantMap) return false;
        } else if (it->metaType().id() != QMetaType::QString) return false;
    }
    if (operation == QLatin1String("communitySearch"))
        return validCommunityText(args, "roaster", false) && validCommunityText(args, "coffee", false)
            && validCommunityText(args, "canonicalId");
    if (operation == QLatin1String("communityProfile")) return validShotId(args.value("shotId").toString());
    if (operation == QLatin1String("researchBean"))
        return validText(args, "roaster", 512, false) && validText(args, "coffee", 512, false)
            && validText(args, "productUrl", 4096)
            && (args.value("productUrl").toString().isEmpty() || validHttpUrl(args.value("productUrl").toString()));
    if (operation == QLatin1String("applyCommunity"))
        return validCommunitySuggestion(args.value(QStringLiteral("suggestion")).toMap());
    if (operation == QLatin1String("search")) return validText(args, "query", 256);
    if (operation == QLatin1String("canonical"))
        return validJsonValue(args.value("entry")) && args.value("entry").toMap().size() <= 256;
    if (operation == QLatin1String("ensureImage") || operation == QLatin1String("refreshImage"))
        return validText(args, "key", 512, false) && validText(args, "name", 512)
            && validText(args, "url", 4096) && (args.value("url").toString().isEmpty() || validHttpUrl(args.value("url").toString()));
    if (operation == QLatin1String("replaceImage"))
        return validText(args, "key", 512, false) && validText(args, "url", 4096, false)
            && validHttpUrl(args.value("url").toString());
    if (operation == QLatin1String("probeLink"))
        return validText(args, "url", 4096, false) && validHttpUrl(args.value("url").toString());
    if (operation == QLatin1String("page"))
        return validText(args, "url", 4096, false) && validHttpUrl(args.value("url").toString());
    if (operation == QLatin1String("extract"))
        return validText(args, "text", 256 * 1024, false) && validKind(args.value("kind").toString())
            && validText(args, "operationId", 128)
            && (args.value("operationId").toString().isEmpty()
                || m_diagnostics.contains(args.value("operationId").toString()));
    if (operation == QLatin1String("extractUrl"))
        return validText(args, "url", 4096, false) && validHttpUrl(args.value("url").toString())
            && validKind(args.value("kind").toString()) && validText(args, "operationId", 128);
    if (operation == QLatin1String("findPage"))
        return validText(args, "roaster", 512, false) && validText(args, "coffee", 512, false)
            && validKind(args.value("kind").toString());
    if (operation == QLatin1String("searchDeclined")) return validText(args, "reason", 256);
    if (operation == QLatin1String("refreshVisualizer")) return args.value("bagId").toLongLong() > 0;
    if (operation == QLatin1String("inspectBagLinks")) return args.value("bagId").toLongLong() > 0;
    if (operation == QLatin1String("refreshVisualizerBags")) return true;
    return false;
}
