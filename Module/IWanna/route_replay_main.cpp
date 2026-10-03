#include "IWanna/Public/game_module.h"
#include "IWanna/Public/game_config.h"
#include <json/json.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
Json::Value ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    Json::Value value;
    Json::CharReaderBuilder reader;
    std::string error;
    if (!input || !Json::parseFromStream(reader, input, &value, &error))
        throw std::runtime_error("Cannot read route evidence: " + path.string() + " " + error);
    return value;
}

double Number(const Json::Value& object, const char* key) {
    if (!object[key].isNumeric() || !std::isfinite(object[key].asDouble()))
        throw std::runtime_error(std::string("Invalid route number: ") + key);
    return object[key].asDouble();
}

int Replay(IWanna::GameModule& game, const IWanna::GameConfig& config,
           const Json::Value& evidence, const std::string& roomId,
           const Json::Value& roomData) {
    const auto& grid = roomData["grid"];
    const float unit = float(Number(grid, "tileSize"));
    const auto& origin = grid["origin"];
    if (unit <= 0 || !origin.isArray() || origin.size() != 2)
        throw std::runtime_error(roomId + ": invalid room grid");
    const float ox = origin[0].asFloat(), oy = origin[1].asFloat();
    const float footRatio = config.character.offsetRatio.y + config.character.sizeRatio.y * .5f;
    const float footOffset = config.ScaledPlayerSize().y * footRatio;
    const float dt = config.physics.fixedStep;
    const int heldFrames = int(std::ceil(config.physics.jumpHoldSeconds / dt));
    int passed = 0;
    for (const auto& jump : evidence) {
        const float sourceX = ox + float(Number(jump, "fromColumn")) * unit;
        const float targetX = ox + float(Number(jump, "toColumn")) * unit;
        const float direction = targetX >= sourceX ? 1.f : -1.f;
        const float sourceFloor = oy + float(Number(jump, "fromRow")) * unit;
        const float destinationFloor = oy + float(Number(jump, "toRow")) * unit;
        int secondFrame = -1;
        if (!jump["secondJumpAt"].isNull())
            secondFrame = int(std::ceil(Number(jump, "secondJumpAt") / dt - 1e-5));
        if (game.GetState() != IWanna::State::Playing) {
            game.Restart();
            game.FixedTick({});
        }
        game.World()->RequestRoom(roomId, "start", glm::vec2(sourceX, sourceFloor - footOffset - .10f));
        game.FixedTick({});
        if (game.World()->CurrentId() != roomId || !game.World()->Error().empty())
            throw std::runtime_error(roomId + ": cannot enter replay room: " + game.World()->Error());
        auto player = game.PlayerEntity();
        bool grounded = false;
        for (int frame = 0; frame < 60 && game.GetState() == IWanna::State::Playing; ++frame) {
            game.FixedTick({});
            if (game.Get<IWanna::Player>(player).grounded) { grounded = true; break; }
        }
        if (!grounded) throw std::runtime_error(roomId + ": replay takeoff is not grounded");
        bool reached = false;
        for (int frame = 0; frame < int(2.6f / dt); ++frame) {
            const float x = game.Get<IWanna::Transform>(player).position.x;
            IWanna::Input input;
            input.right = direction > 0 && x < targetX - .03f;
            input.left = direction < 0 && x > targetX + .03f;
            input.jump = frame == 0 || frame == secondFrame;
            input.jumpHeld = frame < heldFrames ||
                (secondFrame >= 0 && frame >= secondFrame && frame < secondFrame + heldFrames);
            game.FixedTick(input);
            if (game.GetState() != IWanna::State::Playing || !game.World()->Error().empty()) break;
            // The final island can enter its door before the nominal target X.
            // Reaching the next room is stronger evidence than a local landing.
            if (game.World()->CurrentId() != roomId) { reached = true; break; }
            const auto body = game.PlayerBody();
            const float feet = body.position.y + body.size.y * .5f;
            const float nowX = game.Get<IWanna::Transform>(player).position.x;
            if (frame > 1 && game.Get<IWanna::Player>(player).grounded &&
                direction * (nowX - targetX) >= -.25f && std::abs(feet - destinationFloor) < .18f) {
                reached = true;
                break;
            }
        }
        if (!reached) {
            const auto position = game.Get<IWanna::Transform>(player).position;
            std::cerr << roomId << " section " << jump["section"].asInt() << " "
                      << jump["kind"].asString() << " failed: x=" << position.x
                      << " y=" << position.y << " targetX=" << targetX
                      << " state=" << int(game.GetState()) << " script=" << game.World()->Error() << '\n';
            return -1;
        }
        ++passed;
    }
    return passed;
}

