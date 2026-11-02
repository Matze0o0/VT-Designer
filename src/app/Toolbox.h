#pragma once

#include "EditorContext.h"

#include <QTreeWidget>

// Object types grouped by category. Items can be dragged onto the canvas or double-clicked.
class Toolbox : public QTreeWidget {
    Q_OBJECT

public:
    explicit Toolbox(EditorContext* context, QWidget* parent = nullptr);

signals:
    void createRequested(vt::ObjectType type);

protected:
    void startDrag(Qt::DropActions supportedActions) override;

private:
    void updateAvailability();

    EditorContext* m_context;
};
