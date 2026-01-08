#include "profileconverter.h"
#include "profile.h"
#include "recipeanalyzer.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDebug>

ProfileConverter::ProfileConverter(QObject* parent)
    : QObject(parent)
{
}

QString ProfileConverter::detectDE1AppProfilesPath() const
{
    // Common locations for DE1 app profiles
    QStringList possiblePaths = {
#ifdef Q_OS_WIN
        "C:/code/de1app/de1plus/profiles",
        QDir::homePath() + "/de1app/de1plus/profiles",
        QDir::homePath() + "/Documents/de1app/de1plus/profiles",
#endif
#ifdef Q_OS_ANDROID
        "/sdcard/de1plus/profiles",
        "/storage/emulated/0/de1plus/profiles",
#endif
        QDir::homePath() + "/de1plus/profiles",
        QDir::homePath() + "/Documents/de1plus/profiles",
    };

    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            // Check if it contains .tcl files
            QStringList tclFiles = dir.entryList(QStringList() << "*.tcl", QDir::Files);
            if (!tclFiles.isEmpty()) {
                qDebug() << "ProfileConverter: Found DE1 app profiles at" << path
                         << "with" << tclFiles.size() << "profiles";
                return path;
            }
        }
    }

    return QString();
}

bool ProfileConverter::convertProfiles(const QString& sourceDir, const QString& destDir, bool overwriteExisting)
{
    if (m_converting) {
        emit conversionError("Conversion already in progress");
        return false;
    }

    QDir source(sourceDir);
    if (!source.exists()) {
        emit conversionError("Source directory does not exist: " + sourceDir);
        return false;
    }

    QDir dest(destDir);
    if (!dest.exists()) {
        if (!dest.mkpath(".")) {
            emit conversionError("Cannot create destination directory: " + destDir);
            return false;
        }
    }

    // Find all .tcl files
    m_pendingFiles.clear();
    QDirIterator it(sourceDir, QStringList() << "*.tcl", QDir::Files);
    while (it.hasNext()) {
        m_pendingFiles.append(it.next());
    }

    if (m_pendingFiles.isEmpty()) {
        emit conversionError("No .tcl profile files found in source directory");
        return false;
    }

    m_destDir = destDir;
    m_overwriteExisting = overwriteExisting;
    m_totalFiles = m_pendingFiles.size();
    m_processedFiles = 0;
    m_successCount = 0;
    m_errorCount = 0;
    m_skippedCount = 0;
    m_errors.clear();
    m_converting = true;

    setStatus(QString("Converting %1 profiles...").arg(m_totalFiles));
    emit isConvertingChanged();
    emit progressChanged();

    // Process files asynchronously to keep UI responsive
    QTimer::singleShot(0, this, &ProfileConverter::processNextFile);

    return true;
}

void ProfileConverter::processNextFile()
{
    if (m_pendingFiles.isEmpty()) {
        // Conversion complete - update resources.qrc
        updateResourcesQrc();

        m_converting = false;
        emit isConvertingChanged();

        QString statusMsg = QString("Complete: %1 converted").arg(m_successCount);
        if (m_skippedCount > 0) {
            statusMsg += QString(", %1 skipped").arg(m_skippedCount);
        }
        if (m_errorCount > 0) {
            statusMsg += QString(", %1 errors").arg(m_errorCount);
        }
        setStatus(statusMsg);
        emit conversionComplete(m_successCount, m_errorCount);
        return;
    }

    QString tclPath = m_pendingFiles.takeFirst();
    QString filename = QFileInfo(tclPath).fileName();
    m_currentFile = filename;
    emit currentFileChanged();

    // Load and convert the profile
    Profile profile = Profile::loadFromTclFile(tclPath);

    if (profile.title().isEmpty() && profile.steps().isEmpty()) {
        QString error = QString("Failed to parse: %1").arg(filename);
        m_errors.append(error);
        m_errorCount++;
        qWarning() << "ProfileConverter:" << error;
    } else {
        // Generate output filename from title
        QString outputFilename = generateFilename(profile.title());
        QString outputPath = m_destDir + "/" + outputFilename + ".json";

        // Check if file already exists
        if (QFile::exists(outputPath) && !m_overwriteExisting) {
            m_skippedCount++;
            qDebug() << "ProfileConverter: Skipped" << filename << "(already exists)";
        } else {
            // Try to convert to D-Flow (recipe) mode if the profile structure is simple enough
            // Complex profiles (like Damian's LRv3 with 8 frames) stay as frame-based
            if (RecipeAnalyzer::canConvertToRecipe(profile)) {
                RecipeAnalyzer::convertToRecipeMode(profile);
                qDebug() << "ProfileConverter:" << filename << "→ D-Flow mode";
            } else {
                profile.setRecipeMode(false);
                qDebug() << "ProfileConverter:" << filename << "→ Advanced mode (complex profile)";
            }

            if (profile.saveToFile(outputPath)) {
                m_successCount++;
                qDebug() << "ProfileConverter: Converted" << filename << "→" << outputFilename + ".json";
            } else {
                QString error = QString("Failed to save: %1").arg(outputFilename);
                m_errors.append(error);
                m_errorCount++;
                qWarning() << "ProfileConverter:" << error;
            }
        }
    }

    m_processedFiles++;
    emit progressChanged();

    // Update status periodically
    if (m_processedFiles % 10 == 0) {
        setStatus(QString("Converting... %1/%2").arg(m_processedFiles).arg(m_totalFiles));
    }

    // Process next file
    QTimer::singleShot(0, this, &ProfileConverter::processNextFile);
}

