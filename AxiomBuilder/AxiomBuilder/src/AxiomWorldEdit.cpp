#include "AxiomWorldEdit.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

namespace axiom::we {

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------
BlockPos Selection::min() const {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

BlockPos Selection::max() const {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

uint64_t Selection::volume() const {
    if (!valid()) return 0;
    auto lo = min();
    auto hi = max();
    return uint64_t(hi.x - lo.x + 1) * uint64_t(hi.y - lo.y + 1) * uint64_t(hi.z - lo.z + 1);
}

void collectSelection(Selection const& sel, std::vector<BlockPos>& out) {
    if (!sel.valid()) return;
    auto lo = sel.min();
    auto hi = sel.max();
    out.reserve(out.size() + size_t(sel.volume()));
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
            for (int z = lo.z; z <= hi.z; ++z) out.emplace_back(x, y, z);
}

// ---------------------------------------------------------------------------
// Brushes (each one uses its own stored parameters consistently)
// ---------------------------------------------------------------------------
namespace {

class SphereBrush final : public Brush {
    int mRadius;

public:
    explicit SphereBrush(int r) : mRadius(r) {}
    void collect(BlockPos const& c, std::vector<BlockPos>& out) const override {
        int r2 = mRadius * mRadius;
        for (int y = -mRadius; y <= mRadius; ++y)
            for (int x = -mRadius; x <= mRadius; ++x)
                for (int z = -mRadius; z <= mRadius; ++z)
                    if (x * x + y * y + z * z <= r2) out.emplace_back(c.x + x, c.y + y, c.z + z);
    }
    std::string_view name() const override { return "sphere"; }
};

class CubeBrush final : public Brush {
    int mRadius; // half-extent: side length is 2r + 1
public:
    explicit CubeBrush(int r) : mRadius(r) {}
    void collect(BlockPos const& c, std::vector<BlockPos>& out) const override {
        for (int y = -mRadius; y <= mRadius; ++y)
            for (int x = -mRadius; x <= mRadius; ++x)
                for (int z = -mRadius; z <= mRadius; ++z) out.emplace_back(c.x + x, c.y + y, c.z + z);
    }
    std::string_view name() const override { return "cube"; }
};

class CylinderBrush final : public Brush {
    int mRadius;
    int mHeight; // total height in blocks, centered on the target
public:
    CylinderBrush(int r, int h) : mRadius(r), mHeight(h) {}
    void collect(BlockPos const& c, std::vector<BlockPos>& out) const override {
        int r2     = mRadius * mRadius;
        int yStart = -(mHeight / 2);
        for (int i = 0; i < mHeight; ++i)
            for (int x = -mRadius; x <= mRadius; ++x)
                for (int z = -mRadius; z <= mRadius; ++z)
                    if (x * x + z * z <= r2) out.emplace_back(c.x + x, c.y + yStart + i, c.z + z);
    }
    std::string_view name() const override { return "cylinder"; }
};

} // namespace

std::unique_ptr<Brush> makeBrush(std::string_view type, int radius, int height) {
    radius = std::clamp(radius, 1, 64);
    height = std::clamp(height, 1, 128);
    if (type == "sphere") return std::make_unique<SphereBrush>(radius);
    if (type == "cube") return std::make_unique<CubeBrush>(radius);
    if (type == "cylinder") return std::make_unique<CylinderBrush>(radius, height);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Raycast (DDA voxel traversal, exact face detection)
// ---------------------------------------------------------------------------
Hit raycast(Player& player, float maxDistance) {
    Hit  result;
    auto& region = player.getDimensionBlockSource();

    auto  eye = player.getEyePos();
    auto  dir = player.getViewVector(1.0f);
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-6f) return result;
    float dx = dir.x / len, dy = dir.y / len, dz = dir.z / len;

    constexpr float kInf = std::numeric_limits<float>::infinity();

    int cx = int(std::floor(eye.x)), cy = int(std::floor(eye.y)), cz = int(std::floor(eye.z));
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;

    auto tMaxFor = [](float o, float d, int cell) -> float {
        if (d == 0.0f) return kInf;
        float next = d > 0 ? (float(cell) + 1.0f - o) : (o - float(cell));
        return next / std::abs(d);
    };
    auto tDeltaFor = [](float d) -> float { return d == 0.0f ? kInf : 1.0f / std::abs(d); };

    float tMaxX = tMaxFor(eye.x, dx, cx), tMaxY = tMaxFor(eye.y, dy, cy), tMaxZ = tMaxFor(eye.z, dz, cz);
    float tDX = tDeltaFor(dx), tDY = tDeltaFor(dy), tDZ = tDeltaFor(dz);

    BlockPos face{0, 0, 0};
    for (;;) {
        float t;
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            t = tMaxX; cx += sx; tMaxX += tDX; face = {-sx, 0, 0};
        } else if (tMaxY <= tMaxZ) {
            t = tMaxY; cy += sy; tMaxY += tDY; face = {0, -sy, 0};
        } else {
            t = tMaxZ; cz += sz; tMaxZ += tDZ; face = {0, 0, -sz};
        }
        if (t > maxDistance) break;

        BlockPos pos{cx, cy, cz};
        if (!region.getBlock(pos).isAir()) {
            result.pos  = pos;
            result.face = face;
            result.hit  = true;
            return result;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Jobs + history
// ---------------------------------------------------------------------------
namespace {

enum class Kind { Edit, Undo, Redo };

struct Job {
    Kind                  kind = Kind::Edit;
    std::string           owner;
    int                   dimId = 0;
    std::vector<BlockPos> positions;            // Edit only
    Block const*          newBlock    = nullptr; // Edit only
    Block const*          onlyReplace = nullptr; // Edit only (replace mode)
    Transaction           tx;                    // Edit: recorded; Undo/Redo: taken from history
    size_t                cursor = 0;
    DoneFn                onDone;
};

struct History {
    std::vector<Transaction> undo;
    std::vector<Transaction> redo;
};

std::deque<Job>                         gJobs;
std::unordered_map<std::string, History> gHistory;

BlockSource* regionFor(int dimId) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dim = level->getDimension(DimensionType(dimId)).lock();
    if (!dim) return nullptr;
    return &dim->getBlockSourceFromMainChunkSource();
}

void trim(std::vector<Transaction>& v) {
    if (v.size() > kMaxHistory) v.erase(v.begin(), v.begin() + (v.size() - kMaxHistory));
}

void setBlockAt(BlockSource& region, BlockPos const& pos, Block const& block) {
    region.setBlock(pos, block, 3, nullptr, nullptr);
}

// Returns true when the job is finished (or must be aborted).
bool step(Job& j) {
    auto* region = regionFor(j.dimId);
    if (!region) return true;

    size_t budget = kBlocksPerTick;
    int    minY   = region->getMinHeight();
    int    maxY   = region->getMaxHeight();

    switch (j.kind) {
    case Kind::Edit:
        while (budget > 0 && j.cursor < j.positions.size()) {
            BlockPos const& p = j.positions[j.cursor++];
            --budget;
            if (p.y < minY || p.y >= maxY) continue;
            Block const& cur = region->getBlock(p);
            if (j.onlyReplace && &cur != j.onlyReplace) continue;
            if (&cur == j.newBlock) continue;
            j.tx.changes.push_back({p, &cur, j.newBlock});
            setBlockAt(*region, p, *j.newBlock);
        }
        return j.cursor >= j.positions.size();

    case Kind::Undo: { // walk the transaction backwards, restoring old blocks
        size_t n = j.tx.changes.size();
        while (budget > 0 && j.cursor < n) {
            auto const& c = j.tx.changes[n - 1 - j.cursor++];
            --budget;
            setBlockAt(*region, c.pos, *c.oldBlock);
        }
        return j.cursor >= n;
    }
    case Kind::Redo: { // walk forwards, re-applying new blocks
        size_t n = j.tx.changes.size();
        while (budget > 0 && j.cursor < n) {
            auto const& c = j.tx.changes[j.cursor++];
            --budget;
            setBlockAt(*region, c.pos, *c.newBlock);
        }
        return j.cursor >= n;
    }
    }
    return true;
}

void finish(Job& j) {
    auto& h       = gHistory[j.owner];
    size_t changed = j.tx.changes.size();
    switch (j.kind) {
    case Kind::Edit:
        if (!j.tx.changes.empty()) {
            j.tx.dimId = j.dimId;
            h.undo.push_back(std::move(j.tx));
            trim(h.undo);
            h.redo.clear();
        }
        break;
    case Kind::Undo:
        h.redo.push_back(std::move(j.tx));
        trim(h.redo);
        break;
    case Kind::Redo:
        h.undo.push_back(std::move(j.tx));
        trim(h.undo);
        break;
    }
    if (j.onDone) j.onDone(changed);
}

} // namespace

bool isBusy(std::string const& owner) {
    return std::any_of(gJobs.begin(), gJobs.end(), [&](Job const& j) { return j.owner == owner; });
}

bool submitEdit(
    std::string const&    owner,
    int                   dimId,
    std::vector<BlockPos> positions,
    Block const*          newBlock,
    Block const*          onlyReplace,
    DoneFn                onDone
) {
    if (!newBlock || positions.empty() || positions.size() > kMaxVolume || isBusy(owner)) return false;
    Job j;
    j.kind        = Kind::Edit;
    j.owner       = owner;
    j.dimId       = dimId;
    j.positions   = std::move(positions);
    j.newBlock    = newBlock;
    j.onlyReplace = onlyReplace;
    j.onDone      = std::move(onDone);
    gJobs.push_back(std::move(j));
    return true;
}

bool submitUndo(std::string const& owner, DoneFn onDone) {
    auto it = gHistory.find(owner);
    if (it == gHistory.end() || it->second.undo.empty() || isBusy(owner)) return false;
    Job j;
    j.kind   = Kind::Undo;
    j.owner  = owner;
    j.tx     = std::move(it->second.undo.back());
    j.dimId  = j.tx.dimId;
    j.onDone = std::move(onDone);
    it->second.undo.pop_back();
    gJobs.push_back(std::move(j));
    return true;
}

bool submitRedo(std::string const& owner, DoneFn onDone) {
    auto it = gHistory.find(owner);
    if (it == gHistory.end() || it->second.redo.empty() || isBusy(owner)) return false;
    Job j;
    j.kind   = Kind::Redo;
    j.owner  = owner;
    j.tx     = std::move(it->second.redo.back());
    j.dimId  = j.tx.dimId;
    j.onDone = std::move(onDone);
    it->second.redo.pop_back();
    gJobs.push_back(std::move(j));
    return true;
}

size_t undoDepth(std::string const& owner) {
    auto it = gHistory.find(owner);
    return it == gHistory.end() ? 0 : it->second.undo.size();
}

size_t redoDepth(std::string const& owner) {
    auto it = gHistory.find(owner);
    return it == gHistory.end() ? 0 : it->second.redo.size();
}

void clearOwner(std::string const& owner) {
    gHistory.erase(owner);
    std::erase_if(gJobs, [&](Job const& j) { return j.owner == owner; });
}

void tick() {
    if (gJobs.empty()) return;
    Job& front = gJobs.front();
    if (step(front)) {
        finish(front);
        gJobs.pop_front();
    }
}

} // namespace axiom::we
