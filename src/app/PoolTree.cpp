#include "PoolTree.h"

#include "AttributeInfo.h"
#include "ObjectIcons.h"

#include "vtcore/Hierarchy.h"
#include "vtrender/VtRenderer.h"

#include <QHeaderView>
#include <QMenu>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace {

using T = vt::ObjectType;

enum Role {
    ObjectRole = Qt::UserRole,
    ParentRole,
    IndexRole,
    KeyRole,
};

// Top level groups of the hierarchy tab in display order.
const std::vector<T> kTopLevelTypes = {T::WorkingSet,  T::DataMask,  T::AlarmMask,
                                       T::SoftKeyMask, T::WindowMask, T::KeyGroup,
                                       T::AuxiliaryFunctionType2, T::AuxiliaryInputType2,
                                       T::AuxiliaryFunctionType1, T::AuxiliaryInputType1};

} // namespace

PoolTree::PoolTree(EditorContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_tabs(new QTabWidget(this))
    , m_hierarchy(new QTreeWidget)
    , m_byType(new QTreeWidget)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);
    m_tabs->addTab(m_hierarchy, tr("Masks"));
    m_tabs->addTab(m_byType, tr("Objects"));

    for (QTreeWidget* tree : {m_hierarchy, m_byType}) {
        tree->setHeaderHidden(true);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->setExpandsOnDoubleClick(false);
        connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) { activate(item, false); });
        connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) { activate(item, true); });
        connect(tree, &QTreeWidget::customContextMenuRequested, this,
                [this, tree](const QPoint& pos) { showMenu(tree, pos); });
    }

    connect(m_context, &EditorContext::poolChanged, this, &PoolTree::scheduleRebuild);
    connect(m_context, &EditorContext::documentChanged, this, &PoolTree::scheduleRebuild);
    connect(m_context, &EditorContext::selectionChanged, this, &PoolTree::syncSelection);
    scheduleRebuild();
}

void PoolTree::scheduleRebuild()
{
    if (!m_rebuildPending) {
        m_rebuildPending = true;
        QTimer::singleShot(0, this, &PoolTree::rebuild);
    }
}

QTreeWidgetItem* PoolTree::createItem(const vt::VtObject& object, vt::ObjectId parent, int index)
{
    auto* item = new QTreeWidgetItem({ui::objectDisplayName(object)});
    item->setIcon(0, ui::objectIcon(object.type));
    item->setData(0, ObjectRole, object.id);
    item->setData(0, ParentRole, parent);
    item->setData(0, IndexRole, index);
    item->setData(0, KeyRole, QStringLiteral("%1/%2/%3").arg(parent).arg(index).arg(object.id));
    item->setToolTip(0, ui::typeLabel(object.type));
    return item;
}

void PoolTree::addHierarchy(QTreeWidgetItem* parentItem, const vt::VtObject& object, vt::ObjectId parent, int index,
                            std::set<vt::ObjectId>& path)
{
    QTreeWidgetItem* item = createItem(object, parent, index);
    parentItem->addChild(item);
    if (!path.insert(object.id).second) {
        item->setText(0, item->text(0) + tr(" (circular)"));
        return;
    }
    const vt::ObjectPool& pool = m_context->pool();
    if (vt::containmentList(object.type) == vt::ListKind::References) {
        for (std::size_t i = 0; i < object.references.size(); ++i) {
            if (const vt::VtObject* child = pool.find(object.references[i])) {
                addHierarchy(item, *child, object.id, static_cast<int>(i), path);
            }
        }
    }
    for (std::size_t i = 0; i < object.children.size(); ++i) {
        if (const vt::VtObject* child = pool.find(object.children[i].id)) {
            addHierarchy(item, *child, object.id, static_cast<int>(i), path);
        }
    }
    if (object.type == T::DataMask || object.type == T::AlarmMask) {
        if (const vt::VtObject* keys = pool.find(static_cast<vt::ObjectId>(object.get("softKeyMask")))) {
            QTreeWidgetItem* keyItem = createItem(*keys, vt::kNullObjectId, -1);
            keyItem->setText(0, tr("Soft keys: %1").arg(keyItem->text(0)));
            keyItem->setData(0, KeyRole, QStringLiteral("skm/%1/%2").arg(object.id).arg(keys->id));
            item->addChild(keyItem);
        }
    }
    path.erase(object.id);
}

