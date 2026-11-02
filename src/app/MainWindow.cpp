#include "MainWindow.h"

#include "AttributeInfo.h"
#include "MaskCanvas.h"
#include "ObjectFactory.h"
#include "ObjectIcons.h"
#include "ObjectPalette.h"
#include "PoolTree.h"
#include "ProblemsView.h"
#include "PropertyEditor.h"
#include "SettingsDialog.h"

#include "vtcore/Exporters.h"
#include "vtcore/Hierarchy.h"
#include "vtcore/IopCodec.h"
#include "vtcore/PoolEditing.h"
#include "vtcore/Validator.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUndoGroup>
#include <QUndoView>
#include <QVBoxLayout>

namespace {

using T = vt::ObjectType;

// Bump when the dock layout changes so stale saved layouts are ignored.
constexpr int kWindowStateVersion = 2;

vt::Project templateProject()
{
    vt::Project project{vt::ObjectPool(vt::VtVersion::V4)};
    std::vector<vt::VtObject> extra;
    vt::VtObject ws = ui::createObject(project.pool, T::WorkingSet, extra);
    for (vt::VtObject& o : extra) {
        project.pool.add(std::move(o));
    }
    project.pool.add(std::move(ws));
    return project;
}

bool isTopLevelType(T type)
{
    switch (type) {
    case T::DataMask:
    case T::AlarmMask:
    case T::SoftKeyMask:
    case T::WindowMask:
    case T::KeyGroup:
    case T::WorkingSet:
    case T::AuxiliaryFunctionType1:
    case T::AuxiliaryInputType1:
    case T::AuxiliaryFunctionType2:
    case T::AuxiliaryInputType2: return true;
    default: return false;
    }
}

} // namespace

MainWindow::MainWindow()
    : m_context(new EditorContext(this))
    , m_undoGroup(new QUndoGroup(this))
{
    // Object palette directly above the canvas.
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    m_palette = new ObjectPalette(m_context, central);
    auto* line = new QFrame(central);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    m_canvas = new MaskCanvas(m_context, central);
    centralLayout->addWidget(m_palette);
    centralLayout->addWidget(line);
    centralLayout->addWidget(m_canvas, 1);
    setCentralWidget(central);

    createActions();
    createDocks();
    createMenus();

    m_cursorLabel = new QLabel;
    m_zoomLabel = new QLabel;
    m_problemLabel = new QLabel;
    statusBar()->addPermanentWidget(m_problemLabel);
    statusBar()->addPermanentWidget(m_cursorLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);

    connect(m_canvas, &MaskCanvas::zoomChanged, this,
            [this](double zoom) { m_zoomLabel->setText(QStringLiteral("%1 %").arg(qRound(zoom * 100))); });
    connect(m_canvas, &MaskCanvas::cursorMoved, this, [this](QPoint p, bool inside) {
        m_cursorLabel->setText(inside ? QStringLiteral("x %1  y %2").arg(p.x()).arg(p.y()) : QString());
    });
    connect(m_canvas, &MaskCanvas::objectDropped, this, &MainWindow::createObjectAt);
    connect(m_palette, &ObjectPalette::createRequested, this, &MainWindow::createFromToolbox);
    connect(m_tree, &PoolTree::deleteRequested, this, &MainWindow::deleteObject);
    connect(m_problems, &ProblemsView::countsChanged, this, [this](int errors, int warnings) {
        m_problemLabel->setText(tr("%n error(s)", nullptr, errors) + QStringLiteral(", ") +
                                tr("%n warning(s)", nullptr, warnings));
        m_problemLabel->setStyleSheet(errors > 0 ? QStringLiteral("color: #c00000; font-weight: bold") : QString());
    });
    connect(m_context, &EditorContext::poolChanged, this, &MainWindow::updateViewRoots);
    connect(m_context, &EditorContext::viewRootChanged, this, &MainWindow::updateViewRoots);
    connect(m_context, &EditorContext::selectionChanged, this, &MainWindow::updateActions);

    const QList<QAction*> editActions = {m_copyAction, m_pasteAction, m_duplicateAction, m_deleteAction,
                                         m_frontAction, m_backAction};
    m_canvas->setContextActions(editActions);
    m_tree->setContextActions({m_copyAction, m_pasteAction, m_duplicateAction});

    resize(1400, 900);
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray(), kWindowStateVersion);

    setDocument(std::make_unique<Document>(templateProject()));
}

