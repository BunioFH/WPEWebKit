// Copyright 2016 The Chromium Authors. All rights reserved.
// Copyright (C) 2016 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "config.h"
#include "CSSPropertyParserHelpers.h"

#include "CSSCalculationValue.h"
#include "CSSCanvasValue.h"
#include "CSSCrossfadeValue.h"
#include "CSSGradientValue.h"
#include "CSSImageSetValue.h"
#include "CSSImageValue.h"
#include "CSSNamedImageValue.h"
#include "CSSParserIdioms.h"
#include "CSSValuePool.h"
#include "Pair.h"
#include "StyleColor.h"

namespace WebCore {

namespace CSSPropertyParserHelpers {

bool consumeCommaIncludingWhitespace(CSSParserTokenRange& range)
{
    CSSParserToken value = range.peek();
    if (value.type() != CommaToken)
        return false;
    range.consumeIncludingWhitespace();
    return true;
}

bool consumeSlashIncludingWhitespace(CSSParserTokenRange& range)
{
    CSSParserToken value = range.peek();
    if (value.type() != DelimiterToken || value.delimiter() != '/')
        return false;
    range.consumeIncludingWhitespace();
    return true;
}

CSSParserTokenRange consumeFunction(CSSParserTokenRange& range)
{
    ASSERT(range.peek().type() == FunctionToken);
    CSSParserTokenRange contents = range.consumeBlock();
    range.consumeWhitespace();
    contents.consumeWhitespace();
    return contents;
}

// FIXME: consider pulling in the parsing logic from CSSCalculationValue.cpp.
class CalcParser {

public:
    explicit CalcParser(CSSParserTokenRange& range, ValueRange valueRange = ValueRangeAll)
        : m_sourceRange(range)
        , m_range(range)
    {
        const CSSParserToken& token = range.peek();
        if (token.functionId() == CSSValueCalc || token.functionId() == CSSValueWebkitCalc)
            m_calcValue = CSSCalcValue::create(consumeFunction(m_range), valueRange);
    }

    const CSSCalcValue* value() const { return m_calcValue.get(); }
    RefPtr<CSSPrimitiveValue> consumeValue()
    {
        if (!m_calcValue)
            return nullptr;
        m_sourceRange = m_range;
        return CSSValuePool::singleton().createValue(m_calcValue.release());
    }
    RefPtr<CSSPrimitiveValue> consumeNumber()
    {
        if (!m_calcValue)
            return nullptr;
        m_sourceRange = m_range;
        CSSPrimitiveValue::UnitTypes unitType = m_calcValue->isInt() ? CSSPrimitiveValue::UnitTypes::CSS_PARSER_INTEGER : CSSPrimitiveValue::UnitTypes::CSS_NUMBER;
        return CSSValuePool::singleton().createValue(m_calcValue->doubleValue(), unitType);
    }

    bool consumeNumberRaw(double& result)
    {
        if (!m_calcValue || m_calcValue->category() != CalcNumber)
            return false;
        m_sourceRange = m_range;
        result = m_calcValue->doubleValue();
        return true;
    }

private:
    CSSParserTokenRange& m_sourceRange;
    CSSParserTokenRange m_range;
    RefPtr<CSSCalcValue> m_calcValue;
};

RefPtr<CSSPrimitiveValue> consumeInteger(CSSParserTokenRange& range, double minimumValue)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == NumberToken) {
        if (token.numericValueType() == NumberValueType || token.numericValue() < minimumValue)
            return nullptr;
        return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), CSSPrimitiveValue::UnitTypes::CSS_NUMBER);
    }
    CalcParser calcParser(range);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        if (calculation->category() != CalcNumber || !calculation->isInt())
            return nullptr;
        double value = calculation->doubleValue();
        if (value < minimumValue)
            return nullptr;
        return calcParser.consumeNumber();
    }
    return nullptr;
}

RefPtr<CSSPrimitiveValue> consumePositiveInteger(CSSParserTokenRange& range)
{
    return consumeInteger(range, 1);
}

bool consumeNumberRaw(CSSParserTokenRange& range, double& result)
{
    if (range.peek().type() == NumberToken) {
        result = range.consumeIncludingWhitespace().numericValue();
        return true;
    }
    CalcParser calcParser(range, ValueRangeAll);
    return calcParser.consumeNumberRaw(result);
}

// FIXME: Work out if this can just call consumeNumberRaw
RefPtr<CSSPrimitiveValue> consumeNumber(CSSParserTokenRange& range, ValueRange valueRange)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == NumberToken) {
        if (valueRange == ValueRangeNonNegative && token.numericValue() < 0)
            return nullptr;
        return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), token.unitType());
    }
    CalcParser calcParser(range, ValueRangeAll);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        // FIXME: Calcs should not be subject to parse time range checks.
        // spec: https://drafts.csswg.org/css-values-3/#calc-range
        if (calculation->category() != CalcNumber || (valueRange == ValueRangeNonNegative && calculation->isNegative()))
            return nullptr;
        return calcParser.consumeNumber();
    }
    return nullptr;
}

inline bool shouldAcceptUnitlessValue(double value, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    // FIXME: Presentational HTML attributes shouldn't use the CSS parser for lengths
    return value == 0
        || isUnitLessValueParsingEnabledForMode(cssParserMode)
        || (cssParserMode == HTMLQuirksMode && unitless == UnitlessQuirk::Allow);
}

