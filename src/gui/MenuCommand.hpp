#pragma once

#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/window.h>

#include <functional>
#include <span>
#include <variant>
#include <vector>

// !\brief Context menus as data.
//
// A context menu used to be written twice: a run of `menu.Append(id, label)
// ->SetBitmap(...)` calls, and then a `switch` over the returned id with each
// handler's body inlined into its `case`. Two of those switches ran past 150
// lines, so what the menu offered and what the menu did were far enough apart
// to disagree, and adding one item meant three edits - the build, the switch
// and the id enum.
//
// Describing an item once, next to what it does, collapses both halves into a
// single loop.
namespace gui
{
// !\brief One context-menu item.
struct MenuCommand
{
    // !\brief Text shown to the user. An '&' marks the accelerator.
    const char* label = "";

    // !\brief What choosing it does.
    std::function<void()> action;

    // !\brief Optional wxArtProvider id for the item's bitmap.
    wxArtID icon;

    // !\brief Whether the item is offered at all. Empty means always.
    std::function<bool()> is_available;

    // !\brief Whether the item can be chosen. Empty means always.
    //
    // The difference from `is_available`: an item that says no here is still
    // in the menu, greyed out. The backup tree offers "Delete" that way, so
    // the menu keeps its shape whichever item is right-clicked.
    std::function<bool()> is_enabled;
};

// !\brief A group of mutually exclusive items in a submenu.
struct MenuRadioGroup
{
    const char* label = "";

    // !\brief The choices, in order. `id` is passed back to `on_selected`.
    std::vector<std::pair<int, const char*>> items;

    // !\brief What choosing one of them does, given its id.
    std::function<void(int)> on_selected;

    // !\brief Whether the submenu is offered at all. Empty means always.
    std::function<bool()> is_available;
};

// !\brief One entry of a menu: either a plain command or a radio submenu.
//
// Both live in one ordered list so a menu is laid out exactly as it is written,
// rather than having its submenus collected to the end.
using MenuEntry = std::variant<MenuCommand, MenuRadioGroup>;

// !\brief Build the menu from `entries`, show it at the pointer, and run
// whatever the user picked. Entries whose `is_available` says no are skipped.
void RunContextMenu(wxWindow* parent, std::span<const MenuEntry> entries);
}
