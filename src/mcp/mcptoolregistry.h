#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QList>
#include <QVector>
#include <QFile>
#include <QByteArray>
#include <algorithm>
#include <functional>

// Synchronous tool handler: takes arguments, returns result immediately.
using McpToolHandler = std::function<QJsonObject(const QJsonObject& arguments)>;

// Async tool handler: takes arguments and a respond callback.
// The handler runs the work on a background thread and calls respond(result)
// on the main thread when done. The respond callback sends the HTTP response.
using McpAsyncToolHandler = std::function<void(const QJsonObject& arguments,
                                               std::function<void(QJsonObject)> respond)>;

// Why a registry lookup failed. The JSON-RPC code a caller should emit differs
// per reason (a bad name is a bad *request*; a dispatch or access fault is
// server-side), and the alternative to this enum is matching on the `errorOut`
// TEXT — which silently stops working the day someone rewords a message.
// Shared by the tool and resource registries; the latter includes this header.
enum class McpRegistryFailure {
    None,
    NotFound,       // no tool/resource of that name or URI is registered
    WrongDispatch,  // registered, but reached through the wrong sync/async path
    AccessDenied,   // registered, but above the caller's access level
};

// One verb of a merged tool. A family that used to be N tools (`steam_pitcher_add`,
// `steam_pitcher_delete`, …) becomes one tool whose `action` argument selects the
// verb, and each verb keeps its OWN category and confirmation policy — the two
// things that used to be looked up by tool name and would otherwise collapse onto
// the family's most dangerous member.
struct McpToolAction {
    QString name;               // the `action` value, e.g. "list", "delete"
    QString category;           // "read", "control", or "settings"
    QString confirmDescription; // shown in the confirmation dialog / chat payload
    McpToolHandler handler;     // sync handler (null when asyncHandler is set)
    McpAsyncToolHandler asyncHandler;

    // Confirmation is the presence of the wording for it. There is deliberately no
    // separate bool: two fields meaning one thing can disagree, and the failure that
    // matters here is silent — a `confirm = true` with no description raises a dialog
    // that does not say what it will do.
    bool confirms() const { return !confirmDescription.isEmpty(); }
};

// Where a tool sorts in `tools/list`. Clients truncate long tool lists, so the
// order is not cosmetic: it decides what a client loses. Emitted order is
// (tier, name), so the niche tail goes first and the same build always presents
// the same order.
enum McpToolTier {
    // 0: what a dial-in conversation reaches for — machine control and state,
    //    shots, dialing, profiles, recipes, scale, settings.
    McpTierCore = 0,
    // 1: the default. Everything not deliberately placed in one of the others.
    McpTierStandard = 1,
    // 2: real but rarely first — the Home Assistant bridge, themes, backups,
    //    saved AI conversations, bean search, one-shot diagnostics.
    //
    // Do not read these lists as an inventory: they say what the tiers are FOR.
    // Which tools are in each is the registration sites, and only those.
    McpTierNiche = 2,
};

struct McpToolDefinition {
    QString name;
    QString description;
    QJsonObject inputSchema;    // JSON Schema for the tool's parameters
    McpToolHandler handler;     // sync handler (null for async tools)
    McpAsyncToolHandler asyncHandler; // async handler (null for sync tools)
    QString category;           // "read", "control", or "settings" (see actions)
    bool isAsync = false;
    McpToolTier tier = McpTierStandard;
    QVector<McpToolAction> actions;  // empty for a single-verb tool
};

// What the server needs to know before dispatching a call: whether to confirm,
// and what to tell the user it is about to do.
struct McpConfirmationRequirement {
    bool required = false;
    QString actionId;     // "steam_pitcher.delete", or just the tool name if unmerged
    QString description;
};

namespace McpRegistryHelpers {
    // snake_case → "Snake Case" for an auto-derived `title` per MCP 2025-06-18.
    inline QString deriveTitle(const QString& name) {
        QStringList parts = name.split(QLatin1Char('_'), Qt::SkipEmptyParts);
        for (QString& p : parts) {
            if (p.isEmpty()) continue;
            p[0] = p[0].toUpper();
        }
        return parts.join(QLatin1Char(' '));
    }

