#include "Menu.hpp"
#include "Config.hpp"
#include "Globals.hpp"

#include <array>
#include <format>

void select_menu(size_t menuIdx);

// Helper function to create an identity function to return its argument
template <typename T>
auto id(const T& txt) -> auto
{
    return [&] { return txt; };
}

LicenseMenu::LicenseMenu()
    : Menu("openRBRTriples licenses", {})
    , menuScroll(0)
{
    for (const auto& row : g::license_information) {
        this->menu_entries.push_back({ .text = id(row), .font = IRBRGame::EFonts::FONT_SMALL, .menu_color = IRBRGame::EMenuColors::MENU_TEXT });
    }
}
void LicenseMenu::left()
{
    select_menu(0);
}
void LicenseMenu::select()
{
    select_menu(0);
}

void Toggle(bool& value) { value = !value; }
void Toggle(int& value)
{
    if (value == 0) {
        value = 1;
    } else {
        value = 0;
    }
}

static void toggle_side_monitor_setting(bool forward)
{
    if (g::cfg.side_monitors_half_hz) {
        if (g::cfg.side_monitors_half_hz_btb_only) {
            if (forward) {
                g::cfg.side_monitors_half_hz_btb_only = false;
            } else {
                g::cfg.side_monitors_half_hz = false;
                g::cfg.side_monitors_half_hz_btb_only = false;
            }
        } else {
            if (forward) {
                g::cfg.side_monitors_half_hz = false;
            } else {
                g::cfg.side_monitors_half_hz_btb_only = true;
            }
        }
    } else {
        if (forward) {
            g::cfg.side_monitors_half_hz_btb_only = true;
            g::cfg.side_monitors_half_hz = true;
        } else {
            g::cfg.side_monitors_half_hz_btb_only = false;
            g::cfg.side_monitors_half_hz = true;
        }
    }
}

// clang-format off
static class Menu main_menu = { "openRBRTriples", {
  { .text = [] { return std::format("Run side monitors with half FPS: {}", g::cfg.side_monitors_half_hz ? (g::cfg.side_monitors_half_hz_btb_only ? "BTB only" : "ON") : "OFF"); },
    .long_text = {"For better performance it is recommended to enable this setting", "at least for BTB stages."},
    .menu_color = IRBRGame::EMenuColors::MENU_TEXT,
    .position = Menu::menu_items_start_pos,
    .left_action = [] { toggle_side_monitor_setting(false); },
    .right_action = [] { toggle_side_monitor_setting(true); },
    .select_action = [] { toggle_side_monitor_setting(true); },
  },
  { .text = [] { return std::format("Limit anti-aliasing to center screen: {}", g::cfg.aa_center_screen_only ? "ON" : "OFF"); },
    .long_text = {"If anti-aliasing is enabled, apply it to center screen only.", "This will improve performance on cost of graphics on side monitors.", "Requires game restart to take an effect."},
    .left_action = [] { Toggle(g::cfg.aa_center_screen_only); },
    .right_action = [] { Toggle(g::cfg.aa_center_screen_only); },
    .select_action = [] { Toggle(g::cfg.aa_center_screen_only); },
  },
  { .text = id("Licenses"), .long_text = {"License information of open source libraries used in the plugin's implementation."}, .select_action = [] { select_menu(1); } },
  { .text = id("Save the current config to openRBRTriples.toml"),
    .color = [] { return (g::cfg == g::saved_cfg) ? std::make_tuple(0.5f, 0.5f, 0.5f, 1.0f) : std::make_tuple(1.0f, 1.0f, 1.0f, 1.0f); },
    .select_action = [] {
        if (g::cfg.write("Plugins\\openRBRTriples.toml")) {
            g::saved_cfg = g::cfg;
        }
    }
  },
}};

static LicenseMenu license_menu;

// clang-format on

static constexpr auto menus = std::to_array<class Menu*>({
    &main_menu,
    &license_menu,
});

Menu* g::menu = menus[0];

void select_menu(size_t menuIdx)
{
    g::menu = menus[menuIdx % menus.size()];
}
