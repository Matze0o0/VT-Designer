#pragma once

#include "Document.h"

#include "vtrender/VtRenderer.h"

#include <QObject>
#include <QPointer>

// What is selected: an object, and if it was picked inside a parent, which entry of the parent.
struct Selection {
    vt::ObjectId object = vt::kNullObjectId;
    vt::ObjectId parent = vt::kNullObjectId;
    int index = -1;

    bool isValid() const { return object != vt::kNullObjectId; }
    bool operator==(const Selection&) const = default;
};

// Shared editor state: the current document, the selection and the object shown on the canvas.
// Widgets connect to this object once; it forwards the signals of whichever document is open.
class EditorContext : public QObject {
    Q_OBJECT

public:
    explicit EditorContext(QObject* parent = nullptr);

    Document* document() const { return m_document; }
    void setDocument(Document* document);

    const vt::ObjectPool& pool() const { return m_document->pool(); }

    Selection selection() const { return m_selection; }
    void select(const Selection& selection);
    void selectObject(vt::ObjectId id) { select({id, vt::kNullObjectId, -1}); }

    vt::ObjectId viewRoot() const { return m_viewRoot; }
    void setViewRoot(vt::ObjectId root);

    vtr::RenderSettings renderSettings() const;

    // Object copied with Edit > Copy (pasted as a duplicate of its contained subtree).
    vt::ObjectId clipboard() const { return m_clipboard; }
    void setClipboard(vt::ObjectId id) { m_clipboard = id; }

signals:
    void documentChanged();
    void poolChanged();
    void settingsChanged();
    void selectionChanged();
    void viewRootChanged();

private:
    void onPoolChanged();

    QPointer<Document> m_document;
    Selection m_selection;
    vt::ObjectId m_viewRoot = vt::kNullObjectId;
    vt::ObjectId m_clipboard = vt::kNullObjectId;
};