int ReplayTimedTraps(IWanna::GameModule& game, const IWanna::GameConfig& config,
                     const std::string& roomId, const Json::Value& traps) {
    if (traps.isNull()) return 0;
    if (!traps.isArray()) throw std::runtime_error(roomId + ": timedTraps must be an array");
    int passed = 0;
    for (const auto& trap : traps) {
        const std::string sensorId = trap["sensor"].asString();
        const std::string targetId = trap["trap"].asString();
        if (sensorId.empty() || targetId.empty())
            throw std::runtime_error(roomId + ": trap needs sensor and target IDs");
        if (game.GetState() != IWanna::State::Playing) {
            game.Restart();
            game.FixedTick({});
        }
        game.World()->RequestRoom(roomId, "start");
        game.FixedTick({});
        auto sensor = game.Find(sensorId);
        if (!sensor) throw std::runtime_error(roomId + ": missing sensor " + sensorId);
        const auto sensorTransform = game.Get<IWanna::Transform>(sensor);
        const auto sensorCollider = game.Get<IWanna::Collider>(sensor);
        const auto detection = sensorTransform.position +
            (sensorCollider.detectionBox ? sensorCollider.detectionOffset : glm::vec2(0));
        const auto arrival = detection - config.ScaledPlayerSize() * config.character.offsetRatio;
        game.World()->RequestRoom(roomId, "start", arrival);
        game.FixedTick({});
        auto target = game.Find(targetId);
        if (!target) throw std::runtime_error(roomId + ": missing trap " + targetId);
        if (trap.get("enabledInitially", false).asBool() && !game.Get<IWanna::Collider>(target).enabled)
            throw std::runtime_error(roomId + ": bridge begins disabled");
        bool activated = !trap.isMember("velocityY");
        for (int tick = 0; tick < 8 && game.GetState() == IWanna::State::Playing; ++tick) {
            game.FixedTick({});
            if (trap.isMember("velocityY") &&
                std::abs(game.Get<IWanna::Motion>(target).velocity.y - float(Number(trap, "velocityY"))) < .01f)
                activated = true;
        }
        if (!activated || !game.World()->Error().empty())
            throw std::runtime_error(roomId + ": sensor did not activate " + targetId + " " + game.World()->Error());
        const int waitTicks = int(std::ceil(Number(trap, "clearsAt") / config.physics.fixedStep)) + 4;
        for (int tick = 0; tick < waitTicks && game.GetState() == IWanna::State::Playing; ++tick)
            game.FixedTick({});
        if (game.GetState() != IWanna::State::Playing || !game.World()->Error().empty() ||
            game.Get<IWanna::Collider>(target).enabled)
            throw std::runtime_error(roomId + ": timed trap did not clear safely: " + targetId + " " + game.World()->Error());
        if (trap.isMember("straightRunHitAt")) {
            // Reset the trap, then prove the bait is consequential: a player
            // who simply runs through this otherwise safe island must die.
            game.World()->RequestRoom(roomId, "start", arrival);
            game.FixedTick({});
            const int runTicks = int(std::ceil((Number(trap, "straightRunHitAt") + .25) /
                                               config.physics.fixedStep));
            for (int tick = 0; tick < runTicks && game.GetState() == IWanna::State::Playing; ++tick) {
                IWanna::Input running;
                running.right = true;
                game.FixedTick(running);
            }
            if (game.GetState() != IWanna::State::Dead)
                throw std::runtime_error(roomId + ": straight run evaded intended apple trap " + targetId);
        }
        ++passed;
    }
    return passed;
}

glm::vec2 Vector(const Json::Value& value, const std::string& label) {
    if (!value.isArray() || value.size() != 2 || !value[0].isNumeric() ||
        !value[1].isNumeric() || !std::isfinite(value[0].asDouble()) ||
        !std::isfinite(value[1].asDouble()))
        throw std::runtime_error(label + " must be two finite numbers");
    return {value[0].asFloat(), value[1].asFloat()};
}

int Frames(const Json::Value& value, const char* key, int fallback) {
    if (!value.isMember(key)) return fallback;
    if (!value[key].isInt() || value[key].asInt() < 0 || value[key].asInt() > 72000)
        throw std::runtime_error(std::string(key) + " must be an integer from 0 to 72000");
    return value[key].asInt();
}

