#pragma once

#include <string>
#include <vector>

#include "AxiomWorldEdit.h"
#include "ll/api/event/ListenerBase.h"

class Player;

namespace axiom::ui {

// Per-player editing state (selection, brush settings, default material).
struct PlayerState {
    we::Selection sel;
    std::string   brushType   = "sphere";
    int           brushRadius = 3;
    int           brushHeight = 5;
    std::string   material    = "minecraft:stone";
};

PlayerState& stateOf(Player& player);
void         dropState(std::string const& uuid);

// Lifecycle (called from Mod.cpp)
void startTicker();
void stopTicker();
void registerCommands(); // /axiom ...
void registerEvents(std::vector<ll::event::ListenerPtr>& out);

// Forms
void showMainMenu(Player& player);

} // namespace axiom::ui
