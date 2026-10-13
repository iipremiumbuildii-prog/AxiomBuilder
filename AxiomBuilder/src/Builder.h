#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/world/level/BlockPos.h"

class Player;
class Block;

namespace axiom {

enum class Shape { Sphere, Cuboid };

struct UndoRecord {
    BlockPos pos;
    Block const* previous;
};

// One undo entry = every block changed by a single operation.
using UndoEntry = std::vector<UndoRecord>;

class Builder {
public:
    static Builder& get();

    // Returns number of blocks changed, or -1 if the block name is invalid.
    int apply(Player& player, Shape shape, int radius, std::string const& blockName, bool hollow);

    // Returns number of blocks restored, or -1 if nothing to undo.
    int undo(Player& player);

private:
    static constexpr std::size_t MaxUndo = 100;
    std::unordered_map<std::string, std::vector<UndoEntry>> mUndo; // keyed by player UUID
};

} // namespace axiom
