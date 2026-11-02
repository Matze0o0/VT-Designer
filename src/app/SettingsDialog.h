#pragma once

#include "vtcore/ProjectFile.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;

// Pool settings (VT version, preview terminal, export options) and the UI language.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(const vt::Project& project, QWidget* parent = nullptr);

    vt::VtVersion version() const;
    vt::DesignerSettings designerSettings() const;
    vt::ExportOptions exportOptions() const;

    // Stored application setting: "" = system language, "en", "de".
    static QString languageSetting();

private:
    void saveLanguage();

    QComboBox* m_version;
    QSpinBox* m_maskSize;
    QSpinBox* m_keyWidth;
    QSpinBox* m_keyHeight;
    QSpinBox* m_physicalKeys;
    QLineEdit* m_poolName;
    QLineEdit* m_prefix;
    QComboBox* m_headerStyle;
    QComboBox* m_language;
};
