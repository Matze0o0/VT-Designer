#include "SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const vt::Project& project, QWidget* parent)
    : QDialog(parent)
    , m_version(new QComboBox)
    , m_maskSize(new QSpinBox)
    , m_keyWidth(new QSpinBox)
    , m_keyHeight(new QSpinBox)
    , m_physicalKeys(new QSpinBox)
    , m_poolName(new QLineEdit)
    , m_prefix(new QLineEdit)
    , m_headerStyle(new QComboBox)
    , m_language(new QComboBox)
{
    setWindowTitle(tr("Pool settings"));
    auto* layout = new QVBoxLayout(this);

    auto* poolBox = new QGroupBox(tr("Object pool"));
    auto* poolForm = new QFormLayout(poolBox);
    for (int v = vt::toInt(vt::kMinSupportedVersion); v <= vt::toInt(vt::kMaxSupportedVersion); ++v) {
        m_version->addItem(tr("VT version %1").arg(v), v);
    }
    m_version->setCurrentIndex(m_version->findData(vt::toInt(project.pool.version())));
    poolForm->addRow(tr("VT version:"), m_version);
    layout->addWidget(poolBox);

    auto* terminalBox = new QGroupBox(tr("Preview terminal"));
    auto* terminalForm = new QFormLayout(terminalBox);
    m_maskSize->setRange(200, 2000);
    m_maskSize->setSuffix(tr(" px"));
    m_maskSize->setValue(project.designer.dataMaskSize);
    m_keyWidth->setRange(60, 400);
    m_keyWidth->setSuffix(tr(" px"));
    m_keyWidth->setValue(project.designer.softKeyWidth);
    m_keyHeight->setRange(32, 400);
    m_keyHeight->setSuffix(tr(" px"));
    m_keyHeight->setValue(project.designer.softKeyHeight);
    m_physicalKeys->setRange(1, 64);
    m_physicalKeys->setValue(project.designer.physicalSoftKeys);
    terminalForm->addRow(tr("Data Mask size (square):"), m_maskSize);
    terminalForm->addRow(tr("Soft Key designator width:"), m_keyWidth);
    terminalForm->addRow(tr("Soft Key designator height:"), m_keyHeight);
    terminalForm->addRow(tr("Soft keys per column:"), m_physicalKeys);
    layout->addWidget(terminalBox);

    auto* exportBox = new QGroupBox(tr("Export"));
    auto* exportForm = new QFormLayout(exportBox);
    const auto* identifier = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")), this);
    m_poolName->setValidator(identifier);
    m_poolName->setText(QString::fromStdString(project.exportOptions.poolName));
    m_prefix->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9_]*")), this));
    m_prefix->setText(QString::fromStdString(project.exportOptions.symbolPrefix));
    m_headerStyle->addItem(tr("#define (C and C++)"), static_cast<int>(vt::HeaderStyle::Define));
    m_headerStyle->addItem(tr("enum (C and C++)"), static_cast<int>(vt::HeaderStyle::Enum));
    m_headerStyle->addItem(tr("inline constexpr (C++17)"), static_cast<int>(vt::HeaderStyle::Constexpr));
    m_headerStyle->setCurrentIndex(m_headerStyle->findData(static_cast<int>(project.exportOptions.headerStyle)));
    exportForm->addRow(tr("Pool name:"), m_poolName);
    exportForm->addRow(tr("Symbol prefix:"), m_prefix);
    exportForm->addRow(tr("ID header style:"), m_headerStyle);
    layout->addWidget(exportBox);

    auto* appBox = new QGroupBox(tr("Application"));
    auto* appForm = new QFormLayout(appBox);
    m_language->addItem(tr("System language"), QString());
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    m_language->setCurrentIndex(std::max(0, m_language->findData(languageSetting())));
    appForm->addRow(tr("Language:"), m_language);
    layout->addWidget(appBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveLanguage();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

vt::VtVersion SettingsDialog::version() const
{
    return static_cast<vt::VtVersion>(m_version->currentData().toInt());
}

vt::DesignerSettings SettingsDialog::designerSettings() const
{
    vt::DesignerSettings settings;
    settings.dataMaskSize = m_maskSize->value();
    settings.softKeyWidth = m_keyWidth->value();
    settings.softKeyHeight = m_keyHeight->value();
    settings.physicalSoftKeys = m_physicalKeys->value();
    return settings;
}

vt::ExportOptions SettingsDialog::exportOptions() const
{
    vt::ExportOptions options;
    options.poolName = m_poolName->text().isEmpty() ? std::string("pool") : m_poolName->text().toStdString();
    options.symbolPrefix = m_prefix->text().toStdString();
    options.headerStyle = static_cast<vt::HeaderStyle>(m_headerStyle->currentData().toInt());
    return options;
}

QString SettingsDialog::languageSetting()
{
    return QSettings().value(QStringLiteral("language")).toString();
}

void SettingsDialog::saveLanguage()
{
    const QString language = m_language->currentData().toString();
    if (language != languageSetting()) {
        QSettings().setValue(QStringLiteral("language"), language);
        QMessageBox::information(this, tr("Language"), tr("The new language is used after restarting VT-Designer."));
    }
}