    // Read an SVG from qrc and encode as a data: URI suitable for the
    // MCP `icons[].src` field (2025-11-25). Returns an empty string on miss.
    //
    // RESOURCES ONLY, and only where the URI maps to an icon of its own.
    //
    // Tools used to carry these: 216 KB of a ~312 KB `tools/list` (measured), with
    // 41 of 97 tools shipping the SAME 2292-byte generic fallback because their name
    // prefix was not in the map and the map had a default. Resources keep icons
    // because the handful that map to one map to DIFFERENT ones; iconQrcForResource()
    // now returns empty rather than defaulting, so a new resource family cannot
    // silently re-buy the fallback.
    inline QString iconDataUri(const QString& qrcPath) {
        QFile f(qrcPath);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray svg = f.readAll();
        return QStringLiteral("data:image/svg+xml;base64,")
            + QString::fromLatin1(svg.toBase64());
    }

    // Map a resource URI scheme path to a qrc icon path. Empty means NO icon, and
    // that is the right answer for a family with no icon of its own: a default here
    // is how 41 of 97 tools came to ship the same 2292-byte drawing. The
    // decenza://tools/<topic> documentation resources are 15 records that would have
    // fallen through to exactly that fallback — ~46 KB of identical base64 on every
    // resources/list, the same defect this change removed from tools/list, one
    // listing over.
    inline QString iconQrcForResource(const QString& uri) {
        if (uri.startsWith(QStringLiteral("decenza://machine"))) return ":/icons/decent-de1.svg";
        if (uri.startsWith(QStringLiteral("decenza://shots")))   return ":/icons/Graph.svg";
        if (uri.startsWith(QStringLiteral("decenza://profiles"))) return ":/icons/coffeebeans.svg";
        if (uri.startsWith(QStringLiteral("decenza://dialing"))) return ":/icons/grind.svg";
        if (uri.startsWith(QStringLiteral("decenza://debug")))   return ":/icons/list.svg";
        return QString();
    }

    inline QJsonArray iconsArrayFromQrc(const QString& qrcPath) {
        const QString uri = iconDataUri(qrcPath);
        if (uri.isEmpty()) return {};
        QJsonObject icon;
        icon["src"] = uri;
        icon["mimeType"] = "image/svg+xml";
        icon["sizes"] = QJsonArray{ QStringLiteral("any") };  // SVG scales freely; MCP schema requires string[]
        return QJsonArray{ icon };
    }

    // Build one verb of a merged tool. Confirmation is implied by supplying the
    // wording for it: an action that needs a dialog has to say what the dialog will
    // tell the user, so there is no bool that can disagree with the text beside it.
    inline McpToolAction syncAction(const QString& name, const QString& category,
                                    McpToolHandler handler,
                                    const QString& confirmDescription = QString())
    {
        McpToolAction a;
        a.name = name;
        a.category = category;
        a.confirmDescription = confirmDescription;
        a.handler = std::move(handler);
        return a;
    }

    inline McpToolAction asyncAction(const QString& name, const QString& category,
                                     McpAsyncToolHandler handler,
                                     const QString& confirmDescription = QString())
    {
        McpToolAction a;
        a.name = name;
        a.category = category;
        a.confirmDescription = confirmDescription;
        a.asyncHandler = std::move(handler);
        return a;
    }

    // The `action` property every merged tool declares, with its enum filled in
    // from the actions themselves so the schema cannot drift from the dispatch.
    inline QJsonObject actionProperty(const QVector<McpToolAction>& actions,
                                      const QString& description)
    {
        QJsonArray values;
        for (const McpToolAction& a : actions) values.append(a.name);
        return QJsonObject{{"type", "string"},
                           {"enum", values},
                           {"description", description}};
    }

