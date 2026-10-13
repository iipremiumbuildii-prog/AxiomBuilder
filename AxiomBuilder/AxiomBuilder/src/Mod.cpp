#include "Mod.h"

#include "AxiomUi.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/mod/RegisterHelper.h"

namespace axiom {

Mod& Mod::getInstance() {
    static Mod instance;
    return instance;
}

bool Mod::load() {
    getSelf().getLogger().info("AxiomBuilder loaded");
    return true;
}

bool Mod::enable() {
    ui::startTicker();

    if (!mCommandsRegistered) { // commands can't be unregistered, so only do it once
        ui::registerCommands();
        mCommandsRegistered = true;
    }
    ui::registerEvents(mListeners);

    getSelf().getLogger().info("AxiomBuilder enabled");
    return true;
}

bool Mod::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : mListeners) {
        bus.removeListener(listener);
    }
    mListeners.clear();

    ui::stopTicker();
    getSelf().getLogger().info("AxiomBuilder disabled");
    return true;
}

} // namespace axiom

LL_REGISTER_MOD(axiom::Mod, axiom::Mod::getInstance());
