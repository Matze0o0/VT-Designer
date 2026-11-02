#include "ObjectIcons.h"

#include <QCoreApplication>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numbers>

namespace ui {

namespace {

using T = vt::ObjectType;

const QColor kInk(55, 71, 79);
const QColor kPaper(255, 255, 255);

QColor tint(const QColor& colour, double amount = 0.25)
{
    return QColor::fromRgbF(1 - (1 - colour.redF()) * amount, 1 - (1 - colour.greenF()) * amount,
                            1 - (1 - colour.blueF()) * amount);
}

// All icons are drawn on a 24 x 24 grid; the painter is scaled to the requested size.
class Icon {
public:
    Icon(QPainter& painter, const QColor& accent)
        : p(painter)
        , accent(accent)
        , light(tint(accent))
    {
    }

    void pen(const QColor& colour, double width = 1.5, Qt::PenStyle style = Qt::SolidLine)
    {
        p.setPen(QPen(colour, width, style, Qt::RoundCap, Qt::RoundJoin));
    }

    void noPen() { p.setPen(Qt::NoPen); }

    void roundRect(const QRectF& r, double radius, const QColor& fill, const QColor& stroke, double width = 1.5)
    {
        pen(stroke, width);
        p.setBrush(fill);
        p.drawRoundedRect(r, radius, radius);
    }

    void text(const QRectF& r, const QString& s, double size, const QColor& colour, bool bold = true,
              Qt::Alignment align = Qt::AlignCenter, const QString& family = QStringLiteral("Segoe UI"))
    {
        QFont font(family);
        font.setPixelSize(static_cast<int>(size * 4));
        font.setBold(bold);
        p.save();
        // Draw at 4x and scale down so tiny glyphs keep their shape.
        p.translate(r.topLeft());
        p.scale(0.25, 0.25);
        p.setFont(font);
        p.setPen(colour);
        p.drawText(QRectF(0, 0, r.width() * 4, r.height() * 4), static_cast<int>(align), s);
        p.restore();
    }

    void field(const QRectF& r) { roundRect(r, 1.5, kPaper, kInk, 1.3); }

    void screen()
    {
        roundRect(QRectF(2.5, 3.5, 19, 14), 1.8, kPaper, kInk);
        noPen();
        p.setBrush(accent);
        p.drawRect(QRectF(3.3, 4.3, 17.4, 2.8));
        pen(kInk, 1.5);
        p.drawLine(QPointF(12, 17.5), QPointF(12, 20));
        p.drawLine(QPointF(8, 20.5), QPointF(16, 20.5));
    }

    void badge(const QString& letter)
    {
        roundRect(QRectF(14.5, 14.5, 8, 8), 4, accent, kPaper, 1);
        text(QRectF(14.5, 14.5, 8, 8), letter, 5.5, kPaper);
    }

    void joystick(const QColor& knob)
    {
        pen(kInk, 1.5);
        p.setBrush(kInk);
        p.drawRoundedRect(QRectF(4, 17, 13, 4), 2, 2);
        pen(kInk, 2);
        p.drawLine(QPointF(10.5, 17), QPointF(10.5, 9));
        noPen();
        p.setBrush(knob);
        p.drawEllipse(QPointF(10.5, 7), 3.8, 3.8);
    }

