// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/label.h"

#include "base/i18n/rtl.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/canvas.h"
#include "ui/views/border.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

using base::ASCIIToUTF16;

namespace views {

typedef ViewsTestBase LabelTest;

// All text sizing measurements (width and height) should be greater than this.
const int kMinTextDimension = 4;

// A test utility function to set the application default text direction.
void SetRTL(bool rtl) {
  // Override the current locale/direction.
  base::i18n::SetICUDefaultLocale(rtl ? "he" : "en");
  EXPECT_EQ(rtl, base::i18n::IsRTL());
}

TEST_F(LabelTest, FontPropertySymbol) {
  Label label;
  std::string font_name("symbol");
  gfx::Font font(font_name, 26);
  label.SetFontList(gfx::FontList(font));
  gfx::Font font_used = label.font_list().GetPrimaryFont();
  EXPECT_EQ(font_name, font_used.GetFontName());
  EXPECT_EQ(26, font_used.GetFontSize());
}

TEST_F(LabelTest, FontPropertyArial) {
  Label label;
  std::string font_name("arial");
  gfx::Font font(font_name, 30);
  label.SetFontList(gfx::FontList(font));
  gfx::Font font_used = label.font_list().GetPrimaryFont();
  EXPECT_EQ(font_name, font_used.GetFontName());
  EXPECT_EQ(30, font_used.GetFontSize());
}

TEST_F(LabelTest, TextProperty) {
  Label label;
  base::string16 test_text(ASCIIToUTF16("A random string."));
  label.SetText(test_text);
  EXPECT_EQ(test_text, label.text());
}

TEST_F(LabelTest, ColorProperty) {
  Label label;
  SkColor color = SkColorSetARGB(20, 40, 10, 5);
  label.SetAutoColorReadabilityEnabled(false);
  label.SetEnabledColor(color);
  EXPECT_EQ(color, label.enabled_color());
}

TEST_F(LabelTest, AlignmentProperty) {
  const bool was_rtl = base::i18n::IsRTL();

  Label label;
  for (size_t i = 0; i < 2; ++i) {
    // Toggle the application default text direction (to try each direction).
    SetRTL(!base::i18n::IsRTL());
    bool reverse_alignment = base::i18n::IsRTL();

    // The alignment should be flipped in RTL UI.
    label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    EXPECT_EQ(reverse_alignment ? gfx::ALIGN_LEFT : gfx::ALIGN_RIGHT,
              label.GetHorizontalAlignment());
    label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
    EXPECT_EQ(reverse_alignment ? gfx::ALIGN_RIGHT : gfx::ALIGN_LEFT,
              label.GetHorizontalAlignment());
    label.SetHorizontalAlignment(gfx::ALIGN_CENTER);
    EXPECT_EQ(gfx::ALIGN_CENTER, label.GetHorizontalAlignment());

    for (size_t j = 0; j < 2; ++j) {
      label.SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
      const bool rtl = j == 0;
      label.SetText(rtl ? base::WideToUTF16(L"\x5d0") : ASCIIToUTF16("A"));
      EXPECT_EQ(rtl ? gfx::ALIGN_RIGHT : gfx::ALIGN_LEFT,
                label.GetHorizontalAlignment());
    }
  }

  EXPECT_EQ(was_rtl, base::i18n::IsRTL());
}

TEST_F(LabelTest, MultiLineProperty) {
  Label label;
  EXPECT_FALSE(label.multi_line());
  label.SetMultiLine(true);
  EXPECT_TRUE(label.multi_line());
  label.SetMultiLine(false);
  EXPECT_FALSE(label.multi_line());
}

TEST_F(LabelTest, ObscuredProperty) {
  Label label;
  base::string16 test_text(ASCIIToUTF16("Password!"));
  label.SetText(test_text);

  // The text should be unobscured by default.
  EXPECT_FALSE(label.obscured());
  EXPECT_EQ(test_text, label.GetLayoutTextForTesting());
  EXPECT_EQ(test_text, label.text());

  label.SetObscured(true);
  EXPECT_TRUE(label.obscured());
  EXPECT_EQ(ASCIIToUTF16("*********"), label.GetLayoutTextForTesting());
  EXPECT_EQ(test_text, label.text());

  label.SetText(test_text + test_text);
  EXPECT_EQ(ASCIIToUTF16("******************"),
            label.GetLayoutTextForTesting());
  EXPECT_EQ(test_text + test_text, label.text());

  label.SetObscured(false);
  EXPECT_FALSE(label.obscured());
  EXPECT_EQ(test_text + test_text, label.GetLayoutTextForTesting());
  EXPECT_EQ(test_text + test_text, label.text());
}

TEST_F(LabelTest, ObscuredSurrogatePair) {
  // 'MUSICAL SYMBOL G CLEF': represented in UTF-16 as two characters
  // forming the surrogate pair 0x0001D11E.
  Label label;
  base::string16 test_text = base::UTF8ToUTF16("\xF0\x9D\x84\x9E");
  label.SetText(test_text);

  label.SetObscured(true);
  EXPECT_EQ(ASCIIToUTF16("*"), label.GetLayoutTextForTesting());
  EXPECT_EQ(test_text, label.text());
}

TEST_F(LabelTest, TooltipProperty) {
  Label label;
  label.SetText(ASCIIToUTF16("My cool string."));

  // Initially, label has no bounds, its text does not fit, and therefore its
  // text should be returned as the tooltip text.
  base::string16 tooltip;
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  EXPECT_EQ(label.text(), tooltip);

  // While tooltip handling is disabled, GetTooltipText() should fail.
  label.SetHandlesTooltips(false);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));
  label.SetHandlesTooltips(true);

  // When set, custom tooltip text should be returned instead of the label's
  // text.
  base::string16 tooltip_text(ASCIIToUTF16("The tooltip!"));
  label.SetTooltipText(tooltip_text);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  EXPECT_EQ(tooltip_text, tooltip);

  // While tooltip handling is disabled, GetTooltipText() should fail.
  label.SetHandlesTooltips(false);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));
  label.SetHandlesTooltips(true);

  // When the tooltip text is set to an empty string, the original behavior is
  // restored.
  label.SetTooltipText(base::string16());
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  EXPECT_EQ(label.text(), tooltip);

  // While tooltip handling is disabled, GetTooltipText() should fail.
  label.SetHandlesTooltips(false);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));
  label.SetHandlesTooltips(true);

  // Make the label big enough to hold the text
  // and expect there to be no tooltip.
  label.SetBounds(0, 0, 1000, 40);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Shrinking the single-line label's height shouldn't trigger a tooltip.
  label.SetBounds(0, 0, 1000, label.GetPreferredSize().height() / 2);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Verify that explicitly set tooltip text is shown, regardless of size.
  label.SetTooltipText(tooltip_text);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  EXPECT_EQ(tooltip_text, tooltip);
  // Clear out the explicitly set tooltip text.
  label.SetTooltipText(base::string16());

  // Shrink the bounds and the tooltip should come back.
  label.SetBounds(0, 0, 10, 10);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Make the label obscured and there is no tooltip.
  label.SetObscured(true);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Obscuring the text shouldn't permanently clobber the tooltip.
  label.SetObscured(false);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Making the label multiline shouldn't eliminate the tooltip.
  label.SetMultiLine(true);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  // Expanding the multiline label bounds should eliminate the tooltip.
  label.SetBounds(0, 0, 1000, 1000);
  EXPECT_FALSE(label.GetTooltipText(gfx::Point(), &tooltip));

  // Verify that setting the tooltip still shows it.
  label.SetTooltipText(tooltip_text);
  EXPECT_TRUE(label.GetTooltipText(gfx::Point(), &tooltip));
  EXPECT_EQ(tooltip_text, tooltip);
  // Clear out the tooltip.
  label.SetTooltipText(base::string16());
}

