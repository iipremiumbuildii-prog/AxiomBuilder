#include "Builder.h"

#include <cstdlib>

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

namespace axiom {

Builder& Builder::get() {
    static Builder instance;
    return instance;
}

static bool inShape(Shape shape, int x, int y, int z, int r, bool hollow) {
    if (shape == Shape::Sphere) {
        int d2 = x * x + y * y + z * z;
        if (d2 > r * r) return false;
        if (hollow && d2 < (r - 1) * (r - 1)) return false;
        return true;
    }
    if (hollow) {
        return std::abs(x) == r || std::abs(y) == r || std::abs(z) == r;
    }
    return true;
}

int Builder::apply(Player& player, Shape shape, int radius, std::string const& blockName, bool hollow) {
    auto blockRef = Block::tryGetFromRegistry(blockName);
    if (!blockRef) return -1;
    Block const& newBlock = *blockRef;

    BlockSource& region = player.getDimensionBlockSource();
    BlockPos     center = player.getFeetBlockPos();

    UndoEntry entry;
    int       changed = 0;

    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            for (int z = -radius; z <= radius; ++z) {
                if (!inShape(shape, x, y, z, radius, hollow)) continue;
                BlockPos pos{center.x + x, center.y + y, center.z + z};
                entry.push_back({pos, &region.getBlock(pos)});
                region.setBlock(pos, newBlock, 3, nullptr, nullptr);
                ++changed;
            }
        }
    }

    auto& stack = mUndo[player.getUuid().asString()];
    if (stack.size() >= MaxUndo) stack.erase(stack.begin());
    stack.push_back(std::move(entry));
    return changed;
}

int Builder::undo(Player& player) {
    auto it = mUndo.find(player.getUuid().asString());
    if (it == mUndo.end() || it->second.empty()) return -1;

    UndoEntry entry = std::move(it->second.back());
    it->second.pop_back();

    BlockSource& region = player.getDimensionBlockSource();
    for (auto rit = entry.rbegin(); rit != entry.rend(); ++rit) {
        region.setBlock(rit->pos, *rit->previous, 3, nullptr, nullptr);
    }
    return static_cast<int>(entry.size());
}

} // namespace axiom