    QPainter& p;
    QColor accent;
    QColor light;
};

void draw(QPainter& painter, T type)
{
    Icon i(painter, categoryColour(type));
    QPainter& p = i.p;

    switch (type) {
    case T::WorkingSet:
        i.pen(kInk, 1.3);
        for (int k = 0; k < 3; ++k) {
            const double c = 8.5 + k * 3.5;
            p.drawLine(QPointF(c, 2.5), QPointF(c, 5.5));
            p.drawLine(QPointF(c, 18.5), QPointF(c, 21.5));
            p.drawLine(QPointF(2.5, c), QPointF(5.5, c));
            p.drawLine(QPointF(18.5, c), QPointF(21.5, c));
        }
        i.roundRect(QRectF(5.5, 5.5, 13, 13), 1.5, kInk, kInk);
        i.text(QRectF(5.5, 5.5, 13, 13), QStringLiteral("WS"), 5.5, kPaper);
        break;

    case T::DataMask:
        i.screen();
        i.pen(QColor(170, 180, 186), 1.2);
        p.drawLine(QPointF(5.5, 10), QPointF(15, 10));
        p.drawLine(QPointF(5.5, 13), QPointF(18.5, 13));
        break;

    case T::AlarmMask: {
        i.screen();
        QPainterPath triangle;
        triangle.moveTo(12, 8);
        triangle.lineTo(17, 16.2);
        triangle.lineTo(7, 16.2);
        triangle.closeSubpath();
        i.noPen();
        p.setBrush(QColor(229, 57, 53));
        p.drawPath(triangle);
        i.text(QRectF(9, 9.5, 6, 7), QStringLiteral("!"), 6, kPaper);
        break;
    }

    case T::SoftKeyMask:
        i.roundRect(QRectF(2.5, 3, 10, 18), 1.5, kPaper, kInk);
        for (int k = 0; k < 4; ++k) {
            i.roundRect(QRectF(14, 3 + k * 4.6, 7.5, 3.6), 1, i.accent, i.accent, 1);
        }
        break;

    case T::WindowMask:
        i.roundRect(QRectF(2.5, 3.5, 19, 17), 1.5, kPaper, kInk);
        i.noPen();
        p.setBrush(i.accent);
        p.drawRect(QRectF(3.3, 4.3, 17.4, 3.5));
        p.setBrush(i.light);
        p.drawRect(QRectF(5, 10, 6.5, 8));
        p.drawRect(QRectF(13, 10, 6, 3.5));
        p.drawRect(QRectF(13, 14.5, 6, 3.5));
        break;

    case T::KeyGroup:
        for (int k = 0; k < 4; ++k) {
            i.roundRect(QRectF(3 + (k % 2) * 9.5, 3 + (k / 2) * 9.5, 8.5, 8.5), 2, i.light, i.accent, 1.3);
        }
        break;

    case T::Container:
        i.pen(i.accent, 1.5, Qt::DashLine);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 2.5, 19, 19), 2.5, 2.5);
        i.roundRect(QRectF(6, 6, 6, 6), 1, i.light, i.accent, 1.2);
        i.pen(i.accent, 1.2);
        p.setBrush(i.light);
        p.drawEllipse(QRectF(12, 12, 6, 6));
        break;

    case T::Key:
        i.roundRect(QRectF(3, 3.5, 18, 17), 3.5, i.light, i.accent);
        i.roundRect(QRectF(5.5, 5.5, 13, 11), 2, kPaper, i.accent, 1);
        i.text(QRectF(5.5, 5.5, 13, 11), QStringLiteral("F1"), 6, i.accent);
        break;

    case T::Button:
        i.noPen();
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawRoundedRect(QRectF(3.5, 7.5, 19, 12), 3, 3);
        i.roundRect(QRectF(2, 6, 19, 12), 3, i.accent, i.accent.darker(130), 1.2);
        i.text(QRectF(2, 6, 19, 12), QStringLiteral("OK"), 6.5, kPaper);
        break;

    case T::Animation:
        i.roundRect(QRectF(2.5, 5, 19, 14), 1, kInk, kInk, 1);
        i.noPen();
        p.setBrush(kPaper);
        for (int k = 0; k < 5; ++k) {
            p.drawRect(QRectF(4 + k * 3.6, 6, 1.8, 1.6));
            p.drawRect(QRectF(4 + k * 3.6, 16.4, 1.8, 1.6));
        }
        p.setBrush(i.accent);
        p.drawRect(QRectF(4, 8.8, 7, 6.4));
        p.setBrush(i.light);
        p.drawRect(QRectF(13, 8.8, 7, 6.4));
        break;

    case T::InputBoolean:
        i.roundRect(QRectF(4, 4, 16, 16), 2.5, kPaper, i.accent, 1.8);
        i.pen(i.accent, 2.6);
        p.drawPolyline(QPolygonF({QPointF(7.5, 12.5), QPointF(10.8, 16), QPointF(17, 8)}));
        break;

    case T::InputString:
        i.field(QRectF(2, 6.5, 20, 11));
        i.text(QRectF(3.5, 6.5, 11, 11), QStringLiteral("ab"), 6.5, kInk, false, Qt::AlignVCenter | Qt::AlignLeft);
        i.pen(i.accent, 1.8);
        p.drawLine(QPointF(15.5, 8.5), QPointF(15.5, 15.5));
        break;