MainWindow::~MainWindow()
{
    // Widgets reference the document through the context, so drop them first.
    m_context->setDocument(nullptr);
}

void MainWindow::createActions()
{
    m_newAction = new QAction(tr("&New"), this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newDocument);

    m_openAction = new QAction(tr("&Open…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::open);

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::save);

    m_saveAsAction = new QAction(tr("Save &as…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    m_exportAction = new QAction(tr("&Export pool…"), this);
    m_exportAction->setShortcut(Qt::CTRL | Qt::Key_E);
    m_exportAction->setToolTip(tr("Write .iop file, ID header and C array"));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportPool);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = m_undoGroup->createUndoAction(this, tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoGroup->createRedoAction(this, tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);

    m_copyAction = new QAction(tr("&Copy"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelection);

    m_pasteAction = new QAction(tr("&Paste"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::paste);

    m_duplicateAction = new QAction(tr("D&uplicate"), this);
    m_duplicateAction->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(m_duplicateAction, &QAction::triggered, this, &MainWindow::duplicateSelection);

    m_deleteAction = new QAction(tr("&Delete"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setToolTip(tr("Remove the selected object from its parent"));
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelection);

    m_frontAction = new QAction(tr("Bring to &front"), this);
    m_frontAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Up);
    connect(m_frontAction, &QAction::triggered, this, [this] { m_canvas->reorderSelection(true); });

    m_backAction = new QAction(tr("Send to &back"), this);
    m_backAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Down);
    connect(m_backAction, &QAction::triggered, this, [this] { m_canvas->reorderSelection(false); });

    m_zoomInAction = new QAction(tr("Zoom &in"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, m_canvas, &MaskCanvas::zoomIn);

    m_zoomOutAction = new QAction(tr("Zoom &out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, m_canvas, &MaskCanvas::zoomOut);

    m_zoomActualAction = new QAction(tr("&Actual size"), this);
    m_zoomActualAction->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(m_zoomActualAction, &QAction::triggered, this, [this] { m_canvas->setZoom(1.0); });

    m_zoomFitAction = new QAction(tr("&Fit to window"), this);
    m_zoomFitAction->setShortcut(Qt::CTRL | Qt::Key_9);
    connect(m_zoomFitAction, &QAction::triggered, m_canvas, &MaskCanvas::zoomToFit);

    m_gridAction = new QAction(tr("Show &grid"), this);
    m_gridAction->setCheckable(true);
    m_gridAction->setChecked(m_canvas->showGrid());
    connect(m_gridAction, &QAction::toggled, m_canvas, &MaskCanvas::setShowGrid);

    m_snapAction = new QAction(tr("&Snap to grid"), this);
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(m_canvas->snapToGrid());
    connect(m_snapAction, &QAction::toggled, m_canvas, &MaskCanvas::setSnapToGrid);

    m_settingsAction = new QAction(tr("Pool &settings…"), this);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);

    m_aboutAction = new QAction(tr("&About VT-Designer"), this);
    connect(m_aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About VT-Designer"),
                           tr("<h3>VT-Designer %1</h3><p>Graphical editor for ISOBUS virtual terminal object pools "
                              "(ISO 11783-6, VT version 3 to 5).</p><p>Built with Qt %2.</p>")
                               .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
    });
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({m_newAction, m_openAction, m_saveAction, m_saveAsAction});
    file->addSeparator();
    file->addAction(m_exportAction);
    file->addSeparator();
    file->addAction(m_quitAction);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undoAction, m_redoAction});
    edit->addSeparator();
    edit->addActions({m_copyAction, m_pasteAction, m_duplicateAction, m_deleteAction});
    edit->addSeparator();
    edit->addActions({m_frontAction, m_backAction});

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addActions({m_zoomInAction, m_zoomOutAction, m_zoomActualAction, m_zoomFitAction});
    m_viewMenu->addSeparator();
    m_viewMenu->addActions({m_gridAction, m_snapAction});
    m_viewMenu->addSeparator();
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        m_viewMenu->addAction(dock->toggleViewAction());
    }

    QMenu* pool = menuBar()->addMenu(tr("&Pool"));
    pool->addAction(m_settingsAction);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_aboutAction);

    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->addActions({m_newAction, m_openAction, m_saveAction});
    toolbar->addSeparator();
    toolbar->addActions({m_undoAction, m_redoAction});
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr(" View: ")));
    m_viewRoots = new QComboBox;
    m_viewRoots->setMinimumContentsLength(24);
    m_viewRoots->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    toolbar->addWidget(m_viewRoots);
    connect(m_viewRoots, &QComboBox::activated, this, [this](int index) {
        m_context->setViewRoot(static_cast<vt::ObjectId>(m_viewRoots->itemData(index).toInt()));
    });
    toolbar->addSeparator();
    toolbar->addActions({m_zoomOutAction, m_zoomInAction, m_zoomFitAction, m_gridAction, m_snapAction});
    toolbar->addSeparator();
    toolbar->addActions({m_settingsAction, m_exportAction});
}

