// bean_search MCP tool. Lives in its own translation unit (not
// mcptools_ai.cpp) so tests can link it against a fake-server-backed
// BeanBaseClient without dragging in MainController/AIManager.
#include "mcptoolregistry.h"
#include "../network/beanbaseclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

// bean_search — read-only canonical coffee lookup via Visualizer's
// open autocomplete (keyless, substring matching). Returns the canonical
// UUID that both Decenza and visualizer.coffee store, with attributes
// enriched per result via the two-stage canonical flow. Pairs with
// shots_update's `beanBase` arg: search here, then link a shot.
// Standalone (not folded into registerAITools) so tests can register it
// against a fake-server-backed BeanBaseClient without a MainController.
void registerBeanSearchTool(McpToolRegistry* registry, BeanBaseClient* client)
{
    registry->registerAsyncTool(
        "bean_search",
        "Search the community coffee database (Visualizer canonical beans, mirrored from Loffee "
        "Labs Bean Base) by roaster or bean name. Substring and multi-word matching — no API key "
        "or account needed. Returns canonical bean entries: id (the canonical UUID shared with "
        "visualizer.coffee), roasterName, roastName, plus origin/variety/process/roast level/"
        "tasting notes when known. Pass a chosen result object as shots_update's beanBase "
        "argument to link a shot to its bean.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"query", QJsonObject{{"type", "string"},
                    {"description", "Search term(s) — roaster and/or bean name; partial words OK"}}}
            }},
            {"required", QJsonArray{"query"}}
        },
        [client](const QJsonObject& args, std::function<void(QJsonObject)> respond) {
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

                void finish() {
                    if (responded) return;
                    responded = true;
                    respond(QJsonObject{
                        {"query", query},
                        {"count", entries.size()},
                        {"results", QJsonArray::fromVariantList(entries)},
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
                    for (const QVariant& v : std::as_const(gather->entries))
                        client->fetchCanonicalDetails(v.toMap());
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
        "read");
}
