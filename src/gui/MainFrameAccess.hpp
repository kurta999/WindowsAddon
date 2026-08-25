#pragma once

#include "AppNotification.hpp"

class MyFrame;

// !\brief The main frame, or nullptr when it is not the current top window.
//
// Panels used to write ((MyFrame*)(wxGetApp().GetTopWindow())) inline. That is
// a C-style downcast with no check: while a dialog is up, or once the frame has
// been destroyed on the way out, GetTopWindow() is something else and every use
// of the result is undefined behaviour. The checked cast lives here so there is
// one place to get it right.
//
// Prefer PostAppNotification below when all you want is to notify the user;
// this accessor is for the cases that genuinely need the frame itself.
[[nodiscard]] MyFrame* TryGetMainFrame() noexcept;

// !\brief Tell the user something happened, if there is anyone to tell.
//
// Every caller used to spell this out as two statements, and fifteen of the
// seventeen dereferenced TryGetMainFrame() without checking it - which is the
// exact undefined behaviour the checked accessor exists to prevent. A nullable
// accessor that reads well when you ignore the null is a trap, so the common
// case gets a call that cannot be written unsafely. A notification raised while
// no main frame is up has nowhere to go and is dropped.
void PostAppNotification(AppNotification notification);
