#pragma once

#include "EditorContext.h"

#include <QWidget>

#include <set>

class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Two views of the pool: the hierarchy below the top level objects, and all objects grouped by type.
class PoolTree : public QWidget {
    Q_OBJECT

public:
    explicit PoolTree(EditorContext* context, QWidget* parent = nullptr);

    void setContextActions(const QList<QAction*>& actions) { m_contextActions = actions; }

signals:
    void deleteRequested(vt::ObjectId id);

private:
    void scheduleRebuild();
    void rebuild();
    void syncSelection();
    void addHierarchy(QTreeWidgetItem* parentItem, const vt::VtObject& object, vt::ObjectId parent, int index,
                      std::set<vt::ObjectId>& path);
    QTreeWidgetItem* createItem(const vt::VtObject& object, vt::ObjectId parent, int index);
    void activate(QTreeWidgetItem* item, bool open);
    void showMenu(QTreeWidget* tree, const QPoint& pos);
    std::set<QString> expandedKeys(QTreeWidget* tree) const;
    void restoreExpanded(QTreeWidget* tree, const std::set<QString>& keys);

    EditorContext* m_context;
    QTabWidget* m_tabs;
    QTreeWidget* m_hierarchy;
    QTreeWidget* m_byType;
    bool m_rebuildPending = false;
    bool m_syncing = false;
    QList<QAction*> m_contextActions;
};
