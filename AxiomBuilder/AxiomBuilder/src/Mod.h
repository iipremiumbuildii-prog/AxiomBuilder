#pragma once

#include <vector>

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/NativeMod.h"

namespace axiom {

class Mod {
public:
    static Mod& getInstance();

    Mod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod&                mSelf;
    std::vector<ll::event::ListenerPtr> mListeners;
    bool                               mCommandsRegistered = false;
};

} // namespace axiom
