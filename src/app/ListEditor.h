#pragma once

#include "vtcore/ObjectPool.h"

#include <QString>
#include <QWidget>

#include <vector>

class QTableWidget;
class QToolButton;

// Column of a ListEditor row.
struct ListColumn {
    enum class Kind { Integer, ObjectRef, Event };

    QString title;
    Kind kind = Kind::Integer;
    int minimum = 0;
    int maximum = 65535;
    std::vector<vt::ObjectType> refTypes; // ObjectRef: allowed types, empty = any type
    bool nullAllowed = false;             // ObjectRef: offer "(none)"
};

// Small table editor for object lists (children, soft keys, macros, polygon points, ...).
// Every edit emits rowsEdited with the complete new list.
class ListEditor : public QWidget {
    Q_OBJECT

public:
    using Rows = std::vector<std::vector<int>>;

    ListEditor(const vt::ObjectPool& pool, std::vector<ListColumn> columns, QWidget* parent = nullptr);

    void setRows(const Rows& rows); // does not emit rowsEdited
    Rows rows() const { return m_rows; }

    // Values used for a new row; defaults to the first allowed value of each column.
    void setNewRow(std::vector<int> row) { m_newRow = std::move(row); }

signals:
    void rowsEdited(const ListEditor::Rows& rows);

private:
    void rebuild();
    QWidget* createCell(int row, int column);
    void cellChanged(int row, int column, int value);
    void updateButtons();

    const vt::ObjectPool& m_pool;
    std::vector<ListColumn> m_columns;
    Rows m_rows;
    std::vector<int> m_newRow;
    QTableWidget* m_table;
    QToolButton* m_add;
    QToolButton* m_remove;
    QToolButton* m_up;
    QToolButton* m_down;
};