    // Stamp a tool/resource input schema with the JSON Schema 2020-12 dialect
    // declaration (2025-11-25). No-op if the schema already declares `$schema`.
    inline QJsonObject withJsonSchemaDialect(QJsonObject schema) {
        if (!schema.contains(QStringLiteral("$schema")))
            schema[QStringLiteral("$schema")] = QStringLiteral("https://json-schema.org/draft/2020-12/schema");
        return schema;
    }
}

class McpToolRegistry : public QObject {
    Q_OBJECT
public:
    explicit McpToolRegistry(QObject* parent = nullptr) : QObject(parent) {}

    // Tool input schemas are stored in their registered form. The 2025-11-25
    // `$schema` dialect declaration is stamped per-request in listTools() so
    // it can be gated on the negotiated protocol version.
    void registerTool(const QString& name, const QString& description,
                      const QJsonObject& inputSchema, McpToolHandler handler,
                      const QString& category, McpToolTier tier = McpTierStandard)
    {
        McpToolDefinition tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = inputSchema;
        tool.handler = handler;
        tool.category = category;
        tool.tier = tier;
        m_tools[name] = tool;
    }

    void registerAsyncTool(const QString& name, const QString& description,
                           const QJsonObject& inputSchema, McpAsyncToolHandler handler,
                           const QString& category, McpToolTier tier = McpTierStandard)
    {
        McpToolDefinition tool;
        tool.name = name;
        tool.description = description;
        tool.inputSchema = inputSchema;
        tool.asyncHandler = handler;
        tool.isAsync = true;
        tool.category = category;
        tool.tier = tier;
        m_tools[name] = tool;
    }

    // Register a merged tool: one name, one schema, N verbs selected by `action`.
    //
    // Dispatch is uniformly ASYNC even when every action is synchronous, so that a
    // family mixing the two (`auto_load` reads sync, writes async) needs no special
    // case anywhere. A sync action's result is handed to respond() inline, on the
    // thread that called it, which is the main thread — the same place a sync tool's
    // return value was already produced.
    //
    // `action` is required and has no default. An absent or unrecognised action is an
    // error naming the valid values; it is NOT a shortcut past the gate, because
    // categoryFor()/confirmationFor() below resolve an unmatched action to the tool's
    // most restrictive one before this handler is ever reached.
    void registerActionTool(const QString& name, const QString& description,
                            const QJsonObject& inputSchema,
                            const QVector<McpToolAction>& actions,
                            McpToolTier tier = McpTierStandard,
                            const QString& actionDescription = QStringLiteral("Which operation to perform"))
    {
        McpToolDefinition tool;
        tool.name = name;
        tool.description = description;
        // The `action` property is injected here, never written at a registration
        // site: its enum comes from the actions themselves, so the schema a client
        // validates against cannot drift from what dispatch will accept.
        QJsonObject schema = inputSchema;
        QJsonObject props = schema.value("properties").toObject();
        props["action"] = McpRegistryHelpers::actionProperty(actions, actionDescription);
        schema["properties"] = props;
        QJsonArray required = schema.value("required").toArray();
        if (!required.contains(QJsonValue(QStringLiteral("action"))))
            required.prepend(QStringLiteral("action"));
        schema["required"] = required;
        tool.inputSchema = schema;
        tool.actions = actions;
        tool.isAsync = true;
        tool.tier = tier;
        // Kept in step with the actions purely as a safe default: every reader that
        // has the call's arguments uses categoryFor() and takes the merged branch
        // before reaching this field. It is the value anything added later would see
        // if it forgot to, and the strictest verb is the right thing to see.
        tool.category = strictestCategory(actions);
        tool.asyncHandler = [actions, name](const QJsonObject& args,
                                            std::function<void(QJsonObject)> respond) {
            const QString requested = args["action"].toString();
            for (const McpToolAction& a : actions) {
                if (a.name != requested) continue;
                if (a.asyncHandler) { a.asyncHandler(args, std::move(respond)); return; }
                // An action with neither handler set is a registration bug, but calling
                // a null std::function throws out of a socket read and takes the app
                // with it. A machine controller does not abort on a network request.
                if (!a.handler) {
                    respond(QJsonObject{{"error",
                        QStringLiteral("%1 action \"%2\" has no handler registered")
                            .arg(name, requested)}});
                    return;
                }
                respond(a.handler(args));
                return;
            }
            QStringList valid;
            for (const McpToolAction& a : actions) valid << a.name;
            QJsonObject err;
            err["error"] = requested.isEmpty()
                ? QStringLiteral("%1 requires an `action`. Valid actions: %2")
                      .arg(name, valid.join(QStringLiteral(", ")))
                : QStringLiteral("Unknown action \"%1\" for %2. Valid actions: %3")
                      .arg(requested, name, valid.join(QStringLiteral(", ")));
            respond(err);
        };
        m_tools[name] = tool;
    }

