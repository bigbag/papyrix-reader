#include "test_utils.h"

#include <map>
#include <string>

#include "HardwareSerial.h"

#include <CssParser.h>

int main() {
  TestUtils::TestRunner runner("CSS Parser");

  // ============================================
  // parseTextAlign via parseInlineStyle
  // ============================================

  // Test 1: Standard values
  runner.expectTrue(CssParser::parseInlineStyle("text-align: left").textAlign == TextAlign::Left,
                    "parseTextAlign: 'left'");
  runner.expectTrue(CssParser::parseInlineStyle("text-align: right").textAlign == TextAlign::Right,
                    "parseTextAlign: 'right'");
  runner.expectTrue(CssParser::parseInlineStyle("text-align: center").textAlign == TextAlign::Center,
                    "parseTextAlign: 'center'");
  runner.expectTrue(CssParser::parseInlineStyle("text-align: justify").textAlign == TextAlign::Justify,
                    "parseTextAlign: 'justify'");

  // Test 2: Logical values
  runner.expectTrue(CssParser::parseInlineStyle("text-align: start").textAlign == TextAlign::Left,
                    "parseTextAlign: 'start' maps to Left");
  runner.expectTrue(CssParser::parseInlineStyle("text-align: end").textAlign == TextAlign::Right,
                    "parseTextAlign: 'end' maps to Right");

  // Test 3: Case insensitivity
  runner.expectTrue(CssParser::parseInlineStyle("text-align: LEFT").textAlign == TextAlign::Left,
                    "parseTextAlign: 'LEFT' (uppercase)");
  runner.expectTrue(CssParser::parseInlineStyle("text-align: Center").textAlign == TextAlign::Center,
                    "parseTextAlign: 'Center' (mixed case)");

  // Test 4: Whitespace trimming
  runner.expectTrue(CssParser::parseInlineStyle("text-align:   center  ").textAlign == TextAlign::Center,
                    "parseTextAlign: whitespace trimmed");

  // ============================================
  // parseFontStyle via parseInlineStyle
  // ============================================

  // Test 6: Standard values
  runner.expectTrue(CssParser::parseInlineStyle("font-style: normal").fontStyle == CssFontStyle::Normal,
                    "parseFontStyle: 'normal'");
  runner.expectTrue(CssParser::parseInlineStyle("font-style: italic").fontStyle == CssFontStyle::Italic,
                    "parseFontStyle: 'italic'");
  runner.expectTrue(CssParser::parseInlineStyle("font-style: oblique").fontStyle == CssFontStyle::Italic,
                    "parseFontStyle: 'oblique' maps to Italic");

  // Test 7: Case insensitivity
  runner.expectTrue(CssParser::parseInlineStyle("font-style: ITALIC").fontStyle == CssFontStyle::Italic,
                    "parseFontStyle: 'ITALIC' (uppercase)");

  // ============================================
  // parseFontWeight via parseInlineStyle
  // ============================================

  // Test 9: Keyword values
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: normal").fontWeight == CssFontWeight::Normal,
                    "parseFontWeight: 'normal'");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: bold").fontWeight == CssFontWeight::Bold,
                    "parseFontWeight: 'bold'");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: bolder").fontWeight == CssFontWeight::Bold,
                    "parseFontWeight: 'bolder'");

  // Test 10: Numeric values
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 400").fontWeight == CssFontWeight::Normal,
                    "parseFontWeight: '400' is Normal");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 700").fontWeight == CssFontWeight::Bold,
                    "parseFontWeight: '700' is Bold");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 800").fontWeight == CssFontWeight::Bold,
                    "parseFontWeight: '800' is Bold");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 900").fontWeight == CssFontWeight::Bold,
                    "parseFontWeight: '900' is Bold");

  // Test 11: Values below 700 are normal
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 500").fontWeight == CssFontWeight::Normal,
                    "parseFontWeight: '500' is Normal");
  runner.expectTrue(CssParser::parseInlineStyle("font-weight: 600").fontWeight == CssFontWeight::Normal,
                    "parseFontWeight: '600' is Normal");

  // ============================================
  // parseInlineStyle() tests
  // ============================================

  // Test 22: Single property
  {
    CssStyle style = CssParser::parseInlineStyle("text-align: center");
    runner.expectTrue(style.hasTextAlign(), "parseInlineStyle: single prop has text-align");
    runner.expectTrue(style.textAlign == TextAlign::Center, "parseInlineStyle: text-align is center");
  }

  // Test 23: Multiple properties
  {
    CssStyle style = CssParser::parseInlineStyle("text-align: center; font-weight: bold");
    runner.expectTrue(style.hasTextAlign(), "parseInlineStyle: multi-prop has text-align");
    runner.expectTrue(style.hasFontWeight(), "parseInlineStyle: multi-prop has font-weight");
    runner.expectTrue(style.textAlign == TextAlign::Center, "parseInlineStyle: multi-prop text-align");
    runner.expectTrue(style.fontWeight == CssFontWeight::Bold, "parseInlineStyle: multi-prop font-weight");
  }

  // Test 24: With extra whitespace
  {
    CssStyle style = CssParser::parseInlineStyle("  font-style :  italic  ;  font-weight : bold  ");
    runner.expectTrue(style.hasFontStyle(), "parseInlineStyle: whitespace font-style");
    runner.expectTrue(style.fontStyle == CssFontStyle::Italic, "parseInlineStyle: whitespace font-style value");
    runner.expectTrue(style.hasFontWeight(), "parseInlineStyle: whitespace font-weight");
    runner.expectTrue(style.fontWeight == CssFontWeight::Bold, "parseInlineStyle: whitespace font-weight value");
  }

  // Test 25: Empty string
  {
    CssStyle style = CssParser::parseInlineStyle("");
    runner.expectFalse(style.hasTextAlign(), "parseInlineStyle: empty has no properties");
    runner.expectFalse(style.hasFontStyle(), "parseInlineStyle: empty has no font-style");
  }

  // Test 26: Missing semicolons (last property)
  {
    CssStyle style = CssParser::parseInlineStyle("text-align: right");
    runner.expectTrue(style.textAlign == TextAlign::Right, "parseInlineStyle: no trailing semicolon");
  }

  // Test 27: Missing colon (property ignored)
  {
    CssStyle style = CssParser::parseInlineStyle("text-align center; font-weight: bold");
    runner.expectFalse(style.hasTextAlign(), "parseInlineStyle: missing colon ignored");
    runner.expectTrue(style.hasFontWeight(), "parseInlineStyle: valid prop after invalid");
  }

  // Test 28: Unknown properties ignored (color is unknown)
  {
    CssStyle style = CssParser::parseInlineStyle("color: red; text-align: left");
    runner.expectTrue(style.hasTextAlign(), "parseInlineStyle: known prop parsed");
  }

  // Test 29: Case insensitivity for property names
  {
    CssStyle style = CssParser::parseInlineStyle("TEXT-ALIGN: center; FONT-WEIGHT: bold");
    runner.expectTrue(style.textAlign == TextAlign::Center, "parseInlineStyle: uppercase prop name");
    runner.expectTrue(style.fontWeight == CssFontWeight::Bold, "parseInlineStyle: uppercase prop name 2");
  }

  // ============================================
  // text-align: inherit tests
  // ============================================

  {
    CssStyle style = CssParser::parseInlineStyle("text-align: inherit");
    runner.expectFalse(style.hasTextAlign(), "text-align_inherit: hasTextAlign not set");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("text-align: Inherit");
    runner.expectFalse(style.hasTextAlign(), "text-align_Inherit: case insensitive");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("text-align:  INHERIT ");
    runner.expectFalse(style.hasTextAlign(), "text-align_INHERIT: whitespace + uppercase");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("text-align: inherit; font-weight: bold");
    runner.expectFalse(style.hasTextAlign(), "text-align_inherit_combo: hasTextAlign not set");
    runner.expectTrue(style.hasFontWeight(), "text-align_inherit_combo: other props still parsed");
    runner.expectTrue(style.fontWeight == CssFontWeight::Bold, "text-align_inherit_combo: font-weight correct");
  }

  // ============================================
  // CssStyle::applyOver() tests
  // ============================================

  // Test 30: applyOver overrides
  {
    CssStyle base;
    base.textAlign = TextAlign::Left;
    base.defined.textAlign = 1;

    CssStyle other;
    other.textAlign = TextAlign::Center;
    other.defined.textAlign = 1;
    other.fontWeight = CssFontWeight::Bold;
    other.defined.fontWeight = 1;

    base.applyOver(other);

    runner.expectTrue(base.textAlign == TextAlign::Center, "applyOver: override takes precedence");
    runner.expectTrue(base.fontWeight == CssFontWeight::Bold, "applyOver: new property added");
  }

  // Test 31: applyOver preserves unset properties
  {
    CssStyle base;
    base.textAlign = TextAlign::Right;
    base.defined.textAlign = 1;
    base.fontStyle = CssFontStyle::Italic;
    base.defined.fontStyle = 1;

    CssStyle other;
    other.fontWeight = CssFontWeight::Bold;
    other.defined.fontWeight = 1;

    base.applyOver(other);

    runner.expectTrue(base.textAlign == TextAlign::Right, "applyOver: unset property preserved");
    runner.expectTrue(base.fontStyle == CssFontStyle::Italic, "applyOver: unset property preserved 2");
    runner.expectTrue(base.fontWeight == CssFontWeight::Bold, "applyOver: new property added");
  }

  // ============================================
  // CssStyle::reset() tests
  // ============================================

  {
    CssStyle style;
    style.textAlign = TextAlign::Center;
    style.defined.textAlign = 1;
    style.fontWeight = CssFontWeight::Bold;
    style.defined.fontWeight = 1;
    style.fontStyle = CssFontStyle::Italic;
    style.defined.fontStyle = 1;
    style.direction = TextDirection::Rtl;
    style.defined.direction = 1;

    style.reset();

    runner.expectTrue(style.textAlign == TextAlign::None, "reset: textAlign to None");
    runner.expectFalse(style.hasTextAlign(), "reset: hasTextAlign false");
    runner.expectTrue(style.fontWeight == CssFontWeight::Normal, "reset: fontWeight to Normal");
    runner.expectFalse(style.hasFontWeight(), "reset: hasFontWeight false");
    runner.expectTrue(style.fontStyle == CssFontStyle::Normal, "reset: fontStyle to Normal");
    runner.expectFalse(style.hasFontStyle(), "reset: hasFontStyle false");
    runner.expectTrue(style.direction == TextDirection::Ltr, "reset: direction to Ltr");
    runner.expectFalse(style.hasDirection(), "reset: hasDirection false");
  }

  // ============================================
  // Direction property tests
  // ============================================

  {
    CssStyle style = CssParser::parseInlineStyle("direction: rtl");
    runner.expectTrue(style.hasDirection(), "direction: rtl sets hasDirection");
    runner.expectTrue(style.direction == TextDirection::Rtl, "direction: rtl value");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("direction: ltr");
    runner.expectTrue(style.hasDirection(), "direction: ltr sets hasDirection");
    runner.expectTrue(style.direction == TextDirection::Ltr, "direction: ltr value");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("direction: RTL");
    runner.expectTrue(style.hasDirection(), "direction: RTL uppercase");
    runner.expectTrue(style.direction == TextDirection::Rtl, "direction: RTL uppercase value");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("direction: auto");
    runner.expectFalse(style.hasDirection(), "direction: unknown value not set");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("text-align: right; direction: rtl; font-weight: bold");
    runner.expectTrue(style.hasDirection(), "direction: combined has direction");
    runner.expectTrue(style.direction == TextDirection::Rtl, "direction: combined rtl value");
    runner.expectTrue(style.hasTextAlign(), "direction: combined has text-align");
    runner.expectTrue(style.hasFontWeight(), "direction: combined has font-weight");
  }

  // Merge direction
  {
    CssStyle base;
    CssStyle other;
    other.direction = TextDirection::Rtl;
    other.defined.direction = 1;

    base.applyOver(other);
    runner.expectTrue(base.hasDirection(), "applyOver direction: sets hasDirection");
    runner.expectTrue(base.direction == TextDirection::Rtl, "applyOver direction: rtl value");
  }

  {
    CssStyle base;
    base.direction = TextDirection::Rtl;
    base.defined.direction = 1;

    CssStyle other;
    base.applyOver(other);
    runner.expectTrue(base.direction == TextDirection::Rtl, "applyOver direction: preserved when not overridden");
  }

  // ============================================
  // New CSS properties: margin, padding, display
  // ============================================

  // CssLength parsing via margin-top
  {
    CssStyle style = CssParser::parseInlineStyle("margin-top: 10px");
    runner.expectTrue(style.hasMarginTop(), "margin-top: px parsed");
    runner.expectTrue(style.marginTop.value == 10.0f, "margin-top: px value");
    runner.expectTrue(style.marginTop.unit == CssUnit::Pixels, "margin-top: px unit");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("margin-top: 1.5em");
    runner.expectTrue(style.hasMarginTop(), "margin-top: em parsed");
    runner.expectTrue(style.marginTop.value == 1.5f, "margin-top: em value");
    runner.expectTrue(style.marginTop.unit == CssUnit::Em, "margin-top: em unit");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("margin-bottom: 2rem");
    runner.expectTrue(style.hasMarginBottom(), "margin-bottom: rem parsed");
    runner.expectTrue(style.marginBottom.unit == CssUnit::Rem, "margin-bottom: rem unit");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("margin-left: 12pt");
    runner.expectTrue(style.hasMarginLeft(), "margin-left: pt parsed");
    runner.expectTrue(style.marginLeft.unit == CssUnit::Points, "margin-left: pt unit");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("margin-right: 50%");
    runner.expectTrue(style.hasMarginRight(), "margin-right: % parsed");
    runner.expectTrue(style.marginRight.value == 50.0f, "margin-right: % value");
    runner.expectTrue(style.marginRight.unit == CssUnit::Percent, "margin-right: % unit");
  }

  // Bare number → pixels
  {
    CssStyle style = CssParser::parseInlineStyle("margin-top: 5");
    runner.expectTrue(style.marginTop.value == 5.0f, "margin-top: bare number value");
    runner.expectTrue(style.marginTop.unit == CssUnit::Pixels, "margin-top: bare number is pixels");
  }

  // auto/inherit → zero
  {
    CssStyle style = CssParser::parseInlineStyle("margin-top: auto");
    runner.expectTrue(style.marginTop.value == 0.0f, "margin-top: auto → zero");
  }

  // Margin shorthand - 1 value
  {
    CssStyle style = CssParser::parseInlineStyle("margin: 10px");
    runner.expectTrue(style.hasMarginTop(), "margin shorthand 1: top defined");
    runner.expectTrue(style.marginTop.value == 10.0f, "margin shorthand 1: all sides 10px");
    runner.expectTrue(style.marginBottom.value == 10.0f, "margin shorthand 1: bottom");
    runner.expectTrue(style.marginLeft.value == 10.0f, "margin shorthand 1: left");
    runner.expectTrue(style.marginRight.value == 10.0f, "margin shorthand 1: right");
  }

  // Margin shorthand - 2 values (TB LR)
  {
    CssStyle style = CssParser::parseInlineStyle("margin: 1em 2em");
    runner.expectTrue(style.marginTop.value == 1.0f, "margin shorthand 2: top");
    runner.expectTrue(style.marginBottom.value == 1.0f, "margin shorthand 2: bottom");
    runner.expectTrue(style.marginLeft.value == 2.0f, "margin shorthand 2: left");
    runner.expectTrue(style.marginRight.value == 2.0f, "margin shorthand 2: right");
  }

  // Margin shorthand - 3 values (T LR B)
  {
    CssStyle style = CssParser::parseInlineStyle("margin: 1em 2em 3em");
    runner.expectTrue(style.marginTop.value == 1.0f, "margin shorthand 3: top");
    runner.expectTrue(style.marginRight.value == 2.0f, "margin shorthand 3: right");
    runner.expectTrue(style.marginLeft.value == 2.0f, "margin shorthand 3: left");
    runner.expectTrue(style.marginBottom.value == 3.0f, "margin shorthand 3: bottom");
  }

  // Margin shorthand - 4 values (T R B L)
  {
    CssStyle style = CssParser::parseInlineStyle("margin: 1px 2px 3px 4px");
    runner.expectTrue(style.marginTop.value == 1.0f, "margin shorthand 4: top");
    runner.expectTrue(style.marginRight.value == 2.0f, "margin shorthand 4: right");
    runner.expectTrue(style.marginBottom.value == 3.0f, "margin shorthand 4: bottom");
    runner.expectTrue(style.marginLeft.value == 4.0f, "margin shorthand 4: left");
  }

  // Padding individual
  {
    CssStyle style = CssParser::parseInlineStyle("padding-top: 5px; padding-bottom: 10px");
    runner.expectTrue(style.hasPaddingTop(), "padding-top parsed");
    runner.expectTrue(style.paddingTop.value == 5.0f, "padding-top value");
    runner.expectTrue(style.hasPaddingBottom(), "padding-bottom parsed");
    runner.expectTrue(style.paddingBottom.value == 10.0f, "padding-bottom value");
  }

  // Padding shorthand
  {
    CssStyle style = CssParser::parseInlineStyle("padding: 1em 0");
    runner.expectTrue(style.paddingTop.value == 1.0f, "padding shorthand: top");
    runner.expectTrue(style.paddingBottom.value == 1.0f, "padding shorthand: bottom");
    runner.expectTrue(style.paddingLeft.value == 0.0f, "padding shorthand: left");
    runner.expectTrue(style.paddingRight.value == 0.0f, "padding shorthand: right");
  }

  // Display
  {
    CssStyle style = CssParser::parseInlineStyle("display: none");
    runner.expectTrue(style.hasDisplay(), "display: none parsed");
    runner.expectTrue(style.display == CssDisplay::None, "display: none value");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("display: block");
    runner.expectTrue(style.hasDisplay(), "display: block parsed");
    runner.expectTrue(style.display == CssDisplay::Block, "display: block value");
  }

  {
    CssStyle style = CssParser::parseInlineStyle("display: inline");
    runner.expectTrue(style.display == CssDisplay::Block, "display: inline → Block");
  }

  // Display with !important
  {
    CssStyle style = CssParser::parseInlineStyle("display: none !important");
    runner.expectTrue(style.display == CssDisplay::None, "display: none !important");
  }

  // CssLength resolution
  {
    CssLength em(1.5f, CssUnit::Em);
    runner.expectTrue(em.toPixels(20.0f) == 30.0f, "CssLength: 1.5em * 20px = 30px");

    CssLength pt(12.0f, CssUnit::Points);
    float ptPx = pt.toPixels(0);
    runner.expectTrue(ptPx > 15.9f && ptPx < 16.0f, "CssLength: 12pt ≈ 15.96px");

    CssLength pct(50.0f, CssUnit::Percent);
    runner.expectTrue(pct.toPixels(0, 400.0f) == 200.0f, "CssLength: 50% of 400 = 200");
  }

  // CssPropertyFlags
  {
    CssPropertyFlags flags;
    runner.expectFalse(flags.anySet(), "CssPropertyFlags: default none set");
    flags.marginTop = 1;
    runner.expectTrue(flags.anySet(), "CssPropertyFlags: marginTop set");
    flags.clearAll();
    runner.expectFalse(flags.anySet(), "CssPropertyFlags: cleared");
  }

  // applyOver with new properties
  {
    CssStyle base;
    base.marginTop = CssLength{10.0f, CssUnit::Pixels};
    base.defined.marginTop = 1;

    CssStyle other;
    other.marginBottom = CssLength{20.0f, CssUnit::Pixels};
    other.defined.marginBottom = 1;

    base.applyOver(other);
    runner.expectTrue(base.hasMarginTop(), "applyOver: preserves marginTop");
    runner.expectTrue(base.marginTop.value == 10.0f, "applyOver: marginTop value preserved");
    runner.expectTrue(base.hasMarginBottom(), "applyOver: adds marginBottom");
    runner.expectTrue(base.marginBottom.value == 20.0f, "applyOver: marginBottom value");
  }

  return runner.allPassed() ? 0 : 1;
}
