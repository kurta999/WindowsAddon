#pragma once

namespace platform
{
// !\brief Press and release the NumLock key, as hardware would.
//
// The SendInput pair lived inline in MainFrame's 10 ms tick - the only raw
// keyboard injection in the GUI layer, next to siblings (AntiLock, the macro
// actions) that already keep theirs out of it. On non-Windows builds this is
// a no-op, matching the #ifdef that surrounded it.
void TapNumlock();
}