TEST_F(LabelTest, Accessibility) {
  Label label;
  label.SetText(ASCIIToUTF16("My special text."));

  ui::AXViewState state;
  label.GetAccessibleState(&state);
  EXPECT_EQ(ui::AX_ROLE_STATIC_TEXT, state.role);
  EXPECT_EQ(label.text(), state.name);
  EXPECT_TRUE(state.HasStateFlag(ui::AX_STATE_READ_ONLY));
}

TEST_F(LabelTest, EmptyLabelSizing) {
  Label label;
  const gfx::Size expected_size(0, gfx::FontList().GetHeight());
  EXPECT_EQ(expected_size, label.GetPreferredSize());
  label.SetMultiLine(!label.multi_line());
  EXPECT_EQ(expected_size, label.GetPreferredSize());
}

TEST_F(LabelTest, SingleLineSizing) {
  Label label;
  label.SetText(ASCIIToUTF16("A not so random string in one line."));
  const gfx::Size size = label.GetPreferredSize();
  EXPECT_GT(size.height(), kMinTextDimension);
  EXPECT_GT(size.width(), kMinTextDimension);

  // Setting a size smaller than preferred should not change the preferred size.
  label.SetSize(gfx::Size(size.width() / 2, size.height() / 2));
  EXPECT_EQ(size, label.GetPreferredSize());

  const gfx::Insets border(10, 20, 30, 40);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));
  const gfx::Size size_with_border = label.GetPreferredSize();
  EXPECT_EQ(size_with_border.height(), size.height() + border.height());
  EXPECT_EQ(size_with_border.width(), size.width() + border.width());
}

