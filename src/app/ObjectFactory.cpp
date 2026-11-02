#include "ObjectFactory.h"

#include "vtcore/Hierarchy.h"
#include "vtcore/PictureCodec.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSpinBox>

#include <algorithm>

namespace ui {

namespace {

using T = vt::ObjectType;

class ObjectFactory {
    Q_DECLARE_TR_FUNCTIONS(ObjectFactory)
};

std::set<vt::ObjectId> takenIds(const std::vector<vt::VtObject>& extra)
{
    std::set<vt::ObjectId> taken;
    for (const vt::VtObject& object : extra) {
        taken.insert(object.id);
    }
    return taken;
}

vt::VtObject newObject(const vt::ObjectPool& pool, vt::ObjectType type, std::vector<vt::VtObject>& extra,
                       vt::ObjectId alsoTaken = vt::kNullObjectId)
{
    std::set<vt::ObjectId> taken = takenIds(extra);
    if (alsoTaken != vt::kNullObjectId) {
        taken.insert(alsoTaken);
    }
    return vt::VtObject::create(allocateId(pool, type, taken), type);
}

// First object of the type in the pool or among the helpers created so far; creates one if needed.
vt::ObjectId ensureObject(const vt::ObjectPool& pool, vt::ObjectType type, std::vector<vt::VtObject>& extra,
                          vt::ObjectId reserved)
{
    for (const vt::VtObject& object : pool.objects()) {
        if (object.type == type) {
            return object.id;
        }
    }
    for (const vt::VtObject& object : extra) {
        if (object.type == type) {
            return object.id;
        }
    }
    vt::VtObject helper = newObject(pool, type, extra, reserved);
    const vt::ObjectId id = helper.id;
    extra.push_back(std::move(helper));
    return id;
}

vt::ObjectId createText(const vt::ObjectPool& pool, std::vector<vt::VtObject>& extra, vt::ObjectId reserved,
                        const char* text, int width, int height)
{
    const vt::ObjectId font = ensureObject(pool, T::FontAttributes, extra, reserved); // before allocating the label ID
    vt::VtObject label = newObject(pool, T::OutputString, extra, reserved);
    label.set("fontAttributes", font);
    label.set("width", width);
    label.set("height", height);
    label.set("justification", 1 | (1 << 2));
    label.data.assign(text, text + std::char_traits<char>::length(text));
    const vt::ObjectId id = label.id;
    extra.push_back(std::move(label));
    return id;
}

int nextKeyCode(const vt::ObjectPool& pool, vt::ObjectType type)
{
    int highest = type == T::Key ? 0 : -1;
    for (const vt::VtObject& object : pool.objects()) {
        if (object.type == type) {
            highest = std::max(highest, static_cast<int>(object.get("keyCode")));
        }
    }
    return std::min(highest + 1, 255);
}

} // namespace

vt::ObjectId allocateId(const vt::ObjectPool& pool, vt::ObjectType type, const std::set<vt::ObjectId>& taken)
{
    vt::ObjectId id = pool.suggestId(type);
    while (id != vt::kNullObjectId && (taken.count(id) != 0 || pool.contains(id))) {
        ++id;
    }
    return id;
}

vt::VtObject createObject(const vt::ObjectPool& pool, vt::ObjectType type, std::vector<vt::VtObject>& extra)
{
    vt::VtObject object = newObject(pool, type, extra);
    const vt::ObjectId self = object.id;
    auto font = [&] { return ensureObject(pool, T::FontAttributes, extra, self); };
    auto line = [&] { return ensureObject(pool, T::LineAttributes, extra, self); };

    switch (type) {
    case T::WorkingSet: {
        vt::ObjectId mask = vt::kNullObjectId;
        for (const vt::VtObject& o : pool.objects()) {
            if (o.type == T::DataMask) {
                mask = o.id;
                break;
            }
        }
        if (mask == vt::kNullObjectId) {
            vt::VtObject dataMask = newObject(pool, T::DataMask, extra, self);
            mask = dataMask.id;
            extra.push_back(std::move(dataMask));
        }
        object.set("activeMask", mask);
        object.children.push_back({createText(pool, extra, self, "WS", 60, 32), 0, 0});
        object.languages = {"en"};
        break;
    }
    case T::OutputString:
    case T::InputString:
        object.set("fontAttributes", font());
        object.data = {'T', 'e', 'x', 't'};
        break;
    case T::OutputNumber:
    case T::InputNumber:
        object.set("fontAttributes", font());
        break;
    case T::InputBoolean:
        object.set("foregroundColour", font());
        break;
    case T::OutputLine:
    case T::OutputRectangle:
    case T::OutputEllipse:
        object.set("lineAttributes", line());
        break;
    case T::OutputPolygon:
        object.set("lineAttributes", line());
        object.points = {{0, 39}, {30, 0}, {59, 39}};
        break;
    case T::Key:
        object.set("keyCode", nextKeyCode(pool, T::Key));
        break;
    case T::Button:
        object.set("keyCode", nextKeyCode(pool, T::Button));
        break;
    case T::WindowMask:
    case T::KeyGroup:
        object.set("name", createText(pool, extra, self, "Name", 100, 20));
        break;
    case T::AuxiliaryFunctionType1:
    case T::AuxiliaryInputType1:
    case T::AuxiliaryFunctionType2:
    case T::AuxiliaryInputType2:
        object.children.push_back({createText(pool, extra, self, "AUX", 60, 32), 0, 0});
        break;
    case T::ExtendedInputAttributes:
        object.codePlanes = {{0, {{0x20, 0x7E}}}};
        break;
    case T::ColourMap:
        for (int i = 0; i < 16; ++i) {
            object.data.push_back(static_cast<std::uint8_t>(i));
        }
        break;
    case T::StringVariable:
        object.data = {'T', 'e', 'x', 't'};
        break;
    default:
        break;
    }
    return object;
}

bool canPlaceChild(vt::ObjectType parent, vt::ObjectType child, vt::VtVersion version)
{
    if (vt::containmentList(parent) != vt::ListKind::Children) {
        return false;
    }
    const int minVersion = vt::minVersionForChild(parent, child);
    return minVersion > 0 && minVersion <= vt::toInt(version);
}

std::optional<vtr::Placement> dropTarget(const vt::ObjectPool& pool, const std::vector<vtr::Placement>& layout,
                                         QPoint point, vt::ObjectType type)
{
    for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
        if (!it->rect.contains(point)) {
            continue;
        }
        const vt::VtObject* candidate = pool.find(it->object);
        if (candidate != nullptr && canPlaceChild(candidate->type, type, pool.version())) {
            return *it;
        }
    }
    return std::nullopt;
}

