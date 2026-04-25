#pragma once

#include <QString>
#include <QVector>
#include <QPointF>
#include <QVariantMap>
#include "shothistory_types.h"

/**
 * Parser for DE1 app .shot files (Tcl format)
 *
 * These files contain time-series data, metadata, settings, and profile info
 * from shots recorded by the original Decent Espresso tablet app.
 */
class ShotFileParser {
public:
    struct ParseResult {
        bool success = false;
        QString errorMessage;
        ShotRecord record;
    };

    /**
     * Parse a .shot file from its contents
     */
    static ParseResult parse(const QByteArray& fileContents, const QString& filename = QString());

    /**
     * Parse a .shot file from disk
     */
    static ParseResult parseFile(const QString& filePath);

private:
    // Parse Tcl list format: {value1 value2 value3 ...}
    static QVector<double> parseTclList(const QString& listStr);

    // Parse Tcl dictionary format: key1 value1 key2 value2 ...
    static QVariantMap parseTclDict(const QString& dictStr);

    // Extract a top-level key-value pair from the file
    static QString extractValue(const QString& content, const QString& key);

    // Extract a braced block (handles nested braces)
    static QString extractBracedBlock(const QString& content, const QString& key);

    // Convert time + value arrays to QPointF vector
    static QVector<QPointF> toPointVector(const QVector<double>& times, const QVector<double>& values);

    // Parse the embedded JSON profile
    static QString extractProfileJson(const QString& content);

    // Generate UUID from timestamp for deduplication
    static QString generateUuid(qint64 timestamp, const QString& filename);
};