    // The category that governs THIS call: the named action's own category, or —
    // when `action` is missing or unknown — the tool's most restrictive one.
    // Failing closed matters: the alternative lets a caller omit an argument to be
    // gated as a read, and only then discover it was asking to delete something.
    QString categoryFor(const QString& name, const QJsonObject& args) const
    {
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) return QString();
        const auto& tool = it.value();
        if (tool.actions.isEmpty()) return tool.category;
        const QString requested = args["action"].toString();
        for (const McpToolAction& a : tool.actions)
            if (a.name == requested) return a.category;
        return strictestCategory(tool.actions);
    }

    // Whether this call must be confirmed, and what to say it will do. Unmerged
    // tools report nothing here — McpServer keeps its own name list for those.
    McpConfirmationRequirement confirmationFor(const QString& name, const QJsonObject& args) const
    {
        McpConfirmationRequirement req;
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd() || it.value().actions.isEmpty()) return req;
        const auto& actions = it.value().actions;
        const QString requested = args["action"].toString();
        for (const McpToolAction& a : actions) {
            if (a.name != requested) continue;
            req.required = a.confirms();
            req.actionId = name + QLatin1Char('.') + a.name;
            req.description = a.confirmDescription;
            return req;
        }
        // Unmatched action: confirm if ANY verb of this tool would have, so that a
        // malformed call cannot be the cheap way past the dialog.
        //
        // The wording is about the unrecognised action, NOT borrowed from the first
        // confirmable verb. Borrowing it put "Start pulling an espresso shot" on the
        // machine for a call that would then be rejected — and a confirmation net
        // works only while the dialog is worth reading.
        for (const McpToolAction& a : actions) {
            if (!a.confirms()) continue;
            QStringList valid;
            for (const McpToolAction& each : actions) valid << each.name;
            req.required = true;
            req.actionId = name;
            req.description = requested.isEmpty()
                ? QStringLiteral("%1 with no action — the request will be rejected. "
                                 "Valid actions: %2").arg(name, valid.join(QStringLiteral(", ")))
                : QStringLiteral("%1: unrecognised action \"%2\" — the request will be "
                                 "rejected. Valid actions: %3")
                      .arg(name, requested, valid.join(QStringLiteral(", ")));
            return req;
        }
        return req;
    }

    // The tool's declared actions, for tests and for error messages.
    QStringList actionNames(const QString& name) const
    {
        QStringList names;
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) return names;
        for (const McpToolAction& a : it.value().actions) names << a.name;
        return names;
    }

    // List all tools. Tools above the current access level are still listed
    // (so the AI knows they exist) but their descriptions note the required level.
    // Access is enforced in callTool — restricted tools return an error when called.
    // 0 = Monitor (read only), 1 = Control (read + control), 2 = Full (all)
    //
    // protocolVersion gates spec-versioned optional fields. Strict clients reject
    // tools/list responses containing fields from a newer spec than was negotiated,
    // which surfaces as the server connecting with zero tools.
    //
    // Emission order is (tier, name), never hash order. Real clients truncate this
    // list — ChatGPT exposed 87 of 97 tools — and under hash order WHICH tools they
    // dropped was arbitrary and changed between runs of the same build, so a missing
    // tool looked like a bug in that tool. Sorted, a truncating client always loses
    // the same niche tail.
    //
    // No `icons` here at any protocol version: see iconDataUri() above.
    QJsonArray listTools(int accessLevel, const QString& protocolVersion) const
    {
        static const char* levelNames[] = {"Monitor", "Control", "Full"};
        const bool emitTitle = protocolVersion >= QStringLiteral("2025-06-18");
        const bool emitSchemaDialect = protocolVersion >= QStringLiteral("2025-11-25");

        QList<const McpToolDefinition*> ordered;
        ordered.reserve(m_tools.size());
        for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it)
            ordered.append(&it.value());
        std::sort(ordered.begin(), ordered.end(),
                  [](const McpToolDefinition* a, const McpToolDefinition* b) {
                      if (a->tier != b->tier) return a->tier < b->tier;
                      return a->name < b->name;
                  });

        QJsonArray result;
        for (const McpToolDefinition* toolPtr : ordered) {
            const auto& tool = *toolPtr;
            // A merged tool is "disabled" only when EVERY one of its verbs is out of
            // reach; a client at Control level can still call `bag` with action=list.
            int required = tool.actions.isEmpty() ? categoryMinLevel(tool.category)
                                                  : leastRestrictiveLevel(tool.actions);

            QJsonObject toolJson;
            toolJson["name"] = tool.name;
            if (emitTitle) {
                // MCP 2025-06-18: human-readable display name distinct from the
                // programmatic `name`. Auto-derived from snake_case.
                toolJson["title"] = McpRegistryHelpers::deriveTitle(tool.name);
            }
            if (required > accessLevel) {
                int reqClamped = qBound(0, required, 2);
                toolJson["description"] = QString("[DISABLED — requires '%1' access level in Settings > AI > MCP] ")
                    .arg(levelNames[reqClamped]) + tool.description;
            } else {
                toolJson["description"] = tool.description;
            }
            toolJson["inputSchema"] = emitSchemaDialect
                ? McpRegistryHelpers::withJsonSchemaDialect(tool.inputSchema)
                : tool.inputSchema;

            result.append(toolJson);
        }
        return result;
    }

    // Call a tool, checking access level.
    // Arguments are normalized against the tool's input schema before dispatch —
    // MCP clients may send integers as strings (especially after the confirmation
    // round-trip where args are serialized to JSON text and re-parsed).
    // `failureOut`, when supplied, classifies a failure so the caller can pick a
    // JSON-RPC code without reading `errorOut`'s wording.
    QJsonObject callTool(const QString& name, const QJsonObject& arguments,
                         int accessLevel, QString& errorOut,
                         McpRegistryFailure* failureOut = nullptr) const
    {
        if (failureOut)
            *failureOut = McpRegistryFailure::None;
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) {
            errorOut = "Unknown tool: " + name;
            if (failureOut) *failureOut = McpRegistryFailure::NotFound;
            return {};
        }
        const auto& tool = it.value();
        if (tool.isAsync || !tool.handler) {
            errorOut = "Tool is async, use callAsyncTool(): " + name;
            if (failureOut) *failureOut = McpRegistryFailure::WrongDispatch;
            return {};
        }
        if (categoryMinLevel(categoryFor(name, arguments)) > accessLevel) {
            errorOut = "Access level insufficient";
            if (failureOut) *failureOut = McpRegistryFailure::AccessDenied;
            return {};
        }
        return tool.handler(normalizeArguments(arguments, tool.inputSchema));
    }

    // Call an async tool, checking access level. Returns true if dispatched.
    // By convention, each handler must invoke respond() on the main thread
    // via QMetaObject::invokeMethod(qApp, ..., Qt::QueuedConnection).
    // The registry does not enforce this — it is the handler's responsibility.
    bool callAsyncTool(const QString& name, const QJsonObject& arguments,
                       int accessLevel, QString& errorOut,
                       std::function<void(QJsonObject)> respond,
                       McpRegistryFailure* failureOut = nullptr) const
    {
        if (failureOut)
            *failureOut = McpRegistryFailure::None;
        auto it = m_tools.constFind(name);
        if (it == m_tools.constEnd()) {
            errorOut = "Unknown tool: " + name;
            if (failureOut) *failureOut = McpRegistryFailure::NotFound;
            return false;
        }
        const auto& tool = it.value();
        if (!tool.isAsync || !tool.asyncHandler) {
            errorOut = "Tool is not async: " + name;
            if (failureOut) *failureOut = McpRegistryFailure::WrongDispatch;
            return false;
        }
        if (categoryMinLevel(categoryFor(name, arguments)) > accessLevel) {
            errorOut = "Access level insufficient";
            if (failureOut) *failureOut = McpRegistryFailure::AccessDenied;
            return false;
        }
        tool.asyncHandler(normalizeArguments(arguments, tool.inputSchema), std::move(respond));
        return true;
    }

    bool hasTool(const QString& name) const { return m_tools.contains(name); }

    bool isAsyncTool(const QString& name) const
    {
        auto it = m_tools.constFind(name);
        return (it != m_tools.constEnd()) && it.value().isAsync;
    }

    // Returns the category of a tool ("read", "control", "settings") or empty string
    QString toolCategory(const QString& name) const
    {
        auto it = m_tools.constFind(name);
        return (it != m_tools.constEnd()) ? it.value().category : QString();
    }

