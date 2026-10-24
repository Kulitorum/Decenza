// bean_search MCP tool. Lives in its own translation unit (not
// mcptools_ai.cpp) so tests can link it against a fake-server-backed
// BeanBaseClient without dragging in MainController/AIManager.
#include "mcptoolregistry.h"
#include "../network/beanbaseclient.h"
#include "../network/communitysettingsclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace {

// Shape a CommunityAggregator summary into compact, LLM-friendly JSON: top-3
// profiles with unit-named medians. The raw per-shot list is dropped (the LLM
// wants the aggregate, not 50 rows with epoch timestamps).
QJsonObject communitySummaryToJson(const QVariantMap& s)
{
    auto med = [](const QVariantMap& p, const char* k) {
        return p.value(QString::fromLatin1(k)).toMap().value(QStringLiteral("median")).toDouble();
    };
    QJsonArray profs;
    const QVariantList ps = s.value(QStringLiteral("profiles")).toList();
    for (int i = 0; i < ps.size() && i < 3; ++i) {
        const QVariantMap p = ps[i].toMap();
        QJsonObject po{
            {"title", p.value(QStringLiteral("title")).toString()},
            {"shots", p.value(QStringLiteral("shotCount")).toInt()},
            {"users", p.value(QStringLiteral("userCount")).toInt()},
            {"ratingMean0to100", p.value(QStringLiteral("ratingMean")).toDouble()},
            {"doseG", med(p, "dose")},
            {"yieldG", med(p, "yield")},
            {"ratio", med(p, "ratio")},
            {"timeSec", med(p, "timeSec")}
        };
        if (p.value(QStringLiteral("grinderMatched")).toBool()
            && !p.value(QStringLiteral("grindRepresentative")).toString().isEmpty()) {
            po.insert("grind", p.value(QStringLiteral("grindRepresentative")).toString());
            po.insert("grinder", p.value(QStringLiteral("grinderDisplayName")).toString());
        }
        profs.append(po);
    }
    return QJsonObject{
        {"shotCount", s.value(QStringLiteral("shotCount")).toInt()},
        {"userCount", s.value(QStringLiteral("userCount")).toInt()},
        {"ratedShotCount", s.value(QStringLiteral("ratedShotCount")).toInt()},
        {"hasSuggestion", s.value(QStringLiteral("hasSuggestion")).toBool()},
        {"allTime", s.value(QStringLiteral("isAllTime")).toBool()},
        {"topProfiles", profs},
        {"hint", s.value(QStringLiteral("hasSuggestion")).toBool()
            ? "Medians across the community's shots for this bean. Temperature isn't a shot field."
            : "Too few community shots to rank a profile for this bean yet."}
    };
}

} // namespace

