#include "PropertyEditor.h"

#include "AttributeInfo.h"
#include "ListEditor.h"
#include "ObjectFactory.h"
#include "ObjectIcons.h"

#include "vtcore/Hierarchy.h"
#include "vtcore/PictureCodec.h"
#include "vtcore/PoolEditing.h"
#include "vtrender/VtRenderer.h"
#include "vtrender/VtText.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace {

using T = vt::ObjectType;

constexpr int kAttributeMergeBase = 0x40000000;
constexpr int kNewObjectBase = -1000;
constexpr int kGroupKeyRole = Qt::UserRole;

bool isTextType(T type)
{
    return type == T::InputString || type == T::OutputString || type == T::StringVariable ||
           type == T::InputAttributes;
}

// Helper objects whose attributes are shown and edited below the reference that uses them.
bool editableInline(T type)
{
    switch (type) {
    case T::FontAttributes:
    case T::LineAttributes:
    case T::FillAttributes:
    case T::InputAttributes:
    case T::ExtendedInputAttributes:
    case T::NumberVariable:
    case T::StringVariable:
    case T::ExternalReferenceName: return true;
    default: return false;
    }
}

// Types that make sense to create directly from a reference field.
bool creatableFromReference(T type)
{
    switch (type) {
    case T::FontAttributes:
    case T::LineAttributes:
    case T::FillAttributes:
    case T::InputAttributes:
    case T::ExtendedInputAttributes:
    case T::NumberVariable:
    case T::StringVariable:
    case T::SoftKeyMask:
    case T::DataMask:
    case T::AlarmMask:
    case T::OutputString:
    case T::PictureGraphic:
    case T::ExternalReferenceName: return true;
    default: return false;
    }
}

QString hexString(const std::vector<std::uint8_t>& bytes)
{
    QStringList parts;
    for (std::uint8_t b : bytes) {
        parts << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

bool parseHex(const QString& text, std::vector<std::uint8_t>& out)
{
    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("[\\s,;]|0x")));
    if (compact.size() % 2 != 0) {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    for (qsizetype i = 0; i < compact.size(); i += 2) {
        bool ok = false;
        const int value = compact.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    out = std::move(bytes);
    return true;
}

} // namespace

PropertyEditor::PropertyEditor(EditorContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_tree(new QTreeWidget(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Property"), tr("Value")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setIndentation(12);

    // Remember which inline groups the user folded away.
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        const QString key = item->data(0, kGroupKeyRole).toString();
        if (!key.isEmpty()) {
            m_collapsedGroups.insert(key);
        }
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem* item) { m_collapsedGroups.erase(item->data(0, kGroupKeyRole).toString()); });

    connect(m_context, &EditorContext::selectionChanged, this, &PropertyEditor::rebuild);
    connect(m_context, &EditorContext::documentChanged, this, &PropertyEditor::rebuild);
    connect(m_context, &EditorContext::settingsChanged, this, &PropertyEditor::rebuild);
    connect(m_context, &EditorContext::poolChanged, this, &PropertyEditor::refresh);
    rebuild();
}

const vt::VtObject* PropertyEditor::objectById(vt::ObjectId id) const
{
    if (m_context->document() == nullptr) {
        return nullptr;
    }
    return m_context->pool().find(id);
}

void PropertyEditor::commit(const vt::VtObject& changed, const QString& text, int mergeKey)
{
    m_context->document()->commit(text, {changed}, {}, mergeKey);
}

void PropertyEditor::commitAttribute(vt::ObjectId id, std::size_t index, double value)
{
    const vt::VtObject* current = objectById(id);
    if (current == nullptr || index >= current->attributes.size() || current->attributes[index] == value) {
        return;
    }
    vt::VtObject changed = *current;
    changed.attributes[index] = value;
    const vt::AttributeDef& def = changed.schema().attributes[index];
    commit(changed, tr("Change %1").arg(ui::attributeLabel(def)),
           kAttributeMergeBase + (static_cast<int>(changed.id) << 7) + static_cast<int>(index));
}

void PropertyEditor::makeOwnCopy(vt::ObjectId ownerId, std::size_t index)
{
    const vt::VtObject* owner = objectById(ownerId);
    if (owner == nullptr || index >= owner->attributes.size()) {
        return;
    }
    const vt::VtObject* shared = objectById(static_cast<vt::ObjectId>(owner->attributes[index]));
    if (shared == nullptr) {
        return;
    }
    vt::VtObject copy = *shared;
    copy.id = ui::allocateId(m_context->pool(), copy.type, {});
    if (!copy.name.empty()) {
        const std::string base = copy.name + "_copy";
        std::string name = base;
        for (int n = 2; m_context->pool().findByName(name) != nullptr; ++n) {
            name = base + std::to_string(n);
        }
        copy.name = name;
    }
    vt::VtObject changedOwner = *owner;
    changedOwner.attributes[index] = copy.id;
    m_context->document()->commit(tr("Own copy of %1").arg(ui::typeLabel(copy.type)), {copy, changedOwner});
}

void PropertyEditor::scheduleRebuild()
{
    if (m_rebuildQueued) {
        return;
    }
    m_rebuildQueued = true;
    // Deferred: the change may come from a widget that a rebuild would delete.
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        if (m_context->selection().object == m_objectId) {
            const int scroll = m_tree->verticalScrollBar()->value();
            rebuild();
            m_tree->verticalScrollBar()->setValue(scroll);
        } else {
            rebuild();
        }
    });
}

