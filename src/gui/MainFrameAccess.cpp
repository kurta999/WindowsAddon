#include "pch.hpp"

#include "MainFrameAccess.hpp"

#include <utility>

MyFrame* TryGetMainFrame() noexcept
{
    wxApp* app = wxTheApp;
    if(!app)
        return nullptr;
    return dynamic_cast<MyFrame*>(app->GetTopWindow());
}

void PostAppNotification(AppNotification notification)
{
    if(MyFrame* frame = TryGetMainFrame())
        frame->PostNotification(std::move(notification));
}
