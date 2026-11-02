#include "MaskCanvas.h"

#include "AttributeInfo.h"
#include "ObjectFactory.h"

#include <QContextMenuEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kMoveMergeBase = 0x10000000;
constexpr int kArrowMergeBase = 0x20000000;
constexpr int kResizeMergeBase = 0x30000000;
constexpr double kHandleSize = 7.0; // screen pixels
constexpr std::array<double, 12> kZoomLevels = {0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16};

using T = vt::ObjectType;

} // namespace

MaskCanvas::MaskCanvas(EditorContext* context, QWidget* parent)
    : QGraphicsView(parent)
    , m_context(context)
    , m_scene(new QGraphicsScene(this))
    , m_pixmap(new QGraphicsPixmapItem)
{
    setScene(m_scene);
    m_scene->addItem(m_pixmap);
    m_pixmap->setTransformationMode(Qt::FastTransformation);
    m_pixmap->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setMouseTracking(true);
    setAcceptDrops(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);

    connect(m_context, &EditorContext::poolChanged, this, &MaskCanvas::refresh);
    connect(m_context, &EditorContext::settingsChanged, this, &MaskCanvas::refresh);
    connect(m_context, &EditorContext::viewRootChanged, this, &MaskCanvas::refresh);
    connect(m_context, &EditorContext::documentChanged, this, &MaskCanvas::refresh);
    connect(m_context, &EditorContext::selectionChanged, viewport(), qOverload<>(&QWidget::update));
    setZoom(1.0);
}

void MaskCanvas::refresh()
{
    m_layout.clear();
    if (m_context->document() == nullptr || m_context->viewRoot() == vt::kNullObjectId) {
        m_pixmap->setPixmap(QPixmap());
        m_scene->setSceneRect(QRectF(0, 0, 1, 1));
        viewport()->update();
        return;
    }
    const vtr::Renderer renderer(m_context->pool(), m_context->renderSettings());
    const QImage image = renderer.renderViewImage(m_context->viewRoot());
    m_layout = renderer.layoutView(m_context->viewRoot());
    m_pixmap->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(QRectF(-20, -20, image.width() + 40, image.height() + 40));
    viewport()->update();
}

void MaskCanvas::setZoom(double zoom)
{
    m_zoom = std::clamp(zoom, kZoomLevels.front(), kZoomLevels.back());
    setTransform(QTransform::fromScale(m_zoom, m_zoom));
    emit zoomChanged(m_zoom);
}

void MaskCanvas::zoomIn()
{
    const auto it = std::upper_bound(kZoomLevels.begin(), kZoomLevels.end(), m_zoom + 1e-9);
    setZoom(it == kZoomLevels.end() ? kZoomLevels.back() : *it);
}

void MaskCanvas::zoomOut()
{
    auto it = std::lower_bound(kZoomLevels.begin(), kZoomLevels.end(), m_zoom - 1e-9);
    setZoom(it == kZoomLevels.begin() ? kZoomLevels.front() : *std::prev(it));
}

void MaskCanvas::zoomToFit()
{
    const QRectF image = m_pixmap->boundingRect();
    if (image.isEmpty()) {
        return;
    }
    const double fit = std::min((viewport()->width() - 40) / image.width(), (viewport()->height() - 40) / image.height());
    double best = kZoomLevels.front();
    for (double level : kZoomLevels) {
        if (level <= fit) {
            best = level;
        }
    }
    setZoom(best);
    centerOn(image.center());
}

void MaskCanvas::setShowGrid(bool show)
{
    m_showGrid = show;
    viewport()->update();
}

void MaskCanvas::setGridSize(int size)
{
    m_gridSize = std::max(1, size);
    viewport()->update();
}

QPoint MaskCanvas::toVt(QPoint viewPos) const
{
    const QPointF scenePos = mapToScene(viewPos);
    return {static_cast<int>(std::floor(scenePos.x())), static_cast<int>(std::floor(scenePos.y()))};
}

int MaskCanvas::hitTest(QPoint vtPos) const
{
    for (int i = static_cast<int>(m_layout.size()) - 1; i >= 0; --i) {
        const vtr::Placement& p = m_layout[static_cast<std::size_t>(i)];
        if (p.parent != vt::kNullObjectId && p.rect.contains(vtPos)) {
            return i;
        }
    }
    for (int i = static_cast<int>(m_layout.size()) - 1; i >= 0; --i) {
        if (m_layout[static_cast<std::size_t>(i)].rect.contains(vtPos)) {
            return i;
        }
    }
    return -1;
}