void PropertyEditor::refresh()
{
    if (object() == nullptr) {
        rebuild();
        return;
    }
    // A reference pointing somewhere else needs other inline editors.
    for (const InlineTarget& target : m_inlineTargets) {
        const vt::VtObject* owner = objectById(target.owner);
        const vt::ObjectId current = owner != nullptr && target.index < owner->attributes.size()
                                         ? static_cast<vt::ObjectId>(owner->attributes[target.index])
                                         : vt::kNullObjectId;
        const bool exists = objectById(current) != nullptr;
        if (current != target.target || (target.target != vt::kNullObjectId && !exists)) {
            scheduleRebuild();
            return;
        }
    }
    for (const Refresher& refresher : m_refreshers) {
        refresher();
    }
}

QTreeWidgetItem* PropertyEditor::addSection(const QString& title)
{
    auto* item = new QTreeWidgetItem(m_tree, {title});
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    return item;
}

QTreeWidgetItem* PropertyEditor::addRow(QTreeWidgetItem* parent, const QString& label, QWidget* editor,
                                        Refresher refresher, const QString& toolTip)
{
    auto* item = new QTreeWidgetItem(parent, {label});
    item->setToolTip(0, toolTip.isEmpty() ? label : toolTip);
    m_tree->setItemWidget(item, 1, editor);
    if (refresher) {
        refresher();
        m_refreshers.push_back(std::move(refresher));
    }
    return item;
}

QTreeWidgetItem* PropertyEditor::addWideRow(QTreeWidgetItem* parent, QWidget* editor, Refresher refresher)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setFirstColumnSpanned(true);
    m_tree->setItemWidget(item, 0, editor);
    if (refresher) {
        refresher();
        m_refreshers.push_back(std::move(refresher));
    }
    return item;
}

void PropertyEditor::rebuild()
{
    m_refreshers.clear();
    m_inlineTargets.clear();
    m_tree->clear();
    m_selection = m_context->selection();
    m_objectId = m_selection.object;
    if (object() == nullptr) {
        auto* hint = new QTreeWidgetItem(m_tree, {tr("Select an object on the canvas or in the pool tree.")});
        hint->setFirstColumnSpanned(true);
        hint->setDisabled(true);
        return;
    }
    addObjectSection();
    addPlacementSection();
    addAttributeSection();
    addContentSection();
    addUsageSection();
}

void PropertyEditor::addObjectSection()
{
    const vt::VtObject& o = *object();
    QTreeWidgetItem* section = addSection(ui::typeLabel(o.type));
    section->setIcon(0, ui::objectIcon(o.type));
    section->setToolTip(0, ui::objectDescription(o.type));

    auto* name = new QLineEdit;
    name->setPlaceholderText(tr("used as symbol in the ID header"));
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        const vt::VtObject* current = object();
        const std::string text = name->text().trimmed().toStdString();
        if (current != nullptr && current->name != text) {
            vt::VtObject changed = *current;
            changed.name = text;
            commit(changed, tr("Rename object"));
        }
    });
    addRow(section, tr("Name"), name, [this, name] {
        if (const vt::VtObject* current = object(); current != nullptr && !name->hasFocus()) {
            name->setText(QString::fromStdString(current->name));
        }
    });

    auto* id = new QSpinBox;
    id->setRange(0, 65534);
    id->setKeyboardTracking(false);
    connect(id, &QSpinBox::valueChanged, this, [this, id](int value) {
        const vt::ObjectId from = m_objectId;
        const auto to = static_cast<vt::ObjectId>(value);
        if (from == to) {
            return;
        }
        if (!m_context->document()->renumber(from, to)) {
            QMessageBox::warning(this, tr("Object ID"), tr("The object ID %1 is already in use.").arg(value));
            const QSignalBlocker block(id);
            id->setValue(from);
            return;
        }
        m_context->select({to, m_selection.parent, m_selection.index});
    });
    addRow(section, tr("Object ID"), id, [this, id] {
        const QSignalBlocker block(id);
        id->setValue(m_objectId);
    });
}

