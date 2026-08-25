#pragma once

#include "AppNotification.hpp"

// !\brief Where a service publishes a typed user-facing notification.
//
// Services used to reach the main frame directly, through a C-style downcast of
// wxGetApp().GetTopWindow(), which is undefined behaviour whenever the top
// window is not the main frame - during teardown, or while a dialog is up. The
// payload was already a closed variant; only the route to the window was
// missing, and this is it.
class INotificationSink
{
public:
    virtual ~INotificationSink() = default;

    // !\brief Publish a notification. Safe to call from any thread; the
    // implementation marshals to the UI thread.
    virtual void PostNotification(AppNotification notification) = 0;
};
