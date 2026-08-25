#include "pch.hpp"

#include "MenuCommand.hpp"

namespace
{
/* Ids are assigned as the menu is built rather than taken from a shared enum:
   the whole point is that an item is described in one place, and a hand-picked
   id is a second place to keep in step. wxWidgets only needs them to be unique
   within the menu being shown. */
constexpr int kFirstGeneratedId = wxID_HIGHEST + 1;
}

namespace gui
{
void RunContextMenu(wxWindow* parent, std::span<const MenuEntry> entries)
{
    if(parent == nullptr)
        return;

    wxMenu menu;
    int next_id = kFirstGeneratedId;

    std::vector<std::pair<int, std::function<void()>>> actions;

    for(const MenuEntry& entry : entries)
    {
        if(const auto* command = std::get_if<MenuCommand>(&entry))
        {
            if(command->is_available && !command->is_available())
                continue;

            const int id = next_id++;
            wxMenuItem* item = menu.Append(id, command->label);
            if(!command->icon.empty())
            {
                item->SetBitmap(wxArtProvider::GetBitmap(command->icon, wxART_OTHER,
                    parent->FromDIP(wxSize(14, 14))));
            }
            if(command->is_enabled && !command->is_enabled())
                item->Enable(false);

            actions.emplace_back(id, command->action);
            continue;
        }

        const auto& group = std::get<MenuRadioGroup>(entry);
        if(group.is_available && !group.is_available())
            continue;

        auto* submenu = new wxMenu;
        for(const auto& [choice_id, choice_label] : group.items)
        {
            const int id = next_id++;
            submenu->AppendRadioItem(id, choice_label);
            actions.emplace_back(id, [on_selected = group.on_selected, choice_id]
            {
                if(on_selected)
                    on_selected(choice_id);
            });
        }
        menu.AppendSubMenu(submenu, group.label);
    }

    if(menu.GetMenuItemCount() == 0)
        return;

    const int chosen = parent->GetPopupMenuSelectionFromUser(menu);
    if(chosen == wxID_NONE)
        return;

    for(const auto& [id, action] : actions)
    {
        if(id == chosen)
        {
            if(action)
                action();
            return;
        }
    }
}
}