    case T::InputNumber: {
        i.field(QRectF(2, 6.5, 20, 11));
        i.text(QRectF(3.5, 6.5, 11, 11), QStringLiteral("12"), 6.5, kInk, false, Qt::AlignVCenter | Qt::AlignLeft);
        i.noPen();
        p.setBrush(i.accent);
        p.drawPolygon(QPolygonF({QPointF(15.5, 11), QPointF(20, 11), QPointF(17.75, 8.3)}));
        p.drawPolygon(QPolygonF({QPointF(15.5, 13), QPointF(20, 13), QPointF(17.75, 15.7)}));
        break;
    }

    case T::InputList:
        i.field(QRectF(2, 6.5, 20, 11));
        i.pen(QColor(150, 160, 166), 1.3);
        p.drawLine(QPointF(4.5, 12), QPointF(11.5, 12));
        i.noPen();
        p.setBrush(i.accent);
        p.drawRoundedRect(QRectF(14, 7.5, 7, 9), 1, 1);
        i.pen(kPaper, 1.6);
        p.drawPolyline(QPolygonF({QPointF(15.6, 10.6), QPointF(17.5, 12.8), QPointF(19.4, 10.6)}));
        break;

    case T::OutputString:
        i.text(QRectF(0, 3, 24, 18), QStringLiteral("Abc"), 9.5, i.accent);
        i.pen(i.light, 1.5);
        p.drawLine(QPointF(3, 20.5), QPointF(21, 20.5));
        break;

    case T::OutputNumber:
        i.text(QRectF(0, 3, 24, 18), QStringLiteral("1.5"), 10, i.accent);
        i.pen(i.light, 1.5);
        p.drawLine(QPointF(3, 20.5), QPointF(21, 20.5));
        break;

    case T::OutputList:
        i.roundRect(QRectF(3, 3, 18, 18), 1.5, kPaper, kInk, 1.3);
        i.noPen();
        p.setBrush(i.accent);
        p.drawRect(QRectF(4.3, 9.8, 15.4, 4.4));
        i.pen(QColor(150, 160, 166), 1.3);
        p.drawLine(QPointF(6, 6.5), QPointF(17, 6.5));
        p.drawLine(QPointF(6, 17.5), QPointF(17, 17.5));
        i.pen(kPaper, 1.3);
        p.drawLine(QPointF(6, 12), QPointF(17, 12));
        break;

    case T::OutputLine:
        i.pen(i.accent, 2.6);
        p.drawLine(QPointF(4, 20), QPointF(20, 4));
        i.noPen();
        p.setBrush(kInk);
        p.drawEllipse(QPointF(4, 20), 1.8, 1.8);
        p.drawEllipse(QPointF(20, 4), 1.8, 1.8);
        break;

    case T::OutputRectangle:
        i.roundRect(QRectF(3, 5.5, 18, 13), 0.5, i.light, i.accent, 2);
        break;

    case T::OutputEllipse:
        i.pen(i.accent, 2);
        p.setBrush(i.light);
        p.drawEllipse(QRectF(2.5, 5.5, 19, 13));
        break;

    case T::OutputPolygon:
        i.pen(i.accent, 2);
        p.setBrush(i.light);
        p.drawPolygon(QPolygonF({QPointF(12, 3), QPointF(21, 10), QPointF(17.5, 21), QPointF(6.5, 21),
                                 QPointF(3, 10)}));
        break;

    case T::OutputMeter: {
        const QRectF dial(2.5, 5, 19, 19);
        i.pen(kInk, 1.5);
        p.setBrush(kPaper);
        QPainterPath face;
        face.moveTo(dial.center());
        face.arcTo(dial, 0, 180);
        face.closeSubpath();
        p.drawPath(face);
        i.pen(kInk, 1.2);
        for (int k = 0; k <= 4; ++k) {
            const double a = std::numbers::pi * k / 4;
            const QPointF c = dial.center();
            p.drawLine(QPointF(c.x() + 7.5 * std::cos(a), c.y() - 7.5 * std::sin(a)),
                       QPointF(c.x() + 9.5 * std::cos(a), c.y() - 9.5 * std::sin(a)));
        }
        i.pen(i.accent, 2.2);
        p.drawLine(dial.center(), QPointF(dial.center().x() + 6 * std::cos(0.8), dial.center().y() - 6 * std::sin(0.8)));
        i.noPen();
        p.setBrush(kInk);
        p.drawEllipse(dial.center(), 1.6, 1.6);
        break;
    }

