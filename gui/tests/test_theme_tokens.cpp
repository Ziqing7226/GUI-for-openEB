// gui/tests/test_theme_tokens.cpp — theme token WCAG contrast validation
// (design §3.11.2 / §7.1).
//
// Verifies that the semantic color tokens in resources/theme/tokens.h meet
// readability targets against bg-primary for all 10 theme variants (5 colors x
// light/dark). Per §7.1, the WCAG AA 4.5:1 requirement for fg-muted applies to
// DARK modes (the regression was #888 hint text being unreadable on dark
// backgrounds). fg-primary and fg-secondary must reach 4.5:1 in every variant.
// Light-mode fg-muted uses a 3.0:1 floor (it is less-critical hint text and the
// pastel backgrounds keep it comfortably readable).
//
// This is a header-only test: it includes tokens.h and computes contrast ratios
// using the WCAG relative-luminance formula. No Qt widgets are required.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QString>

#include "resources/theme/tokens.h"

using gui::theme_tokens::kColorCount;
using gui::theme_tokens::kDark;
using gui::theme_tokens::kLight;
using gui::theme_tokens::kModeCount;
using gui::theme_tokens::kValues;
using gui::theme_tokens::Token;

namespace {

struct RGB { double r, g, b; };

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

RGB parse_hex(const char* hex) {
    const int r = hex_digit(hex[0]) * 16 + hex_digit(hex[1]);
    const int g = hex_digit(hex[2]) * 16 + hex_digit(hex[3]);
    const int b = hex_digit(hex[4]) * 16 + hex_digit(hex[5]);
    return {r / 255.0, g / 255.0, b / 255.0};
}

// WCAG sRGB channel linearization.
double linearize(double c) {
    return (c <= 0.03928) ? (c / 12.92)
                          : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const char* hex) {
    const RGB c = parse_hex(hex);
    return 0.2126 * linearize(c.r) +
           0.7152 * linearize(c.g) +
           0.0722 * linearize(c.b);
}

// WCAG contrast ratio: (L_lighter + 0.05) / (L_darker + 0.05).
double contrast(const char* a, const char* b) {
    double la = luminance(a);
    double lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

const char* color_name(int c) {
    switch (c) {
        case 0: return "Gray";
        case 1: return "Green";
        case 2: return "Yellow";
        case 3: return "Pink";
        case 4: return "Blue";
        default: return "?";
    }
}

const char* token_hex(int color, int mode, Token token) {
    return kValues[color][mode][static_cast<int>(token)];
}

double contrast_vs_bg(int color, int mode, Token token) {
    return contrast(token_hex(color, mode, token),
                    token_hex(color, mode, Token::BgPrimary));
}

} // namespace

// fg-primary must meet WCAG AA (4.5:1) in every variant.
TEST(ThemeTokens, FgPrimaryContrastAllVariants) {
    for (int c = 0; c < kColorCount; ++c) {
        for (int m = 0; m < kModeCount; ++m) {
            const double r = contrast_vs_bg(c, m, Token::FgPrimary);
            EXPECT_GE(r, 4.5)
                << "fg-primary " << color_name(c) << " mode=" << m
                << " ratio=" << r;
        }
    }
}

// fg-secondary must meet WCAG AA (4.5:1) in every variant.
TEST(ThemeTokens, FgSecondaryContrastAllVariants) {
    for (int c = 0; c < kColorCount; ++c) {
        for (int m = 0; m < kModeCount; ++m) {
            const double r = contrast_vs_bg(c, m, Token::FgSecondary);
            EXPECT_GE(r, 4.5)
                << "fg-secondary " << color_name(c) << " mode=" << m
                << " ratio=" << r;
        }
    }
}

// fg-muted in DARK modes must meet WCAG AA (design §7.1 target ≥ 4.5:1).
// The dark fg-muted tokens are tuned so every variant reaches 4.5:1 against
// its bg-primary (Dark Green ≈ 4.76:1, Dark Yellow ≈ 4.73:1), fixing the
// unreadable #888 hint text (2.3:1) regression that prompted §7.1.
TEST(ThemeTokens, FgMutedContrastDarkMeetsWCAGAA) {
    for (int c = 0; c < kColorCount; ++c) {
        const double r = contrast_vs_bg(c, kDark, Token::FgMuted);
        EXPECT_GE(r, 4.5)
            << "fg-muted(dark) " << color_name(c) << " ratio=" << r;
    }
}

// fg-muted in LIGHT modes uses a softer 3.0:1 floor (hint text on pastel
// backgrounds; the 4.5:1 target is scoped to dark mode per §7.1).
TEST(ThemeTokens, FgMutedContrastLightFloor) {
    for (int c = 0; c < kColorCount; ++c) {
        const double r = contrast_vs_bg(c, kLight, Token::FgMuted);
        EXPECT_GE(r, 3.0)
            << "fg-muted(light) " << color_name(c) << " ratio=" << r;
    }
}

// Sanity: every variant defines a non-trivial fg/bg pair.
TEST(ThemeTokens, FgPrimaryDiffersFromBgPrimary) {
    for (int c = 0; c < kColorCount; ++c) {
        for (int m = 0; m < kModeCount; ++m) {
            EXPECT_STRNE(token_hex(c, m, Token::FgPrimary),
                         token_hex(c, m, Token::BgPrimary));
        }
    }
}

// lookup() builds the "#RRGGBB" string from the same table.
TEST(ThemeTokens, LookupProducesHashPrefixedHex) {
    const QString s = gui::theme_tokens::lookup(
        kColorCount, false, Token::BgPrimary);  // out-of-range -> Gray light
    EXPECT_TRUE(s.startsWith(QStringLiteral("#")));
    EXPECT_EQ(s, QStringLiteral("#E8E8E8"));
}