std::string Status(IWanna::GameModule& game) {
    const auto id = game.PlayerEntity();
    const auto p = game.Get<IWanna::Transform>(id).position;
    const auto body = game.PlayerBody();
    std::ostringstream out;
    out << "room=" << game.World()->CurrentId() << " position=[" << p.x << ',' << p.y
        << "] feet=" << body.position.y + body.size.y * .5f
        << " grounded=" << game.Get<IWanna::Player>(id).grounded
        << " alive=" << (game.GetState() == IWanna::State::Playing);
    return out.str();
}

void CheckExpected(IWanna::GameModule& game, const Json::Value& expected,
                   const std::string& label) {
    auto fail = [&](const std::string& reason) {
        throw std::runtime_error(label + ": " + reason + "; " + Status(game));
    };
    if (!game.World()->Error().empty()) fail("room script: " + game.World()->Error());
    const bool alive = game.GetState() == IWanna::State::Playing;
    if (alive != expected.get("alive", true).asBool()) fail("unexpected survival state");
    if (expected.isMember("room") && game.World()->CurrentId() != expected["room"].asString())
        fail("expected room " + expected["room"].asString());
    if (expected.isMember("grounded") &&
        game.Get<IWanna::Player>(game.PlayerEntity()).grounded != expected["grounded"].asBool())
        fail("unexpected grounded state");
    if (expected.isMember("position")) {
        const auto target = Vector(expected["position"], label + " expected position");
        const auto tolerance = expected.isMember("tolerance") ?
            Vector(expected["tolerance"], label + " tolerance") : glm::vec2(.25f);
        if (tolerance.x < 0 || tolerance.y < 0) fail("negative position tolerance");
        const auto error = glm::abs(game.Get<IWanna::Transform>(game.PlayerEntity()).position - target);
        if (error.x > tolerance.x || error.y > tolerance.y) fail("position outside tolerance");
    }
}

void EnterReplayRoom(IWanna::GameModule& game, const Json::Value& test,
                     const std::string& label, bool usePosition) {
    const auto room = test["room"].asString();
    if (room.empty()) throw std::runtime_error(label + ": room is required");
    if (game.GetState() != IWanna::State::Playing) {
        game.Restart();
        game.FixedTick({});
    }
    std::optional<glm::vec2> position;
    if (usePosition && test.isMember("position")) position = Vector(test["position"], label + " position");
    game.World()->RequestRoom(room, test.get("spawn", "start").asString(), position);
    game.FixedTick({});
    Json::Value expected; expected["room"] = room;
    CheckExpected(game, expected, label + " entry");
}

// Authored button sequences run uninterrupted through the real ECS physics and Lua
// systems. Unlike independent jump probes, these catch blocked stairways and paths
// that only appear reachable when the player is repositioned between platforms.
int ReplayInputs(IWanna::GameModule& game, const Json::Value& cases) {
    if (cases.isNull()) return 0;
    if (!cases.isArray()) throw std::runtime_error("cases must be an array");
    int passed = 0;
    for (const auto& test : cases) {
        const auto name = test.get("name", "input case " + std::to_string(passed)).asString();
        EnterReplayRoom(game, test, name, true);
        for (int frame = 0; frame < Frames(test, "settleFrames", 0); ++frame) {
            game.FixedTick({});
            CheckExpected(game, {}, name + " settling");
        }
        if (!test["steps"].isArray()) throw std::runtime_error(name + ": steps must be an array");
        int index = 0;
        for (const auto& step : test["steps"]) {
            const auto label = name + " step " + std::to_string(++index);
            const int move = step.get("move", 0).asInt();
            if (move < -1 || move > 1) throw std::runtime_error(label + ": move must be -1, 0 or 1");
            IWanna::Input input;
            input.left = move < 0; input.right = move > 0;
            input.jump = step.get("jump", false).asBool();
            input.jumpHeld = step.get("jumpHeld", false).asBool();
            input.shoot = input.shootHeld = step.get("shoot", false).asBool();
            const int frames = Frames(step, "frames", 1);
            for (int frame = 0; frame < frames && game.GetState() == IWanna::State::Playing; ++frame) {
                game.FixedTick(input);
                input.jump = input.shoot = false;
                if (!game.World()->Error().empty())
                    throw std::runtime_error(label + ": " + game.World()->Error());
            }
            std::cout << label << ": " << Status(game) << '\n';
            CheckExpected(game, step["expect"], label);
        }
        CheckExpected(game, test["expect"], name + " final");
        std::cout << name << ": continuous input replay PASS\n";
        ++passed;
    }
    return passed;
}