void PropertyEditor::addPlacementSection()
{
    const vt::VtObject* parent = objectById(m_selection.parent);
    if (parent == nullptr || m_selection.index < 0 ||
        static_cast<std::size_t>(m_selection.index) >= parent->children.size() ||
        vt::containmentList(parent->type) != vt::ListKind::Children) {
        return;
    }
    QTreeWidgetItem* section = addSection(tr("Position in %1").arg(ui::objectDisplayName(*parent)));
    for (int axis = 0; axis < 2; ++axis) {
        auto* spin = new QSpinBox;
        spin->setRange(-32768, 32767);
        spin->setKeyboardTracking(false);
        connect(spin, &QSpinBox::valueChanged, this, [this, axis](int value) {
            const vt::VtObject* p = objectById(m_selection.parent);
            const auto index = static_cast<std::size_t>(m_selection.index);
            if (p == nullptr || index >= p->children.size()) {
                return;
            }
            vt::VtObject changed = *p;
            std::int16_t& coordinate = axis == 0 ? changed.children[index].x : changed.children[index].y;
            if (coordinate == value) {
                return;
            }
            coordinate = static_cast<std::int16_t>(value);
            commit(changed, tr("Move object"));
        });
        addRow(section, axis == 0 ? tr("X") : tr("Y"), spin, [this, spin, axis] {
            const vt::VtObject* p = objectById(m_selection.parent);
            const auto index = static_cast<std::size_t>(m_selection.index);
            if (p != nullptr && index < p->children.size()) {
                const QSignalBlocker block(spin);
                spin->setValue(axis == 0 ? p->children[index].x : p->children[index].y);
            }
        });
    }
}

void PropertyEditor::addAttributeSection()
{
    if (object()->schema().attributes.empty()) {
        return;
    }
    addAttributeRows(addSection(tr("Attributes")), m_objectId, true);
}

void PropertyEditor::addAttributeRows(QTreeWidgetItem* parent, vt::ObjectId id, bool withInlineTargets)
{
    const vt::VtObject* o = objectById(id);
    if (o == nullptr) {
        return;
    }
    const vt::ObjectSchema& schema = o->schema();
    for (std::size_t i = 0; i < schema.attributes.size(); ++i) {
        const vt::AttributeDef& def = schema.attributes[i];
        Refresher refresher;
        QWidget* editor = attributeEditor(id, i, refresher);
        QString toolTip = ui::attributeLabel(def);
        if (def.aid >= 0) {
            toolTip += tr(" (AID %1)").arg(def.aid);
        }
        QTreeWidgetItem* row = addRow(parent, ui::attributeLabel(def), editor, std::move(refresher), toolTip);

        if (withInlineTargets && def.type == vt::FieldType::ObjectRef) {
            const bool inlineType = std::any_of(def.refTypes.begin(), def.refTypes.end(), editableInline);
            if (inlineType) {
                addInlineObject(row, id, i);
            }
        }
    }
}

void PropertyEditor::addInlineObject(QTreeWidgetItem* referenceRow, vt::ObjectId ownerId, std::size_t index)
{
    const vt::VtObject* owner = objectById(ownerId);
    const auto targetId = static_cast<vt::ObjectId>(owner->attributes[index]);
    m_inlineTargets.push_back({ownerId, index, targetId});
    const vt::VtObject* target = objectById(targetId);
    if (target == nullptr || !editableInline(target->type)) {
        return;
    }

    const QString key = QStringLiteral("%1.%2")
                            .arg(QString::fromLatin1(vt::objectTypeName(owner->type).data()))
                            .arg(QString::fromLatin1(owner->schema().attributes[index].name.data()));
    referenceRow->setData(0, kGroupKeyRole, key);
    referenceRow->setIcon(0, ui::objectIcon(target->type));

    // Shared attribute objects change every object that uses them; offer to split them off.
    const std::vector<vt::ObjectId> users = vt::usersOf(m_context->pool(), targetId);
    if (users.size() > 1) {
        auto* box = new QWidget;
        auto* layout = new QHBoxLayout(box);
        layout->setContentsMargins(2, 1, 2, 1);
        auto* label = new QLabel(tr("Also used by %n other object(s) – changes apply to all.", nullptr,
                                    static_cast<int>(users.size()) - 1));
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color: #8a6d00"));
        auto* copy = new QPushButton(tr("Own copy"));
        copy->setToolTip(tr("Copy %1 so that changes only affect this object").arg(ui::objectDisplayName(*target)));
        connect(copy, &QPushButton::clicked, this, [this, ownerId, index] { makeOwnCopy(ownerId, index); });
        layout->addWidget(label, 1);
        layout->addWidget(copy);
        addWideRow(referenceRow, box, {});
    }

    addAttributeRows(referenceRow, targetId, false);
    if (isTextType(target->type)) {
        Refresher refresher;
        QWidget* editor = textEditor(targetId, refresher);
        addRow(referenceRow, target->type == T::InputAttributes ? tr("Characters") : tr("Text"), editor,
               std::move(refresher));
    }
    referenceRow->setExpanded(m_collapsedGroups.count(key) == 0);
}