RefPtr<CSSPrimitiveValue> consumeLength(CSSParserTokenRange& range, CSSParserMode cssParserMode, ValueRange valueRange, UnitlessQuirk unitless)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == DimensionToken) {
        switch (token.unitType()) {
        case CSSPrimitiveValue::UnitTypes::CSS_QUIRKY_EMS:
            if (cssParserMode != UASheetMode)
                return nullptr;
            FALLTHROUGH;
        case CSSPrimitiveValue::UnitTypes::CSS_EMS:
        case CSSPrimitiveValue::UnitTypes::CSS_REMS:
        case CSSPrimitiveValue::UnitTypes::CSS_CHS:
        case CSSPrimitiveValue::UnitTypes::CSS_EXS:
        case CSSPrimitiveValue::UnitTypes::CSS_PX:
        case CSSPrimitiveValue::UnitTypes::CSS_CM:
        case CSSPrimitiveValue::UnitTypes::CSS_MM:
        case CSSPrimitiveValue::UnitTypes::CSS_IN:
        case CSSPrimitiveValue::UnitTypes::CSS_PT:
        case CSSPrimitiveValue::UnitTypes::CSS_PC:
        case CSSPrimitiveValue::UnitTypes::CSS_VW:
        case CSSPrimitiveValue::UnitTypes::CSS_VH:
        case CSSPrimitiveValue::UnitTypes::CSS_VMIN:
        case CSSPrimitiveValue::UnitTypes::CSS_VMAX:
            break;
        default:
            return nullptr;
        }
        if (valueRange == ValueRangeNonNegative && token.numericValue() < 0)
            return nullptr;
        return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), token.unitType());
    }
    if (token.type() == NumberToken) {
        if (!shouldAcceptUnitlessValue(token.numericValue(), cssParserMode, unitless)
            || (valueRange == ValueRangeNonNegative && token.numericValue() < 0))
            return nullptr;
        CSSPrimitiveValue::UnitTypes unitType = CSSPrimitiveValue::UnitTypes::CSS_PX;
        return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), unitType);
    }
    if (cssParserMode == SVGAttributeMode)
        return nullptr;
    CalcParser calcParser(range, valueRange);
    if (calcParser.value() && calcParser.value()->category() == CalcLength)
        return calcParser.consumeValue();
    return nullptr;
}

RefPtr<CSSPrimitiveValue> consumePercent(CSSParserTokenRange& range, ValueRange valueRange)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == PercentageToken) {
        if (valueRange == ValueRangeNonNegative && token.numericValue() < 0)
            return nullptr;
        return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
    }
    CalcParser calcParser(range, valueRange);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        if (calculation->category() == CalcPercent)
            return calcParser.consumeValue();
    }
    return nullptr;
}

static bool canConsumeCalcValue(CalculationCategory category, CSSParserMode cssParserMode)
{
    if (category == CalcLength || category == CalcPercent || category == CalcPercentLength)
        return true;

    if (cssParserMode != SVGAttributeMode)
        return false;

    if (category == CalcNumber || category == CalcPercentNumber)
        return true;

    return false;
}

RefPtr<CSSPrimitiveValue> consumeLengthOrPercent(CSSParserTokenRange& range, CSSParserMode cssParserMode, ValueRange valueRange, UnitlessQuirk unitless)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == DimensionToken || token.type() == NumberToken)
        return consumeLength(range, cssParserMode, valueRange, unitless);
    if (token.type() == PercentageToken)
        return consumePercent(range, valueRange);
    CalcParser calcParser(range, valueRange);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        if (canConsumeCalcValue(calculation->category(), cssParserMode))
            return calcParser.consumeValue();
    }
    return nullptr;
}

RefPtr<CSSPrimitiveValue> consumeAngle(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == DimensionToken) {
        switch (token.unitType()) {
        case CSSPrimitiveValue::UnitTypes::CSS_DEG:
        case CSSPrimitiveValue::UnitTypes::CSS_RAD:
        case CSSPrimitiveValue::UnitTypes::CSS_GRAD:
        case CSSPrimitiveValue::UnitTypes::CSS_TURN:
            return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), token.unitType());
        default:
            return nullptr;
        }
    }
    if (token.type() == NumberToken && shouldAcceptUnitlessValue(token.numericValue(), cssParserMode, unitless)) {
        range.consumeIncludingWhitespace();
        return CSSValuePool::singleton().createValue(0, CSSPrimitiveValue::UnitTypes::CSS_DEG);
    }

    CalcParser calcParser(range, ValueRangeAll);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        if (calculation->category() == CalcAngle)
            return calcParser.consumeValue();
    }
    return nullptr;
}

RefPtr<CSSPrimitiveValue> consumeTime(CSSParserTokenRange& range, CSSParserMode cssParserMode, ValueRange valueRange, UnitlessQuirk unitless)
{
    const CSSParserToken& token = range.peek();
    CSSPrimitiveValue::UnitTypes unit = token.unitType();
    bool acceptUnitless = token.type() == NumberToken && shouldAcceptUnitlessValue(token.numericValue(), cssParserMode, unitless);
    if (acceptUnitless)
        unit = CSSPrimitiveValue::UnitTypes::CSS_MS;
    if (token.type() == DimensionToken || acceptUnitless) {
        if (valueRange == ValueRangeNonNegative && token.numericValue() < 0)
            return nullptr;
        if (unit == CSSPrimitiveValue::UnitTypes::CSS_MS || unit == CSSPrimitiveValue::UnitTypes::CSS_S)
            return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().numericValue(), unit);
        return nullptr;
    }
    CalcParser calcParser(range, valueRange);
    if (const CSSCalcValue* calculation = calcParser.value()) {
        if (calculation->category() == CalcTime)
            return calcParser.consumeValue();
    }
    return nullptr;
}

RefPtr<CSSPrimitiveValue> consumeIdent(CSSParserTokenRange& range)
{
    if (range.peek().type() != IdentToken)
        return nullptr;
    return CSSValuePool::singleton().createIdentifierValue(range.consumeIncludingWhitespace().id());
}

RefPtr<CSSPrimitiveValue> consumeIdentRange(CSSParserTokenRange& range, CSSValueID lower, CSSValueID upper)
{
    if (range.peek().id() < lower || range.peek().id() > upper)
        return nullptr;
    return consumeIdent(range);
}

