#include "Document.h"

#include "vtcore/Hierarchy.h"
#include "vtcore/PoolEditing.h"

#include <QFileInfo>
#include <QUndoCommand>

#include <algorithm>
#include <map>
#include <set>

namespace {

class PoolCommand : public QUndoCommand {
public:
    PoolCommand(Document* document, const QString& text, std::vector<ObjectState> before,
                std::vector<ObjectState> after, int mergeKey)
        : QUndoCommand(text)
        , m_document(document)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeKey(mergeKey)
    {
    }

    void redo() override { m_document->applyStates(m_after); }
    void undo() override { m_document->applyStates(m_before); }
    int id() const override { return m_mergeKey != 0 ? m_mergeKey : -1; }

    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* next = dynamic_cast<const PoolCommand*>(other);
        if (next == nullptr || next->m_mergeKey != m_mergeKey || ids(next->m_after) != ids(m_after)) {
            return false;
        }
        m_after = next->m_after;
        return true;
    }

private:
    static std::set<vt::ObjectId> ids(const std::vector<ObjectState>& states)
    {
        std::set<vt::ObjectId> result;
        for (const ObjectState& s : states) {
            result.insert(s.id);
        }
        return result;
    }

    Document* m_document;
    std::vector<ObjectState> m_before;
    std::vector<ObjectState> m_after;
    int m_mergeKey;
};

// Types that only exist as part of something else and may be cleaned up with it.
bool isDisposable(vt::ObjectType type)
{
    switch (type) {
    case vt::ObjectType::Container:
    case vt::ObjectType::Button:
    case vt::ObjectType::Key:
    case vt::ObjectType::InputBoolean:
    case vt::ObjectType::InputString:
    case vt::ObjectType::InputNumber:
    case vt::ObjectType::InputList:
    case vt::ObjectType::OutputString:
    case vt::ObjectType::OutputNumber:
    case vt::ObjectType::OutputList:
    case vt::ObjectType::OutputLine:
    case vt::ObjectType::OutputRectangle:
    case vt::ObjectType::OutputEllipse:
    case vt::ObjectType::OutputPolygon:
    case vt::ObjectType::OutputMeter:
    case vt::ObjectType::OutputLinearBarGraph:
    case vt::ObjectType::OutputArchedBarGraph:
    case vt::ObjectType::PictureGraphic:
    case vt::ObjectType::ObjectPointer:
    case vt::ObjectType::Animation:
    case vt::ObjectType::GraphicsContext: return true;
    default: return false;
    }
}

} // namespace

Document::Document(vt::Project project, QString filePath, QObject* parent)
    : QObject(parent)
    , m_project(std::move(project))
    , m_filePath(std::move(filePath))
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, [this] { emit modifiedChanged(isModified()); });
}

QString Document::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

void Document::markSaved(const QString& filePath)
{
    m_filePath = filePath;
    m_settingsModified = false;
    m_undoStack.setClean();
    emit modifiedChanged(false);
}

std::vector<ObjectState> Document::statesOf(const std::vector<vt::ObjectId>& ids) const
{
    std::vector<ObjectState> states;
    for (vt::ObjectId id : ids) {
        ObjectState state;
        state.id = id;
        if (const vt::VtObject* object = pool().find(id)) {
            state.object = *object;
            state.index = *pool().indexOf(id);
        } else {
            state.index = pool().size();
        }
        states.push_back(std::move(state));
    }
    return states;
}

void Document::applyStates(const std::vector<ObjectState>& states)
{
    vt::ObjectPool& pool = m_project.pool;

    // Removals first (from the back so indices stay valid), then replacements, then insertions in
    // ascending order so restored objects get their original positions back.
    std::vector<const ObjectState*> inserts;
    std::vector<std::size_t> removalIndices;
    for (const ObjectState& state : states) {
        const std::optional<std::size_t> index = pool.indexOf(state.id);
        if (!state.object) {
            if (index) {
                removalIndices.push_back(*index);
            }
        } else if (!index) {
            inserts.push_back(&state);
        }
    }
    std::sort(removalIndices.rbegin(), removalIndices.rend());
    for (std::size_t index : removalIndices) {
        pool.remove(pool.objects()[index].id);
    }
    for (const ObjectState& state : states) {
        if (state.object) {
            if (vt::VtObject* existing = pool.find(state.id)) {
                *existing = *state.object;
            }
        }
    }
    std::sort(inserts.begin(), inserts.end(),
              [](const ObjectState* a, const ObjectState* b) { return a->index < b->index; });
    for (const ObjectState* state : inserts) {
        pool.insert(state->index, *state->object);
    }
    emit poolChanged();
}