void MainWindow::createDocks()
{
    auto addDock = [this](const QString& title, const char* name, QWidget* widget, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QString::fromLatin1(name));
        dock->setWidget(widget);
        addDockWidget(area, dock);
        return dock;
    };
    m_tree = new PoolTree(m_context);
    m_properties = new PropertyEditor(m_context);
    m_problems = new ProblemsView(m_context);
    auto* history = new QUndoView(m_undoGroup);

    QDockWidget* treeDock = addDock(tr("Pool"), "poolDock", m_tree, Qt::LeftDockWidgetArea);
    QDockWidget* propertiesDock = addDock(tr("Properties"), "propertiesDock", m_properties, Qt::RightDockWidgetArea);
    QDockWidget* problemsDock = addDock(tr("Problems"), "problemsDock", m_problems, Qt::BottomDockWidgetArea);
    QDockWidget* historyDock = addDock(tr("History"), "historyDock", history, Qt::BottomDockWidgetArea);
    tabifyDockWidget(problemsDock, historyDock);
    problemsDock->raise();
    resizeDocks({treeDock, propertiesDock}, {260, 380}, Qt::Horizontal);
    resizeDocks({problemsDock}, {140}, Qt::Vertical);
}

void MainWindow::setDocument(std::unique_ptr<Document> document)
{
    std::unique_ptr<Document> previous = std::move(m_document);
    m_document = std::move(document);
    m_undoGroup->addStack(m_document->undoStack());
    m_undoGroup->setActiveStack(m_document->undoStack());
    connect(m_document.get(), &Document::modifiedChanged, this, &MainWindow::updateTitle);
    m_context->setDocument(m_document.get());
    if (previous) {
        m_undoGroup->removeStack(previous->undoStack());
    }
    updateTitle();
    updateViewRoots();
    updateActions();
    // Fit once the window has its final size.
    QTimer::singleShot(0, m_canvas, &MaskCanvas::zoomToFit);
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1[*] – VT-Designer").arg(m_document->displayName()));
    setWindowModified(m_document->isModified());
}

void MainWindow::updateViewRoots()
{
    if (m_viewRoots == nullptr || m_context->document() == nullptr) {
        return;
    }
    const QSignalBlocker block(m_viewRoots);
    m_viewRoots->clear();
    const std::vector<T> order = {T::DataMask,   T::AlarmMask, T::SoftKeyMask, T::WindowMask,
                                  T::WorkingSet, T::Key,       T::Container,   T::Button,
                                  T::Animation,  T::AuxiliaryFunctionType2, T::AuxiliaryInputType2,
                                  T::AuxiliaryFunctionType1, T::AuxiliaryInputType1};
    for (T type : order) {
        for (const vt::VtObject& object : m_context->pool().objects()) {
            const bool important = isTopLevelType(type) || object.id == m_context->viewRoot();
            if (object.type == type && important) {
                m_viewRoots->addItem(QStringLiteral("%1: %2").arg(ui::typeLabel(type), ui::objectDisplayName(object)),
                                     static_cast<int>(object.id));
            }
        }
    }
    m_viewRoots->setCurrentIndex(m_viewRoots->findData(static_cast<int>(m_context->viewRoot())));
}

