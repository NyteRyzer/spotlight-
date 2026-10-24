#include "core/project.h"

#include "core/titles.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QUndoCommand>

namespace spotlight {

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Project *project, ProjectData before, ProjectData after, const QString &text,
                    const QString &mergeKey)
        : QUndoCommand(text)
        , m_project(project)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeKey(mergeKey)
    {
    }
    void undo() override { m_project->setData(m_before); }
    void redo() override { m_project->setData(m_after); }
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const SnapshotCommand *>(other);
        if (o->m_mergeKey != m_mergeKey)
            return false;
        m_after = o->m_after;
        return true;
    }

private:
    Project *m_project;
    ProjectData m_before;
    ProjectData m_after;
    QString m_mergeKey;
};

namespace {

// Cheap structural equality via serialization. Edits are user driven, so this
// runs at most a few times per second.
bool sameData(const ProjectData &a, const ProjectData &b)
{
    return a.toJson() == b.toJson();
}

} // namespace

Project::Project(QObject *parent)
    : QObject(parent)
    , m_data(ProjectData::createDefault())
{
    m_undo.setUndoLimit(200);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool) { emit modifiedChanged(isModified()); });
}

bool Project::edit(const QString &text, const std::function<void(ProjectData &)> &fn, const QString &mergeKey)
{
    ProjectData after = m_data;
    fn(after);
    if (sameData(m_data, after))
        return false;
    m_undo.push(new SnapshotCommand(this, m_data, after, text, mergeKey));
    return true;
}

void Project::setData(const ProjectData &data)
{
    m_data = data;
    ++m_revision;
    emit changed();
}

void Project::reset(const ProjectData &data, const QString &filePath)
{
    m_undo.clear();
    m_forceModified = false;
    setFilePath(filePath);
    setData(data);
    m_undo.setClean();
    emit modifiedChanged(isModified());
}

void Project::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit filePathChanged(path);
}

QString Project::displayName() const
{
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).completeBaseName();
    return m_data.name;
}

void Project::markSaved()
{
    m_forceModified = false;
    m_undo.setClean();
    emit modifiedChanged(isModified());
}

void Project::markModified()
{
    m_forceModified = true;
    ++m_revision;
    emit modifiedChanged(true);
}

bool Project::save(QString *error)
{
    if (m_filePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Project has no file name.");
        return false;
    }
    return saveAs(m_filePath, error);
}

bool Project::saveAs(const QString &path, QString *error)
{
    ProjectData copy = m_data;
    copy.name = QFileInfo(path).completeBaseName();
    if (!saveProjectFile(copy, path, error))
        return false;
    m_data.name = copy.name;
    setFilePath(path);
    markSaved();
    return true;
}

bool Project::open(const QString &path, QString *error)
{
    ProjectData d;
    if (!loadProjectFile(path, &d, error))
        return false;
    // Titles are generated images; recreate any that didn't travel with the project.
    titles::restoreMissingTitles(d.media);
    reset(d, path);
    return true;
}

} // namespace spotlight