// FIXME-NEWPARSER: Eventually we'd like this to use CSSCustomIdentValue, but we need
// to do other plumbing work first (like changing Pair to CSSValuePair and make it not
// use only primitive values).
RefPtr<CSSPrimitiveValue> consumeCustomIdent(CSSParserTokenRange& range)
{
    if (range.peek().type() != IdentToken || isCSSWideKeyword(range.peek().id()))
        return nullptr;
    return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().value().toString(), CSSPrimitiveValue::UnitTypes::CSS_STRING);
}

RefPtr<CSSPrimitiveValue> consumeString(CSSParserTokenRange& range)
{
    if (range.peek().type() != StringToken)
        return nullptr;
    return CSSValuePool::singleton().createValue(range.consumeIncludingWhitespace().value().toString(), CSSPrimitiveValue::UnitTypes::CSS_STRING);
}

StringView consumeUrlAsStringView(CSSParserTokenRange& range)
{
    const CSSParserToken& token = range.peek();
    if (token.type() == UrlToken) {
        range.consumeIncludingWhitespace();
        return token.value();
    }
    if (token.functionId() == CSSValueUrl) {
        CSSParserTokenRange urlRange = range;
        CSSParserTokenRange urlArgs = urlRange.consumeBlock();
        const CSSParserToken& next = urlArgs.consumeIncludingWhitespace();
        if (next.type() == BadStringToken || !urlArgs.atEnd())
            return StringView();
        ASSERT(next.type() == StringToken);
        range = urlRange;
        range.consumeWhitespace();
        return next.value();
    }

    return StringView();
}

RefPtr<CSSPrimitiveValue> consumeUrl(CSSParserTokenRange& range)
{
    StringView url = consumeUrlAsStringView(range);
    if (url.isNull())
        return nullptr;
    return CSSValuePool::singleton().createValue(url.toString(), CSSPrimitiveValue::UnitTypes::CSS_URI);
}

static int clampRGBComponent(const CSSPrimitiveValue& value)
{
    double result = value.doubleValue();
    // FIXME: Multiply by 2.55 and round instead of floor.
    if (value.isPercentage())
        result *= 2.56;
    return clampTo<int>(result, 0, 255);
}

static Color parseRGBParameters(CSSParserTokenRange& range, bool parseAlpha)
{
    ASSERT(range.peek().functionId() == CSSValueRgb || range.peek().functionId() == CSSValueRgba);
    Color result;
    CSSParserTokenRange args = consumeFunction(range);
    RefPtr<CSSPrimitiveValue> colorParameter = consumeInteger(args);
    if (!colorParameter)
        colorParameter = consumePercent(args, ValueRangeAll);
    if (!colorParameter)
        return Color();
    const bool isPercent = colorParameter->isPercentage();
    int colorArray[3];
    colorArray[0] = clampRGBComponent(*colorParameter);
    for (int i = 1; i < 3; i++) {
        if (!consumeCommaIncludingWhitespace(args))
            return Color();
        colorParameter = isPercent ? consumePercent(args, ValueRangeAll) : consumeInteger(args);
        if (!colorParameter)
            return Color();
        colorArray[i] = clampRGBComponent(*colorParameter);
    }
    if (parseAlpha) {
        if (!consumeCommaIncludingWhitespace(args))
            return Color();
        double alpha;
        if (!consumeNumberRaw(args, alpha))
            return Color();
        // Convert the floating pointer number of alpha to an integer in the range [0, 256),
        // with an equal distribution across all 256 values.
        int alphaComponent = static_cast<int>(clampTo<double>(alpha, 0.0, 1.0) * nextafter(256.0, 0.0));
        result = Color(makeRGBA(colorArray[0], colorArray[1], colorArray[2], alphaComponent));
    } else {
        result = Color(makeRGB(colorArray[0], colorArray[1], colorArray[2]));
    }

    if (!args.atEnd())
        return Color();

    return result;
}

static Color parseHSLParameters(CSSParserTokenRange& range, bool parseAlpha)
{
    ASSERT(range.peek().functionId() == CSSValueHsl || range.peek().functionId() == CSSValueHsla);
    CSSParserTokenRange args = consumeFunction(range);
    RefPtr<CSSPrimitiveValue> hslValue = consumeNumber(args, ValueRangeAll);
    if (!hslValue)
        return Color();
    double colorArray[3];
    colorArray[0] = (((hslValue->intValue() % 360) + 360) % 360) / 360.0;
    for (int i = 1; i < 3; i++) {
        if (!consumeCommaIncludingWhitespace(args))
            return Color();
        hslValue = consumePercent(args, ValueRangeAll);
        if (!hslValue)
            return Color();
        double doubleValue = hslValue->doubleValue();
        colorArray[i] = clampTo<double>(doubleValue, 0.0, 100.0) / 100.0; // Needs to be value between 0 and 1.0.
    }
    double alpha = 1.0;
    if (parseAlpha) {
        if (!consumeCommaIncludingWhitespace(args))
            return Color();
        if (!consumeNumberRaw(args, alpha))
            return Color();
        alpha = clampTo<double>(alpha, 0.0, 1.0);
    }

    if (!args.atEnd())
        return Color();

    return Color(makeRGBAFromHSLA(colorArray[0], colorArray[1], colorArray[2], alpha));
}

