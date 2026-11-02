#include "Toolbox.h"

#include "AttributeInfo.h"
#include "MaskCanvas.h"

#include <QDrag>
#include <QMimeData>

namespace {

using T = vt::ObjectType;

constexpr int kTypeRole = Qt::UserRole;

struct Category {
    const char* title;
    std::vector<T> types;
};

const std::vector<Category>& categories()
{
    static const std::vector<Category> list = {
        {QT_TRANSLATE_NOOP("Toolbox", "Masks"), {T::DataMask, T::AlarmMask, T::SoftKeyMask, T::WindowMask, T::KeyGroup}},
        {QT_TRANSLATE_NOOP("Toolbox", "Containers and keys"), {T::Container, T::Key, T::Button, T::Animation}},
        {QT_TRANSLATE_NOOP("Toolbox", "Input fields"), {T::InputBoolean, T::InputString, T::InputNumber, T::InputList}},
        {QT_TRANSLATE_NOOP("Toolbox", "Output fields"), {T::OutputString, T::OutputNumber, T::OutputList}},
        {QT_TRANSLATE_NOOP("Toolbox", "Shapes"),
         {T::OutputLine, T::OutputRectangle, T::OutputEllipse, T::OutputPolygon}},
        {QT_TRANSLATE_NOOP("Toolbox", "Graphics"),
         {T::OutputMeter, T::OutputLinearBarGraph, T::OutputArchedBarGraph, T::PictureGraphic, T::GraphicsContext}},
        {QT_TRANSLATE_NOOP("Toolbox", "Variables"), {T::NumberVariable, T::StringVariable}},
        {QT_TRANSLATE_NOOP("Toolbox", "Attributes"),
         {T::FontAttributes, T::LineAttributes, T::FillAttributes, T::InputAttributes, T::ExtendedInputAttributes,
          T::ColourMap}},
        {QT_TRANSLATE_NOOP("Toolbox", "Other"),
         {T::ObjectPointer, T::Macro, T::ObjectLabelReferenceList, T::AuxiliaryFunctionType2, T::AuxiliaryInputType2,
          T::AuxiliaryControlDesignatorType2, T::ExternalObjectDefinition, T::ExternalReferenceName,
          T::ExternalObjectPointer, T::WorkingSet}},
    };
    return list;
}

} // namespace

Toolbox::Toolbox(EditorContext* context, QWidget* parent)
    : QTreeWidget(parent)
    , m_context(context)
{
    setHeaderHidden(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIndentation(12);

    for (const Category& category : categories()) {
        auto* group = new QTreeWidgetItem(this, {tr(category.title)});
        group->setFlags(Qt::ItemIsEnabled);
        for (T type : category.types) {
            auto* item = new QTreeWidgetItem(group, {ui::typeLabel(type)});
            item->setData(0, kTypeRole, static_cast<int>(type));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        group->setExpanded(true);
    }

    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, kTypeRole).isValid() && !item->isDisabled()) {
            emit createRequested(static_cast<T>(item->data(0, kTypeRole).toInt()));
        }
    });
    connect(m_context, &EditorContext::documentChanged, this, &Toolbox::updateAvailability);
    connect(m_context, &EditorContext::settingsChanged, this, &Toolbox::updateAvailability);
}

void Toolbox::updateAvailability()
{
    const int version = m_context->document() != nullptr ? vt::toInt(m_context->pool().version()) : 5;
    QTreeWidgetItemIterator it(this);
    while (*it) {
        if ((*it)->data(0, kTypeRole).isValid()) {
            const auto type = static_cast<T>((*it)->data(0, kTypeRole).toInt());
            const int minVersion = vt::toInt(vt::schemaFor(type).minVersion);
            (*it)->setDisabled(minVersion > version);
            (*it)->setToolTip(0, minVersion > version ? tr("Requires VT version %1").arg(minVersion)
                                                       : tr("Drag onto the canvas or double-click"));
        }
        ++it;
    }
}

void Toolbox::startDrag(Qt::DropActions supportedActions)
{
    QTreeWidgetItem* item = currentItem();
    if (item == nullptr || !item->data(0, kTypeRole).isValid() || item->isDisabled()) {
        return;
    }
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(MaskCanvas::kTypeMime), QByteArray::number(item->data(0, kTypeRole).toInt()));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(supportedActions & Qt::CopyAction ? Qt::CopyAction : supportedActions);
}
