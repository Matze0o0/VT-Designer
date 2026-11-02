#pragma once

#include "EditorContext.h"

#include "vtrender/VtRenderer.h"

#include <QGraphicsView>

#include <optional>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QMimeData;

// WYSIWYG view of the current view root. Scene coordinates are VT pixels.
class MaskCanvas : public QGraphicsView {
    Q_OBJECT

public:
    explicit MaskCanvas(EditorContext* context, QWidget* parent = nullptr);

    static constexpr const char* kTypeMime = "application/x-vtdesigner-type";

    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool show);
    bool snapToGrid() const { return m_snap; }
    void setSnapToGrid(bool snap) { m_snap = snap; }
    int gridSize() const { return m_gridSize; }
    void setGridSize(int size);

    void setContextActions(const QList<QAction*>& actions) { m_contextActions = actions; }

    // Moves the selected object inside its parent (arrow keys).
    void moveSelection(int dx, int dy);
    // Changes the drawing order of the selection inside its parent.
    void reorderSelection(bool toFront);

    // Layout of the current view (for paste/drop decisions).
    const std::vector<vtr::Placement>& currentLayout() const { return m_layout; }

signals:
    void zoomChanged(double zoom);
    void cursorMoved(QPoint vtPosition, bool inside);
    void objectDropped(vt::ObjectType type, std::optional<vtr::Placement> target, QPoint vtPosition);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class Mode { None, Move, Resize };
    enum Handle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, HandleCount };

    void refresh();
    QPoint toVt(QPoint viewPos) const;
    int hitTest(QPoint vtPos) const;
    int selectedPlacement() const;
    bool resizableWidth(const vt::VtObject& object) const;
    bool resizableHeight(const vt::VtObject& object) const;
    QRectF handleRect(Handle handle, const QRect& itemRect) const;
    int handleAt(QPoint viewPos) const;
    bool handleUsable(Handle handle, const vtr::Placement& placement) const;
    int snap(int value) const;
    void applyMove(QPoint delta);
    void applyResize(QPoint delta);
    std::optional<vt::ObjectType> draggedType(const QMimeData* mime) const;
    void prepareDragPreview(vt::ObjectType type);

    // Object being dragged in from the palette, drawn where it would be dropped.
    struct DragPreview {
        vt::ObjectType type = vt::ObjectType::DataMask;
        QImage image;          // rendered object in VT pixels, or the type icon
        bool iconOnly = false; // masks and non-visual objects show their icon
        QPoint position;       // top left in VT pixels
        bool placeable = false;
    };

    EditorContext* m_context;
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmap;
    std::vector<vtr::Placement> m_layout;

    double m_zoom = 1.0;
    bool m_showGrid = true;
    bool m_snap = false;
    int m_gridSize = 10;

    Mode m_mode = Mode::None;
    Handle m_handle = TopLeft;
    QPoint m_pressVt;
    QPoint m_pressView;
    bool m_dragStarted = false;
    vtr::Placement m_startPlacement;
    vt::VtObject m_startObject;
    vt::ChildRef m_startChild;
    int m_interaction = 0;
    std::optional<QRect> m_dropHighlight;
    std::optional<DragPreview> m_dragPreview;
    QList<QAction*> m_contextActions;
};
