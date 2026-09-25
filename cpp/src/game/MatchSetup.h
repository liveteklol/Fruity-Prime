#pragma once

#include "CameraSequence.h"
#include "MatchRoom.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fp {

// What a match played on this machine is: the launcher's Play offline card
// (map, mode, bots and their skill, the hunter and its suit) and the match
// rules from the settings. Unset rules are the mode's own (GameState.Setup).
struct MatchOptions {
    std::string roomKey = "MP3 PROVING GROUND";
    GameMode mode = GameMode::Battle;
    int hunter = 0, suit = 0;
    int bots = 3, botLevel = 2;
    int targets = 0;
    int teams = 2;
    bool friendlyFire = false;
    std::optional<int> pointGoal;
    std::optional<float> timeLimitSeconds, timeGoalSeconds;
    bool allWeapons = false;
    bool intro = true;
};

// Loads the room and sets the match up with its bots; nullopt (the reason on
// stderr) when the room cannot be loaded.
std::optional<MatchRoom> buildOfflineMatch(const std::filesystem::path& root, const MatchOptions& options);

// The launcher's ground: a room drawn with nobody in it, and the intro its
// camera flies on a loop. Null when the room cannot be loaded.
struct Backdrop {
    std::unique_ptr<Scene> scene;
    std::optional<CameraSequence> flight;
};
Backdrop buildBackdrop(const std::filesystem::path& root, const std::string& roomKey);

// The multiplayer rooms a match can be played in, in the table's order.
std::vector<const RoomMetadata*> matchRooms();
// "battle", "battleteams", ... as the command line and the settings name them.
const char* modeKey(GameMode mode);
std::optional<GameMode> modeFromKey(const std::string& key);
// A mode's name as the HUD's results screen writes it ("Battle Teams").
const char* modeTitle(GameMode mode);

} // namespace fp
