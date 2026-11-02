#include "EditorContext.h"

EditorContext::EditorContext(QObject* parent)
    : QObject(parent)
{
}

void EditorContext::setDocument(Document* document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    m_selection = {};
    m_viewRoot = vt::kNullObjectId;
    m_clipboard = vt::kNullObjectId;
    if (m_document) {
        connect(m_document, &Document::poolChanged, this, &EditorContext::onPoolChanged);
        connect(m_document, &Document::settingsChanged, this, &EditorContext::settingsChanged);

        // Start with the active mask of the Working Set, or the first Data Mask.
        if (const vt::VtObject* ws = pool().workingSet()) {
            const auto active = static_cast<vt::ObjectId>(ws->get("activeMask"));
            if (pool().contains(active)) {
                m_viewRoot = active;
            }
        }
        if (m_viewRoot == vt::kNullObjectId) {
            for (const vt::VtObject& object : pool().objects()) {
                if (object.type == vt::ObjectType::DataMask) {
                    m_viewRoot = object.id;
                    break;
                }
            }
        }
    }
    emit documentChanged();
    emit viewRootChanged();
    emit selectionChanged();
}

void EditorContext::select(const Selection& selection)
{
    if (selection == m_selection) {
        return;
    }
    m_selection = selection;
    emit selectionChanged();
}

void EditorContext::setViewRoot(vt::ObjectId root)
{
    if (root == m_viewRoot) {
        return;
    }
    m_viewRoot = root;
    emit viewRootChanged();
}

vtr::RenderSettings EditorContext::renderSettings() const
{
    vtr::RenderSettings settings;
    if (m_document) {
        const vt::DesignerSettings& d = m_document->project().designer;
        settings.dataMaskSize = d.dataMaskSize;
        settings.softKeyWidth = d.softKeyWidth;
        settings.softKeyHeight = d.softKeyHeight;
        settings.physicalSoftKeys = d.physicalSoftKeys;
    }
    return settings;
}

void EditorContext::onPoolChanged()
{
    // Drop references to objects that no longer exist.
    if (m_selection.isValid() && !pool().contains(m_selection.object)) {
        m_selection = {};
        emit selectionChanged();
    }
    if (m_viewRoot != vt::kNullObjectId && !pool().contains(m_viewRoot)) {
        m_viewRoot = vt::kNullObjectId;
        emit viewRootChanged();
    }
    emit poolChanged();
}