static Color parseColorFunctionParameters(CSSParserTokenRange& range)
{
    ASSERT(range.peek().functionId() == CSSValueColor);
    CSSParserTokenRange args = consumeFunction(range);

    ColorSpace colorSpace;
    switch (args.peek().id()) {
    case CSSValueSrgb:
        colorSpace = ColorSpaceSRGB;
        break;
    case CSSValueDisplayP3:
        colorSpace = ColorSpaceDisplayP3;
        break;
    default:
        return Color();
    }
    consumeIdent(args);

    double colorChannels[4] = { 0, 0, 0, 1 };
    for (int i = 0; i < 3; ++i) {
        double value;
        if (consumeNumberRaw(args, value))
            colorChannels[i] = std::max(0.0, std::min(1.0, value));
        else
            break;
    }

    if (consumeSlashIncludingWhitespace(args)) {
        auto alphaParameter = consumePercent(args, ValueRangeAll);
        if (!alphaParameter)
            alphaParameter = consumeNumber(args, ValueRangeAll);
        if (!alphaParameter)
            return Color();

        colorChannels[3] = std::max(0.0, std::min(1.0, alphaParameter->isPercentage() ? (alphaParameter->doubleValue() / 100) : alphaParameter->doubleValue()));
    }

    // FIXME: Support the comma-separated list of fallback color values.

    if (!args.atEnd())
        return Color();
    
    return Color(colorChannels[0], colorChannels[1], colorChannels[2], colorChannels[3], colorSpace);
}

static Color parseHexColor(CSSParserTokenRange& range, bool acceptQuirkyColors)
{
    RGBA32 result;
    const CSSParserToken& token = range.peek();
    if (token.type() == HashToken) {
        if (!Color::parseHexColor(token.value(), result))
            return Color();
    } else if (acceptQuirkyColors) {
        String color;
        if (token.type() == NumberToken || token.type() == DimensionToken) {
            if (token.numericValueType() != IntegerValueType
                || token.numericValue() < 0. || token.numericValue() >= 1000000.)
                return Color();
            if (token.type() == NumberToken) // e.g. 112233
                color = String::format("%d", static_cast<int>(token.numericValue()));
            else // e.g. 0001FF
                color = String::number(static_cast<int>(token.numericValue())) + token.value().toString();
            while (color.length() < 6)
                color = "0" + color;
        } else if (token.type() == IdentToken) { // e.g. FF0000
            color = token.value().toString();
        }
        unsigned length = color.length();
        if (length != 3 && length != 6)
            return Color();
        if (!Color::parseHexColor(color, result))
            return Color();
    } else {
        return Color();
    }
    range.consumeIncludingWhitespace();
    return Color(result);
}

static Color parseColorFunction(CSSParserTokenRange& range)
{
    CSSParserTokenRange colorRange = range;
    CSSValueID functionId = range.peek().functionId();
    Color color;
    switch (functionId) {
    case CSSValueRgb:
    case CSSValueRgba:
        color = parseRGBParameters(colorRange, functionId == CSSValueRgba);
        break;
    case CSSValueHsl:
    case CSSValueHsla:
        color = parseHSLParameters(colorRange, functionId == CSSValueHsla);
        break;
    case CSSValueColor:
        color = parseColorFunctionParameters(colorRange);
        break;
    default:
        return Color();
    }
    range = colorRange;
    return color;
}

RefPtr<CSSPrimitiveValue> consumeColor(CSSParserTokenRange& range, CSSParserMode cssParserMode, bool acceptQuirkyColors)
{
    CSSValueID id = range.peek().id();
    if (StyleColor::isColorKeyword(id)) {
        if (!isValueAllowedInMode(id, cssParserMode))
            return nullptr;
        return consumeIdent(range);
    }
    Color color = parseHexColor(range, acceptQuirkyColors);
    if (!color.isValid())
        color = parseColorFunction(range);
    if (!color.isValid())
        return nullptr;
    return CSSValuePool::singleton().createValue(color);
}

static RefPtr<CSSPrimitiveValue> consumePositionComponent(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    if (range.peek().type() == IdentToken)
        return consumeIdent<CSSValueLeft, CSSValueTop, CSSValueBottom, CSSValueRight, CSSValueCenter>(range);
    return consumeLengthOrPercent(range, cssParserMode, ValueRangeAll, unitless);
}

static bool isHorizontalPositionKeywordOnly(const CSSPrimitiveValue& value)
{
    return value.isValueID() && (value.valueID() == CSSValueLeft || value.valueID() == CSSValueRight);
}

static bool isVerticalPositionKeywordOnly(const CSSPrimitiveValue& value)
{
    return value.isValueID() && (value.valueID() == CSSValueTop || value.valueID() == CSSValueBottom);
}

static void positionFromOneValue(CSSPrimitiveValue& value, RefPtr<CSSPrimitiveValue>& resultX, RefPtr<CSSPrimitiveValue>& resultY)
{
    bool valueAppliesToYAxisOnly = isVerticalPositionKeywordOnly(value);
    resultX = &value;
    resultY = CSSPrimitiveValue::createIdentifier(CSSValueCenter);
    if (valueAppliesToYAxisOnly)
        std::swap(resultX, resultY);
}

static bool positionFromTwoValues(CSSPrimitiveValue& value1, CSSPrimitiveValue& value2,
    RefPtr<CSSPrimitiveValue>& resultX, RefPtr<CSSPrimitiveValue>& resultY)
{
    bool mustOrderAsXY = isHorizontalPositionKeywordOnly(value1) || isVerticalPositionKeywordOnly(value2)
        || !value1.isValueID() || !value2.isValueID();
    bool mustOrderAsYX = isVerticalPositionKeywordOnly(value1) || isHorizontalPositionKeywordOnly(value2);
    if (mustOrderAsXY && mustOrderAsYX)
        return false;
    resultX = &value1;
    resultY = &value2;
    if (mustOrderAsYX)
        std::swap(resultX, resultY);
    return true;
}

    
template<typename... Args>
static Ref<CSSPrimitiveValue> createPrimitiveValuePair(Args&&... args)
{
    return CSSValuePool::singleton().createValue(Pair::create(std::forward<Args>(args)...));
}