    case T::OutputLinearBarGraph:
        i.roundRect(QRectF(7.5, 2.5, 9, 19), 1, kPaper, kInk, 1.4);
        i.noPen();
        p.setBrush(i.accent);
        p.drawRect(QRectF(8.4, 10, 7.2, 10.6));
        i.pen(QColor(229, 57, 53), 1.4);
        p.drawLine(QPointF(5.5, 7), QPointF(18.5, 7));
        break;

    case T::OutputArchedBarGraph: {
        const QRectF arc(4, 5, 16, 16);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(207, 216, 220), 4, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arc, 0, 180 * 16);
        p.setPen(QPen(i.accent, 4, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arc, 70 * 16, 110 * 16);
        break;
    }

    case T::PictureGraphic: {
        i.roundRect(QRectF(2.5, 4.5, 19, 15), 1.5, kPaper, kInk, 1.4);
        i.noPen();
        p.setBrush(QColor(251, 192, 45));
        p.drawEllipse(QPointF(16.5, 8.8), 2, 2);
        p.setBrush(i.accent);
        p.drawPolygon(QPolygonF({QPointF(3.4, 18.6), QPointF(9, 10), QPointF(13, 15), QPointF(15.5, 12.5),
                                 QPointF(20.6, 18.6)}));
        break;
    }

    case T::GraphicsContext:
        i.roundRect(QRectF(2.5, 3.5, 19, 17), 1.5, kPaper, kInk, 1.4);
        i.pen(i.accent, 1.6);
        p.setBrush(Qt::NoBrush);
        {
            QPainterPath squiggle;
            squiggle.moveTo(5, 16);
            squiggle.cubicTo(8, 8, 11, 20, 14, 11);
            p.drawPath(squiggle);
        }
        i.pen(kInk, 2.4);
        p.drawLine(QPointF(14.5, 15.5), QPointF(20, 7));
        break;

    case T::NumberVariable:
        i.roundRect(QRectF(2.5, 5.5, 19, 13), 3, i.light, i.accent, 1.4);
        i.text(QRectF(2.5, 5.5, 19, 13), QStringLiteral("#12"), 6.5, i.accent.darker(120));
        break;

    case T::StringVariable:
        i.roundRect(QRectF(2.5, 5.5, 19, 13), 3, i.light, i.accent, 1.4);
        i.text(QRectF(2.5, 5.5, 19, 13), QStringLiteral("“ab”"), 6.5, i.accent.darker(120));
        break;

    case T::FontAttributes:
        i.text(QRectF(1, 1, 16, 21), QStringLiteral("A"), 13, kInk, true, Qt::AlignCenter,
               QStringLiteral("Georgia"));
        i.text(QRectF(12, 8, 11, 14), QStringLiteral("a"), 9, i.accent, true, Qt::AlignCenter,
               QStringLiteral("Georgia"));
        break;

    case T::LineAttributes:
        i.pen(kInk, 1);
        p.drawLine(QPointF(3, 6), QPointF(21, 6));
        i.pen(i.accent, 2.2);
        p.drawLine(QPointF(3, 11.5), QPointF(21, 11.5));
        p.setPen(QPen(kInk, 3.5, Qt::DashLine, Qt::FlatCap));
        p.drawLine(QPointF(3, 18), QPointF(21, 18));
        break;

    case T::FillAttributes: {
        const QRectF box(3.5, 3.5, 17, 17);
        p.save();
        p.setClipRect(box);
        i.pen(i.accent, 1.3);
        for (int k = -20; k < 24; k += 4) {
            p.drawLine(QPointF(k, 24), QPointF(k + 24, 0));
        }
        p.restore();
        i.pen(kInk, 1.5);
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);
        break;
    }

    case T::InputAttributes:
        i.field(QRectF(2, 6.5, 20, 11));
        i.text(QRectF(2, 6.5, 20, 11), QStringLiteral("a–z"), 6, i.accent);
        break;

    case T::ExtendedInputAttributes:
        i.field(QRectF(2, 6.5, 20, 11));
        i.text(QRectF(2, 6.5, 20, 11), QStringLiteral("ΩäЖ"), 6, i.accent);
        break;