int MaskCanvas::selectedPlacement() const
{
    const Selection s = m_context->selection();
    if (!s.isValid()) {
        return -1;
    }
    for (std::size_t i = 0; i < m_layout.size(); ++i) {
        const vtr::Placement& p = m_layout[i];
        if (p.object == s.object && p.parent == s.parent && p.index == s.index) {
            return static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < m_layout.size(); ++i) {
        if (m_layout[i].object == s.object) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool MaskCanvas::resizableWidth(const vt::VtObject& object) const
{
    switch (object.type) {
    case T::DataMask:
    case T::AlarmMask:
    case T::WorkingSet:
    case T::Key:
    case T::SoftKeyMask:
    case T::WindowMask:
    case T::ObjectPointer:
    case T::ExternalObjectPointer: return false;
    default: return object.hasAttribute("width") || object.type == T::GraphicsContext;
    }
}

bool MaskCanvas::resizableHeight(const vt::VtObject& object) const
{
    return resizableWidth(object) && (object.hasAttribute("height") || object.type == T::GraphicsContext);
}

QRectF MaskCanvas::handleRect(Handle handle, const QRect& itemRect) const
{
    const QRectF r(itemRect);
    const double s = kHandleSize / m_zoom;
    QPointF c;
    switch (handle) {
    case TopLeft: c = r.topLeft(); break;
    case Top: c = QPointF(r.center().x(), r.top()); break;
    case TopRight: c = QPointF(r.right() + 1, r.top()); break;
    case Right: c = QPointF(r.right() + 1, r.center().y()); break;
    case BottomRight: c = QPointF(r.right() + 1, r.bottom() + 1); break;
    case Bottom: c = QPointF(r.center().x(), r.bottom() + 1); break;
    case BottomLeft: c = QPointF(r.left(), r.bottom() + 1); break;
    case Left: c = QPointF(r.left(), r.center().y()); break;
    default: break;
    }
    return {c.x() - s / 2, c.y() - s / 2, s, s};
}

bool MaskCanvas::handleUsable(Handle handle, const vtr::Placement& placement) const
{
    const vt::VtObject* object = m_context->pool().find(placement.object);
    if (object == nullptr || !resizableWidth(*object)) {
        return false;
    }
    const bool width = handle != Top && handle != Bottom;
    const bool height = handle != Left && handle != Right;
    const bool movesOrigin = handle == TopLeft || handle == Top || handle == TopRight || handle == Left ||
                             handle == BottomLeft;
    if (movesOrigin && !placement.movable) {
        return false;
    }
    if (!resizableHeight(*object)) {
        return handle == Right || handle == BottomRight; // width only (square or aspect-locked objects)
    }
    return width || height;
}

int MaskCanvas::handleAt(QPoint viewPos) const
{
    const int selected = selectedPlacement();
    if (selected < 0) {
        return -1;
    }
    const vtr::Placement& placement = m_layout[static_cast<std::size_t>(selected)];
    const QPointF scenePos = mapToScene(viewPos);
    for (int h = 0; h < HandleCount; ++h) {
        if (handleUsable(static_cast<Handle>(h), placement) &&
            handleRect(static_cast<Handle>(h), placement.rect).adjusted(-1 / m_zoom, -1 / m_zoom, 1 / m_zoom, 1 / m_zoom)
                .contains(scenePos)) {
            return h;
        }
    }
    return -1;
}

int MaskCanvas::snap(int value) const
{
    if (!m_snap || m_gridSize <= 1) {
        return value;
    }
    return static_cast<int>(std::lround(static_cast<double>(value) / m_gridSize)) * m_gridSize;
}

void MaskCanvas::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->fillRect(rect, palette().color(QPalette::Dark));
}

void MaskCanvas::drawForeground(QPainter* painter, const QRectF& rect)
{
    const QRectF image = m_pixmap->boundingRect();
    if (image.isEmpty() || m_context->viewRoot() == vt::kNullObjectId) {
        painter->save();
        painter->resetTransform();
        painter->setPen(palette().color(QPalette::BrightText));
        painter->drawText(viewport()->rect(), Qt::AlignCenter,
                          tr("Open a mask from the pool tree or drop a Data Mask from the toolbox."));
        painter->restore();
        return;
    }

    painter->save();
    const QRectF visible = rect.intersected(image);

    // Pixel grid at high zoom and the snap grid.
    if (m_showGrid && !visible.isEmpty()) {
        QPen pixelPen(QColor(128, 128, 128, 60), 0);
        QPen gridPen(QColor(0, 120, 215, 70), 0);
        const int left = static_cast<int>(std::floor(visible.left()));
        const int right = static_cast<int>(std::ceil(visible.right()));
        const int top = static_cast<int>(std::floor(visible.top()));
        const int bottom = static_cast<int>(std::ceil(visible.bottom()));
        const bool pixelGrid = m_zoom >= 6;
        const bool snapGrid = m_gridSize * m_zoom >= 6;
        for (int x = left; x <= right; ++x) {
            const bool onGrid = snapGrid && x % m_gridSize == 0;
            if (onGrid || pixelGrid) {
                painter->setPen(onGrid ? gridPen : pixelPen);
                painter->drawLine(QLineF(x, visible.top(), x, visible.bottom()));
            }
        }
        for (int y = top; y <= bottom; ++y) {
            const bool onGrid = snapGrid && y % m_gridSize == 0;
            if (onGrid || pixelGrid) {
                painter->setPen(onGrid ? gridPen : pixelPen);
                painter->drawLine(QLineF(visible.left(), y, visible.right(), y));
            }
        }
    }

    // Thin outlines of all placed objects make invisible ones findable.
    QPen outline(QColor(255, 255, 255, 90), 0, Qt::DotLine);
    painter->setPen(outline);
    painter->setBrush(Qt::NoBrush);
    for (const vtr::Placement& p : m_layout) {
        if (p.movable && !p.rect.isEmpty()) {
            painter->drawRect(QRectF(p.rect));
        }
    }

    if (m_dropHighlight) {
        painter->setPen(QPen(QColor(0, 200, 0), 2.0 / m_zoom));
        painter->drawRect(QRectF(*m_dropHighlight));
    }

    const int selected = selectedPlacement();
    if (selected >= 0) {
        const vtr::Placement& placement = m_layout[static_cast<std::size_t>(selected)];
        painter->setPen(QPen(QColor(0, 120, 215), 2.0 / m_zoom));
        painter->drawRect(QRectF(placement.rect));
        painter->setPen(QPen(QColor(0, 80, 160), 1.0 / m_zoom));
        painter->setBrush(Qt::white);
        for (int h = 0; h < HandleCount; ++h) {
            if (handleUsable(static_cast<Handle>(h), placement)) {
                painter->drawRect(handleRect(static_cast<Handle>(h), placement.rect));
            }
        }
    }
    painter->restore();
}

void MaskCanvas::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    if (event->button() != Qt::LeftButton || m_context->document() == nullptr) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    const QPoint viewPos = event->position().toPoint();
    m_pressView = viewPos;
    m_pressVt = toVt(viewPos);
    m_dragStarted = false;
    m_mode = Mode::None;
    ++m_interaction;

    const int handle = handleAt(viewPos);
    int index = handle >= 0 ? selectedPlacement() : hitTest(m_pressVt);
    if (index < 0) {
        m_context->select({});
        return;
    }
    if (handle < 0 && (event->modifiers() & Qt::AltModifier) != 0) {
        // Alt-click selects the parent of the object under the cursor.
        const vtr::Placement& hit = m_layout[static_cast<std::size_t>(index)];
        for (int i = index - 1; i >= 0; --i) {
            if (m_layout[static_cast<std::size_t>(i)].object == hit.parent) {
                index = i;
                break;
            }
        }
    }
    const vtr::Placement& placement = m_layout[static_cast<std::size_t>(index)];
    m_context->select({placement.object, placement.parent, placement.index});

    m_startPlacement = placement;
    m_startObject = *m_context->pool().find(placement.object);
    if (placement.movable) {
        const vt::VtObject* parent = m_context->pool().find(placement.parent);
        if (parent != nullptr && static_cast<std::size_t>(placement.index) < parent->children.size()) {
            m_startChild = parent->children[static_cast<std::size_t>(placement.index)];
        }
    }
    if (handle >= 0) {
        m_mode = Mode::Resize;
        m_handle = static_cast<Handle>(handle);
    } else if (placement.movable) {
        m_mode = Mode::Move;
    }
}

void MaskCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint viewPos = event->position().toPoint();
    const QPoint vt = toVt(viewPos);
    emit cursorMoved(vt, m_pixmap->boundingRect().contains(QPointF(vt) + QPointF(0.5, 0.5)));

    if (m_mode == Mode::None || (event->buttons() & Qt::LeftButton) == 0) {
        const int handle = handleAt(viewPos);
        static const std::array<Qt::CursorShape, HandleCount> cursors = {
            Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
            Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        viewport()->setCursor(handle >= 0 ? cursors[static_cast<std::size_t>(handle)] : Qt::ArrowCursor);
        return;
    }
    if (!m_dragStarted && (viewPos - m_pressView).manhattanLength() < 3) {
        return;
    }
    m_dragStarted = true;
    const QPoint delta = vt - m_pressVt;
    if (m_mode == Mode::Move) {
        applyMove(delta);
    } else if (m_mode == Mode::Resize) {
        applyResize(delta);
    }
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    m_mode = Mode::None;
    m_dragStarted = false;
    QGraphicsView::mouseReleaseEvent(event);
}

void MaskCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = hitTest(toVt(event->position().toPoint()));
    if (index < 0) {
        return;
    }
    const vtr::Placement& placement = m_layout[static_cast<std::size_t>(index)];
    const vt::VtObject* object = m_context->pool().find(placement.object);
    if (object != nullptr && placement.parent != vt::kNullObjectId && vtr::Renderer::isViewRoot(object->type) &&
        object->type != T::WorkingSet) {
        m_context->setViewRoot(object->id);
        m_context->select({object->id, vt::kNullObjectId, -1});
    }
}