int ReplayBoundaries(IWanna::GameModule& game, const Json::Value& cases) {
    if (cases.isNull()) return 0;
    if (!cases.isArray()) throw std::runtime_error("boundaries must be an array");
    int passed = 0;
    for (const auto& test : cases) {
        const auto name = test.get("name", "boundary " + std::to_string(passed)).asString();
        EnterReplayRoom(game, test, name, false);
        // Probe exact coordinates, including half-open segment endpoints, without
        // gravity shifting Y or contact recovery moving the sample back inside.
        // Real movement through exits is covered by the continuous input cases.
        if (!game.World()->CrossBoundary(Vector(test["position"], name + " position")))
            throw std::runtime_error(name + ": probe did not cross a handled boundary");
        game.FixedTick({});
        Json::Value expected;
        if (test.isMember("expectRoom")) {
            expected["room"] = test["expectRoom"];
            expected["alive"] = true;
        } else {
            expected["room"] = test["room"];
            expected["alive"] = false;
        }
        CheckExpected(game, expected, name);
        std::cout << name << ": " << Status(game) << " PASS\n";
        ++passed;
    }
    return passed;
}

void ReplayInputEvidence(IWanna::GameModule& game, const std::filesystem::path& world,
                         const Json::Value& route) {
    const auto catalog = IWanna::RoomCatalog::Load(world);
    if (!route["noPortals"].isNull() && !route["noPortals"].isArray())
        throw std::runtime_error("noPortals must be an array of room IDs");
    for (const auto& value : route["noPortals"]) {
        const auto id = value.asString();
        if (!catalog.rooms.contains(id)) throw std::runtime_error("Unknown noPortals room " + id);
        for (const auto& object : catalog.rooms.at(id).objects)
            if (object.role == IWanna::Role::Exit)
                throw std::runtime_error(id + ": portal remains: " + object.id);
        std::cout << id << ": no portal entities PASS\n";
    }
    const int inputs = ReplayInputs(game, route["cases"]);
    const int boundaries = ReplayBoundaries(game, route["boundaries"]);
    if (inputs + boundaries == 0) throw std::runtime_error("Input evidence contains no replay cases");
    std::cout << "Authored input replay PASS: " << inputs << " continuous routes, "
              << boundaries << " boundary probes\n";
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: iwanna_route_replay <world.json or project directory> <route evidence.json>\n";
        return 2;
    }
    try {
        auto world = std::filesystem::absolute(argv[1]);
        if (std::filesystem::is_directory(world)) world /= "world.json";
        const auto root = world.parent_path();
        const auto route = ReadJson(argv[2]);
        const bool inputEvidence = route["format"] == "IWANNA_INPUT_REPLAY_1";
        if (!inputEvidence && (route["format"] != "IWANNA_ROUTE_EVIDENCE_1" || !route["rooms"].isObject()))
            throw std::runtime_error("Expected IWANNA_ROUTE_EVIDENCE_1 or IWANNA_INPUT_REPLAY_1");
        const auto config = IWanna::GameConfig::Load(root / "gameplay.json");
        IWanna::GameModule game(root, root / "gameplay.json", world);
        if (!game.Startup()) throw std::runtime_error(game.Error());
        if (inputEvidence) {
            ReplayInputEvidence(game, world, route);
            return 0;
        }
        int total = 0, totalTraps = 0;
        for (const auto& roomId : route["rooms"].getMemberNames()) {
            const auto roomFile = root / "rooms" / (roomId + ".room.json");
            const auto roomData = ReadJson(roomFile);
            int count = Replay(game, config, route["rooms"][roomId], roomId, roomData);
            if (count < 0) return 1;
            total += count;
            std::cout << roomId << ": " << count << " actual-physics jumps PASS\n";
            const int traps = ReplayTimedTraps(game, config, roomId, route["timedTraps"][roomId]);
            totalTraps += traps;
            std::cout << roomId << ": " << traps << " Lua sensor/timer traps PASS\n";
        }
        std::cout << "Campaign actual-physics replay PASS: " << total
                  << " transitions, " << totalTraps << " timed traps\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