QWidget* PropertyEditor::attributeEditor(vt::ObjectId id, std::size_t index, Refresher& refresher)
{
    const vt::VtObject& o = *objectById(id);
    const vt::AttributeDef& def = o.schema().attributes[index];
    auto current = [this, id, index]() -> double {
        const vt::VtObject* obj = objectById(id);
        return obj != nullptr && index < obj->attributes.size() ? obj->attributes[index] : 0;
    };

    if (def.name == "lineArt") {
        // 16 squares, one per brush spot of the line pattern (most significant bit first).
        auto* box = new QWidget;
        auto* layout = new QHBoxLayout(box);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(1);
        auto bits = std::make_shared<std::vector<QToolButton*>>();
        for (int i = 0; i < 16; ++i) {
            auto* bit = new QToolButton;
            bit->setCheckable(true);
            bit->setFixedSize(12, 18);
            bit->setStyleSheet(QStringLiteral("QToolButton { border: 1px solid #90a4ae; background: white; }"
                                              "QToolButton:checked { background: #37474f; }"));
            layout->addWidget(bit);
            bits->push_back(bit);
            connect(bit, &QToolButton::toggled, this, [this, id, index, bits] {
                int value = 0;
                for (int b = 0; b < 16; ++b) {
                    if ((*bits)[static_cast<std::size_t>(b)]->isChecked()) {
                        value |= 1 << (15 - b);
                    }
                }
                commitAttribute(id, index, value);
            });
        }
        layout->addStretch();
        box->setToolTip(tr("Line pattern: each square is one brush spot of the line width; dark squares are drawn."));
        refresher = [bits, current] {
            const int value = static_cast<int>(current());
            for (int b = 0; b < 16; ++b) {
                QToolButton* bit = (*bits)[static_cast<std::size_t>(b)];
                const QSignalBlocker block(bit);
                bit->setChecked((value & (1 << (15 - b))) != 0);
            }
        };
        return box;
    }

    switch (ui::editorKind(o.type, def)) {
    case ui::EditorKind::ObjectRef:
        return objectRefEditor(id, index, refresher);

    case ui::EditorKind::Boolean: {
        auto* box = new QCheckBox;
        connect(box, &QCheckBox::toggled, this,
                [this, id, index](bool on) { commitAttribute(id, index, on ? 1 : 0); });
        refresher = [box, current] {
            const QSignalBlocker block(box);
            box->setChecked(current() != 0);
        };
        return box;
    }

    case ui::EditorKind::Colour: {
        auto* combo = new QComboBox;
        for (int c = 0; c < 256; ++c) {
            combo->addItem(ui::colourIcon(c), ui::colourName(c), c);
        }
        combo->setMaxVisibleItems(20);
        connect(combo, &QComboBox::activated, this, [this, id, index](int i) { commitAttribute(id, index, i); });
        refresher = [combo, current] {
            const QSignalBlocker block(combo);
            combo->setCurrentIndex(std::clamp(static_cast<int>(current()), 0, 255));
        };
        return combo;
    }

    case ui::EditorKind::Enum: {
        auto* combo = new QComboBox;
        for (const ui::NamedValue& v : ui::enumValues(o.type, def)) {
            combo->addItem(v.name, v.value);
        }
        connect(combo, &QComboBox::activated, this,
                [this, id, index, combo](int i) { commitAttribute(id, index, combo->itemData(i).toInt()); });
        refresher = [combo, current] {
            const QSignalBlocker block(combo);
            const int value = static_cast<int>(current());
            int i = combo->findData(value);
            if (i < 0) {
                combo->addItem(QString::number(value), value);
                i = combo->count() - 1;
            }
            combo->setCurrentIndex(i);
        };
        return combo;
    }

    case ui::EditorKind::Flags: {
        auto* button = new QToolButton;
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* menu = new QMenu(button);
        for (const ui::NamedValue& bit : ui::flagBits(o.type, def)) {
            QAction* action = menu->addAction(bit.name);
            action->setCheckable(true);
            action->setData(bit.value);
            connect(action, &QAction::toggled, this, [this, id, index, menu, current] {
                auto value = static_cast<int>(current());
                for (QAction* a : menu->actions()) {
                    const int mask = a->data().toInt();
                    value = a->isChecked() ? (value | mask) : (value & ~mask);
                }
                commitAttribute(id, index, value);
            });
        }
        button->setMenu(menu);
        refresher = [button, menu, current] {
            const int value = static_cast<int>(current());
            QStringList names;
            for (QAction* a : menu->actions()) {
                const QSignalBlocker block(a);
                const bool on = (value & a->data().toInt()) != 0;
                a->setChecked(on);
                if (on) {
                    names << a->text();
                }
            }
            button->setText(names.isEmpty() ? tr("(none)") : names.join(QStringLiteral(", ")));
            button->setToolTip(QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')));
        };
        return button;
    }

    case ui::EditorKind::Float: {
        auto* spin = new QDoubleSpinBox;
        spin->setDecimals(6);
        spin->setRange(std::max(def.minValue, -1e9), std::min(def.maxValue, 1e9));
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, id, index](double value) { commitAttribute(id, index, static_cast<float>(value)); });
        refresher = [spin, current] {
            const QSignalBlocker block(spin);
            spin->setValue(current());
        };
        return spin;
    }

    case ui::EditorKind::Integer:
    default: {
        constexpr double intMax = std::numeric_limits<int>::max();
        if (def.maxValue <= intMax && def.minValue >= -intMax) {
            auto* spin = new QSpinBox;
            spin->setRange(static_cast<int>(def.minValue), static_cast<int>(def.maxValue));
            spin->setKeyboardTracking(false);
            connect(spin, &QSpinBox::valueChanged, this,
                    [this, id, index](int value) { commitAttribute(id, index, value); });
            refresher = [spin, current] {
                const QSignalBlocker block(spin);
                spin->setValue(static_cast<int>(current()));
            };
            return spin;
        }
        auto* spin = new QDoubleSpinBox;
        spin->setDecimals(0);
        spin->setRange(def.minValue, def.maxValue);
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, id, index](double value) { commitAttribute(id, index, std::round(value)); });
        refresher = [spin, current] {
            const QSignalBlocker block(spin);
            spin->setValue(current());
        };
        return spin;
    }
    }
}

