#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/level/BlockPos.h"

class Block;
class Player;

// All of this runs on the server thread (commands, events, form callbacks and the
// tick scheduler all do), so no mutexes are needed.
namespace axiom::we {

constexpr uint64_t kMaxVolume     = 2'000'000; // refuse bigger operations
constexpr size_t   kBlocksPerTick = 4096;      // work budget per server tick
constexpr size_t   kMaxHistory    = 20;        // transactions kept per player

struct BlockChange {
    BlockPos     pos;
    Block const* oldBlock;
    Block const* newBlock;
};

// One undoable operation (a whole fill/replace/brush stroke).
struct Transaction {
    int                      dimId = 0;
    std::vector<BlockChange> changes;
};

struct Selection {
    BlockPos a{0, 0, 0};
    BlockPos b{0, 0, 0};
    bool     hasA  = false;
    bool     hasB  = false;
    int      dimId = 0;

    [[nodiscard]] bool     valid() const { return hasA && hasB; }
    [[nodiscard]] BlockPos min() const;
    [[nodiscard]] BlockPos max() const;
    [[nodiscard]] uint64_t volume() const;
};

struct Hit {
    BlockPos pos{0, 0, 0};
    BlockPos face{0, 0, 0}; // unit offset pointing out of the hit face
    bool     hit = false;
};

// ---- brushes -------------------------------------------------------------
class Brush {
public:
    virtual ~Brush()                                                              = default;
    virtual void             collect(BlockPos const& center, std::vector<BlockPos>& out) const = 0;
    [[nodiscard]] virtual std::string_view name() const                          = 0;
};

// type: "sphere" | "cube" | "cylinder". Returns nullptr for an unknown type.
std::unique_ptr<Brush> makeBrush(std::string_view type, int radius, int height);

// ---- selection helpers -----------------------------------------------------
void collectSelection(Selection const& sel, std::vector<BlockPos>& out);

// DDA voxel traversal from the player's eyes; replaces the 0.1-step marcher.
Hit raycast(Player& player, float maxDistance = 100.0f);

// ---- jobs + history ------------------------------------------------------
// onDone(changedBlocks) is called on the server thread when the job finishes.
using DoneFn = std::function<void(size_t changed)>;

// Fill (onlyReplace == nullptr) or replace (onlyReplace != nullptr) the positions.
bool submitEdit(
    std::string const&    owner,
    int                   dimId,
    std::vector<BlockPos> positions,
    Block const*          newBlock,
    Block const*          onlyReplace,
    DoneFn                onDone
);
bool submitUndo(std::string const& owner, DoneFn onDone);
bool submitRedo(std::string const& owner, DoneFn onDone);

[[nodiscard]] bool   isBusy(std::string const& owner); // owner has a running job
[[nodiscard]] size_t undoDepth(std::string const& owner);
[[nodiscard]] size_t redoDepth(std::string const& owner);
void                 clearOwner(std::string const& owner); // drop history + queued jobs

void tick(); // call once per server tick

} // namespace axiom::we