static bool positionFromThreeOrFourValues(CSSPrimitiveValue** values, RefPtr<CSSPrimitiveValue>& resultX, RefPtr<CSSPrimitiveValue>& resultY)
{
    CSSPrimitiveValue* center = nullptr;
    for (int i = 0; values[i]; i++) {
        CSSPrimitiveValue* currentValue = values[i];
        if (!currentValue->isValueID())
            return false;
        CSSValueID id = currentValue->valueID();

        if (id == CSSValueCenter) {
            if (center)
                return false;
            center = currentValue;
            continue;
        }

        RefPtr<CSSPrimitiveValue> result;
        if (values[i + 1] && !values[i + 1]->isValueID())
            result = createPrimitiveValuePair(currentValue, values[++i]);
        else
            result = currentValue;

        if (id == CSSValueLeft || id == CSSValueRight) {
            if (resultX)
                return false;
            resultX = result;
        } else {
            ASSERT(id == CSSValueTop || id == CSSValueBottom);
            if (resultY)
                return false;
            resultY = result;
        }
    }

    if (center) {
        ASSERT(resultX || resultY);
        if (resultX && resultY)
            return false;
        if (!resultX)
            resultX = center;
        else
            resultY = center;
    }

    ASSERT(resultX && resultY);
    return true;
}

// FIXME: This may consume from the range upon failure. The background
// shorthand works around it, but we should just fix it here.
bool consumePosition(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless, RefPtr<CSSPrimitiveValue>& resultX, RefPtr<CSSPrimitiveValue>& resultY)
{
    RefPtr<CSSPrimitiveValue> value1 = consumePositionComponent(range, cssParserMode, unitless);
    if (!value1)
        return false;

    RefPtr<CSSPrimitiveValue> value2 = consumePositionComponent(range, cssParserMode, unitless);
    if (!value2) {
        positionFromOneValue(*value1, resultX, resultY);
        return true;
    }

    RefPtr<CSSPrimitiveValue> value3 = consumePositionComponent(range, cssParserMode, unitless);
    if (!value3)
        return positionFromTwoValues(*value1, *value2, resultX, resultY);

    RefPtr<CSSPrimitiveValue> value4 = consumePositionComponent(range, cssParserMode, unitless);
    CSSPrimitiveValue* values[5];
    values[0] = value1.get();
    values[1] = value2.get();
    values[2] = value3.get();
    values[3] = value4.get();
    values[4] = nullptr;
    return positionFromThreeOrFourValues(values, resultX, resultY);
}

RefPtr<CSSPrimitiveValue> consumePosition(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless)
{
    RefPtr<CSSPrimitiveValue> resultX;
    RefPtr<CSSPrimitiveValue> resultY;
    if (consumePosition(range, cssParserMode, unitless, resultX, resultY))
        return createPrimitiveValuePair(resultX.releaseNonNull(), resultY.releaseNonNull());
    return nullptr;
}

bool consumeOneOrTwoValuedPosition(CSSParserTokenRange& range, CSSParserMode cssParserMode, UnitlessQuirk unitless, RefPtr<CSSPrimitiveValue>& resultX, RefPtr<CSSPrimitiveValue>& resultY)
{
    RefPtr<CSSPrimitiveValue> value1 = consumePositionComponent(range, cssParserMode, unitless);
    if (!value1)
        return false;
    RefPtr<CSSPrimitiveValue> value2 = consumePositionComponent(range, cssParserMode, unitless);
    if (!value2) {
        positionFromOneValue(*value1, resultX, resultY);
        return true;
    }
    return positionFromTwoValues(*value1, *value2, resultX, resultY);
}