QWidget* PropertyEditor::objectRefEditor(vt::ObjectId id, std::size_t index, Refresher& refresher)
{
    const vt::AttributeDef def = objectById(id)->schema().attributes[index];
    auto* container = new QWidget;
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* combo = new QComboBox;
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* go = new QToolButton;
    go->setText(QStringLiteral("→"));
    go->setToolTip(tr("Select the referenced object"));
    layout->addWidget(combo);
    layout->addWidget(go);

    auto allowed = [def](T type) {
        return def.refTypes.empty() || std::find(def.refTypes.begin(), def.refTypes.end(), type) != def.refTypes.end();
    };

    connect(combo, &QComboBox::activated, this, [this, combo, id, index](int i) {
        const int data = combo->itemData(i).toInt();
        if (data >= 0) {
            commitAttribute(id, index, data);
            return;
        }
        // "New ..." entry: create the object and reference it in one step.
        const auto type = static_cast<T>(kNewObjectBase - data);
        std::vector<vt::VtObject> extra;
        vt::VtObject created = ui::createObject(m_context->pool(), type, extra);
        if (type == T::PictureGraphic && !ui::importPicture(this, created, m_context->pool().version())) {
            refresh();
            return;
        }
        vt::VtObject changed = *objectById(id);
        changed.attributes[index] = created.id;
        extra.push_back(created);
        extra.push_back(changed);
        m_context->document()->commit(tr("New %1").arg(ui::typeLabel(type)), extra);
    });
    connect(go, &QToolButton::clicked, this, [this, id, index] {
        const vt::VtObject* obj = objectById(id);
        if (obj != nullptr && m_context->pool().contains(static_cast<vt::ObjectId>(obj->attributes[index]))) {
            m_context->selectObject(static_cast<vt::ObjectId>(obj->attributes[index]));
        }
    });

    refresher = [this, combo, go, id, index, def, allowed] {
        const vt::VtObject* obj = objectById(id);
        if (obj == nullptr) {
            return;
        }
        const auto value = static_cast<int>(obj->attributes[index]);
        const QSignalBlocker block(combo);
        combo->clear();
        if (def.nullAllowed) {
            combo->addItem(tr("(none)"), static_cast<int>(vt::kNullObjectId));
        }
        for (const vt::VtObject& candidate : m_context->pool().objects()) {
            if (allowed(candidate.type)) {
                combo->addItem(ui::objectIcon(candidate.type), ui::objectDisplayName(candidate),
                               static_cast<int>(candidate.id));
            }
        }
        if (combo->findData(value) < 0) {
            combo->addItem(tr("%1 (missing)").arg(value), value);
        }
        if (!def.refTypes.empty()) {
            combo->insertSeparator(combo->count());
            for (T type : def.refTypes) {
                if (creatableFromReference(type)) {
                    combo->addItem(ui::objectIcon(type), tr("New %1…").arg(ui::typeLabel(type)),
                                   kNewObjectBase - static_cast<int>(type));
                }
            }
        }
        combo->setCurrentIndex(combo->findData(value));
        go->setEnabled(value != vt::kNullObjectId && m_context->pool().contains(static_cast<vt::ObjectId>(value)));
    };
    return container;
}