    case T::ColourMap: {
        const QColor colours[] = {QColor(229, 57, 53), QColor(67, 160, 71), QColor(30, 136, 229), QColor(253, 216, 53)};
        for (int k = 0; k < 4; ++k) {
            i.roundRect(QRectF(3 + (k % 2) * 9.5, 3 + (k / 2) * 9.5, 8.5, 8.5), 1.5, colours[k], kInk, 1);
        }
        break;
    }

    case T::ObjectLabelReferenceList: {
        i.pen(i.accent, 1.5);
        p.setBrush(i.light);
        p.drawPolygon(QPolygonF({QPointF(2.5, 7), QPointF(15, 7), QPointF(21, 12), QPointF(15, 17), QPointF(2.5, 17)}));
        i.noPen();
        p.setBrush(kPaper);
        p.drawEllipse(QPointF(15.5, 12), 1.6, 1.6);
        i.pen(i.accent, 1.2);
        p.drawLine(QPointF(5, 10.5), QPointF(11, 10.5));
        p.drawLine(QPointF(5, 13.5), QPointF(9.5, 13.5));
        break;
    }

    case T::ObjectPointer:
        i.roundRect(QRectF(13, 7, 8.5, 10), 1.5, i.light, i.accent, 1.4);
        i.pen(kInk, 2);
        p.drawLine(QPointF(2.5, 12), QPointF(10.5, 12));
        p.drawPolyline(QPolygonF({QPointF(7.5, 8.5), QPointF(11, 12), QPointF(7.5, 15.5)}));
        break;

    case T::Macro:
        i.pen(i.accent.darker(120), 1.2);
        p.setBrush(QColor(255, 202, 40));
        p.drawPolygon(QPolygonF({QPointF(14, 2), QPointF(5, 13.5), QPointF(11, 13.5), QPointF(9, 22),
                                 QPointF(19, 9.5), QPointF(13, 9.5)}));
        break;

    case T::AuxiliaryFunctionType1:
    case T::AuxiliaryFunctionType2:
        i.joystick(i.accent);
        i.badge(QStringLiteral("F"));
        break;

    case T::AuxiliaryInputType1:
    case T::AuxiliaryInputType2:
        i.joystick(i.accent);
        i.badge(QStringLiteral("I"));
        break;

    case T::AuxiliaryControlDesignatorType2:
        i.joystick(i.light);
        i.badge(QStringLiteral("→"));
        break;

    case T::ExternalObjectDefinition:
        i.roundRect(QRectF(2.5, 6.5, 10, 11), 1.5, i.light, i.accent, 1.4);
        i.pen(kInk, 1.8);
        p.drawLine(QPointF(12.5, 12), QPointF(21, 12));
        p.drawPolyline(QPolygonF({QPointF(17.5, 8.5), QPointF(21, 12), QPointF(17.5, 15.5)}));
        break;

    case T::ExternalReferenceName:
        i.roundRect(QRectF(1.5, 6.5, 21, 11), 2, i.light, i.accent, 1.4);
        i.text(QRectF(1.5, 6.5, 21, 11), QStringLiteral("NAME"), 4.8, i.accent.darker(120));
        break;

    case T::ExternalObjectPointer:
        i.pen(i.accent, 1.4, Qt::DashLine);
        p.setBrush(i.light);
        p.drawRoundedRect(QRectF(12.5, 6, 9, 12), 1.5, 1.5);
        i.pen(kInk, 1.8);
        p.drawLine(QPointF(2.5, 12), QPointF(10.5, 12));
        p.drawPolyline(QPolygonF({QPointF(7.5, 8.5), QPointF(11, 12), QPointF(7.5, 15.5)}));
        break;

    default:
        i.roundRect(QRectF(4, 4, 16, 16), 2, i.light, i.accent);
        break;
    }
}

} // namespace