QString ProfileConverter::generateFilename(const QString& title) const
{
    if (title.isEmpty()) {
        return "unnamed_profile";
    }

    QString filename = title.toLower();

    // Replace special characters and spaces with underscores
    filename.replace(QRegularExpression("[^a-z0-9]+"), "_");

    // Remove leading/trailing underscores
    filename.replace(QRegularExpression("^_+|_+$"), "");

    // Collapse multiple underscores
    filename.replace(QRegularExpression("_+"), "_");

    // Limit length
    if (filename.length() > 50) {
        filename = filename.left(50);
    }

    if (filename.isEmpty()) {
        filename = "profile";
    }

    return filename;
}

void ProfileConverter::setStatus(const QString& message)
{
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusMessageChanged();
    }
}

void ProfileConverter::updateResourcesQrc()
{
    // Find resources.qrc - it's in the parent directory of profiles
    QDir profilesDir(m_destDir);
    QString qrcPath = profilesDir.absolutePath() + "/../resources.qrc";

    QFile qrcFile(qrcPath);
    if (!qrcFile.exists()) {
        qWarning() << "ProfileConverter: resources.qrc not found at" << qrcPath;
        return;
    }

    // Read current qrc content
    if (!qrcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ProfileConverter: Cannot read resources.qrc";
        return;
    }
    QString content = QTextStream(&qrcFile).readAll();
    qrcFile.close();

    // Get list of all profile JSON files
    QDir dir(m_destDir);
    QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);

    if (jsonFiles.isEmpty()) {
        qWarning() << "ProfileConverter: No JSON files found in" << m_destDir;
        return;
    }

    // Build the new profiles section
    QString newProfilesSection;
    for (const QString& filename : jsonFiles) {
        newProfilesSection += QString("        <file>profiles/%1</file>\n").arg(filename);
    }

    // Find and replace the profiles section in the qrc file
    // Look for the pattern: profiles/*.json entries between other sections
    QRegularExpression profilesRe(
        "(<!-- Profiles -->\\s*\\n)"
        "((?:\\s*<file>profiles/[^<]+</file>\\s*\\n)*)"
        "(\\s*<!-- )");

    QRegularExpressionMatch match = profilesRe.match(content);
    if (match.hasMatch()) {
        // Replace the profiles section
        QString replacement = match.captured(1) + newProfilesSection + match.captured(3);
        content.replace(match.capturedStart(), match.capturedLength(), replacement);
    } else {
        // Try simpler approach: replace all profile entries
        QRegularExpression simpleRe("<file>profiles/[^<]+\\.json</file>");
        QRegularExpressionMatchIterator it = simpleRe.globalMatch(content);

        if (it.hasNext()) {
            // Find the first and last profile entry positions
            int firstPos = -1, lastEnd = 0;
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                if (firstPos < 0) firstPos = m.capturedStart();
                lastEnd = m.capturedEnd();
            }

            if (firstPos >= 0) {
                // Find the line start of first entry and line end of last entry
                int lineStart = content.lastIndexOf('\n', firstPos) + 1;
                int lineEnd = content.indexOf('\n', lastEnd);
                if (lineEnd < 0) lineEnd = content.length();

                // Replace the entire profiles block
                content = content.left(lineStart) + newProfilesSection.trimmed() + content.mid(lineEnd);
            }
        } else {
            qWarning() << "ProfileConverter: Could not find profiles section in resources.qrc";
            return;
        }
    }

    // Write updated qrc file
    if (!qrcFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "ProfileConverter: Cannot write resources.qrc";
        return;
    }
    QTextStream out(&qrcFile);
    out << content;
    qrcFile.close();

    qDebug() << "ProfileConverter: Updated resources.qrc with" << jsonFiles.size() << "profiles";
}