void PropertyEditor::addContentSection()
{
    const vt::VtObject& o = *object();
    const vt::ObjectSchema& schema = o.schema();
    const bool hasContent = schema.hasList(vt::ListKind::Data) || schema.hasList(vt::ListKind::Children) ||
                            schema.hasList(vt::ListKind::References) || schema.hasList(vt::ListKind::Macros) ||
                            schema.hasList(vt::ListKind::Points) || schema.hasList(vt::ListKind::Languages) ||
                            schema.hasList(vt::ListKind::Labels) || schema.hasList(vt::ListKind::CodePlanes);
    if (!hasContent) {
        return;
    }
    QTreeWidgetItem* section = addSection(tr("Content"));

    if (schema.hasList(vt::ListKind::Data)) {
        Refresher refresher;
        if (isTextType(o.type)) {
            QWidget* editor = textEditor(m_objectId, refresher);
            addRow(section, o.type == T::InputAttributes ? tr("Characters") : tr("Text"), editor, refresher);
        } else if (o.type == T::PictureGraphic) {
            QWidget* editor = pictureEditor(refresher);
            addWideRow(section, editor, refresher);
        } else {
            QWidget* editor = hexEditor(refresher);
            addRow(section, o.type == T::Macro ? tr("Commands (hex)") : tr("Data (hex)"), editor, refresher);
        }
    }
    if (schema.hasList(vt::ListKind::Languages)) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(QStringLiteral("de, en, fr"));
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            vt::VtObject changed = *object();
            changed.languages.clear();
            for (const QString& code :
                 edit->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
                changed.languages.push_back(code.left(2).toLower().toStdString());
            }
            if (changed.languages != object()->languages) {
                commit(changed, tr("Change languages"));
            }
        });
        addRow(section, tr("Languages"), edit, [this, edit] {
            QStringList codes;
            for (const std::string& code : object()->languages) {
                codes << QString::fromStdString(code);
            }
            if (!edit->hasFocus()) {
                edit->setText(codes.join(QStringLiteral(", ")));
            }
        });
    }
    if (schema.hasList(vt::ListKind::CodePlanes)) {
        auto* label = new QLabel;
        addRow(section, tr("Code planes"), label, [this, label] {
            QStringList ranges;
            for (const vt::CodePlane& plane : object()->codePlanes) {
                for (const vt::CharacterRange& r : plane.ranges) {
                    ranges << QStringLiteral("%1:U+%2–U+%3")
                                  .arg(plane.plane)
                                  .arg(r.first, 4, 16, QLatin1Char('0'))
                                  .arg(r.last, 4, 16, QLatin1Char('0'));
                }
            }
            label->setText(ranges.join(QStringLiteral(", ")));
        });
    }

    const std::pair<vt::ListKind, QString> lists[] = {
        {vt::ListKind::Children, tr("Child objects")}, {vt::ListKind::References, tr("Object references")},
        {vt::ListKind::Points, tr("Points")},          {vt::ListKind::Labels, tr("Labels")},
        {vt::ListKind::Macros, tr("Macros")}};
    for (const auto& [kind, title] : lists) {
        if (!schema.hasList(kind)) {
            continue;
        }
        QString heading = title;
        if (kind == vt::ListKind::References && o.type == T::SoftKeyMask) {
            heading = tr("Soft keys");
        } else if (kind == vt::ListKind::References && (o.type == T::InputList || o.type == T::OutputList)) {
            heading = tr("List items");
        }
        auto* item = new QTreeWidgetItem(section, {heading});
        item->setFirstColumnSpanned(true);
        Refresher refresher;
        QWidget* editor = listEditor(kind, refresher);
        addWideRow(section, editor, refresher);
    }
}

