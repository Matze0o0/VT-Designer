#pragma once

#include "EditorContext.h"

#include <QTreeWidget>

class QTimer;

// Validation results of the current pool, refreshed shortly after every change.
class ProblemsView : public QTreeWidget {
    Q_OBJECT

public:
    explicit ProblemsView(EditorContext* context, QWidget* parent = nullptr);

    int errorCount() const { return m_errors; }

signals:
    void countsChanged(int errors, int warnings);

private:
    void validate();

    EditorContext* m_context;
    QTimer* m_timer;
    int m_errors = 0;
};
