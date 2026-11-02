#pragma once

#include "EditorContext.h"

#include <QToolButton>
#include <QWidget>

#include <vector>

// Icon button for one object type. Click inserts the object, dragging places it on the canvas.
class PaletteButton : public QToolButton {
    Q_OBJECT

public:
    PaletteButton(vt::ObjectType type, QWidget* parent = nullptr);

    vt::ObjectType type() const { return m_type; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void startDrag();

    vt::ObjectType m_type;
    QPoint m_pressPosition;
    bool m_dragging = false;
};

// Tool bar with all object types above the canvas, grouped by category. Names and descriptions
// appear only as tool tips.
class ObjectPalette : public QWidget {
    Q_OBJECT

public:
    explicit ObjectPalette(EditorContext* context, QWidget* parent = nullptr);

signals:
    void createRequested(vt::ObjectType type);

private:
    void updateAvailability();

    EditorContext* m_context;
    std::vector<PaletteButton*> m_buttons;
};