QWidget* PropertyEditor::textEditor(vt::ObjectId id, Refresher& refresher)
{
    auto* edit = new QLineEdit;
    auto fontType = [this, id]() -> int {
        const vt::VtObject* obj = objectById(id);
        if (obj == nullptr || !obj->hasAttribute("fontAttributes")) {
            return 0;
        }
        const vt::VtObject* font = objectById(static_cast<vt::ObjectId>(obj->get("fontAttributes")));
        return font != nullptr ? static_cast<int>(font->get("fontType")) : 0;
    };
    connect(edit, &QLineEdit::editingFinished, this, [this, id, edit, fontType] {
        const vt::VtObject* obj = objectById(id);
        if (obj == nullptr) {
            return;
        }
        std::vector<std::uint8_t> bytes;
        const bool allowWide = m_context->pool().version() >= vt::VtVersion::V4 && obj->type != T::InputAttributes;
        if (!vtr::encodeVtString(edit->text(), fontType(), allowWide, bytes)) {
            QMessageBox::warning(this, tr("Text"),
                                 tr("The text contains characters that the selected font type cannot show."));
            return;
        }
        if (bytes != obj->data) {
            vt::VtObject changed = *obj;
            changed.data = std::move(bytes);
            commit(changed, tr("Change text"));
        }
    });
    refresher = [this, id, edit, fontType] {
        const vt::VtObject* obj = objectById(id);
        if (obj != nullptr && !edit->hasFocus()) {
            edit->setText(vtr::decodeVtString(obj->data, fontType()));
            edit->setToolTip(vtr::isWideString(obj->data) ? tr("Stored as WideString (UTF-16)")
                                                          : tr("Stored as 8-bit string (%n byte(s))", nullptr,
                                                               static_cast<int>(obj->data.size())));
        }
    };
    return edit;
}

QWidget* PropertyEditor::hexEditor(Refresher& refresher)
{
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(QStringLiteral("A8 01 00 FF …"));
    connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
        std::vector<std::uint8_t> bytes;
        if (!parseHex(edit->text(), bytes)) {
            edit->setStyleSheet(QStringLiteral("background: #ffd0d0"));
            return;
        }
        edit->setStyleSheet({});
        if (bytes != object()->data) {
            vt::VtObject changed = *object();
            changed.data = std::move(bytes);
            commit(changed, tr("Change data"));
        }
    });
    refresher = [this, edit] {
        if (!edit->hasFocus()) {
            edit->setText(hexString(object()->data));
        }
    };
    return edit;
}

QWidget* PropertyEditor::pictureEditor(Refresher& refresher)
{
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 2, 0, 2);
    auto* preview = new QLabel;
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(40);
    auto* info = new QLabel;
    auto* buttons = new QHBoxLayout;
    auto* import = new QPushButton(tr("Import image…"));
    auto* exportButton = new QPushButton(tr("Export image…"));
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    layout->addWidget(preview);
    layout->addWidget(info);
    layout->addLayout(buttons);

    connect(import, &QPushButton::clicked, this, [this] {
        vt::VtObject changed = *object();
        if (ui::importPicture(this, changed, m_context->pool().version())) {
            commit(changed, tr("Import image"));
        }
    });
    connect(exportButton, &QPushButton::clicked, this, [this] { ui::exportPicture(this, *object()); });

    refresher = [this, preview, info] {
        const vt::VtObject& picture = *object();
        const vtr::Renderer renderer(m_context->pool(), m_context->renderSettings());
        const QSize size = renderer.objectSize(picture.id);
        QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            renderer.drawObject(painter, picture.id, {0, 0});
        }
        QPixmap pixmap = QPixmap::fromImage(image);
        if (pixmap.width() > 220 || pixmap.height() > 160) {
            pixmap = pixmap.scaled(220, 160, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        preview->setPixmap(pixmap);
        const int format = static_cast<int>(picture.get("format"));
        const QString formatName = format == 0 ? tr("monochrome") : format == 1 ? tr("16 colours") : tr("256 colours");
        info->setText(tr("%1 × %2 px, %3, %4 bytes%5")
                          .arg(picture.get("actualWidth"))
                          .arg(picture.get("actualHeight"))
                          .arg(formatName)
                          .arg(picture.data.size())
                          .arg((static_cast<int>(picture.get("options")) & vt::kPictureOptionRunLength) != 0
                                   ? tr(" (RLE)")
                                   : QString()));
    };
    return container;
}

std::vector<vt::ObjectType> PropertyEditor::allowedContainedTypes(vt::ObjectType parent) const
{
    std::vector<vt::ObjectType> types;
    const int version = vt::toInt(m_context->pool().version());
    for (const vt::ObjectSchema& schema : vt::allSchemas()) {
        const int min = vt::minVersionForChild(parent, schema.type);
        if (min > 0 && min <= version) {
            types.push_back(schema.type);
        }
    }
    return types;
}

