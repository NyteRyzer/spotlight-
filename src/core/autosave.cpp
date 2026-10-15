#include "core/autosave.h"

#include "core/project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace spotlight {

AutosaveManager::AutosaveManager(Project *project, const QString &directory, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_dir(directory)
{
    QDir().mkpath(m_dir);
    m_timer.setInterval(120 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { autosaveNow(); });
    m_timer.start();

    // A save, open or new project makes the previous autosave obsolete.
    connect(project, &Project::modifiedChanged, this, [this](bool modified) {
        if (!modified)
            discardCurrent();
    });
}

QString AutosaveManager::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave");
}

void AutosaveManager::setIntervalSeconds(int seconds)
{
    m_timer.setInterval(std::max(10, seconds) * 1000);
}

int AutosaveManager::intervalSeconds() const
{
    return m_timer.interval() / 1000;
}

void AutosaveManager::setEnabled(bool enabled)
{
    if (enabled)
        m_timer.start();
    else
        m_timer.stop();
}

QString AutosaveManager::autosavePathFor(const QString &projectId) const
{
    return m_dir + QLatin1Char('/') + projectId + QStringLiteral(".spotlight");
}

bool AutosaveManager::autosaveNow()
{
    if (!m_project->isModified() || m_project->revision() == m_lastSavedRevision)
        return false;

    const QString id = m_project->data().id.toString(QUuid::WithoutBraces);
    const QString path = autosavePathFor(id);
    QString error;
    if (!saveProjectFile(m_project->data(), path, &error))
        return false;

    QJsonObject meta;
    meta["originalPath"] = m_project->filePath();
    meta["name"] = m_project->displayName();
    meta["savedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSaveFile mf(m_dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (mf.open(QIODevice::WriteOnly)) {
        mf.write(QJsonDocument(meta).toJson());
        mf.commit();
    }

    m_lastSavedRevision = m_project->revision();
    emit autosaved(path);
    return true;
}

void AutosaveManager::discardCurrent()
{
    const QString id = m_project->data().id.toString(QUuid::WithoutBraces);
    QFile::remove(autosavePathFor(id));
    QFile::remove(m_dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    m_lastSavedRevision = m_project->revision();
}

QVector<RecoveryEntry> AutosaveManager::findRecoverable() const
{
    QVector<RecoveryEntry> result;
    const QDir dir(m_dir);
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.spotlight")}, QDir::Files)) {
        RecoveryEntry e;
        e.autosavePath = fi.absoluteFilePath();
        e.savedAt = fi.lastModified();
        e.projectName = fi.completeBaseName();
        QFile mf(dir.filePath(fi.completeBaseName() + QStringLiteral(".json")));
        if (mf.open(QIODevice::ReadOnly)) {
            const QJsonObject meta = QJsonDocument::fromJson(mf.readAll()).object();
            e.originalPath = meta["originalPath"].toString();
            e.projectName = meta["name"].toString(e.projectName);
            const QDateTime t = QDateTime::fromString(meta["savedAt"].toString(), Qt::ISODate);
            if (t.isValid())
                e.savedAt = t.toLocalTime();
        }
        result.append(e);
    }
    std::sort(result.begin(), result.end(),
              [](const RecoveryEntry &a, const RecoveryEntry &b) { return a.savedAt > b.savedAt; });
    return result;
}

void AutosaveManager::discard(const RecoveryEntry &e)
{
    QFile::remove(e.autosavePath);
    const QFileInfo fi(e.autosavePath);
    QFile::remove(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".json"));
}

} // namespace spotlight
