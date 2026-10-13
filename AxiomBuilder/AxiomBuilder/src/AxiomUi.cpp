#include "AxiomUi.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <variant>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/schedule/Scheduler.h"
#include "ll/api/schedule/Task.h"
#include "ll/api/service/Bedrock.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

namespace axiom::ui {

namespace {

constexpr char const* kWandItem  = "minecraft:wooden_axe"; // left-click = pos1, right-click = pos2
constexpr char const* kBrushItem = "minecraft:blaze_rod";  // right-click a block = brush stroke

std::unordered_map<std::string, PlayerState>       gStates;
std::unique_ptr<ll::schedule::ServerTimeScheduler> gScheduler;

struct Result {
    bool        ok;
    std::string msg;
};

std::string uuidOf(Player& p) { return p.getUuid().asString(); }

int dimOf(Player& p) { return static_cast<int>(p.getDimensionId()); }

BlockPos feetPos(Player& p) {
    auto v = p.getFeetPos();
    return {int(std::floor(v.x)), int(std::floor(v.y)), int(std::floor(v.z))};
}

std::string normalize(std::string name) {
    if (name.find(':') == std::string::npos) name = "minecraft:" + name;
    return name;
}

Block const* findBlock(std::string const& name) { return Block::tryGetFromRegistry(normalize(name)).as_ptr(); }

bool holding(Player& p, char const* id) { return p.getSelectedItem().getTypeName() == id; }

// Async completion messages: the player may have moved on or left, so look them up again.
void notify(std::string const& uuid, std::string const& msg) {
    if (auto level = ll::service::getLevel()) {
        if (auto* p = level->getPlayer(mce::UUID::fromString(uuid))) p->sendMessage(msg);
    }
}

// ---- actions (shared by commands, events and forms) -------------------------
Result setPoint(Player& p, bool first, BlockPos pos) {
    auto& sel = stateOf(p).sel;
    // Changing dimension invalidates the other corner.
    if (sel.dimId != dimOf(p)) sel = {};
    sel.dimId = dimOf(p);
    (first ? sel.a : sel.b)      = pos;
    (first ? sel.hasA : sel.hasB) = true;

    std::string msg = std::string(first ? "Position 1" : "Position 2") + " set to (" + std::to_string(pos.x) + ", "
                    + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")";
    if (sel.valid()) msg += " - " + std::to_string(sel.volume()) + " blocks selected";
    return {true, msg + "."};
}

Result checkSelection(Player& p, we::Selection const& sel) {
    if (!sel.valid()) return {false, "Set both positions first (/axiom pos1, /axiom pos2)."};
    if (sel.dimId != dimOf(p)) return {false, "Your selection is in a different dimension."};
    if (sel.volume() > we::kMaxVolume)
        return {false, "Selection too large (max " + std::to_string(we::kMaxVolume) + " blocks)."};
    return {true, {}};
}

Result doFill(Player& p, std::string const& blockName) {
    auto& sel = stateOf(p).sel;
    if (auto r = checkSelection(p, sel); !r.ok) return r;
    Block const* block = findBlock(blockName);
    if (!block) return {false, "Unknown block: " + blockName};

    std::vector<BlockPos> blocks;
    we::collectSelection(sel, blocks);
    std::string uuid = uuidOf(p);
    bool ok = we::submitEdit(uuid, sel.dimId, std::move(blocks), block, nullptr, [uuid](size_t n) {
        notify(uuid, "Filled " + std::to_string(n) + " blocks.");
    });
    return ok ? Result{true, "Filling..."} : Result{false, "Wait for your previous operation to finish."};
}

Result doReplace(Player& p, std::string const& fromName, std::string const& toName) {
    auto& sel = stateOf(p).sel;
    if (auto r = checkSelection(p, sel); !r.ok) return r;
    Block const* from = findBlock(fromName);
    Block const* to   = findBlock(toName);
    if (!from) return {false, "Unknown block: " + fromName};
    if (!to) return {false, "Unknown block: " + toName};

    std::vector<BlockPos> blocks;
    we::collectSelection(sel, blocks);
    std::string uuid = uuidOf(p);
    bool ok = we::submitEdit(uuid, sel.dimId, std::move(blocks), to, from, [uuid](size_t n) {
        notify(uuid, "Replaced " + std::to_string(n) + " blocks.");
    });
    return ok ? Result{true, "Replacing..."} : Result{false, "Wait for your previous operation to finish."};
}

Result doBrush(Player& p, BlockPos center) {
    auto& st    = stateOf(p);
    auto  brush = we::makeBrush(st.brushType, st.brushRadius, st.brushHeight);
    if (!brush) return {false, "Unknown brush type: " + st.brushType};
    Block const* block = findBlock(st.material);
    if (!block) return {false, "Unknown material: " + st.material};

    std::vector<BlockPos> blocks;
    brush->collect(center, blocks);
    std::string uuid = uuidOf(p);
    bool ok = we::submitEdit(uuid, dimOf(p), std::move(blocks), block, nullptr, [uuid](size_t n) {
        notify(uuid, "Brush changed " + std::to_string(n) + " blocks.");
    });
    return ok ? Result{true, "Brush applied."} : Result{false, "Wait for your previous operation to finish."};
}

Result doStroke(Player& p) {
    auto hit = we::raycast(p);
    if (!hit.hit) return {false, "No block in range."};
    return doBrush(p, hit.pos);
}

Result doUndo(Player& p) {
    std::string uuid = uuidOf(p);
    bool ok = we::submitUndo(uuid, [uuid](size_t n) { notify(uuid, "Undid " + std::to_string(n) + " blocks."); });
    return ok ? Result{true, "Undoing..."} : Result{false, "Nothing to undo (or an operation is still running)."};
}

Result doRedo(Player& p) {
    std::string uuid = uuidOf(p);
    bool ok = we::submitRedo(uuid, [uuid](size_t n) { notify(uuid, "Redid " + std::to_string(n) + " blocks."); });
    return ok ? Result{true, "Redoing..."} : Result{false, "Nothing to redo (or an operation is still running)."};
}

Result doSetBrush(Player& p, std::string const& type, int radius) {
    if (!we::makeBrush(type, radius, 1)) return {false, "Brush types: sphere, cube, cylinder."};
    auto& st       = stateOf(p);
    st.brushType   = type;
    st.brushRadius = std::clamp(radius, 1, 64);
    return {true, "Brush: " + type + ", radius " + std::to_string(st.brushRadius) + "."};
}

void say(Player& p, Result const& r) { p.sendMessage((r.ok ? "§a" : "§c") + r.msg); }

// ---- command helpers --------------------------------------------------------
Player* playerOf(CommandOrigin const& origin, CommandOutput& output) {
    auto* entity = origin.getEntity();
    if (!entity || !entity->isPlayer()) {
        output.error("This command can only be used by a player.");
        return nullptr;
    }
    return static_cast<Player*>(entity);
}

void reply(CommandOutput& output, Result const& r) {
    if (r.ok) output.success(r.msg);
    else output.error(r.msg);
}

struct FillArgs {
    std::string block;
};
struct ReplaceArgs {
    std::string from;
    std::string to;
};
struct BrushArgs {
    std::string type;
    int         radius;
};

// ---- forms ------------------------------------------------------------------
void showFillForm(Player& player) {
    ll::form::CustomForm form("Fill selection");
    form.appendInput("block", "Block id", "minecraft:stone", stateOf(player).material);
    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& res, ll::form::FormCancelReason) {
        if (!res) return;
        auto name = std::get<std::string>(res->at("block"));
        stateOf(p).material = normalize(name);
        say(p, doFill(p, name));
    });
}