void MaskCanvas::applyMove(QPoint delta)
{
    const vt::VtObject* parentObject = m_context->pool().find(m_startPlacement.parent);
    const auto index = static_cast<std::size_t>(m_startPlacement.index);
    if (parentObject == nullptr || index >= parentObject->children.size()) {
        return;
    }
    vt::VtObject parent = *parentObject;
    vt::ChildRef& child = parent.children[index];
    const int x = std::clamp(snap(m_startChild.x + delta.x()), -32768, 32767);
    const int y = std::clamp(snap(m_startChild.y + delta.y()), -32768, 32767);
    if (child.x == x && child.y == y) {
        return;
    }
    child.x = static_cast<std::int16_t>(x);
    child.y = static_cast<std::int16_t>(y);
    m_context->document()->commit(tr("Move %1").arg(ui::typeLabel(m_startObject.type)), {parent}, {},
                                  kMoveMergeBase + (m_interaction & 0xFFFFFF));
}

void MaskCanvas::applyResize(QPoint delta)
{
    const QRect start = m_startPlacement.rect;
    int left = start.left();
    int top = start.top();
    int right = start.right() + 1;
    int bottom = start.bottom() + 1;
    if (m_handle == TopLeft || m_handle == Left || m_handle == BottomLeft) left = snap(left + delta.x());
    if (m_handle == TopRight || m_handle == Right || m_handle == BottomRight) right = snap(right + delta.x());
    if (m_handle == TopLeft || m_handle == Top || m_handle == TopRight) top = snap(top + delta.y());
    if (m_handle == BottomLeft || m_handle == Bottom || m_handle == BottomRight) bottom = snap(bottom + delta.y());
    left = std::min(left, right - 1);
    top = std::min(top, bottom - 1);
    const int width = right - left;
    const int height = bottom - top;

    vt::VtObject object = m_startObject;
    const bool viewport = object.type == T::GraphicsContext;
    object.set(viewport ? "viewportWidth" : "width", width);
    if (resizableHeight(object)) {
        object.set(viewport ? "viewportHeight" : "height", height);
    }
    if (object.type == T::OutputPolygon && start.width() > 0 && start.height() > 0) {
        const double sx = static_cast<double>(width) / start.width();
        const double sy = static_cast<double>(height) / start.height();
        for (vt::Point& p : object.points) {
            p.x = static_cast<std::uint16_t>(std::clamp(std::lround(p.x * sx), 0L, static_cast<long>(width - 1)));
            p.y = static_cast<std::uint16_t>(std::clamp(std::lround(p.y * sy), 0L, static_cast<long>(height - 1)));
        }
    }
    std::vector<vt::VtObject> upserts = {object};

    if (m_startPlacement.movable && (left != start.left() || top != start.top())) {
        const vt::VtObject* parentObject = m_context->pool().find(m_startPlacement.parent);
        const auto index = static_cast<std::size_t>(m_startPlacement.index);
        if (parentObject != nullptr && index < parentObject->children.size()) {
            vt::VtObject parent = *parentObject;
            parent.children[index].x = static_cast<std::int16_t>(m_startChild.x + left - start.left());
            parent.children[index].y = static_cast<std::int16_t>(m_startChild.y + top - start.top());
            upserts.push_back(std::move(parent));
        }
    }
    m_context->document()->commit(tr("Resize %1").arg(ui::typeLabel(object.type)), upserts, {},
                                  kResizeMergeBase + (m_interaction & 0xFFFFFF));
}