TEST_F(LabelTest, MultilineSmallAvailableWidthSizing) {
  Label label;
  label.SetMultiLine(true);
  label.SetAllowCharacterBreak(true);
  label.SetText(ASCIIToUTF16("Too Wide."));

  // Check that Label can be laid out at a variety of small sizes,
  // splitting the words into up to one character per line if necessary.
  // Incorrect word splitting may cause infinite loops in text layout.
  gfx::Size required_size = label.GetPreferredSize();
  for (int i = 1; i < required_size.width(); ++i)
    EXPECT_GT(label.GetHeightForWidth(i), 0);
}

TEST_F(LabelTest, MultiLineSizing) {
  Label label;
  label.SetFocusable(false);
  label.SetText(
      ASCIIToUTF16("A random string\nwith multiple lines\nand returns!"));
  label.SetMultiLine(true);

  // GetPreferredSize
  gfx::Size required_size = label.GetPreferredSize();
  EXPECT_GT(required_size.height(), kMinTextDimension);
  EXPECT_GT(required_size.width(), kMinTextDimension);

  // SizeToFit with unlimited width.
  label.SizeToFit(0);
  int required_width = label.GetLocalBounds().width();
  EXPECT_GT(required_width, kMinTextDimension);

  // SizeToFit with limited width.
  label.SizeToFit(required_width - 1);
  int constrained_width = label.GetLocalBounds().width();
#if defined(OS_WIN)
  // Canvas::SizeStringInt (in ui/gfx/canvas_linux.cc)
  // has to be fixed to return the size that fits to given width/height.
  EXPECT_LT(constrained_width, required_width);
#endif
  EXPECT_GT(constrained_width, kMinTextDimension);

  // Change the width back to the desire width.
  label.SizeToFit(required_width);
  EXPECT_EQ(required_width, label.GetLocalBounds().width());

  // General tests for GetHeightForWidth.
  int required_height = label.GetHeightForWidth(required_width);
  EXPECT_GT(required_height, kMinTextDimension);
  int height_for_constrained_width = label.GetHeightForWidth(constrained_width);
#if defined(OS_WIN)
  // Canvas::SizeStringInt (in ui/gfx/canvas_linux.cc)
  // has to be fixed to return the size that fits to given width/height.
  EXPECT_GT(height_for_constrained_width, required_height);
#endif
  // Using the constrained width or the required_width - 1 should give the
  // same result for the height because the constrainted width is the tight
  // width when given "required_width - 1" as the max width.
  EXPECT_EQ(height_for_constrained_width,
            label.GetHeightForWidth(required_width - 1));

  // Test everything with borders.
  gfx::Insets border(10, 20, 30, 40);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));

  // SizeToFit and borders.
  label.SizeToFit(0);
  int required_width_with_border = label.GetLocalBounds().width();
  EXPECT_EQ(required_width_with_border, required_width + border.width());

  // GetHeightForWidth and borders.
  int required_height_with_border =
      label.GetHeightForWidth(required_width_with_border);
  EXPECT_EQ(required_height_with_border, required_height + border.height());

  // Test that the border width is subtracted before doing the height
  // calculation.  If it is, then the height will grow when width
  // is shrunk.
  int height1 = label.GetHeightForWidth(required_width_with_border - 1);
#if defined(OS_WIN)
  // Canvas::SizeStringInt (in ui/gfx/canvas_linux.cc)
  // has to be fixed to return the size that fits to given width/height.
  EXPECT_GT(height1, required_height_with_border);