QWidget* PropertyEditor::listEditor(vt::ListKind kind, Refresher& refresher)
{
    const vt::VtObject& o = *object();
    std::vector<ListColumn> columns;
    using K = ListColumn::Kind;
    const bool containment = vt::containmentList(o.type) == kind;
    switch (kind) {
    case vt::ListKind::Children:
        columns = {{tr("Object"), K::ObjectRef, 0, 65534, containment ? allowedContainedTypes(o.type) : std::vector<T>{}, false},
                   {tr("X"), K::Integer, -32768, 32767, {}, false},
                   {tr("Y"), K::Integer, -32768, 32767, {}, false}};
        break;
    case vt::ListKind::References: {
        const bool placeholders = o.type == T::SoftKeyMask || o.type == T::InputList || o.type == T::OutputList ||
                                  o.type == T::WindowMask || o.type == T::ExternalObjectDefinition;
        columns = {{tr("Object"), K::ObjectRef, 0, 65535, containment ? allowedContainedTypes(o.type) : std::vector<T>{},
                    placeholders}};
        break;
    }
    case vt::ListKind::Macros:
        columns = {{tr("Event"), K::Event, 1, 254, {}, false}, {tr("Macro"), K::ObjectRef, 0, 65534, {T::Macro}, false}};
        break;
    case vt::ListKind::Points:
        columns = {{tr("X"), K::Integer, 0, 65535, {}, false}, {tr("Y"), K::Integer, 0, 65535, {}, false}};
        break;
    case vt::ListKind::Labels:
        columns = {{tr("Object"), K::ObjectRef, 0, 65534, {}, false},
                   {tr("Text"), K::ObjectRef, 0, 65535, {T::StringVariable}, true},
                   {tr("Font type"), K::Integer, 0, 255, {}, false},
                   {tr("Graphic"), K::ObjectRef, 0, 65535, {}, true}};
        break;
    default:
        return new QLabel;
    }

    auto* editor = new ListEditor(m_context->pool(), columns);
    auto toRows = [kind](const vt::VtObject& obj) {
        ListEditor::Rows rows;
        switch (kind) {
        case vt::ListKind::Children:
            for (const vt::ChildRef& c : obj.children) rows.push_back({c.id, c.x, c.y});
            break;
        case vt::ListKind::References:
            for (vt::ObjectId ref : obj.references) rows.push_back({ref});
            break;
        case vt::ListKind::Macros:
            for (const vt::MacroRef& m : obj.macros) rows.push_back({m.event, m.macro});
            break;
        case vt::ListKind::Points:
            for (const vt::Point& p : obj.points) rows.push_back({p.x, p.y});
            break;
        case vt::ListKind::Labels:
            for (const vt::LabelRef& l : obj.labels) rows.push_back({l.object, l.stringVariable, l.fontType, l.graphic});
            break;
        default:
            break;
        }
        return rows;
    };
    connect(editor, &ListEditor::rowsEdited, this, [this, kind](const ListEditor::Rows& rows) {
        vt::VtObject changed = *object();
        switch (kind) {
        case vt::ListKind::Children:
            changed.children.clear();
            for (const auto& r : rows)
                changed.children.push_back(
                    {static_cast<vt::ObjectId>(r[0]), static_cast<std::int16_t>(r[1]), static_cast<std::int16_t>(r[2])});
            break;
        case vt::ListKind::References:
            changed.references.clear();
            for (const auto& r : rows) changed.references.push_back(static_cast<vt::ObjectId>(r[0]));
            break;
        case vt::ListKind::Macros:
            changed.macros.clear();
            for (const auto& r : rows)
                changed.macros.push_back({static_cast<std::uint8_t>(r[0]), static_cast<vt::ObjectId>(r[1])});
            break;
        case vt::ListKind::Points:
            changed.points.clear();
            for (const auto& r : rows)
                changed.points.push_back({static_cast<std::uint16_t>(r[0]), static_cast<std::uint16_t>(r[1])});
            break;
        case vt::ListKind::Labels:
            changed.labels.clear();
            for (const auto& r : rows)
                changed.labels.push_back({static_cast<vt::ObjectId>(r[0]), static_cast<vt::ObjectId>(r[1]),
                                          static_cast<std::uint8_t>(r[2]), static_cast<vt::ObjectId>(r[3])});
            break;
        default:
            break;
        }
        commit(changed, tr("Change %1").arg(QString::fromLatin1(vt::listKindName(kind).data())));
    });
    refresher = [this, editor, toRows] {
        if (const vt::VtObject* obj = object()) {
            editor->setRows(toRows(*obj));
        }
    };
    return editor;
}

void PropertyEditor::addUsageSection()
{
    QTreeWidgetItem* section = addSection(tr("Used by"));
    auto* label = new QLabel;
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(label, &QLabel::linkActivated, this,
            [this](const QString& link) { m_context->selectObject(static_cast<vt::ObjectId>(link.toInt())); });
    addWideRow(section, label, [this, label] {
        QStringList links;
        for (vt::ObjectId user : vt::usersOf(m_context->pool(), m_objectId)) {
            const vt::VtObject* o = objectById(user);
            links << QStringLiteral("<a href=\"%1\">%2</a>").arg(user).arg(ui::objectDisplayName(*o).toHtmlEscaped());
        }
        label->setText(links.isEmpty() ? tr("<i>not referenced by any object</i>") : links.join(QStringLiteral(", ")));
    });
}