// This should go away once we drop support for -webkit-gradient
static RefPtr<CSSPrimitiveValue> consumeDeprecatedGradientPoint(CSSParserTokenRange& args, bool horizontal)
{
    if (args.peek().type() == IdentToken) {
        if ((horizontal && consumeIdent<CSSValueLeft>(args)) || (!horizontal && consumeIdent<CSSValueTop>(args)))
            return CSSValuePool::singleton().createValue(0., CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
        if ((horizontal && consumeIdent<CSSValueRight>(args)) || (!horizontal && consumeIdent<CSSValueBottom>(args)))
            return CSSValuePool::singleton().createValue(100., CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
        if (consumeIdent<CSSValueCenter>(args))
            return CSSValuePool::singleton().createValue(50., CSSPrimitiveValue::UnitTypes::CSS_PERCENTAGE);
        return nullptr;
    }
    RefPtr<CSSPrimitiveValue> result = consumePercent(args, ValueRangeAll);
    if (!result)
        result = consumeNumber(args, ValueRangeAll);
    return result;
}

// Used to parse colors for -webkit-gradient(...).
static RefPtr<CSSPrimitiveValue> consumeDeprecatedGradientStopColor(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    if (args.peek().id() == CSSValueCurrentcolor)
        return nullptr;
    return consumeColor(args, cssParserMode);
}

static bool consumeDeprecatedGradientColorStop(CSSParserTokenRange& range, CSSGradientColorStop& stop, CSSParserMode cssParserMode)
{
    CSSValueID id = range.peek().functionId();
    if (id != CSSValueFrom && id != CSSValueTo && id != CSSValueColorStop)
        return false;

    CSSParserTokenRange args = consumeFunction(range);
    double position;
    if (id == CSSValueFrom || id == CSSValueTo) {
        position = (id == CSSValueFrom) ? 0 : 1;
    } else {
        ASSERT(id == CSSValueColorStop);
        const CSSParserToken& arg = args.consumeIncludingWhitespace();
        if (arg.type() == PercentageToken)
            position = arg.numericValue() / 100.0;
        else if (arg.type() == NumberToken)
            position = arg.numericValue();
        else
            return false;

        if (!consumeCommaIncludingWhitespace(args))
            return false;
    }

    stop.m_position = CSSValuePool::singleton().createValue(position, CSSPrimitiveValue::UnitTypes::CSS_NUMBER);
    stop.m_color = consumeDeprecatedGradientStopColor(args, cssParserMode);
    return stop.m_color && args.atEnd();
}

static RefPtr<CSSValue> consumeDeprecatedGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode)
{
    RefPtr<CSSGradientValue> result;
    CSSValueID id = args.consumeIncludingWhitespace().id();
    bool isDeprecatedRadialGradient = (id == CSSValueRadial);
    if (isDeprecatedRadialGradient)
        result = CSSRadialGradientValue::create(NonRepeating, CSSDeprecatedRadialGradient);
    else if (id == CSSValueLinear)
        result = CSSLinearGradientValue::create(NonRepeating, CSSDeprecatedLinearGradient);
    if (!result || !consumeCommaIncludingWhitespace(args))
        return nullptr;

    RefPtr<CSSPrimitiveValue> point = consumeDeprecatedGradientPoint(args, true);
    if (!point)
        return nullptr;
    result->setFirstX(point.copyRef());
    point = consumeDeprecatedGradientPoint(args, false);
    if (!point)
        return nullptr;
    result->setFirstY(point.copyRef());

    if (!consumeCommaIncludingWhitespace(args))
        return nullptr;

    // For radial gradients only, we now expect a numeric radius.
    if (isDeprecatedRadialGradient) {
        RefPtr<CSSPrimitiveValue> radius = consumeNumber(args, ValueRangeAll);
        if (!radius || !consumeCommaIncludingWhitespace(args))
            return nullptr;
        downcast<CSSRadialGradientValue>(result.get())->setFirstRadius(radius.copyRef());
    }

    point = consumeDeprecatedGradientPoint(args, true);
    if (!point)
        return nullptr;
    result->setSecondX(point.copyRef());
    point = consumeDeprecatedGradientPoint(args, false);
    if (!point)
        return nullptr;
    result->setSecondY(point.copyRef());

    // For radial gradients only, we now expect the second radius.
    if (isDeprecatedRadialGradient) {
        if (!consumeCommaIncludingWhitespace(args))
            return nullptr;
        RefPtr<CSSPrimitiveValue> radius = consumeNumber(args, ValueRangeAll);
        if (!radius)
            return nullptr;
        downcast<CSSRadialGradientValue>(result.get())->setSecondRadius(radius.copyRef());
    }

    CSSGradientColorStop stop;
    while (consumeCommaIncludingWhitespace(args)) {
        if (!consumeDeprecatedGradientColorStop(args, stop, cssParserMode))
            return nullptr;
        result->addStop(stop);
    }

    return result;
}

static bool consumeGradientColorStops(CSSParserTokenRange& range, CSSParserMode cssParserMode, CSSGradientValue* gradient)
{
    bool supportsColorHints = gradient->gradientType() == CSSLinearGradient || gradient->gradientType() == CSSRadialGradient;

    // The first color stop cannot be a color hint.
    bool previousStopWasColorHint = true;
    do {
        CSSGradientColorStop stop;
        stop.m_color = consumeColor(range, cssParserMode);
        // Two hints in a row are not allowed.
        if (!stop.m_color && (!supportsColorHints || previousStopWasColorHint))
            return false;
        
        previousStopWasColorHint = !stop.m_color;
        
        // FIXME-NEWPARSER: This boolean could be removed. Null checking color would be sufficient.
        stop.isMidpoint = !stop.m_color;

        stop.m_position = consumeLengthOrPercent(range, cssParserMode, ValueRangeAll);
        if (!stop.m_color && !stop.m_position)
            return false;
        gradient->addStop(stop);
    } while (consumeCommaIncludingWhitespace(range));

    // The last color stop cannot be a color hint.
    if (previousStopWasColorHint)
        return false;

    // Must have 2 or more stops to be valid.
    return gradient->stopCount() >= 2;
}

static RefPtr<CSSValue> consumeDeprecatedRadialGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating)
{
    RefPtr<CSSRadialGradientValue> result = CSSRadialGradientValue::create(repeating, CSSPrefixedRadialGradient);
    RefPtr<CSSPrimitiveValue> centerX;
    RefPtr<CSSPrimitiveValue> centerY;
    consumeOneOrTwoValuedPosition(args, cssParserMode, UnitlessQuirk::Forbid, centerX, centerY);
    if ((centerX || centerY) && !consumeCommaIncludingWhitespace(args))
        return nullptr;

    result->setFirstX(centerX.copyRef());
    result->setFirstY(centerY.copyRef());
    result->setSecondX(centerX.copyRef());
    result->setSecondY(centerY.copyRef());

    RefPtr<CSSPrimitiveValue> shape = consumeIdent<CSSValueCircle, CSSValueEllipse>(args);
    RefPtr<CSSPrimitiveValue> sizeKeyword = consumeIdent<CSSValueClosestSide, CSSValueClosestCorner, CSSValueFarthestSide, CSSValueFarthestCorner, CSSValueContain, CSSValueCover>(args);
    if (!shape)
        shape = consumeIdent<CSSValueCircle, CSSValueEllipse>(args);
    result->setShape(shape.copyRef());
    result->setSizingBehavior(sizeKeyword.copyRef());

    // Or, two lengths or percentages
    if (!shape && !sizeKeyword) {
        RefPtr<CSSPrimitiveValue> horizontalSize = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
        RefPtr<CSSPrimitiveValue> verticalSize;
        if (horizontalSize) {
            verticalSize = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!verticalSize)
                return nullptr;
            consumeCommaIncludingWhitespace(args);
            result->setEndHorizontalSize(horizontalSize.copyRef());
            result->setEndVerticalSize(verticalSize.copyRef());
        }
    } else {
        consumeCommaIncludingWhitespace(args);
    }
    if (!consumeGradientColorStops(args, cssParserMode, result.get()))
        return nullptr;

    return result;
}