void MainWindow::updateActions()
{
    const Selection s = m_context->selection();
    m_copyAction->setEnabled(s.isValid());
    m_duplicateAction->setEnabled(s.isValid());
    m_deleteAction->setEnabled(s.isValid());
    const bool inParent = s.isValid() && s.parent != vt::kNullObjectId;
    m_frontAction->setEnabled(inParent);
    m_backAction->setEnabled(inParent);
}

bool MainWindow::maybeSave()
{
    if (!m_document || !m_document->isModified()) {
        return true;
    }
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
                                             tr("Do you want to save the changes to %1?").arg(m_document->displayName()),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return save();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(kWindowStateVersion));
    event->accept();
}

void MainWindow::newDocument()
{
    if (maybeSave()) {
        setDocument(std::make_unique<Document>(templateProject()));
    }
}

void MainWindow::open()
{
    if (!maybeSave()) {
        return;
    }
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open"), settings.value(QStringLiteral("lastDirectory")).toString(),
        tr("VT-Designer projects (*.vtp);;ISOBUS object pools (*.iop);;All files (*)"));
    if (!path.isEmpty()) {
        settings.setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
        openFile(path);
    }
}

bool MainWindow::openFile(const QString& path)
{
    try {
        const std::filesystem::path file = path.toStdWString();
        if (QFileInfo(path).suffix().compare(QStringLiteral("iop"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            const QStringList versions = {QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5")};
            const QString choice = QInputDialog::getItem(this, tr("Import object pool"),
                                                         tr("VT version the pool was made for:"), versions, 1, false,
                                                         &ok);
            if (!ok) {
                return false;
            }
            vt::Project project(vt::decodePool(vt::readBinaryFile(file), *vt::vtVersionFromInt(choice.toInt())));
            project.exportOptions.poolName = QFileInfo(path).completeBaseName().toStdString();
            setDocument(std::make_unique<Document>(std::move(project)));
            statusBar()->showMessage(tr("Imported %1 objects from %2").arg(m_document->pool().size()).arg(path), 5000);
        } else {
            setDocument(std::make_unique<Document>(vt::loadProject(file), path));
        }
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Open"), tr("%1 could not be opened:\n%2").arg(path, QString::fromUtf8(e.what())));
        return false;
    }
}

bool MainWindow::save()
{
    if (m_document->filePath().isEmpty()) {
        return saveAs();
    }
    try {
        vt::saveProject(m_document->project(), m_document->filePath().toStdWString());
        m_document->markSaved(m_document->filePath());
        statusBar()->showMessage(tr("Saved %1").arg(m_document->filePath()), 3000);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save"), QString::fromUtf8(e.what()));
        return false;
    }
}

bool MainWindow::saveAs()
{
    QSettings settings;
    QString suggestion = m_document->filePath();
    if (suggestion.isEmpty()) {
        suggestion = QDir(settings.value(QStringLiteral("lastDirectory")).toString())
                         .filePath(QString::fromStdString(m_document->project().exportOptions.poolName) +
                                   QStringLiteral(".vtp"));
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save as"), suggestion, tr("VT-Designer projects (*.vtp)"));
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".vtp");
    }
    settings.setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
    m_document->markSaved(path);
    return save();
}

void MainWindow::exportPool()
{
    const std::vector<vt::Diagnostic> diagnostics = vt::validatePool(m_document->pool());
    const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(),
                                      [](const vt::Diagnostic& d) { return d.severity == vt::Severity::Error; });
    if (errors > 0) {
        QMessageBox::warning(this, tr("Export"),
                             tr("The pool has %n error(s). Please fix them first (see Problems).", nullptr,
                                static_cast<int>(errors)));
        return;
    }
    QSettings settings;
    QString start = settings.value(QStringLiteral("lastExportDirectory")).toString();
    if (start.isEmpty() && !m_document->filePath().isEmpty()) {
        start = QFileInfo(m_document->filePath()).absolutePath();
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Export pool to folder"), start);
    if (directory.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("lastExportDirectory"), directory);
    try {
        const vt::ExportOptions& options = m_document->project().exportOptions;
        const std::filesystem::path dir = directory.toStdWString();
        const std::vector<std::uint8_t> iop = vt::encodePool(m_document->pool());
        vt::writeBinaryFile(dir / vt::iopFileName(options), iop);
        vt::writeTextFile(dir / vt::idHeaderFileName(options), vt::exportIdHeader(m_document->pool(), options));
        vt::writeTextFile(dir / vt::arrayHeaderFileName(options), vt::exportPoolArrayHeader(options));
        vt::writeTextFile(dir / vt::arraySourceFileName(options), vt::exportPoolArraySource(iop, options));
        statusBar()->showMessage(
            tr("Exported %1 (%2 bytes) with ID header and C array to %3")
                .arg(QString::fromStdString(vt::iopFileName(options)))
                .arg(iop.size())
                .arg(directory),
            8000);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(m_document->project(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_document->setVtVersion(dialog.version());
    m_document->setDesignerSettings(dialog.designerSettings());
    m_document->setExportOptions(dialog.exportOptions());
}

vt::ObjectId MainWindow::softKeyMaskInView() const
{
    const vt::VtObject* root = m_context->pool().find(m_context->viewRoot());
    if (root == nullptr) {
        return vt::kNullObjectId;
    }
    if (root->type == T::SoftKeyMask) {
        return root->id;
    }
    if (root->type == T::DataMask || root->type == T::AlarmMask) {
        const auto id = static_cast<vt::ObjectId>(root->get("softKeyMask"));
        const vt::VtObject* keys = m_context->pool().find(id);
        return keys != nullptr && keys->type == T::SoftKeyMask ? id : vt::kNullObjectId;
    }
    return vt::kNullObjectId;
}

void MainWindow::createObjectAt(vt::ObjectType type, std::optional<vtr::Placement> target, QPoint position)
{
    const vt::ObjectPool& pool = m_context->pool();
    std::vector<vt::VtObject> upserts;
    vt::VtObject object = ui::createObject(pool, type, upserts);
    if (type == T::PictureGraphic && !ui::importPicture(this, object, pool.version())) {
        return;
    }
    const vt::ObjectId id = object.id;
    upserts.push_back(object);

    Selection selection{id, vt::kNullObjectId, -1};
    const vt::VtObject* root = pool.find(m_context->viewRoot());
    const QString text = tr("Add %1").arg(ui::typeLabel(type));

    if (type == T::Key && softKeyMaskInView() != vt::kNullObjectId) {
        vt::VtObject keys = *pool.find(softKeyMaskInView());
        keys.references.push_back(id);
        selection = {id, keys.id, static_cast<int>(keys.references.size()) - 1};
        upserts.push_back(std::move(keys));
    } else if (type == T::SoftKeyMask && root != nullptr && (root->type == T::DataMask || root->type == T::AlarmMask) &&
               root->get("softKeyMask") == vt::kNullObjectId) {
        vt::VtObject mask = *root;
        mask.set("softKeyMask", id);
        upserts.push_back(std::move(mask));
    } else if (target) {
        vt::VtObject parent = *pool.find(target->object);
        const QPoint local = position - target->rect.topLeft();
        parent.children.push_back({id, static_cast<std::int16_t>(local.x()), static_cast<std::int16_t>(local.y())});
        selection = {id, parent.id, static_cast<int>(parent.children.size()) - 1};
        upserts.push_back(std::move(parent));
    }

    m_document->commit(text, upserts);
    if (type == T::DataMask || type == T::AlarmMask || type == T::WindowMask) {
        m_context->setViewRoot(id);
    }
    m_context->select(selection);
    if (selection.parent == vt::kNullObjectId && !isTopLevelType(type) && type != T::SoftKeyMask) {
        statusBar()->showMessage(tr("%1 was added to the pool without a position. Drag it from the toolbox onto a "
                                    "mask to place it.")
                                     .arg(ui::objectDisplayName(*m_context->pool().find(id))),
                                 6000);
    }
}

std::optional<vtr::Placement> MainWindow::placementTargetFor(vt::ObjectType type) const
{
    const std::vector<vtr::Placement>& layout = m_canvas->currentLayout();
    const Selection s = m_context->selection();
    const vt::VtVersion version = m_context->pool().version();
    // Prefer the selected object, then its parent, then the view root.
    for (const vt::ObjectId candidate : {s.object, s.parent, m_context->viewRoot()}) {
        const vt::VtObject* object = m_context->pool().find(candidate);
        if (object == nullptr || !ui::canPlaceChild(object->type, type, version)) {
            continue;
        }
        for (const vtr::Placement& p : layout) {
            if (p.object == candidate) {
                return p;
            }
        }
    }
    return std::nullopt;
}

void MainWindow::createFromToolbox(vt::ObjectType type)
{
    const std::optional<vtr::Placement> target = placementTargetFor(type);
    const QPoint position = target ? target->rect.topLeft() + QPoint(10, 10) : QPoint();
    createObjectAt(type, target, position);
}

void MainWindow::copySelection()
{
    if (m_context->selection().isValid()) {
        m_context->setClipboard(m_context->selection().object);
        statusBar()->showMessage(tr("Copied %1").arg(ui::objectDisplayName(*m_context->pool().find(m_context->clipboard()))),
                                 3000);
    }
}

void MainWindow::paste()
{
    const vt::ObjectPool& pool = m_context->pool();
    if (!pool.contains(m_context->clipboard())) {
        return;
    }
    std::vector<vt::VtObject> copies = vt::duplicateSubtree(pool, m_context->clipboard());
    const vt::VtObject& copy = copies.front();
    const vt::ObjectId id = copy.id;
    const T type = copy.type;
    Selection selection{id, vt::kNullObjectId, -1};

    if (type == T::Key && softKeyMaskInView() != vt::kNullObjectId) {
        vt::VtObject keys = *pool.find(softKeyMaskInView());
        keys.references.push_back(id);
        selection = {id, keys.id, static_cast<int>(keys.references.size()) - 1};
        copies.push_back(std::move(keys));
    } else if (const std::optional<vtr::Placement> target = placementTargetFor(type)) {
        vt::VtObject parent = *pool.find(target->object);
        QPoint position(10, 10);
        // Next to the original if it lives in the same parent.
        for (const vt::ChildRef& child : parent.children) {
            if (child.id == m_context->clipboard()) {
                position = QPoint(child.x + 10, child.y + 10);
            }
        }
        parent.children.push_back({id, static_cast<std::int16_t>(position.x()), static_cast<std::int16_t>(position.y())});
        selection = {id, parent.id, static_cast<int>(parent.children.size()) - 1};
        copies.push_back(std::move(parent));
    }
    m_document->commit(tr("Paste %1").arg(ui::typeLabel(type)), copies);
    m_context->select(selection);
}

void MainWindow::duplicateSelection()
{
    copySelection();
    paste();
}

void MainWindow::deleteSelection()
{
    const Selection s = m_context->selection();
    if (!s.isValid()) {
        return;
    }
    if (s.parent != vt::kNullObjectId && s.index >= 0) {
        m_document->removePlacement(s.parent, s.index,
                                    tr("Remove %1").arg(ui::typeLabel(m_context->pool().find(s.object)->type)));
        m_context->select({});
        return;
    }
    deleteObject(s.object);
}

void MainWindow::deleteObject(vt::ObjectId id)
{
    const vt::VtObject* object = m_context->pool().find(id);
    if (object == nullptr) {
        return;
    }
    const std::vector<vt::ObjectId> users = vt::usersOf(m_context->pool(), id);
    QString question = tr("Delete %1 from the pool?").arg(ui::objectDisplayName(*object));
    if (!users.empty()) {
        question += QLatin1Char('\n') + tr("It is referenced by %n object(s); these references are removed.", nullptr,
                                           static_cast<int>(users.size()));
    }
    if (QMessageBox::question(this, tr("Delete object"), question) != QMessageBox::Yes) {
        return;
    }
    m_document->removeObjects({id}, tr("Delete %1").arg(ui::typeLabel(object->type)));
}
