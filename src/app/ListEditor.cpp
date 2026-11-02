#include "ListEditor.h"

#include "AttributeInfo.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

ListEditor::ListEditor(const vt::ObjectPool& pool, std::vector<ListColumn> columns, QWidget* parent)
    : QWidget(parent)
    , m_pool(pool)
    , m_columns(std::move(columns))
    , m_table(new QTableWidget(this))
    , m_add(new QToolButton(this))
    , m_remove(new QToolButton(this))
    , m_up(new QToolButton(this))
    , m_down(new QToolButton(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_table->setColumnCount(static_cast<int>(m_columns.size()));
    QStringList headers;
    for (const ListColumn& column : m_columns) {
        headers << column.title;
    }
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    m_add->setText(tr("Add"));
    m_remove->setText(tr("Remove"));
    m_up->setArrowType(Qt::UpArrow);
    m_down->setArrowType(Qt::DownArrow);
    m_up->setToolTip(tr("Move up"));
    m_down->setToolTip(tr("Move down"));
    for (QToolButton* button : {m_add, m_remove, m_up, m_down}) {
        buttons->addWidget(button);
    }
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(m_add, &QToolButton::clicked, this, [this] {
        std::vector<int> row = m_newRow;
        row.resize(m_columns.size(), 0);
        if (m_newRow.empty()) {
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                const ListColumn& column = m_columns[c];
                if (column.kind == ListColumn::Kind::ObjectRef) {
                    row[c] = vt::kNullObjectId;
                    for (const vt::VtObject& object : m_pool.objects()) {
                        if (column.refTypes.empty() || std::find(column.refTypes.begin(), column.refTypes.end(),
                                                                 object.type) != column.refTypes.end()) {
                            row[c] = object.id;
                            break;
                        }
                    }
                } else if (column.kind == ListColumn::Kind::Event) {
                    row[c] = 1;
                } else {
                    row[c] = std::clamp(0, column.minimum, column.maximum);
                }
            }
        }
        m_rows.push_back(row);
        rebuild();
        m_table->selectRow(static_cast<int>(m_rows.size()) - 1);
        emit rowsEdited(m_rows);
    });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0 || row >= static_cast<int>(m_rows.size())) {
            return;
        }
        m_rows.erase(m_rows.begin() + row);
        rebuild();
        emit rowsEdited(m_rows);
    });
    auto move = [this](int offset) {
        const int row = m_table->currentRow();
        const int target = row + offset;
        if (row < 0 || target < 0 || target >= static_cast<int>(m_rows.size())) {
            return;
        }
        std::swap(m_rows[static_cast<std::size_t>(row)], m_rows[static_cast<std::size_t>(target)]);
        rebuild();
        m_table->selectRow(target);
        emit rowsEdited(m_rows);
    };
    connect(m_up, &QToolButton::clicked, this, [move] { move(-1); });
    connect(m_down, &QToolButton::clicked, this, [move] { move(1); });
    connect(m_table, &QTableWidget::currentCellChanged, this, &ListEditor::updateButtons);
    rebuild();
}

void ListEditor::setRows(const Rows& rows)
{
    if (rows == m_rows) {
        return;
    }
    m_rows = rows;
    rebuild();
}

void ListEditor::rebuild()
{
    const int current = m_table->currentRow();
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    for (int r = 0; r < static_cast<int>(m_rows.size()); ++r) {
        for (int c = 0; c < static_cast<int>(m_columns.size()); ++c) {
            m_table->setCellWidget(r, c, createCell(r, c));
        }
    }
    const int visibleRows = std::clamp(static_cast<int>(m_rows.size()), 2, 8);
    m_table->setFixedHeight(m_table->horizontalHeader()->height() + visibleRows * 24 + 4);
    if (current >= 0 && current < m_table->rowCount()) {
        m_table->selectRow(current);
    }
    updateButtons();
}

QWidget* ListEditor::createCell(int row, int column)
{
    const ListColumn& def = m_columns[static_cast<std::size_t>(column)];
    const int value = m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];

    if (def.kind == ListColumn::Kind::Integer) {
        auto* spin = new QSpinBox;
        spin->setRange(def.minimum, def.maximum);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        spin->setFrame(false);
        connect(spin, &QSpinBox::valueChanged, this, [this, row, column](int v) { cellChanged(row, column, v); });
        return spin;
    }

    auto* combo = new QComboBox;
    combo->setFrame(false);
    if (def.kind == ListColumn::Kind::Event) {
        for (const ui::NamedValue& event : ui::macroEvents()) {
            combo->addItem(event.name, event.value);
        }
    } else {
        if (def.nullAllowed) {
            combo->addItem(tr("(none)"), static_cast<int>(vt::kNullObjectId));
        }
        for (const vt::VtObject& object : m_pool.objects()) {
            if (def.refTypes.empty() ||
                std::find(def.refTypes.begin(), def.refTypes.end(), object.type) != def.refTypes.end()) {
                combo->addItem(ui::objectDisplayName(object), static_cast<int>(object.id));
            }
        }
    }
    int index = combo->findData(value);
    if (index < 0) {
        combo->addItem(tr("%1 (invalid)").arg(value), value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
    connect(combo, &QComboBox::activated, this,
            [this, combo, row, column](int i) { cellChanged(row, column, combo->itemData(i).toInt()); });
    return combo;
}

void ListEditor::cellChanged(int row, int column, int value)
{
    int& cell = m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    if (cell == value) {
        return;
    }
    cell = value;
    emit rowsEdited(m_rows);
}

void ListEditor::updateButtons()
{
    const int row = m_table->currentRow();
    const int count = static_cast<int>(m_rows.size());
    m_remove->setEnabled(row >= 0 && row < count);
    m_up->setEnabled(row > 0 && row < count);
    m_down->setEnabled(row >= 0 && row + 1 < count);
}
