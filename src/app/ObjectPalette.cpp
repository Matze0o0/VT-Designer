#include "ObjectPalette.h"

#include "AttributeInfo.h"
#include "FlowLayout.h"
#include "MaskCanvas.h"
#include "ObjectIcons.h"

#include <QApplication>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

namespace {

using T = vt::ObjectType;

constexpr int kIconSize = 26;

const std::vector<std::vector<T>>& groups()
{
    static const std::vector<std::vector<T>> list = {
        {T::DataMask, T::AlarmMask, T::SoftKeyMask, T::WindowMask, T::KeyGroup},
        {T::Container, T::Key, T::Button, T::Animation},
        {T::InputBoolean, T::InputString, T::InputNumber, T::InputList},
        {T::OutputString, T::OutputNumber, T::OutputList},
        {T::OutputLine, T::OutputRectangle, T::OutputEllipse, T::OutputPolygon},
        {T::OutputMeter, T::OutputLinearBarGraph, T::OutputArchedBarGraph, T::PictureGraphic, T::GraphicsContext},
        {T::NumberVariable, T::StringVariable},
        {T::FontAttributes, T::LineAttributes, T::FillAttributes, T::InputAttributes, T::ExtendedInputAttributes,
         T::ColourMap},
        {T::ObjectPointer, T::Macro, T::ObjectLabelReferenceList, T::AuxiliaryFunctionType2, T::AuxiliaryInputType2,
         T::AuxiliaryControlDesignatorType2, T::ExternalObjectDefinition, T::ExternalReferenceName,
         T::ExternalObjectPointer},
    };
    return list;
}

QString toolTipFor(T type, bool available, int minVersion)
{
    QString tip = QStringLiteral("<b>%1</b><br>%2").arg(ui::typeLabel(type).toHtmlEscaped(),
                                                        ui::objectDescription(type).toHtmlEscaped());
    if (available) {
        tip += QStringLiteral("<br><i>%1</i>")
                   .arg(ObjectPalette::tr("Drag onto a mask, or click to insert into the selected object."));
    } else {
        tip += QStringLiteral("<br><span style=\"color:#c62828\">%1</span>")
                   .arg(ObjectPalette::tr("Requires VT version %1 (see Pool settings).").arg(minVersion));
    }
    return tip;
}

} // namespace

PaletteButton::PaletteButton(vt::ObjectType type, QWidget* parent)
    : QToolButton(parent)
    , m_type(type)
{
    setIcon(ui::objectIcon(type));
    setIconSize(QSize(kIconSize, kIconSize));
    setAutoRaise(true);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::OpenHandCursor);
}

void PaletteButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->position().toPoint();
        m_dragging = false;
    }
    QToolButton::mousePressEvent(event);
}

void PaletteButton::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) != 0 && !m_dragging &&
        (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;
        setDown(false);
        startDrag();
        return;
    }
    QToolButton::mouseMoveEvent(event);
}

void PaletteButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging) {
        m_dragging = false;
        setDown(false);
        event->accept();
        return;
    }
    QToolButton::mouseReleaseEvent(event);
}

void PaletteButton::startDrag()
{
    // The cursor carries the icon on a small card; the canvas shows the object itself at the drop point.
    const qreal dpr = devicePixelRatioF();
    constexpr int card = 40;
    QPixmap pixmap(QSize(card, card) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(ui::categoryColour(m_type), 1.5));
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.drawRoundedRect(QRectF(1, 1, card - 2, card - 2), 6, 6);
        painter.drawPixmap(QPointF((card - 30) / 2.0, (card - 30) / 2.0), ui::objectPixmap(m_type, 30, dpr));
    }

    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(MaskCanvas::kTypeMime), QByteArray::number(static_cast<int>(m_type)));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(card + 4, card + 4)); // card sits above left of the cursor
    drag->exec(Qt::CopyAction);
}

ObjectPalette::ObjectPalette(EditorContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    auto* flow = new FlowLayout(this, 2);
    flow->setContentsMargins(4, 3, 4, 3);
    const auto& list = groups();
    for (std::size_t g = 0; g < list.size(); ++g) {
        auto* group = new QWidget(this);
        auto* row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        for (T type : list[g]) {
            auto* button = new PaletteButton(type, group);
            connect(button, &QToolButton::clicked, this, [this, type] { emit createRequested(type); });
            row->addWidget(button);
            m_buttons.push_back(button);
        }
        if (g + 1 < list.size()) {
            auto* separator = new QFrame(group);
            separator->setFrameShape(QFrame::VLine);
            separator->setFrameShadow(QFrame::Sunken);
            separator->setFixedWidth(10);
            row->addWidget(separator);
        }
        flow->addWidget(group);
    }
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(m_context, &EditorContext::documentChanged, this, &ObjectPalette::updateAvailability);
    connect(m_context, &EditorContext::settingsChanged, this, &ObjectPalette::updateAvailability);
    updateAvailability();
}

void ObjectPalette::updateAvailability()
{
    const int version = m_context->document() != nullptr ? vt::toInt(m_context->pool().version()) : 5;
    for (PaletteButton* button : m_buttons) {
        const int minVersion = vt::toInt(vt::schemaFor(button->type()).minVersion);
        const bool available = minVersion <= version;
        button->setEnabled(available);
        button->setToolTip(toolTipFor(button->type(), available, minVersion));
    }
}