static RefPtr<CSSValue> consumeRadialGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating)
{
    RefPtr<CSSRadialGradientValue> result = CSSRadialGradientValue::create(repeating, CSSRadialGradient);

    RefPtr<CSSPrimitiveValue> shape;
    RefPtr<CSSPrimitiveValue> sizeKeyword;
    RefPtr<CSSPrimitiveValue> horizontalSize;
    RefPtr<CSSPrimitiveValue> verticalSize;

    // First part of grammar, the size/shape clause:
    // [ circle || <length> ] |
    // [ ellipse || [ <length> | <percentage> ]{2} ] |
    // [ [ circle | ellipse] || <size-keyword> ]
    for (int i = 0; i < 3; ++i) {
        if (args.peek().type() == IdentToken) {
            CSSValueID id = args.peek().id();
            if (id == CSSValueCircle || id == CSSValueEllipse) {
                if (shape)
                    return nullptr;
                shape = consumeIdent(args);
            } else if (id == CSSValueClosestSide || id == CSSValueClosestCorner || id == CSSValueFarthestSide || id == CSSValueFarthestCorner) {
                if (sizeKeyword)
                    return nullptr;
                sizeKeyword = consumeIdent(args);
            } else {
                break;
            }
        } else {
            RefPtr<CSSPrimitiveValue> center = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (!center)
                break;
            if (horizontalSize)
                return nullptr;
            horizontalSize = center;
            center = consumeLengthOrPercent(args, cssParserMode, ValueRangeAll);
            if (center) {
                verticalSize = center;
                ++i;
            }
        }
    }

    // You can specify size as a keyword or a length/percentage, not both.
    if (sizeKeyword && horizontalSize)
        return nullptr;
    // Circles must have 0 or 1 lengths.
    if (shape && shape->valueID() == CSSValueCircle && verticalSize)
        return nullptr;
    // Ellipses must have 0 or 2 length/percentages.
    if (shape && shape->valueID() == CSSValueEllipse && horizontalSize && !verticalSize)
        return nullptr;
    // If there's only one size, it must be a length.
    if (!verticalSize && horizontalSize && horizontalSize->isPercentage())
        return nullptr;
    if ((horizontalSize && horizontalSize->isCalculatedPercentageWithLength())
        || (verticalSize && verticalSize->isCalculatedPercentageWithLength()))
        return nullptr;

    result->setShape(shape.copyRef());
    result->setSizingBehavior(sizeKeyword.copyRef());
    result->setEndHorizontalSize(horizontalSize.copyRef());
    result->setEndVerticalSize(verticalSize.copyRef());

    RefPtr<CSSPrimitiveValue> centerX;
    RefPtr<CSSPrimitiveValue> centerY;
    if (args.peek().id() == CSSValueAt) {
        args.consumeIncludingWhitespace();
        consumePosition(args, cssParserMode, UnitlessQuirk::Forbid, centerX, centerY);
        if (!(centerX && centerY))
            return nullptr;
        
        result->setFirstX(centerX.copyRef());
        result->setFirstY(centerY.copyRef());
        
        // Right now, CSS radial gradients have the same start and end centers.
        result->setSecondX(centerX.copyRef());
        result->setSecondY(centerY.copyRef());
    }

    if ((shape || sizeKeyword || horizontalSize || centerX || centerY) && !consumeCommaIncludingWhitespace(args))
        return nullptr;
    if (!consumeGradientColorStops(args, cssParserMode, result.get()))
        return nullptr;
    return result;
}

static RefPtr<CSSValue> consumeLinearGradient(CSSParserTokenRange& args, CSSParserMode cssParserMode, CSSGradientRepeat repeating, CSSGradientType gradientType)
{
    RefPtr<CSSLinearGradientValue> result = CSSLinearGradientValue::create(repeating, gradientType);

    bool expectComma = true;
    RefPtr<CSSPrimitiveValue> angle = consumeAngle(args, cssParserMode, UnitlessQuirk::Forbid);
    if (angle)
        result->setAngle(angle.releaseNonNull());
    else if (gradientType == CSSPrefixedLinearGradient || consumeIdent<CSSValueTo>(args)) {
        RefPtr<CSSPrimitiveValue> endX = consumeIdent<CSSValueLeft, CSSValueRight>(args);
        RefPtr<CSSPrimitiveValue> endY = consumeIdent<CSSValueBottom, CSSValueTop>(args);
        if (!endX && !endY) {
            if (gradientType == CSSLinearGradient)
                return nullptr;
            endY = CSSPrimitiveValue::createIdentifier(CSSValueTop);
            expectComma = false;
        } else if (!endX) {
            endX = consumeIdent<CSSValueLeft, CSSValueRight>(args);
        }

        result->setFirstX(endX.copyRef());
        result->setFirstY(endY.copyRef());
    } else {
        expectComma = false;
    }

    if (expectComma && !consumeCommaIncludingWhitespace(args))
        return nullptr;
    if (!consumeGradientColorStops(args, cssParserMode, result.get()))
        return nullptr;
    return result;
}

RefPtr<CSSValue> consumeImageOrNone(CSSParserTokenRange& range, CSSParserContext context)
{
    if (range.peek().id() == CSSValueNone)
        return consumeIdent(range);
    return consumeImage(range, context);
}

static RefPtr<CSSValue> consumeCrossFade(CSSParserTokenRange& args, CSSParserContext context, bool prefixed)
{
    RefPtr<CSSValue> fromImageValue = consumeImageOrNone(args, context);
    if (!fromImageValue || !consumeCommaIncludingWhitespace(args))
        return nullptr;
    RefPtr<CSSValue> toImageValue = consumeImageOrNone(args, context);
    if (!toImageValue || !consumeCommaIncludingWhitespace(args))
        return nullptr;

    RefPtr<CSSPrimitiveValue> percentage;
    const CSSParserToken& percentageArg = args.consumeIncludingWhitespace();
    if (percentageArg.type() == PercentageToken)
        percentage = CSSValuePool::singleton().createValue(clampTo<double>(percentageArg.numericValue() / 100, 0, 1), CSSPrimitiveValue::UnitTypes::CSS_NUMBER);
    else if (percentageArg.type() == NumberToken)
        percentage = CSSValuePool::singleton().createValue(clampTo<double>(percentageArg.numericValue(), 0, 1), CSSPrimitiveValue::UnitTypes::CSS_NUMBER);

    if (!percentage)
        return nullptr;
    return CSSCrossfadeValue::create(fromImageValue.releaseNonNull(), toImageValue.releaseNonNull(), percentage.releaseNonNull(), prefixed);
}