void Document::commit(const QString& text, const std::vector<vt::VtObject>& upserts,
                      const std::vector<vt::ObjectId>& removals, int mergeKey)
{
    std::vector<vt::ObjectId> ids;
    for (const vt::VtObject& object : upserts) {
        ids.push_back(object.id);
    }
    ids.insert(ids.end(), removals.begin(), removals.end());
    if (ids.empty()) {
        return;
    }

    std::vector<ObjectState> before = statesOf(ids);
    std::vector<ObjectState> after;
    std::size_t appendIndex = pool().size();
    for (const vt::VtObject& object : upserts) {
        const std::optional<std::size_t> index = pool().indexOf(object.id);
        after.push_back({object.id, object, index ? *index : appendIndex++});
    }
    for (vt::ObjectId id : removals) {
        after.push_back({id, std::nullopt, 0});
    }

    const bool wasModified = isModified();
    m_undoStack.push(new PoolCommand(this, text, std::move(before), std::move(after), mergeKey));
    if (isModified() != wasModified) {
        emit modifiedChanged(isModified());
    }
}

void Document::removeObjects(const std::vector<vt::ObjectId>& ids, const QString& text)
{
    std::map<vt::ObjectId, vt::VtObject> changed;
    const std::set<vt::ObjectId> removed(ids.begin(), ids.end());
    for (vt::ObjectId id : ids) {
        for (vt::ObjectId user : vt::usersOf(pool(), id)) {
            if (removed.count(user) != 0) {
                continue;
            }
            auto it = changed.find(user);
            if (it == changed.end()) {
                it = changed.emplace(user, *pool().find(user)).first;
            }
            vt::stripReferences(it->second, id);
        }
    }
    std::vector<vt::VtObject> upserts;
    for (auto& [id, object] : changed) {
        upserts.push_back(std::move(object));
    }
    commit(text, upserts, ids);
}

void Document::removePlacement(vt::ObjectId parentId, int index, const QString& text)
{
    const vt::VtObject* parentObject = pool().find(parentId);
    if (parentObject == nullptr || index < 0) {
        return;
    }
    vt::VtObject parent = *parentObject;
    vt::ObjectId removedId = vt::kNullObjectId;
    if (static_cast<std::size_t>(index) < parent.children.size() &&
        vt::containmentList(parent.type) == vt::ListKind::Children) {
        removedId = parent.children[static_cast<std::size_t>(index)].id;
        parent.children.erase(parent.children.begin() + index);
    } else if (static_cast<std::size_t>(index) < parent.references.size()) {
        removedId = parent.references[static_cast<std::size_t>(index)];
        parent.references.erase(parent.references.begin() + index);
    } else {
        return;
    }

    // Work on a scratch pool to find objects that are no longer used anywhere.
    vt::ObjectPool scratch = pool();
    *scratch.find(parentId) = parent;
    std::vector<vt::ObjectId> candidates = vt::containedSubtree(scratch, removedId);
    std::vector<vt::ObjectId> removals;
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
            const vt::VtObject* object = scratch.find(*it);
            if (object != nullptr && isDisposable(object->type) && vt::usersOf(scratch, *it).empty()) {
                removals.push_back(*it);
                scratch.remove(*it);
                candidates.erase(it);
                progress = true;
                break;
            }
        }
    }
    commit(text, {parent}, removals);
}

bool Document::renumber(vt::ObjectId from, vt::ObjectId to)
{
    const vt::VtObject* object = pool().find(from);
    if (object == nullptr || to == vt::kNullObjectId || from == to || pool().contains(to)) {
        return false;
    }
    std::vector<vt::VtObject> upserts;
    for (vt::ObjectId user : vt::usersOf(pool(), from)) {
        if (user == from) {
            continue;
        }
        vt::VtObject copy = *pool().find(user);
        vt::forEachReference(copy, [from, to](vt::ObjectId& id) {
            if (id == from) {
                id = to;
            }
        });
        upserts.push_back(std::move(copy));
    }
    vt::VtObject renamed = *object;
    renamed.id = to;
    vt::forEachReference(renamed, [from, to](vt::ObjectId& id) {
        if (id == from) {
            id = to;
        }
    });
    upserts.push_back(std::move(renamed));
    commit(tr("Change object ID"), upserts, {from});
    return true;
}

void Document::setVtVersion(vt::VtVersion version)
{
    if (m_project.pool.version() == version) {
        return;
    }
    m_project.pool.setVersion(version);
    m_settingsModified = true;
    emit settingsChanged();
    emit poolChanged();
    emit modifiedChanged(true);
}

void Document::setExportOptions(const vt::ExportOptions& options)
{
    m_project.exportOptions = options;
    m_settingsModified = true;
    emit settingsChanged();
    emit modifiedChanged(true);
}

void Document::setDesignerSettings(const vt::DesignerSettings& settings)
{
    if (m_project.designer == settings) {
        return;
    }
    m_project.designer = settings;
    m_settingsModified = true;
    emit settingsChanged();
    emit modifiedChanged(true);
}