void MaskCanvas::moveSelection(int dx, int dy)
{
    const int selected = selectedPlacement();
    if (selected < 0 || !m_layout[static_cast<std::size_t>(selected)].movable) {
        return;
    }
    const vtr::Placement& placement = m_layout[static_cast<std::size_t>(selected)];
    const vt::VtObject* parentObject = m_context->pool().find(placement.parent);
    const auto index = static_cast<std::size_t>(placement.index);
    if (parentObject == nullptr || index >= parentObject->children.size()) {
        return;
    }
    vt::VtObject parent = *parentObject;
    parent.children[index].x = static_cast<std::int16_t>(std::clamp(parent.children[index].x + dx, -32768, 32767));
    parent.children[index].y = static_cast<std::int16_t>(std::clamp(parent.children[index].y + dy, -32768, 32767));
    m_context->document()->commit(tr("Move"), {parent}, {}, kArrowMergeBase + placement.object);
}

void MaskCanvas::reorderSelection(bool toFront)
{
    const Selection s = m_context->selection();
    const vt::VtObject* parentObject = m_context->pool().find(s.parent);
    if (parentObject == nullptr || s.index < 0) {
        return;
    }
    vt::VtObject parent = *parentObject;
    auto& children = parent.children;
    const auto index = static_cast<std::size_t>(s.index);
    if (index >= children.size()) {
        return;
    }
    const vt::ChildRef entry = children[index];
    children.erase(children.begin() + s.index);
    const std::size_t target = toFront ? children.size() : 0;
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(target), entry);
    m_context->document()->commit(toFront ? tr("Bring to front") : tr("Send to back"), {parent});
    m_context->select({s.object, s.parent, static_cast<int>(target)});
}