bool importPicture(QWidget* parent, vt::VtObject& picture, vt::VtVersion version)
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    }
    const QString file = QFileDialog::getOpenFileName(
        parent, ObjectFactory::tr("Import image"), {},
        ObjectFactory::tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (file.isEmpty()) {
        return false;
    }
    QImage image(file);
    if (image.isNull()) {
        QMessageBox::warning(parent, ObjectFactory::tr("Import image"),
                             ObjectFactory::tr("The file %1 could not be read as an image.").arg(file));
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(ObjectFactory::tr("Image conversion"));
    auto* form = new QFormLayout(&dialog);
    form->addRow(new QLabel(ObjectFactory::tr("%1 (%2 × %3 pixels)")
                                .arg(QFileInfo(file).fileName())
                                .arg(image.width())
                                .arg(image.height())));
    auto* format = new QComboBox(&dialog);
    format->addItem(ObjectFactory::tr("256 colours (8 bit)"), 2);
    format->addItem(ObjectFactory::tr("16 colours (4 bit)"), 1);
    format->addItem(ObjectFactory::tr("Monochrome (1 bit)"), 0);
    form->addRow(ObjectFactory::tr("Format:"), format);
    auto* width = new QSpinBox(&dialog);
    width->setRange(1, 4096);
    width->setValue(image.width());
    width->setToolTip(ObjectFactory::tr("Stored width; the image is resampled before conversion."));
    form->addRow(ObjectFactory::tr("Stored width:"), width);
    auto* dither = new QCheckBox(ObjectFactory::tr("Dithering (better for photos)"), &dialog);
    form->addRow(dither);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    (void)version;
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    if (width->value() != image.width()) {
        image = image.scaledToWidth(width->value(), Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGBA8888);
    std::vector<std::uint8_t> rgba;
    rgba.reserve(static_cast<std::size_t>(image.width()) * image.height() * 4);
    for (int y = 0; y < image.height(); ++y) {
        const uchar* line = image.constScanLine(y);
        rgba.insert(rgba.end(), line, line + image.width() * 4);
    }
    const auto pictureFormat = static_cast<vt::PictureFormat>(format->currentData().toInt());
    const bool keepDisplayWidth = picture.get("width") != 0;
    const double displayWidth = picture.get("width");
    vt::setPictureFromRgba(picture, rgba, image.width(), image.height(), pictureFormat, dither->isChecked());
    picture.set("width", keepDisplayWidth ? displayWidth : image.width());
    if (picture.name.empty()) {
        QString base = QFileInfo(file).completeBaseName();
        base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
        if (!base.isEmpty() && !base[0].isDigit()) {
            picture.name = QStringLiteral("pic_%1").arg(base).toStdString();
        }
    }
    return true;
}

bool exportPicture(QWidget* parent, const vt::VtObject& picture)
{
    const int width = static_cast<int>(picture.get("actualWidth"));
    const int height = static_cast<int>(picture.get("actualHeight"));
    std::vector<std::uint8_t> indices;
    try {
        indices = vt::pictureIndices(picture);
    } catch (const std::exception&) {
        return false;
    }
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, vtr::vtColour(indices[static_cast<std::size_t>(y) * width + x]));
        }
    }
    const QString file = QFileDialog::getSaveFileName(parent, ObjectFactory::tr("Export image"), {},
                                                      ObjectFactory::tr("PNG image (*.png)"));
    return !file.isEmpty() && image.save(file, "PNG");
}

} // namespace ui
