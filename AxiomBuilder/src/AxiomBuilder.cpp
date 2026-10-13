#include "AxiomBuilder.h"

#include <string>

#include "Builder.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"

namespace axiom {

AxiomBuilder& AxiomBuilder::getInstance() {
    static AxiomBuilder instance;
    return instance;
}

namespace {

struct ShapeParams {
    int         radius;
    std::string block;
};

Player* originPlayer(CommandOrigin const& origin, CommandOutput& output) {
    auto* entity = origin.getEntity();
    if (!entity || !entity->isPlayer()) {
        output.error("This command must be run by a player.");
        return nullptr;
    }
    return static_cast<Player*>(entity);
}

void runShape(CommandOrigin const& origin, CommandOutput& output, ShapeParams const& p, Shape shape, bool hollow) {
    auto* player = originPlayer(origin, output);
    if (!player) return;
    if (p.radius < 1 || p.radius > 32) {
        output.error("Radius must be between 1 and 32.");
        return;
    }
    int n = Builder::get().apply(*player, shape, p.radius, p.block, hollow);
    if (n < 0) {
        output.error("Unknown block: " + p.block + " (use e.g. minecraft:stone)");
        return;
    }
    output.success("Changed " + std::to_string(n) + " blocks.");
}

} // namespace

bool AxiomBuilder::load() {
    getSelf().getLogger().info("AxiomBuilder loaded.");
    return true;
}

bool AxiomBuilder::enable() {
    auto& cmd = ll::command::CommandRegistrar::getInstance()
                    .getOrCreateCommand("axiom", "Axiom-style world building", CommandPermissionLevel::GameDirectors);

    cmd.overload<ShapeParams>()
        .text("sphere")
        .required("radius")
        .required("block")
        .execute([](CommandOrigin const& o, CommandOutput& out, ShapeParams const& p) {
            runShape(o, out, p, Shape::Sphere, false);
        });

    cmd.overload<ShapeParams>()
        .text("hsphere")
        .required("radius")
        .required("block")
        .execute([](CommandOrigin const& o, CommandOutput& out, ShapeParams const& p) {
            runShape(o, out, p, Shape::Sphere, true);
        });

    cmd.overload<ShapeParams>()
        .text("cuboid")
        .required("radius")
        .required("block")
        .execute([](CommandOrigin const& o, CommandOutput& out, ShapeParams const& p) {
            runShape(o, out, p, Shape::Cuboid, false);
        });

    cmd.overload<ShapeParams>()
        .text("hcuboid")
        .required("radius")
        .required("block")
        .execute([](CommandOrigin const& o, CommandOutput& out, ShapeParams const& p) {
            runShape(o, out, p, Shape::Cuboid, true);
        });

    struct NoParams {};
    cmd.overload<NoParams>().text("undo").execute([](CommandOrigin const& o, CommandOutput& out, NoParams const&) {
        auto* player = originPlayer(o, out);
        if (!player) return;
        int n = Builder::get().undo(*player);
        if (n < 0) {
            out.error("Nothing to undo.");
            return;
        }
        out.success("Restored " + std::to_string(n) + " blocks.");
    });

    getSelf().getLogger().info("AxiomBuilder enabled.");
    return true;
}

bool AxiomBuilder::disable() {
    getSelf().getLogger().info("AxiomBuilder disabled.");
    return true;
}

} // namespace axiom

LL_REGISTER_MOD(axiom::AxiomBuilder, axiom::AxiomBuilder::getInstance());
