#include "ProblemsView.h"

#include "AttributeInfo.h"

#include "vtcore/Validator.h"

#include <QApplication>
#include <QHeaderView>
#include <QStyle>
#include <QTimer>

ProblemsView::ProblemsView(EditorContext* context, QWidget* parent)
    : QTreeWidget(parent)
    , m_context(context)
    , m_timer(new QTimer(this))
{
    setColumnCount(2);
    setHeaderLabels({tr("Object"), tr("Message")});
    setRootIsDecorated(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_timer->setSingleShot(true);
    m_timer->setInterval(250);

    connect(m_timer, &QTimer::timeout, this, &ProblemsView::validate);
    connect(m_context, &EditorContext::poolChanged, m_timer, qOverload<>(&QTimer::start));
    connect(m_context, &EditorContext::documentChanged, m_timer, qOverload<>(&QTimer::start));
    connect(m_context, &EditorContext::settingsChanged, m_timer, qOverload<>(&QTimer::start));
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const int id = item->data(0, Qt::UserRole).toInt();
        if (m_context->document() != nullptr && m_context->pool().contains(static_cast<vt::ObjectId>(id))) {
            m_context->selectObject(static_cast<vt::ObjectId>(id));
        }
    });
}

void ProblemsView::validate()
{
    clear();
    m_errors = 0;
    int warnings = 0;
    if (m_context->document() == nullptr) {
        emit countsChanged(0, 0);
        return;
    }
    const QIcon errorIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    const std::vector<vt::Diagnostic> diagnostics = vt::validatePool(m_context->pool());
    for (const vt::Diagnostic& d : diagnostics) {
        const bool error = d.severity == vt::Severity::Error;
        (error ? m_errors : warnings)++;
        QString objectText = tr("Pool");
        if (const vt::VtObject* object = m_context->pool().find(d.object)) {
            objectText = ui::objectDisplayName(*object);
        }
        auto* item = new QTreeWidgetItem(this, {objectText, QString::fromStdString(d.message)});
        item->setIcon(0, error ? errorIcon : warningIcon);
        item->setData(0, Qt::UserRole, static_cast<int>(d.object));
        item->setToolTip(1, item->text(1));
    }
    emit countsChanged(m_errors, warnings);
}