void MaskCanvas::keyPressEvent(QKeyEvent* event)
{
    const int step = (event->modifiers() & Qt::ShiftModifier) != 0 ? std::max(m_gridSize, 2) : 1;
    switch (event->key()) {
    case Qt::Key_Left: moveSelection(-step, 0); return;
    case Qt::Key_Right: moveSelection(step, 0); return;
    case Qt::Key_Up: moveSelection(0, -step); return;
    case Qt::Key_Down: moveSelection(0, step); return;
    default: QGraphicsView::keyPressEvent(event);
    }
}

void MaskCanvas::wheelEvent(QWheelEvent* event)
{
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else if (event->angleDelta().y() < 0) {
            zoomOut();
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void MaskCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    const int index = hitTest(toVt(event->pos()));
    if (index >= 0) {
        const vtr::Placement& placement = m_layout[static_cast<std::size_t>(index)];
        m_context->select({placement.object, placement.parent, placement.index});
    }
    QMenu menu(this);
    menu.addActions(m_contextActions);
    menu.exec(event->globalPos());
}

std::optional<vt::ObjectType> MaskCanvas::draggedType(const QMimeData* mime) const
{
    if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kTypeMime))) {
        return std::nullopt;
    }
    bool ok = false;
    const int type = mime->data(QString::fromLatin1(kTypeMime)).toInt(&ok);
    if (!ok || vt::findSchema(static_cast<std::uint8_t>(type)) == nullptr) {
        return std::nullopt;
    }
    return static_cast<vt::ObjectType>(type);
}

void MaskCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (draggedType(event->mimeData()) && m_context->document() != nullptr) {
        event->acceptProposedAction();
    }
}

void MaskCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    const std::optional<vt::ObjectType> type = draggedType(event->mimeData());
    if (!type) {
        return;
    }
    const auto target = ui::dropTarget(m_context->pool(), m_layout, toVt(event->position().toPoint()), *type);
    m_dropHighlight = target ? std::optional<QRect>(target->rect) : std::nullopt;
    viewport()->update();
    event->acceptProposedAction();
}

void MaskCanvas::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_dropHighlight.reset();
    viewport()->update();
    QGraphicsView::dragLeaveEvent(event);
}

void MaskCanvas::dropEvent(QDropEvent* event)
{
    m_dropHighlight.reset();
    viewport()->update();
    const std::optional<vt::ObjectType> type = draggedType(event->mimeData());
    if (!type) {
        return;
    }
    const QPoint vt = toVt(event->position().toPoint());
    const auto target = ui::dropTarget(m_context->pool(), m_layout, vt, *type);
    event->acceptProposedAction();
    emit objectDropped(*type, target, QPoint(snap(vt.x()), snap(vt.y())));
}