void PoolTree::rebuild()
{
    m_rebuildPending = false;
    const std::set<QString> expandedHierarchy = expandedKeys(m_hierarchy);
    const std::set<QString> expandedTypes = expandedKeys(m_byType);
    m_hierarchy->clear();
    m_byType->clear();
    if (m_context->document() == nullptr) {
        return;
    }
    const vt::ObjectPool& pool = m_context->pool();

    for (T type : kTopLevelTypes) {
        auto* group = new QTreeWidgetItem(m_hierarchy, {ui::typeLabel(type)});
        group->setIcon(0, ui::objectIcon(type));
        group->setData(0, ObjectRole, static_cast<int>(vt::kNullObjectId));
        group->setData(0, KeyRole, QStringLiteral("group/%1").arg(static_cast<int>(type)));
        group->setFlags(Qt::ItemIsEnabled);
        for (const vt::VtObject& object : pool.objects()) {
            if (object.type == type) {
                std::set<vt::ObjectId> path;
                addHierarchy(group, object, vt::kNullObjectId, -1, path);
            }
        }
        if (group->childCount() == 0) {
            delete group;
        }
    }

    std::map<int, std::vector<const vt::VtObject*>> byType;
    for (const vt::VtObject& object : pool.objects()) {
        byType[static_cast<int>(object.type)].push_back(&object);
    }
    for (auto& [type, objects] : byType) {
        auto* group = new QTreeWidgetItem(
            m_byType, {QStringLiteral("%1 (%2)").arg(ui::typeLabel(static_cast<T>(type))).arg(objects.size())});
        group->setIcon(0, ui::objectIcon(static_cast<T>(type)));
        group->setData(0, ObjectRole, static_cast<int>(vt::kNullObjectId));
        group->setData(0, KeyRole, QStringLiteral("type/%1").arg(type));
        group->setFlags(Qt::ItemIsEnabled);
        std::sort(objects.begin(), objects.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
        for (const vt::VtObject* object : objects) {
            group->addChild(createItem(*object, vt::kNullObjectId, -1));
        }
    }

    if (expandedHierarchy.empty()) {
        m_hierarchy->expandToDepth(1);
    } else {
        restoreExpanded(m_hierarchy, expandedHierarchy);
    }
    restoreExpanded(m_byType, expandedTypes);
    syncSelection();
}

std::set<QString> PoolTree::expandedKeys(QTreeWidget* tree) const
{
    std::set<QString> keys;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->isExpanded()) {
            keys.insert((*it)->data(0, KeyRole).toString());
        }
        ++it;
    }
    return keys;
}

void PoolTree::restoreExpanded(QTreeWidget* tree, const std::set<QString>& keys)
{
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if (keys.count((*it)->data(0, KeyRole).toString()) != 0) {
            (*it)->setExpanded(true);
        }
        ++it;
    }
}

void PoolTree::syncSelection()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    const Selection s = m_context->selection();
    for (QTreeWidget* tree : {m_hierarchy, m_byType}) {
        QTreeWidgetItem* best = nullptr;
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QTreeWidgetItem* item = *it;
            if (item->data(0, ObjectRole).toInt() == s.object) {
                const bool exact = item->data(0, ParentRole).toInt() == s.parent &&
                                   item->data(0, IndexRole).toInt() == s.index;
                if (best == nullptr || exact) {
                    best = item;
                }
                if (exact) {
                    break;
                }
            }
            ++it;
        }
        tree->setCurrentItem(best);
        if (best != nullptr) {
            tree->scrollToItem(best);
        } else {
            tree->clearSelection();
        }
    }
    m_syncing = false;
}

void PoolTree::activate(QTreeWidgetItem* item, bool open)
{
    if (item == nullptr) {
        return;
    }
    const auto id = static_cast<vt::ObjectId>(item->data(0, ObjectRole).toInt());
    const vt::VtObject* object = m_context->pool().find(id);
    if (object == nullptr) {
        return;
    }
    // Show the mask an item belongs to when it is picked in the hierarchy.
    if (item->treeWidget() == m_hierarchy) {
        QTreeWidgetItem* top = item;
        while (top->parent() != nullptr && top->parent()->data(0, ObjectRole).toInt() != vt::kNullObjectId) {
            top = top->parent();
        }
        const auto rootId = static_cast<vt::ObjectId>(top->data(0, ObjectRole).toInt());
        const vt::VtObject* root = m_context->pool().find(rootId);
        if (root != nullptr && vtr::Renderer::isViewRoot(root->type) && root->type != T::WorkingSet) {
            m_context->setViewRoot(rootId);
        }
    }
    if (open && vtr::Renderer::isViewRoot(object->type)) {
        m_context->setViewRoot(id);
    }
    m_syncing = true;
    m_context->select({id, static_cast<vt::ObjectId>(item->data(0, ParentRole).toInt()),
                       item->data(0, IndexRole).toInt()});
    m_syncing = false;
}

void PoolTree::showMenu(QTreeWidget* tree, const QPoint& pos)
{
    QTreeWidgetItem* item = tree->itemAt(pos);
    if (item != nullptr) {
        activate(item, false);
    }
    QMenu menu(this);
    const auto id = item != nullptr ? static_cast<vt::ObjectId>(item->data(0, ObjectRole).toInt()) : vt::kNullObjectId;
    const vt::VtObject* object = m_context->document() != nullptr ? m_context->pool().find(id) : nullptr;
    if (object != nullptr && vtr::Renderer::isViewRoot(object->type)) {
        menu.addAction(tr("Open in canvas"), this, [this, id] { m_context->setViewRoot(id); });
    }
    if (object != nullptr) {
        menu.addAction(tr("Delete object from pool"), this, [this, id] { emit deleteRequested(id); });
        menu.addSeparator();
    }
    menu.addActions(m_contextActions);
    menu.exec(tree->viewport()->mapToGlobal(pos));
}
