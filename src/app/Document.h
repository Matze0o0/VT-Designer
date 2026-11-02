#pragma once

#include "vtcore/ProjectFile.h"

#include <QObject>
#include <QString>
#include <QUndoStack>

#include <optional>
#include <vector>

// State of one object slot in the pool: present (with its position in the pool) or absent.
struct ObjectState {
    vt::ObjectId id = vt::kNullObjectId;
    std::optional<vt::VtObject> object;
    std::size_t index = 0;
};

// An open project. Every change to the pool goes through commit() so it can be undone.
class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(vt::Project project = {}, QString filePath = {}, QObject* parent = nullptr);

    const vt::Project& project() const { return m_project; }
    const vt::ObjectPool& pool() const { return m_project.pool; }
    QUndoStack* undoStack() { return &m_undoStack; }

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isModified() const { return !m_undoStack.isClean() || m_settingsModified; }
    void markSaved(const QString& filePath);

    // Replaces or adds the given objects and removes the given IDs as one undoable step.
    // Consecutive commits with the same non-zero mergeKey touching the same objects become one step.
    void commit(const QString& text, const std::vector<vt::VtObject>& upserts,
                const std::vector<vt::ObjectId>& removals = {}, int mergeKey = 0);

    // Removes objects and every reference to them from the rest of the pool.
    void removeObjects(const std::vector<vt::ObjectId>& ids, const QString& text);

    // Removes the entry `index` from the parent's children/references; objects that become unused
    // and only lived inside the removed part of the hierarchy are deleted as well.
    void removePlacement(vt::ObjectId parent, int index, const QString& text);

    // Changes an object ID and updates all references to it.
    bool renumber(vt::ObjectId from, vt::ObjectId to);

    void setVtVersion(vt::VtVersion version);
    void setExportOptions(const vt::ExportOptions& options);
    void setDesignerSettings(const vt::DesignerSettings& settings);

    // Used by the undo commands.
    void applyStates(const std::vector<ObjectState>& states);
    std::vector<ObjectState> statesOf(const std::vector<vt::ObjectId>& ids) const;

signals:
    void poolChanged();
    void settingsChanged();
    void modifiedChanged(bool modified);

private:
    vt::Project m_project;
    QString m_filePath;
    QUndoStack m_undoStack;
    bool m_settingsModified = false;
};
