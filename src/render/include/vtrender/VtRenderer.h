#pragma once

#include "vtcore/ObjectPool.h"

#include <QColor>
#include <QImage>
#include <QRect>

#include <vector>

class QPainter;

namespace vtr {

struct RenderSettings {
    int dataMaskSize = 480;
    int softKeyWidth = 80;
    int softKeyHeight = 80;
    int physicalSoftKeys = 6; // soft keys per column in the preview
    bool designMode = true;   // hidden containers are drawn faintly instead of being left out
};

// An object as it appears in a rendered view. rect is in view coordinates.
struct Placement {
    vt::ObjectId object = vt::kNullObjectId;
    vt::ObjectId parent = vt::kNullObjectId; // NULL for the view root
    int index = -1;                          // entry in the parent's children/references list
    QRect rect;
    int depth = 0;
    bool movable = false; // positioned by the parent's children list, so it can be dragged
};

// Standard VT palette colour.
QColor vtColour(int index);

// Draws VT objects the way a virtual terminal would show them (static state, no flashing).
class Renderer {
public:
    Renderer(const vt::ObjectPool& pool, const RenderSettings& settings);

    // Whether the object can be opened as a view (masks, keys, containers, designators, ...).
    static bool isViewRoot(vt::ObjectType type);

    QSize objectSize(vt::ObjectId id) const;

    // A view shows a root object; Data and Alarm Masks are shown with their Soft Key Mask beside them.
    QSize viewSize(vt::ObjectId root) const;
    void renderView(QPainter& painter, vt::ObjectId root) const;
    QImage renderViewImage(vt::ObjectId root) const;

    // All objects of a view in drawing order (later entries are on top).
    std::vector<Placement> layoutView(vt::ObjectId root) const;

    // Draws one object with its top left corner at pos.
    void drawObject(QPainter& painter, vt::ObjectId id, QPoint pos) const;

private:
    QSize sizeOf(vt::ObjectId id, int depth) const;
    QSize designatorSize() const;
    QPoint softKeyColumnOrigin() const;
    QRect softKeySlot(int index, QPoint origin) const;
    QSize softKeyMaskSize(const vt::VtObject& mask) const;

    void draw(QPainter& painter, const vt::VtObject& object, QPoint pos, int depth) const;
    void drawChildren(QPainter& painter, const vt::VtObject& object, QPoint pos, int depth) const;
    void drawSoftKeyMask(QPainter& painter, const vt::VtObject& mask, QPoint origin) const;
    void drawTextField(QPainter& painter, const vt::VtObject& object, QRect rect, const QString& text) const;
    void drawNumberField(QPainter& painter, const vt::VtObject& object, QRect rect) const;
    void drawShape(QPainter& painter, const vt::VtObject& object, QRect rect) const;
    void drawMeter(QPainter& painter, const vt::VtObject& object, QRect rect) const;
    void drawLinearBar(QPainter& painter, const vt::VtObject& object, QRect rect) const;
    void drawArchedBar(QPainter& painter, const vt::VtObject& object, QRect rect) const;
    void drawPicture(QPainter& painter, const vt::VtObject& object, QRect rect) const;

    void layout(std::vector<Placement>& out, const vt::VtObject& object, vt::ObjectId parent, int index, QPoint pos,
                int depth, bool movable) const;
    void layoutSoftKeyMask(std::vector<Placement>& out, const vt::VtObject& mask, QPoint origin) const;

    const vt::VtObject* find(vt::ObjectId id) const;
    double attr(const vt::VtObject& object, const char* name, double fallback = 0) const;
    double numericValue(const vt::VtObject& object, const char* valueAttribute = "value") const;
    QImage pictureImage(const vt::VtObject& picture) const;

    const vt::ObjectPool& m_pool;
    RenderSettings m_settings;
};

} // namespace vtr