QColor categoryColour(vt::ObjectType type)
{
    switch (type) {
    case T::DataMask:
    case T::AlarmMask:
    case T::SoftKeyMask:
    case T::WindowMask:
    case T::KeyGroup: return {30, 136, 229};
    case T::Container:
    case T::Key:
    case T::Button:
    case T::Animation: return {94, 53, 177};
    case T::InputBoolean:
    case T::InputString:
    case T::InputNumber:
    case T::InputList: return {67, 160, 71};
    case T::OutputString:
    case T::OutputNumber:
    case T::OutputList: return {239, 108, 0};
    case T::OutputLine:
    case T::OutputRectangle:
    case T::OutputEllipse:
    case T::OutputPolygon: return {142, 36, 170};
    case T::OutputMeter:
    case T::OutputLinearBarGraph:
    case T::OutputArchedBarGraph:
    case T::PictureGraphic:
    case T::GraphicsContext: return {0, 137, 123};
    case T::NumberVariable:
    case T::StringVariable: return {109, 76, 65};
    case T::FontAttributes:
    case T::LineAttributes:
    case T::FillAttributes:
    case T::InputAttributes:
    case T::ExtendedInputAttributes:
    case T::ColourMap: return {84, 110, 122};
    case T::AuxiliaryFunctionType1:
    case T::AuxiliaryInputType1: return {144, 164, 174};
    default: return {198, 40, 40};
    }
}

QPixmap objectPixmap(vt::ObjectType type, int size, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(size / 24.0, size / 24.0);
    draw(painter, type);
    return pixmap;
}

QIcon objectIcon(vt::ObjectType type)
{
    static QHash<int, QIcon> cache;
    const int key = static_cast<int>(type);
    auto it = cache.find(key);
    if (it == cache.end()) {
        QIcon icon;
        for (int size : {16, 24, 32, 48}) {
            icon.addPixmap(objectPixmap(type, size, 1.0));
            icon.addPixmap(objectPixmap(type, size, 2.0));
        }
        it = cache.insert(key, icon); // Qt derives the greyed disabled state
    }
    return *it;
}