void showReplaceForm(Player& player) {
    ll::form::CustomForm form("Replace in selection");
    form.appendInput("from", "Block to replace", "minecraft:stone");
    form.appendInput("to", "Replace with", "minecraft:dirt");
    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& res, ll::form::FormCancelReason) {
        if (!res) return;
        say(p, doReplace(p, std::get<std::string>(res->at("from")), std::get<std::string>(res->at("to"))));
    });
}

void showBrushForm(Player& player) {
    auto&                    st = stateOf(player);
    std::vector<std::string> types{"sphere", "cube", "cylinder"};
    size_t                   def = st.brushType == "cube" ? 1 : st.brushType == "cylinder" ? 2 : 0;

    ll::form::CustomForm form("Brush settings");
    form.appendDropdown("type", "Shape", types, def);
    form.appendSlider("radius", "Radius", 1, 32, 1, st.brushRadius);
    form.appendSlider("height", "Cylinder height", 1, 64, 1, st.brushHeight);
    form.appendInput("material", "Material", "minecraft:stone", st.material);
    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& res, ll::form::FormCancelReason) {
        if (!res) return;
        auto& s       = stateOf(p);
        s.brushType   = std::get<std::string>(res->at("type"));
        s.brushRadius = int(std::get<double>(res->at("radius")));
        s.brushHeight = int(std::get<double>(res->at("height")));
        std::string mat = normalize(std::get<std::string>(res->at("material")));
        if (findBlock(mat)) s.material = mat;
        else p.sendMessage("§cUnknown material, keeping " + s.material + ".");
        p.sendMessage("§aBrush: " + s.brushType + ", radius " + std::to_string(s.brushRadius) + ", material "
                      + s.material + ".");
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
PlayerState& stateOf(Player& player) { return gStates[uuidOf(player)]; }

void dropState(std::string const& uuid) { gStates.erase(uuid); }

void showMainMenu(Player& player) {
    ll::form::SimpleForm form(
        "AxiomBuilder",
        "Wand: " + std::string(kWandItem) + " (left = pos1, right = pos2)\nBrush: " + kBrushItem
    );
    form.appendButton("Set pos1 here", [](Player& p) { say(p, setPoint(p, true, feetPos(p))); });
    form.appendButton("Set pos2 here", [](Player& p) { say(p, setPoint(p, false, feetPos(p))); });
    form.appendButton("Fill selection", [](Player& p) { showFillForm(p); });
    form.appendButton("Replace in selection", [](Player& p) { showReplaceForm(p); });
    form.appendButton("Brush settings", [](Player& p) { showBrushForm(p); });
    form.appendButton("Apply brush (where I look)", [](Player& p) { say(p, doStroke(p)); });
    form.appendButton("Undo", [](Player& p) { say(p, doUndo(p)); });
    form.appendButton("Redo", [](Player& p) { say(p, doRedo(p)); });
    form.sendTo(player);
}

void startTicker() {
    if (gScheduler) return;
    gScheduler = std::make_unique<ll::schedule::ServerTimeScheduler>();
    gScheduler->add<ll::schedule::RepeatTask>(ll::chrono::ticks{1}, [] { we::tick(); });
}

void stopTicker() {
    if (!gScheduler) return;
    gScheduler->clear();
    gScheduler.reset();
}

void registerCommands() {
    auto& cmd = ll::command::CommandRegistrar::getInstance()
                    .getOrCreateCommand("axiom", "AxiomBuilder world editing", CommandPermissionLevel::GameDirectors);

    cmd.overload().execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) {
            showMainMenu(*p);
            out.success("Opening menu.");
        }
    });
    cmd.overload().text("pos1").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) reply(out, setPoint(*p, true, feetPos(*p)));
    });
    cmd.overload().text("pos2").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) reply(out, setPoint(*p, false, feetPos(*p)));
    });
    cmd.overload<FillArgs>().text("fill").required("block").execute(
        [](CommandOrigin const& o, CommandOutput& out, FillArgs const& a) {
            if (auto* p = playerOf(o, out)) reply(out, doFill(*p, a.block));
        }
    );
    cmd.overload<ReplaceArgs>().text("replace").required("from").required("to").execute(
        [](CommandOrigin const& o, CommandOutput& out, ReplaceArgs const& a) {
            if (auto* p = playerOf(o, out)) reply(out, doReplace(*p, a.from, a.to));
        }
    );
    cmd.overload<BrushArgs>().text("brush").required("type").required("radius").execute(
        [](CommandOrigin const& o, CommandOutput& out, BrushArgs const& a) {
            if (auto* p = playerOf(o, out)) reply(out, doSetBrush(*p, a.type, a.radius));
        }
    );
    cmd.overload().text("stroke").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) reply(out, doStroke(*p));
    });
    cmd.overload().text("undo").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) reply(out, doUndo(*p));
    });
    cmd.overload().text("redo").execute([](CommandOrigin const& o, CommandOutput& out) {
        if (auto* p = playerOf(o, out)) reply(out, doRedo(*p));
    });
}

void registerEvents(std::vector<ll::event::ListenerPtr>& out) {
    auto& bus = ll::event::EventBus::getInstance();

    // Wand left-click: pos1 (cancel so the block isn't broken)
    out.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([](ll::event::PlayerDestroyBlockEvent& ev) {
        auto& p = ev.self();
        if (!holding(p, kWandItem)) return;
        ev.cancel();
        say(p, setPoint(p, true, ev.pos()));
    }));

    // Wand right-click: pos2. Blaze rod right-click: brush stroke at the clicked block.
    out.push_back(
        bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& p = ev.self();
            if (holding(p, kWandItem)) {
                ev.cancel();
                say(p, setPoint(p, false, ev.blockPos()));
            } else if (holding(p, kBrushItem)) {
                ev.cancel();
                say(p, doBrush(p, ev.blockPos()));
            }
        })
    );

    // Free per-player state and history on leave.
    out.push_back(
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            std::string uuid = uuidOf(ev.self());
            dropState(uuid);
            we::clearOwner(uuid);
        })
    );
}

} // namespace axiom::ui
