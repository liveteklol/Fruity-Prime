#include "MatchSetup.h"

#include "CameraSequence.h"
#include "World.h"

#include <algorithm>

namespace fp {

namespace {

constexpr const char* modeKeys[] = {"battle", "battleteams", "survival", "survivalteams", "capture", "bounty", "bountyteams", "nodes",
    "nodesteams", "defender", "defenderteams", "primehunter"};
constexpr const char* modeTitles[] = {"Battle", "Battle Teams", "Survival", "Survival Teams", "Capture", "Bounty", "Bounty Teams", "Nodes",
    "Nodes Teams", "Defender", "Defender Teams", "Prime Hunter"};

} // namespace

const char* modeKey(GameMode mode) { return modeKeys[static_cast<int>(mode)]; }
const char* modeTitle(GameMode mode) { return modeTitles[static_cast<int>(mode)]; }

std::optional<GameMode> modeFromKey(const std::string& key)
{
    std::string k;
    for (char c : key) {
        if (c != '-' && c != '_' && c != ' ') {
            k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    for (int i = 0; i < 12; i++) {
        if (k == modeKeys[i]) {
            return static_cast<GameMode>(i);
        }
    }
    return std::nullopt;
}

std::vector<const RoomMetadata*> matchRooms()
{
    std::vector<const RoomMetadata*> rooms;
    for (const RoomMetadata& room : allRooms()) {
        if (room.multiplayer && !room.firstHunt && !room.hybrid) {
            rooms.push_back(&room);
        }
    }
    return rooms;
}

Backdrop buildBackdrop(const std::filesystem::path& root, const std::string& roomKey)
{
    const RoomMetadata* room = findRoom(roomKey);
    if (room == nullptr) {
        return {};
    }
    // The room alone: no collision, no node data, nobody in it.
    std::optional<MatchRoom> loaded = loadMatchRoom(root, *room, GameMode::Battle, 4, 0, false);
    if (!loaded) {
        return {};
    }
    loaded->world.reset(); // before the scene it holds a reference to
    return {std::move(loaded->scene), CameraSequence::loadIntro(root, room->id)};
}

std::optional<MatchRoom> buildOfflineMatch(const std::filesystem::path& root, const MatchOptions& o)
{
    const RoomMetadata* room = findRoom(o.roomKey);
    if (room == nullptr) {
        std::fprintf(stderr, "Unknown room \"%s\"\n", o.roomKey.c_str());
        return std::nullopt;
    }
    const int bots = std::clamp(o.bots, 0, 7);
    const int targets = std::clamp(o.targets, 0, 7 - bots);
    const int players = std::clamp(1 + bots + targets, 2, 8);
    const int hunter = std::clamp(o.hunter, 0, 6);
    std::optional<MatchRoom> match = loadMatchRoom(root, *room, o.mode, players, hunter, false);
    if (!match) {
        return std::nullopt;
    }
    World& world = *match->world;
    world.setFriendlyFire(o.friendlyFire);
    if (o.pointGoal || o.timeLimitSeconds) {
        world.setRules(o.pointGoal.value_or(world.pointGoal()), o.timeLimitSeconds.value_or(world.matchTime()));
    }
    if (o.timeGoalSeconds) {
        world.setTimeGoal(*o.timeGoalSeconds);
    }
    if (o.allWeapons) {
        world.player()->giveAllWeapons();
    }
    for (int i = 0; i < targets; i++) {
        world.addPlayer((hunter + 1 + i) % 7);
    }
    for (int i = 0; i < bots; i++) {
        world.addBot((hunter + 1 + targets + i) % 7, std::clamp(o.botLevel, 0, 3));
    }
    world.setSuit(0, std::clamp(o.suit, 0, 3));
    world.setTeamCount(o.teams); // after everybody is in: TeamRules.ChooseTeam deals them round
    if (o.intro) {
        world.setIntro(CameraSequence::loadIntro(root, room->id));
    }
    return match;
}

} // namespace fp
