#pragma once

#include "EditorContext.h"

#include <QWidget>

#include <functional>
#include <vector>

class QTreeWidget;
class QTreeWidgetItem;

// Shows and edits all attributes and lists of the selected object.
class PropertyEditor : public QWidget {
    Q_OBJECT

public:
    explicit PropertyEditor(EditorContext* context, QWidget* parent = nullptr);

private:
    using Refresher = std::function<void()>;

    void rebuild();
    void refresh();
    const vt::VtObject* object() const;
    void commit(const vt::VtObject& changed, const QString& text, int mergeKey = 0);
    void commitAttribute(std::size_t index, double value);

    QTreeWidgetItem* addSection(const QString& title);
    void addRow(QTreeWidgetItem* section, const QString& label, QWidget* editor, Refresher refresher,
                const QString& toolTip = {});
    void addWideRow(QTreeWidgetItem* section, QWidget* editor, Refresher refresher);

    void addObjectSection();
    void addPlacementSection();
    void addAttributeSection();
    void addContentSection();
    void addUsageSection();

    QWidget* attributeEditor(std::size_t index, Refresher& refresher);
    QWidget* objectRefEditor(std::size_t index, Refresher& refresher);
    QWidget* textEditor(Refresher& refresher);
    QWidget* hexEditor(Refresher& refresher);
    QWidget* pictureEditor(Refresher& refresher);
    QWidget* listEditor(vt::ListKind kind, Refresher& refresher);

    std::vector<vt::ObjectType> allowedContainedTypes(vt::ObjectType parent) const;

    EditorContext* m_context;
    QTreeWidget* m_tree;
    vt::ObjectId m_objectId = vt::kNullObjectId;
    Selection m_selection;
    std::vector<Refresher> m_refreshers;
};
