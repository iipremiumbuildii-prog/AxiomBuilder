#pragma once

#include "ll/api/mod/NativeMod.h"

namespace axiom {

class AxiomBuilder {
public:
    static AxiomBuilder& getInstance();

    AxiomBuilder() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace axiom