private:
    // Coerce string-typed values to the type declared in the tool's inputSchema.
    // MCP clients may send "123" instead of 123 after a confirmation round-trip.
    static QJsonObject normalizeArguments(const QJsonObject& args, const QJsonObject& schema)
    {
        QJsonObject properties = schema["properties"].toObject();
        if (properties.isEmpty()) return args;

        QJsonObject normalized = args;
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (!it.value().isString()) continue;  // only coerce strings
            QJsonObject prop = properties[it.key()].toObject();
            QString type = prop["type"].toString();
            if (type == "integer") {
                bool ok;
                qint64 v = it.value().toString().toLongLong(&ok);
                if (ok) normalized[it.key()] = v;
            } else if (type == "number") {
                bool ok;
                double v = it.value().toString().toDouble(&ok);
                if (ok) normalized[it.key()] = v;
            } else if (type == "boolean") {
                QString s = it.value().toString().toLower();
                if (s == "true") normalized[it.key()] = true;
                else if (s == "false") normalized[it.key()] = false;
            }
        }
        return normalized;
    }

    static int categoryMinLevel(const QString& category)
    {
        if (category == "read") return 0;
        if (category == "control") return 1;
        if (category == "settings") return 2;
        return 3; // unknown category — deny
    }

    // The strictest verb of a merged tool. This is what an unresolvable `action`
    // gets gated as, and what legacy single-category readers see.
    static QString strictestCategory(const QVector<McpToolAction>& actions)
    {
        // No actions at all is a registration bug, and the two derivations must not
        // disagree about it: leastRestrictiveLevel() denies an empty list, so this
        // returns an unrecognised category, which categoryMinLevel() also denies.
        // Seeding with "read" instead made an actionless tool list as DISABLED while
        // passing the gate at Monitor.
        if (actions.isEmpty()) return QString();
        QString strictest = QStringLiteral("read");
        for (const McpToolAction& a : actions)
            if (categoryMinLevel(a.category) > categoryMinLevel(strictest))
                strictest = a.category;
        return strictest;
    }

    // The access level at which a merged tool becomes useful at all — used only to
    // decide whether to stamp the "[DISABLED — …]" prefix on its listing.
    static int leastRestrictiveLevel(const QVector<McpToolAction>& actions)
    {
        int lowest = 3;
        for (const McpToolAction& a : actions)
            lowest = qMin(lowest, categoryMinLevel(a.category));
        return lowest;
    }

    QHash<QString, McpToolDefinition> m_tools;
};
