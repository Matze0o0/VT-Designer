#pragma once

#include "EditorContext.h"

#include <QWidget>

#include <functional>
#include <set>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

// Shows and edits all attributes and lists of the selected object. Referenced attribute objects
// (font, line and fill attributes, variables, ...) are shown below their reference and can be
// edited in place.
class PropertyEditor : public QWidget {
    Q_OBJECT

public:
    explicit PropertyEditor(EditorContext* context, QWidget* parent = nullptr);

private:
    using Refresher = std::function<void()>;

    // A reference attribute whose target is edited inline; a change of the target rebuilds the view.
    struct InlineTarget {
        vt::ObjectId owner;
        std::size_t index;
        vt::ObjectId target;
    };

    void rebuild();
    void scheduleRebuild();
    void refresh();
    const vt::VtObject* object() const { return objectById(m_objectId); }
    const vt::VtObject* objectById(vt::ObjectId id) const;
    void commit(const vt::VtObject& changed, const QString& text, int mergeKey = 0);
    void commitAttribute(vt::ObjectId id, std::size_t index, double value);
    void makeOwnCopy(vt::ObjectId owner, std::size_t index);

    QTreeWidgetItem* addSection(const QString& title);
    QTreeWidgetItem* addRow(QTreeWidgetItem* parent, const QString& label, QWidget* editor, Refresher refresher,
                            const QString& toolTip = {});
    QTreeWidgetItem* addWideRow(QTreeWidgetItem* parent, QWidget* editor, Refresher refresher);

    void addObjectSection();
    void addPlacementSection();
    void addAttributeSection();
    void addContentSection();
    void addUsageSection();
    void addAttributeRows(QTreeWidgetItem* parent, vt::ObjectId id, bool withInlineTargets);
    void addInlineObject(QTreeWidgetItem* referenceRow, vt::ObjectId owner, std::size_t index);

    QWidget* attributeEditor(vt::ObjectId id, std::size_t index, Refresher& refresher);
    QWidget* objectRefEditor(vt::ObjectId id, std::size_t index, Refresher& refresher);
    QWidget* textEditor(vt::ObjectId id, Refresher& refresher);
    QWidget* hexEditor(Refresher& refresher);
    QWidget* pictureEditor(Refresher& refresher);
    QWidget* listEditor(vt::ListKind kind, Refresher& refresher);

    std::vector<vt::ObjectType> allowedContainedTypes(vt::ObjectType parent) const;

    EditorContext* m_context;
    QTreeWidget* m_tree;
    vt::ObjectId m_objectId = vt::kNullObjectId;
    Selection m_selection;
    std::vector<Refresher> m_refreshers;
    std::vector<InlineTarget> m_inlineTargets;
    std::set<QString> m_collapsedGroups;
    bool m_rebuildQueued = false;
};