#endif
  EXPECT_EQ(height1, height_for_constrained_width + border.height());

  // GetPreferredSize and borders.
  label.SetBounds(0, 0, 0, 0);
  gfx::Size required_size_with_border = label.GetPreferredSize();
  EXPECT_EQ(required_size_with_border.height(),
            required_size.height() + border.height());
  EXPECT_EQ(required_size_with_border.width(),
            required_size.width() + border.width());
}

TEST_F(LabelTest, DrawSingleLineString) {
  Label label;
  label.SetFocusable(false);

  label.SetText(ASCIIToUTF16("Here's a string with no returns."));
  gfx::Size required_size(label.GetPreferredSize());
  gfx::Size extra(22, 8);
  label.SetBounds(0, 0, required_size.width() + extra.width(),
                  required_size.height() + extra.height());

  // Do some basic verifications for all three alignments.
  // Centered text.
  const Label::DrawStringParams* params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be centered horizontally and vertically.
  EXPECT_EQ(extra.width() / 2, params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_CENTER,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Left aligned text.
  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be left aligned horizontally and centered vertically.
  EXPECT_EQ(0, params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_LEFT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Right aligned text.
  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be right aligned horizontally and centered vertically.
  EXPECT_EQ(extra.width(), params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_RIGHT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Test single line drawing with a border.
  gfx::Insets border(39, 34, 8, 96);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));

  gfx::Size required_size_with_border(label.GetPreferredSize());
  EXPECT_EQ(required_size.width() + border.width(),
            required_size_with_border.width());
  EXPECT_EQ(required_size.height() + border.height(),
            required_size_with_border.height());
  label.SetBounds(0, 0, required_size_with_border.width() + extra.width(),
                  required_size_with_border.height() + extra.height());

  // Centered text with border.
  label.SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be centered horizontally and vertically within the border.
  EXPECT_EQ(border.left() + extra.width() / 2, params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_CENTER,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Left aligned text with border.
  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be left aligned horizontally and centered vertically.
  EXPECT_EQ(border.left(), params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_LEFT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Right aligned text with border.
  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be right aligned horizontally and centered vertically.
  EXPECT_EQ(border.left() + extra.width(), params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_RIGHT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));
}

// Pango needs a max height to elide multiline text; that is not supported here.
TEST_F(LabelTest, DrawMultiLineString) {
  Label label;
  label.SetFocusable(false);
  // Set a background color to prevent gfx::Canvas::NO_SUBPIXEL_RENDERING flags.
  label.SetBackgroundColor(SK_ColorWHITE);

  label.SetText(ASCIIToUTF16("Another string\nwith returns\n\n!"));
  label.SetMultiLine(true);
  label.SizeToFit(0);
  gfx::Size extra(50, 10);
  label.SetBounds(label.x(), label.y(),
                  label.width() + extra.width(),
                  label.height() + extra.height());

  // Do some basic verifications for all three alignments.
  const Label::DrawStringParams* params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(extra.width() / 2, params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  int expected_flags = gfx::Canvas::MULTI_LINE |
                       gfx::Canvas::TEXT_ALIGN_CENTER;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);
  gfx::Rect center_bounds(params->bounds);

  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(0, params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  expected_flags = gfx::Canvas::MULTI_LINE |
                   gfx::Canvas::TEXT_ALIGN_LEFT;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);

  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(extra.width(), params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  expected_flags = gfx::Canvas::MULTI_LINE |
                   gfx::Canvas::TEXT_ALIGN_RIGHT;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);

  // Test multiline drawing with a border.
  gfx::Insets border(19, 92, 23, 2);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));
  label.SizeToFit(0);
  label.SetBounds(label.x(), label.y(),
                  label.width() + extra.width(),
                  label.height() + extra.height());

  label.SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(border.left() + extra.width() / 2, params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  expected_flags = gfx::Canvas::MULTI_LINE |
                   gfx::Canvas::TEXT_ALIGN_CENTER;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);

  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(border.left(), params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  expected_flags = gfx::Canvas::MULTI_LINE |
                   gfx::Canvas::TEXT_ALIGN_LEFT;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);

  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(extra.width() + border.left(), params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  expected_flags = gfx::Canvas::MULTI_LINE |
                   gfx::Canvas::TEXT_ALIGN_RIGHT;
#if !defined(OS_WIN)
  expected_flags |= gfx::Canvas::NO_ELLIPSIS;
#endif
  EXPECT_EQ(expected_flags, expected_flags);
}

TEST_F(LabelTest, DrawSingleLineStringInRTL) {
  Label label;
  label.SetFocusable(false);

  std::string locale = l10n_util::GetApplicationLocale("");
  base::i18n::SetICUDefaultLocale("he");

  label.SetText(ASCIIToUTF16("Here's a string with no returns."));
  gfx::Size required_size(label.GetPreferredSize());
  gfx::Size extra(22, 8);
  label.SetBounds(0, 0, required_size.width() + extra.width(),
                  required_size.height() + extra.height());

  // Do some basic verifications for all three alignments.
  // Centered text.
  const Label::DrawStringParams* params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be centered horizontally and vertically.
  EXPECT_EQ(extra.width() / 2, params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_CENTER,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // ALIGN_LEFT label.
  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be right aligned horizontally and centered vertically.
  EXPECT_EQ(extra.width(), params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_RIGHT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // ALIGN_RIGHT label.
  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be left aligned horizontally and centered vertically.
  EXPECT_EQ(0, params->bounds.x());
  EXPECT_EQ(0, params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_LEFT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));


  // Test single line drawing with a border.
  gfx::Insets border(39, 34, 8, 96);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));

  gfx::Size required_size_with_border(label.GetPreferredSize());
  EXPECT_EQ(required_size.width() + border.width(),
            required_size_with_border.width());
  EXPECT_EQ(required_size.height() + border.height(),
            required_size_with_border.height());
  label.SetBounds(0, 0, required_size_with_border.width() + extra.width(),
                  required_size_with_border.height() + extra.height());

  // Centered text with border.
  label.SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be centered horizontally and vertically within the border.
  EXPECT_EQ(border.left() + extra.width() / 2, params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_CENTER,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // ALIGN_LEFT text with border.
  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be right aligned horizontally and centered vertically.
  EXPECT_EQ(border.left() + extra.width(), params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_RIGHT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // ALIGN_RIGHT text.
  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  // The text should be left aligned horizontally and centered vertically.
  EXPECT_EQ(border.left(), params->bounds.x());
  EXPECT_EQ(border.top(), params->bounds.y());
  EXPECT_EQ(required_size.width(), params->bounds.width());
  EXPECT_EQ(label.GetContentsBounds().height(), params->bounds.height());
  EXPECT_EQ(gfx::Canvas::TEXT_ALIGN_LEFT,
            params->flags & (gfx::Canvas::TEXT_ALIGN_LEFT |
                             gfx::Canvas::TEXT_ALIGN_CENTER |
                             gfx::Canvas::TEXT_ALIGN_RIGHT));

  // Reset locale.
  base::i18n::SetICUDefaultLocale(locale);
}

// On Linux the underlying pango routines require a max height in order to
// ellide multiline text. So until that can be resolved, we set all
// multiline lables to not ellide in Linux only.
TEST_F(LabelTest, DrawMultiLineStringInRTL) {
  Label label;
  label.SetFocusable(false);

  // Test for RTL.
  std::string locale = l10n_util::GetApplicationLocale("");
  base::i18n::SetICUDefaultLocale("he");

  label.SetText(ASCIIToUTF16("Another string\nwith returns\n\n!"));
  label.SetMultiLine(true);
  label.SizeToFit(0);
  gfx::Size extra(50, 10);
  label.SetBounds(label.x(), label.y(),
                  label.width() + extra.width(),
                  label.height() + extra.height());

  // Do some basic verifications for all three alignments.
  const Label::DrawStringParams* params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(extra.width() / 2, params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_CENTER & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif
  gfx::Rect center_bounds(params->bounds);

  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(extra.width(), params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_RIGHT & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif

  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(0, params->bounds.x());
  EXPECT_EQ(extra.height() / 2, params->bounds.y());
  EXPECT_GT(params->bounds.width(), kMinTextDimension);
  EXPECT_GT(params->bounds.height(), kMinTextDimension);
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_LEFT & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif

  // Test multiline drawing with a border.
  gfx::Insets border(19, 92, 23, 2);
  label.SetBorder(Border::CreateEmptyBorder(
      border.top(), border.left(), border.bottom(), border.right()));
  label.SizeToFit(0);
  label.SetBounds(label.x(), label.y(),
                  label.width() + extra.width(),
                  label.height() + extra.height());

  label.SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(border.left() + extra.width() / 2, params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_CENTER & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif

  label.SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(border.left() + extra.width(), params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_RIGHT & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif

  label.SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label.ResetLayoutCache();
  params = label.CalculateDrawStringParams();
  EXPECT_EQ(label.text(), params->text);
  EXPECT_EQ(border.left(), params->bounds.x());
  EXPECT_EQ(border.top() + extra.height() / 2, params->bounds.y());
  EXPECT_EQ(center_bounds.width(), params->bounds.width());
  EXPECT_EQ(center_bounds.height(), params->bounds.height());
  EXPECT_TRUE(gfx::Canvas::MULTI_LINE & params->flags);
  EXPECT_TRUE(gfx::Canvas::TEXT_ALIGN_LEFT & params->flags);
#if !defined(OS_WIN)
  EXPECT_TRUE(gfx::Canvas::NO_ELLIPSIS & params->flags);
#endif

  // Reset Locale
  base::i18n::SetICUDefaultLocale(locale);
}

// Ensure the subpixel rendering flag and background color alpha are respected.
TEST_F(LabelTest, DisableSubpixelRendering) {
  Label label;
  label.SetBackgroundColor(SK_ColorWHITE);
  const int flag = gfx::Canvas::NO_SUBPIXEL_RENDERING;
  EXPECT_EQ(0, label.ComputeDrawStringFlags() & flag);
  label.SetSubpixelRenderingEnabled(false);
  EXPECT_EQ(flag, label.ComputeDrawStringFlags() & flag);
  label.SetSubpixelRenderingEnabled(true);
  EXPECT_EQ(0, label.ComputeDrawStringFlags() & flag);
  // Text cannot be drawn with subpixel rendering on transparent backgrounds.
  label.SetBackgroundColor(SkColorSetARGB(64, 255, 255, 255));
  EXPECT_EQ(flag, label.ComputeDrawStringFlags() & flag);
}

// Check that labels support GetTooltipHandlerForPoint.
TEST_F(LabelTest, GetTooltipHandlerForPoint) {
  // A root view must be defined for this test because the hit-testing
  // behaviour used by GetTooltipHandlerForPoint() is defined by
  // the ViewTargeter installed on the root view.
  Widget widget;
  Widget::InitParams init_params =
      CreateParams(Widget::InitParams::TYPE_POPUP);
  init_params.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  init_params.bounds = gfx::Rect(0, 0, 200, 200);
  widget.Init(init_params);

  Label label;
  label.SetText(
      ASCIIToUTF16("A string that's long enough to exceed the bounds"));
  label.SetBounds(0, 0, 10, 10);
  widget.SetContentsView(&label);

  // By default, labels start out as tooltip handlers.
  ASSERT_TRUE(label.handles_tooltips());

  // There's a default tooltip if the text is too big to fit.
  EXPECT_EQ(&label, label.GetTooltipHandlerForPoint(gfx::Point(2, 2)));

  // If tooltip handling is disabled, the label should not provide a tooltip
  // handler.
  label.SetHandlesTooltips(false);
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(2, 2)));
  label.SetHandlesTooltips(true);

  // If there's no default tooltip, this should return NULL.
  label.SetBounds(0, 0, 500, 50);
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(2, 2)));

  label.SetTooltipText(ASCIIToUTF16("a tooltip"));
  // If the point hits the label, and tooltip is set, the label should be
  // returned as its tooltip handler.
  EXPECT_EQ(&label, label.GetTooltipHandlerForPoint(gfx::Point(2, 2)));

  // Additionally, GetTooltipHandlerForPoint should verify that the label
  // actually contains the point.
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(2, 51)));
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(-1, 20)));

  // Again, if tooltip handling is disabled, the label should not provide a
  // tooltip handler.
  label.SetHandlesTooltips(false);
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(2, 2)));
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(2, 51)));
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(-1, 20)));
  label.SetHandlesTooltips(true);

  // GetTooltipHandlerForPoint works should work in child bounds.
  label.SetBounds(2, 2, 10, 10);
  EXPECT_EQ(&label, label.GetTooltipHandlerForPoint(gfx::Point(1, 5)));
  EXPECT_FALSE(label.GetTooltipHandlerForPoint(gfx::Point(3, 11)));
}

}  // namespace views
