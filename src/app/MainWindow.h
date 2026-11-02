#pragma once

#include "EditorContext.h"

#include "vtrender/VtRenderer.h"

#include <QMainWindow>

#include <memory>
#include <optional>

class MaskCanvas;
class PoolTree;
class ProblemsView;
class PropertyEditor;
class QAction;
class QComboBox;
class QLabel;
class QUndoGroup;
class Toolbox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    bool openFile(const QString& path);
    EditorContext* context() const { return m_context; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createDocks();
    void setDocument(std::unique_ptr<Document> document);
    void updateTitle();
    void updateViewRoots();
    void updateActions();
    bool maybeSave();

    void newDocument();
    void open();
    bool save();
    bool saveAs();
    void exportPool();
    void showSettings();

    void createObjectAt(vt::ObjectType type, std::optional<vtr::Placement> target, QPoint position);
    void createFromToolbox(vt::ObjectType type);
    void copySelection();
    void paste();
    void duplicateSelection();
    void deleteSelection();
    void deleteObject(vt::ObjectId id);
    std::optional<vtr::Placement> placementTargetFor(vt::ObjectType type) const;
    vt::ObjectId softKeyMaskInView() const;

    EditorContext* m_context;
    std::unique_ptr<Document> m_document;
    QUndoGroup* m_undoGroup;

    MaskCanvas* m_canvas = nullptr;
    PoolTree* m_tree = nullptr;
    PropertyEditor* m_properties = nullptr;
    Toolbox* m_toolbox = nullptr;
    ProblemsView* m_problems = nullptr;
    QComboBox* m_viewRoots = nullptr;
    QLabel* m_cursorLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_problemLabel = nullptr;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_quitAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_duplicateAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_frontAction = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomActualAction = nullptr;
    QAction* m_zoomFitAction = nullptr;
    QAction* m_gridAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QMenu* m_viewMenu = nullptr;
};