// bean_search — read-only canonical coffee lookup via Visualizer's
// open autocomplete (keyless, substring matching). Returns the canonical
// UUID that both Decenza and visualizer.coffee store, with attributes
// enriched per result via the two-stage canonical flow. Pairs with
// shots_update's `beanBase` arg: search here, then link a shot.
// Standalone (not folded into registerAITools) so tests can register it
// against a fake-server-backed BeanBaseClient without a MainController.
void registerBeanSearchTool(McpToolRegistry* registry, BeanBaseClient* client,
                            CommunitySettingsClient* community)
{
    registry->registerAsyncTool(
        "bean_search",
        "Community coffee lookup. action=search (default): find Visualizer canonical beans by "
        "roaster/bean name (substring, keyless); returns id (canonical UUID shared with "
        "visualizer.coffee), roasterName, roastName, attributes, linkState — pass a result as "
        "shots_update's beanBase arg to link a shot. action=community: summarise community shots "
        "for a bean (needs roaster+coffee, optional canonicalId); returns top profiles ranked by "
        "rating with dose/yield/ratio/time medians.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"action", QJsonObject{{"type", "string"},
                    {"enum", QJsonArray{"search", "community"}},
                    {"description", "search (default): canonical beans. community: community-shot summary for a bean"}}},
                {"query", QJsonObject{{"type", "string"},
                    {"description", "search action: roaster and/or bean name; partial words OK"}}},
                {"roaster", QJsonObject{{"type", "string"},
                    {"description", "community action: roaster (bean_brand)"}}},
                {"coffee", QJsonObject{{"type", "string"},
                    {"description", "community action: bean/coffee name (bean_type)"}}},
                {"canonicalId", QJsonObject{{"type", "string"},
                    {"description", "community action: canonical bag UUID (optional; narrows the match)"}}}
            }}
            // No top-level required: `search` needs query, `community` needs roaster+coffee —
            // validated per-action in the handler, so existing {query} callers keep working.
        },
        [client, community](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
            const QString action = args.value("action").toString().isEmpty()
                ? QStringLiteral("search") : args["action"].toString();

            if (action == QStringLiteral("community")) {
                if (!community) {
                    respond(QJsonObject{{"error", "Community data not available"}});
                    return;
                }
                const QString roaster = args["roaster"].toString().trimmed();
                const QString coffee = args["coffee"].toString().trimmed();
                const QString canonicalId = args["canonicalId"].toString().trimmed();
                if (roaster.isEmpty() || coffee.isEmpty()) {
                    respond(QJsonObject{{"error", "community action needs roaster and coffee"}});
                    return;
                }
                const QString key = CommunitySettingsClient::searchKey(roaster, coffee, canonicalId);
                // Bridge: respond once on the matching reply or the deadline (the client is
                // shared, and a superseded/aborted query may emit nothing).
                class CGather : public QObject {
                public:
                    std::function<void(QJsonObject)> respond;
                    QString key;
                    bool responded = false;
                    QTimer grace;
                };
                auto* g = new CGather;
                g->setParent(community);
                g->respond = respond;
                g->key = key;
                g->grace.setSingleShot(true);
                QObject::connect(&g->grace, &QTimer::timeout, g, [g]() {
                    if (g->responded) return;
                    g->responded = true;
                    g->respond(QJsonObject{{"error", "Community search timed out"}});
                    g->deleteLater();
                });
                QObject::connect(community, &CommunitySettingsClient::resultsReady, g,
                    [g](const QString& k, const QVariantMap& summary) {
                        if (g->responded || k != g->key) return;
                        g->responded = true;
                        g->respond(communitySummaryToJson(summary));
                        g->deleteLater();
                    });
                QObject::connect(community, &CommunitySettingsClient::searchFailed, g,
                    [g](const QString& k, const QString& status) {
                        if (g->responded || k != g->key) return;
                        g->responded = true;
                        QString msg;
                        if (status == QStringLiteral("superseded"))  // displaced by a concurrent consumer of the shared client
                            msg = QStringLiteral("Community search superseded by a concurrent search — retry");
                        else if (status == QStringLiteral("unsupported"))
                            msg = QStringLiteral("Community endpoint not available on this server");
                        else
                            msg = QStringLiteral("Could not reach community data");
                        g->respond(QJsonObject{{"error", msg}, {"status", status}});
                        g->deleteLater();
                    });
                g->grace.start(20000);
                community->search(roaster, coffee, canonicalId);
                return;
            }

            // action=search (default): Visualizer canonical lookup over BeanBaseClient.
            if (!client) {
                respond(QJsonObject{{"error", "Bean search client not available"}});
                return;
            }
            const QString query = args["query"].toString().trimmed();
            if (query.length() < 2) {
                respond(QJsonObject{{"error", "query must be at least 2 characters"}});
                return;
            }

            // Bridge object: gathers the search result, then best-effort
            // attribute enrichment for the top results, then responds once —
            // when every enrichment reported or the grace timer fires.
            // Parented to the client so app teardown cleans it up.
            class Gather : public QObject {
            public:
                std::function<void(QJsonObject)> respond;
                QString query;
                QVariantList entries;
                int pendingDetails = 0;
                bool responded = false;
                QTimer grace;

                BeanBaseClient* client = nullptr;  // set at construction; used by finish()

                void finish() {
                    if (responded) return;
                    responded = true;
                    // linkState per entry: "live", "archived" (the product page
                    // is gone but the Internet Archive has a capture), "none"
                    // (no usable page) or "unknown" (not resolved in time).
                    // Near-duplicate entries for one coffee routinely differ in
                    // nothing else, so this is often the only thing that picks
                    // one over the other.
                    QVariantList annotated = entries;
                    for (QVariant& v : annotated) {
                        QVariantMap m = v.toMap();
                        const QString link = m.value(QStringLiteral("link")).toString().trimmed();
                        m.insert(QStringLiteral("linkState"),
                                 (client && !link.isEmpty()) ? client->linkState(link)
                                                             : QStringLiteral("none"));
                        v = m;
                    }
                    respond(QJsonObject{
                        {"query", query},
                        {"count", annotated.size()},
                        {"results", QJsonArray::fromVariantList(annotated)},
                        {"hint", entries.isEmpty()
                            ? "No matches — the bean may not be in the community database yet."
                            : "To link a shot to one of these beans, pass the chosen result object as shots_update's beanBase argument."}
                    });
                    deleteLater();
                }
            };
            auto* gather = new Gather;
            gather->setParent(client);
            gather->respond = respond;
            gather->query = query;
            gather->client = client;
            gather->grace.setSingleShot(true);
            QObject::connect(&gather->grace, &QTimer::timeout, gather, [gather]() { gather->finish(); });

            QObject::connect(client, &BeanBaseClient::searchResults, gather,
                [client, gather](const QString& q, const QVariantList& entries) {
                    if (gather->responded || q.compare(gather->query, Qt::CaseInsensitive) != 0) return;
                    // Cap for the LLM; each entry gets a best-effort
                    // attribute fetch (keyless, roaster UUID cached).
                    gather->entries = entries.mid(0, 5);
                    if (gather->entries.isEmpty()) { gather->finish(); return; }
                    gather->pendingDetails = static_cast<int>(gather->entries.size());
                    gather->grace.start(4000);  // Shorter window for enrichment only.
                    for (const QVariant& v : std::as_const(gather->entries)) {
                        client->fetchCanonicalDetails(v.toMap());
                        // Kick the link probe too, so a caller choosing between
                        // near-identical entries can see which product page
                        // still answers. Probes are session-cached, so the
                        // value is best-effort on a first search and accurate
                        // afterwards — the same bargain the app's picker makes.
                        const QString link =
                            v.toMap().value(QStringLiteral("link")).toString().trimmed();
                        if (!link.isEmpty())
                            client->probeLinkState(link);
                    }
                });
            QObject::connect(client, &BeanBaseClient::canonicalDetails, gather,
                [gather](const QString& canonicalId, const QVariantMap& attrs) {
                    if (gather->responded) return;
                    bool matched = false;
                    for (QVariant& v : gather->entries) {
                        QVariantMap m = v.toMap();
                        if (m.value(QStringLiteral("id")).toString() == canonicalId) {
                            for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
                                m.insert(it.key(), it.value());
                            v = m;
                            matched = true;
                            break;
                        }
                    }
                    // Only count OUR entries down — the signal is shared with
                    // the Beans-page consumer. Silent enrichment failures are
                    // covered by the grace timer.
                    if (matched && --gather->pendingDetails <= 0) gather->finish();
                });
            QObject::connect(client, &BeanBaseClient::searchFailed, gather,
                [gather](const QString& q, const QString& status) {
                    if (gather->responded || q.compare(gather->query, Qt::CaseInsensitive) != 0) return;
                    gather->responded = true;
                    const QString msg = status == QStringLiteral("superseded")
                        ? QStringLiteral("Search superseded by a concurrent search — retry")
                        : QStringLiteral("Could not reach the bean database");
                    gather->respond(QJsonObject{{"error", msg}, {"status", status}});
                    gather->deleteLater();
                });
            // Overall deadline armed BEFORE the search: the client is shared
            // with the Beans-page bar, and a superseded/aborted query may
            // produce no signal at all — without this the tool call would
            // hang forever and leak the gather.
            gather->grace.start(20000);
            client->search(query);
        },
        "read", McpTierNiche);
}