QString objectDescription(vt::ObjectType type)
{
    const char* text = nullptr;
    switch (type) {
    case T::WorkingSet: text = QT_TRANSLATE_NOOP("ObjectDescription", "Describes the implement. Its child objects form the Working Set designator shown in the VT's Working Set selection."); break;
    case T::DataMask: text = QT_TRANSLATE_NOOP("ObjectDescription", "A screen page in the Data Mask area. Contains the visible objects and refers to a Soft Key Mask."); break;
    case T::AlarmMask: text = QT_TRANSLATE_NOOP("ObjectDescription", "A screen page for alarms with priority and acoustic signal; shown on top of the Data Mask."); break;
    case T::Container: text = QT_TRANSLATE_NOOP("ObjectDescription", "Groups objects so they can be moved, hidden and shown together."); break;
    case T::SoftKeyMask: text = QT_TRANSLATE_NOOP("ObjectDescription", "The set of Soft Keys shown next to a Data or Alarm Mask."); break;
    case T::Key: text = QT_TRANSLATE_NOOP("ObjectDescription", "A Soft Key. Its child objects form the key designator; pressing it sends the key code."); break;
    case T::Button: text = QT_TRANSLATE_NOOP("ObjectDescription", "A button inside a mask, operated by touch or navigation."); break;
    case T::InputBoolean: text = QT_TRANSLATE_NOOP("ObjectDescription", "Check box for an on/off value."); break;
    case T::InputString: text = QT_TRANSLATE_NOOP("ObjectDescription", "Text input field."); break;
    case T::InputNumber: text = QT_TRANSLATE_NOOP("ObjectDescription", "Numeric input field with scaling, offset and limits."); break;
    case T::InputList: text = QT_TRANSLATE_NOOP("ObjectDescription", "Selection from a list of objects."); break;
    case T::OutputString: text = QT_TRANSLATE_NOOP("ObjectDescription", "Displays a text."); break;
    case T::OutputNumber: text = QT_TRANSLATE_NOOP("ObjectDescription", "Displays a number with scaling, offset and decimals."); break;
    case T::OutputList: text = QT_TRANSLATE_NOOP("ObjectDescription", "Displays one object of a list, selected by a value."); break;
    case T::OutputLine: text = QT_TRANSLATE_NOOP("ObjectDescription", "A straight line."); break;
    case T::OutputRectangle: text = QT_TRANSLATE_NOOP("ObjectDescription", "A rectangle or square, optionally filled."); break;
    case T::OutputEllipse: text = QT_TRANSLATE_NOOP("ObjectDescription", "An ellipse, circle, arc, segment or section."); break;
    case T::OutputPolygon: text = QT_TRANSLATE_NOOP("ObjectDescription", "A polygon defined by a list of points."); break;
    case T::OutputMeter: text = QT_TRANSLATE_NOOP("ObjectDescription", "A round gauge with a needle."); break;
    case T::OutputLinearBarGraph: text = QT_TRANSLATE_NOOP("ObjectDescription", "A horizontal or vertical bar graph with optional target line."); break;
    case T::OutputArchedBarGraph: text = QT_TRANSLATE_NOOP("ObjectDescription", "A bar graph along an arc."); break;
    case T::PictureGraphic: text = QT_TRANSLATE_NOOP("ObjectDescription", "A bitmap picture with 2, 16 or 256 colours. Importing an image starts when it is added."); break;
    case T::NumberVariable: text = QT_TRANSLATE_NOOP("ObjectDescription", "Stores a 32-bit value that number fields, lists and graphs can show."); break;
    case T::StringVariable: text = QT_TRANSLATE_NOOP("ObjectDescription", "Stores a text that string fields can show."); break;
    case T::FontAttributes: text = QT_TRANSLATE_NOOP("ObjectDescription", "Font colour, size, character set and style for text objects."); break;
    case T::LineAttributes: text = QT_TRANSLATE_NOOP("ObjectDescription", "Line colour, width and pattern for shapes."); break;
    case T::FillAttributes: text = QT_TRANSLATE_NOOP("ObjectDescription", "Fill type, colour or pattern for shapes."); break;
    case T::InputAttributes: text = QT_TRANSLATE_NOOP("ObjectDescription", "Characters that are allowed or forbidden in a text input."); break;
    case T::ObjectPointer: text = QT_TRANSLATE_NOOP("ObjectDescription", "Shows the object it points to; the target can be changed at runtime."); break;
    case T::Macro: text = QT_TRANSLATE_NOOP("ObjectDescription", "A list of commands the VT executes when an event occurs."); break;
    case T::AuxiliaryFunctionType1: text = QT_TRANSLATE_NOOP("ObjectDescription", "Auxiliary function of type 1 (obsolete, only kept for compatibility)."); break;
    case T::AuxiliaryInputType1: text = QT_TRANSLATE_NOOP("ObjectDescription", "Auxiliary input of type 1 (obsolete, only kept for compatibility)."); break;
    case T::AuxiliaryFunctionType2: text = QT_TRANSLATE_NOOP("ObjectDescription", "A function of the implement that the operator can assign to an auxiliary input."); break;
    case T::AuxiliaryInputType2: text = QT_TRANSLATE_NOOP("ObjectDescription", "An auxiliary input control, e.g. a joystick button."); break;
    case T::AuxiliaryControlDesignatorType2: text = QT_TRANSLATE_NOOP("ObjectDescription", "Shows the designator of an auxiliary function or input, e.g. the current assignment."); break;
    case T::WindowMask: text = QT_TRANSLATE_NOOP("ObjectDescription", "A window for user-layout Data Masks that the operator can arrange."); break;
    case T::KeyGroup: text = QT_TRANSLATE_NOOP("ObjectDescription", "A group of keys for user-layout Soft Key Masks."); break;
    case T::GraphicsContext: text = QT_TRANSLATE_NOOP("ObjectDescription", "A drawing canvas the implement paints on with graphics commands."); break;
    case T::ExtendedInputAttributes: text = QT_TRANSLATE_NOOP("ObjectDescription", "Unicode character ranges that are allowed or forbidden in a text input."); break;
    case T::ColourMap: text = QT_TRANSLATE_NOOP("ObjectDescription", "Maps colour indices to other palette colours, e.g. for a night mode."); break;
    case T::ObjectLabelReferenceList: text = QT_TRANSLATE_NOOP("ObjectDescription", "Names and symbols for objects, used by the VT in user-layout screens."); break;
    case T::ExternalObjectDefinition: text = QT_TRANSLATE_NOOP("ObjectDescription", "Lists objects that other Working Sets may show."); break;
    case T::ExternalReferenceName: text = QT_TRANSLATE_NOOP("ObjectDescription", "Identifies another Working Set by its NAME."); break;
    case T::ExternalObjectPointer: text = QT_TRANSLATE_NOOP("ObjectDescription", "Shows an object of another Working Set."); break;
    case T::Animation: text = QT_TRANSLATE_NOOP("ObjectDescription", "Shows its child objects one after another."); break;
    }
    return text != nullptr ? QCoreApplication::translate("ObjectDescription", text) : QString();
}

} // namespace ui