static RefPtr<CSSValue> consumeWebkitCanvas(CSSParserTokenRange& args)
{
    if (args.peek().type() != IdentToken)
        return nullptr;
    auto canvasName = args.consumeIncludingWhitespace().value().toString();
    if (!args.atEnd())
        return nullptr;
    return CSSCanvasValue::create(canvasName);
}

static RefPtr<CSSValue> consumeWebkitNamedImage(CSSParserTokenRange& args)
{
    if (args.peek().type() != IdentToken)
        return nullptr;
    auto imageName = args.consumeIncludingWhitespace().value().toString();
    if (!args.atEnd())
        return nullptr;
    return CSSNamedImageValue::create(imageName);
}
    
static RefPtr<CSSValue> consumeGeneratedImage(CSSParserTokenRange& range, CSSParserContext context)
{
    CSSValueID id = range.peek().functionId();
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    RefPtr<CSSValue> result;
    if (id == CSSValueRadialGradient)
        result = consumeRadialGradient(args, context.mode, NonRepeating);
    else if (id == CSSValueRepeatingRadialGradient)
        result = consumeRadialGradient(args, context.mode, Repeating);
    else if (id == CSSValueWebkitLinearGradient)
        result = consumeLinearGradient(args, context.mode, NonRepeating, CSSPrefixedLinearGradient);
    else if (id == CSSValueWebkitRepeatingLinearGradient)
        result = consumeLinearGradient(args, context.mode, Repeating, CSSPrefixedLinearGradient);
    else if (id == CSSValueRepeatingLinearGradient)
        result = consumeLinearGradient(args, context.mode, Repeating, CSSLinearGradient);
    else if (id == CSSValueLinearGradient)
        result = consumeLinearGradient(args, context.mode, NonRepeating, CSSLinearGradient);
    else if (id == CSSValueWebkitGradient)
        result = consumeDeprecatedGradient(args, context.mode);
    else if (id == CSSValueWebkitRadialGradient)
        result = consumeDeprecatedRadialGradient(args, context.mode, NonRepeating);
    else if (id == CSSValueWebkitRepeatingRadialGradient)
        result = consumeDeprecatedRadialGradient(args, context.mode, Repeating);
    else if (id == CSSValueWebkitCrossFade || id == CSSValueCrossFade)
        result = consumeCrossFade(args, context, id == CSSValueWebkitCrossFade);
    else if (id == CSSValueWebkitCanvas)
        result = consumeWebkitCanvas(args);
    else if (id == CSSValueWebkitNamedImage)
        result = consumeWebkitNamedImage(args);

    if (!result || !args.atEnd())
        return nullptr;
    range = rangeCopy;
    return result;
}

static RefPtr<CSSValue> consumeImageSet(CSSParserTokenRange& range, const CSSParserContext& context)
{
    CSSParserTokenRange rangeCopy = range;
    CSSParserTokenRange args = consumeFunction(rangeCopy);
    RefPtr<CSSImageSetValue> imageSet = CSSImageSetValue::create();
    do {
        AtomicString urlValue = consumeUrlAsStringView(args).toAtomicString();
        if (urlValue.isNull())
            return nullptr;

        RefPtr<CSSValue> image = CSSImageValue::create(completeURL(context, urlValue));
        imageSet->append(image.releaseNonNull());

        const CSSParserToken& token = args.consumeIncludingWhitespace();
        if (token.type() != DimensionToken)
            return nullptr;
        if (token.value() != "x")
            return nullptr;
        ASSERT(token.unitType() == CSSPrimitiveValue::UnitTypes::CSS_UNKNOWN);
        double imageScaleFactor = token.numericValue();
        if (imageScaleFactor <= 0)
            return nullptr;
        imageSet->append(CSSValuePool::singleton().createValue(imageScaleFactor, CSSPrimitiveValue::UnitTypes::CSS_NUMBER));
    } while (consumeCommaIncludingWhitespace(args));
    if (!args.atEnd())
        return nullptr;
    range = rangeCopy;
    return imageSet;
}

static bool isGeneratedImage(CSSValueID id)
{
    return id == CSSValueLinearGradient || id == CSSValueRadialGradient
        || id == CSSValueRepeatingLinearGradient || id == CSSValueRepeatingRadialGradient
        || id == CSSValueWebkitLinearGradient || id == CSSValueWebkitRadialGradient
        || id == CSSValueWebkitRepeatingLinearGradient || id == CSSValueWebkitRepeatingRadialGradient
        || id == CSSValueWebkitGradient || id == CSSValueWebkitCrossFade || id == CSSValueWebkitCanvas
        || id == CSSValueCrossFade || id == CSSValueWebkitNamedImage;
}

RefPtr<CSSValue> consumeImage(CSSParserTokenRange& range, CSSParserContext context, ConsumeGeneratedImage generatedImage)
{
    AtomicString uri = consumeUrlAsStringView(range).toAtomicString();
    if (!uri.isNull())
        return CSSImageValue::create(completeURL(context, uri));
    if (range.peek().type() == FunctionToken) {
        CSSValueID id = range.peek().functionId();
        if (id == CSSValueWebkitImageSet || id == CSSValueImageSet)
            return consumeImageSet(range, context);
        if (generatedImage == ConsumeGeneratedImage::Allow && isGeneratedImage(id))
            return consumeGeneratedImage(range, context);
    }
    return nullptr;
}

} // namespace CSSPropertyParserHelpers

} // namespace WebCore
