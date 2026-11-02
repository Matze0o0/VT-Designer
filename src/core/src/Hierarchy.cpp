#include "vtcore/Hierarchy.h"

#include <array>

namespace vt {

namespace {

// Parent columns of Table A.2, in the order of the standard.
enum Column : int {
    ColWorkingSet,
    ColDataMask,
    ColAlarmMask,
    ColContainer,
    ColWindowMask,
    ColSoftKeyMask,
    ColKey,
    ColButton,
    ColKeyGroup,
    ColInputList,
    ColOutputList,
    ColAuxFunction1,
    ColAuxInput1,
    ColAuxFunction2,
    ColAuxInput2,
    ColLabelGraphic,
    ColLabelReferenceList,
    ColAnimation,
    ColumnCount
};

using Row = std::array<std::uint8_t, ColumnCount>;

// Rows of Table A.2 indexed by child object type. Each cell is the VT version from which the
// child may be used in that parent column (0 = not allowed).
//                              WS DM AM Co WM SK Ky Bt KG IL OL F1 I1 F2 I2 LG LR An
constexpr Row kNone          = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr Row kLabelOnly4    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0};
constexpr Row kLabelOnly5    = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0};
constexpr Row kWorkingSet    = {0, 3, 3, 3, 4, 0, 4, 4, 0, 4, 4, 0, 0, 0, 0, 0, 4, 0};
constexpr Row kContainer     = {4, 2, 2, 2, 4, 0, 2, 2, 0, 4, 4, 0, 0, 3, 3, 4, 4, 5};
constexpr Row kKey           = {0, 0, 0, 0, 0, 2, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0};
constexpr Row kInputOrButton = {0, 2, 0, 2, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0};
constexpr Row kOutputField   = {2, 2, 2, 2, 4, 0, 2, 2, 0, 2, 4, 2, 2, 3, 3, 4, 4, 5};
constexpr Row kOutputList    = {4, 4, 4, 4, 4, 0, 4, 4, 0, 4, 4, 0, 0, 4, 4, 4, 4, 5};
constexpr Row kOutputShape   = {2, 2, 2, 2, 4, 0, 2, 2, 0, 4, 4, 2, 2, 3, 3, 4, 4, 5};
constexpr Row kOutputGraphic = {4, 2, 2, 2, 4, 0, 4, 4, 0, 4, 4, 0, 0, 3, 3, 4, 4, 5};
constexpr Row kGraphicsCtx   = {4, 4, 4, 4, 4, 0, 4, 4, 0, 4, 4, 0, 0, 4, 4, 4, 4, 5};
constexpr Row kAnimation     = {0, 5, 5, 5, 5, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0, 0, 5, 0};
constexpr Row kPicture       = {2, 2, 2, 2, 4, 0, 2, 2, 0, 2, 4, 2, 2, 3, 3, 4, 4, 5};
constexpr Row kObjectPointer = {4, 2, 2, 2, 4, 2, 2, 2, 4, 4, 4, 0, 0, 3, 3, 4, 5, 5};
constexpr Row kExternalPtr   = {0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 5, 0};
constexpr Row kAuxType2      = {0, 3, 3, 3, 6, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 4, 0};
constexpr Row kAuxDesignator = {0, 3, 3, 3, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 5, 0};

const Row& rowFor(ObjectType child)
{
    switch (child) {
    case ObjectType::WorkingSet: return kWorkingSet;
    case ObjectType::DataMask:
    case ObjectType::AlarmMask:
    case ObjectType::WindowMask:
    case ObjectType::SoftKeyMask:
    case ObjectType::KeyGroup: return kLabelOnly4;
    case ObjectType::Container: return kContainer;
    case ObjectType::Key: return kKey;
    case ObjectType::Button:
    case ObjectType::InputBoolean:
    case ObjectType::InputString:
    case ObjectType::InputNumber:
    case ObjectType::InputList: return kInputOrButton;
    case ObjectType::OutputString:
    case ObjectType::OutputNumber: return kOutputField;
    case ObjectType::OutputList: return kOutputList;
    case ObjectType::OutputLine:
    case ObjectType::OutputRectangle:
    case ObjectType::OutputEllipse:
    case ObjectType::OutputPolygon: return kOutputShape;
    case ObjectType::OutputMeter:
    case ObjectType::OutputLinearBarGraph:
    case ObjectType::OutputArchedBarGraph: return kOutputGraphic;
    case ObjectType::GraphicsContext: return kGraphicsCtx;
    case ObjectType::Animation: return kAnimation;
    case ObjectType::PictureGraphic: return kPicture;
    case ObjectType::ObjectPointer: return kObjectPointer;
    case ObjectType::ExternalObjectPointer: return kExternalPtr;
    case ObjectType::AuxiliaryFunctionType2:
    case ObjectType::AuxiliaryInputType2: return kAuxType2;
    case ObjectType::AuxiliaryControlDesignatorType2: return kAuxDesignator;
    case ObjectType::NumberVariable:
    case ObjectType::StringVariable:
    case ObjectType::FontAttributes:
    case ObjectType::LineAttributes:
    case ObjectType::FillAttributes:
    case ObjectType::InputAttributes:
    case ObjectType::ExtendedInputAttributes:
    case ObjectType::ColourMap:
    case ObjectType::ObjectLabelReferenceList:
    case ObjectType::ExternalObjectDefinition:
    case ObjectType::ExternalReferenceName:
    case ObjectType::Macro:
    case ObjectType::AuxiliaryFunctionType1:
    case ObjectType::AuxiliaryInputType1: return kLabelOnly5;
    }
    return kNone;
}

std::optional<Column> columnFor(ObjectType parent)
{
    switch (parent) {
    case ObjectType::WorkingSet: return ColWorkingSet;
    case ObjectType::DataMask: return ColDataMask;
    case ObjectType::AlarmMask: return ColAlarmMask;
    case ObjectType::Container: return ColContainer;
    case ObjectType::WindowMask: return ColWindowMask;
    case ObjectType::SoftKeyMask: return ColSoftKeyMask;
    case ObjectType::Key: return ColKey;
    case ObjectType::Button: return ColButton;
    case ObjectType::KeyGroup: return ColKeyGroup;
    case ObjectType::InputList: return ColInputList;
    case ObjectType::OutputList: return ColOutputList;
    case ObjectType::AuxiliaryFunctionType1: return ColAuxFunction1;
    case ObjectType::AuxiliaryInputType1: return ColAuxInput1;
    case ObjectType::AuxiliaryFunctionType2: return ColAuxFunction2;
    case ObjectType::AuxiliaryInputType2: return ColAuxInput2;
    case ObjectType::Animation: return ColAnimation;
    default: return std::nullopt;
    }
}

} // namespace

std::optional<ListKind> containmentList(ObjectType parent)
{
    switch (parent) {
    case ObjectType::SoftKeyMask:
    case ObjectType::KeyGroup:
    case ObjectType::InputList:
    case ObjectType::OutputList: return ListKind::References;
    default: return columnFor(parent) ? std::optional<ListKind>(ListKind::Children) : std::nullopt;
    }
}

int minVersionForChild(ObjectType parent, ObjectType child)
{
    const std::optional<Column> column = columnFor(parent);
    return column ? rowFor(child)[*column] : 0;
}

int minVersionForLabel(ObjectType labelled)
{
    return rowFor(labelled)[ColLabelReferenceList];
}

int minVersionForLabelGraphic(ObjectType graphic)
{
    return rowFor(graphic)[ColLabelGraphic];
}

} // namespace vt
