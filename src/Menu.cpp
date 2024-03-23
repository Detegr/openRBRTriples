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

// clang-format off
static class Menu main_menu = { "openRBRTriples", {
  { .text = id("Licenses"), .long_text = {"License information of open source libraries used in the plugin's implementation."}, .select_action = [] { select_menu(1); } },
  { .text = id("Save the current config to openRBRVR.toml"),
    .color = [] { return (g::cfg == g::saved_cfg) ? std::make_tuple(0.5f, 0.5f, 0.5f, 1.0f) : std::make_tuple(1.0f, 1.0f, 1.0f, 1.0f); },
    .select_action = [] {
        if (g::cfg.write("Plugins\\openRBRVR.toml")) {
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
