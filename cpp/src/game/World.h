#pragma once

#include "Collision.h"
#include "Effects.h"
#include "Hud.h"
#include "Player.h"
#include "AiData.h"
#include "CameraSequence.h"
#include "PlayerAi.h"
#include "WorldEntities.h"
#include "formats/Entities.h"
#include "render/Scene.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace fp {


// BeamFlags
namespace BeamFlags {
constexpr uint16_t Collided = 0x1;
constexpr uint16_t Charged = 0x2;
constexpr uint16_t Homing = 0x4;
constexpr uint16_t Ricochet = 0x8;
constexpr uint16_t SelfDamage = 0x10;
constexpr uint16_t ForceEffect = 0x20;
constexpr uint16_t Continuous = 0x40;
constexpr uint16_t Destroyable = 0x80;
constexpr uint16_t HasModel = 0x100;
constexpr uint16_t LifeDrain = 0x800;
constexpr uint16_t SurfaceCollision = 0x1000;
} // namespace BeamFlags

// BeamProjectileEntity: one shot in flight. Times are in seconds, as the C#
// keeps them; speeds are per 60 Hz tick.
struct BeamProjectile {
    uint16_t flags = 0;
    int owner = -1; // player slot
    bool fromHalfturret = false; // fired by the owner's Halfturret
    int beam = 0, beamKind = 0;
    Vec3 velocity{}, acceleration{}, backPosition{}, spawnPosition{}, position{};
    std::array<Vec3, 10> pastPositions{};
    int drawFuncId = 0;
    float age = 0, lifespan = 0;
    Vec3 color{};
    int damageDirType = 0, splashDamageType = 0;
    float homing = 0;
    Vec3 direction{}, right{}, up{};
    float damage = 0, headshotDamage = 0, splashDamage = 0, splashRadius = 0, maxDistance = 0;
    int afflictions = 0;
    int ricochetWeapon = -1; // index into ricochetWeapons()
    int targetPlayer = -1;   // homing target, a player slot
    int targetDoor = -1;     // or a door
    int damageInterpolation = 0, speedInterpolation = 0;
    float speedDecayTime = 0, speed = 0, initialSpeed = 0, finalSpeed = 0, damageDirMag = 0;
    float ricochetLossH = 0, ricochetLossV = 0, cylinderRadius = 0;
    std::optional<size_t> model; // iceShard, for the Judicator
    int effect = -1;             // the EffectEntry that draws it (Magmaul, Omega Cannon, Volt Driver...)
    int collisionEffect = 255;   // WeaponInfo.CollisionEffects: an effect id + 3, or a beam effect model below 3
    uint32_t launchFrame = 0;    // ModLaunchFrame: the world the shot was fired in (online), a ricochet's its parent's
};

// The entities that change while playing, driven one 60 Hz tick at a time the
// way their C# Process methods do: the players, their shots, and the doors,
// jump pads, items and force fields.
class World {
    friend class PlayerAi; // the bots see the match the way PlayerAiData sees the Scene
    friend class NetGame;  // a match played on a server drives the World from the network
    friend class ServerGame; // and a server runs one for its clients
public:
    // A match this machine runs for a server's clients (ServerGame): every
    // player is somebody's input, and what the engine does to them goes back
    // out. Set, the players respawn as soon as they may (NetHooks.ForceSpawn)
    // and a slot nobody holds (PlayerSlot.active off) takes no part.
    struct Authority {
        std::function<void(size_t slot)> beforePlayer, afterPlayer; // around each player's step
        std::function<void(Player& shooter)> beginShot, endShot;    // around each shot fired (NetUnlagged)
        std::function<void(Player& victim, const DamageSource& source, bool died, uint32_t flags)> damage;
    };
    void setAuthority(Authority authority)
    {
        m_authority = std::move(authority);
        m_server = true;
    }
    bool server() const { return m_server; }
    World(Scene& scene, const std::filesystem::path& root, std::unique_ptr<RoomCollision> collision,
        const std::vector<Entity>& entities, int hunter);

    // Another hunter in the match, standing where it spawns and taking hits
    // (no AI yet). Returns its slot.
    int addPlayer(int hunter);
    // A bot (PlayerAi) in the match at `level` (0-3), playing `hunter`. Returns its slot.
    int addBot(int hunter, int level);
    // levels/nodeData for the room: the paths the bots follow. Without it they stand still.
    void loadNodeData(const std::filesystem::path& file);
    const NodeData* nodeData() const { return m_nodeData.get(); }
    PlayerAi* ai(size_t slot) { return m_slots.at(slot).ai.get(); }
    int roomId() const;
    void spawnPlayer(const Vec3& position, const Vec3& facing);
    // One 60 Hz tick. The main player moves only when `movePlayer` (walking mode).
    void tick(const PlayerInput& input, bool movePlayer);

    Player* player() { return m_slots[0].player.get(); }
    size_t playerCount() const { return m_slots.size(); }
    Player* player(size_t slot) { return m_slots.at(slot).player.get(); }
    const RoomCollision* collision() const { return m_collision.get(); }
    long long ticks() const { return m_ticks; }
    // Input for the other players (testing: FP_TARGET_SCRIPT); they stand still without it.
    void setOtherInput(std::function<PlayerInput(size_t slot, long long tick)> provider) { m_otherInput = std::move(provider); }
    // Shows the main player's own models (gun, biped, alt form) as its state asks.
    void setShowPlayer(bool show) { m_showPlayer = show; }
    // What the HUD shows that is not the player's: the radar's blips, the score.
    HudContext hudContext() const;
    // GameState.Setup: the mode and its defaults -- Battle to 7 points in 7
    // minutes, Survival with 2 spare lives for 15, Prime Hunter to a minute
    // and a half of prime time in 15. Before the first tick.
    void setMode(GameMode mode);
    GameMode mode() const { return m_mode; }
    // GameState.Teams: the team modes (Capture and the "Teams" ones). The
    // players are dealt round the teams (TeamRules.ChooseTeam with even
    // teams); Capture always has two.
    bool teams() const { return teamRules().teams; }
    int teamCount() const { return teamRules().teamCount; }
    void setTeamCount(int count);
    // GameState.FriendlyFire: teammates hurt each other (off by default).
    void setFriendlyFire(bool on) { teamRules().friendlyFire = on; }
    // GameState.TeamPoints, TeamKills, TeamDeaths, TeamTime.
    int teamPoints(int team) const { return team >= 0 && team < 16 ? m_teamPoints[team] : 0; }
    int teamKills(int team) const { return team >= 0 && team < 16 ? m_teamKills[team] : 0; }
    int teamDeaths(int team) const { return team >= 0 && team < 16 ? m_teamDeaths[team] : 0; }
    float teamTime(int team) const { return team >= 0 && team < 16 ? m_teamTime[team] : 0; }
    // Overrides: points that win (Battle) or spare lives (Survival), 0 for
    // none; the match length in seconds, 0 for no limit.
    void setRules(int pointGoal, float seconds);
    int pointGoal() const { return m_pointGoal; }
    // GameState.TimeGoal: the prime time (Prime Hunter) or ring time
    // (Defender) that wins, in seconds; 0 for none.
    void setTimeGoal(float seconds) { m_timeGoal = std::max(seconds, 0.0f); }
    // GameState.OctolithReset: a carrier's death sends the Octolith home at
    // once instead of dropping it (off, as the game's own default).
    void setOctolithReset(bool reset) { m_octolithReset = reset; }
    float timeGoal() const { return m_timeGoal; }
    // GameState.PrimeHunter: the slot of the Prime Hunter, -1 for none.
    int primeHunter() const { return m_primeHunter; }
    // Hands the Prime Hunter's role to `slot` (-1: nobody). The match does
    // it on kills; testing can start with one (FP_PRIME_HUNTER).
    void setPrimeHunter(int slot);
    MatchState matchState() const { return m_matchState; }
    // GameState.MatchTime: seconds left to play, then left in the current end
    // phase; negative without a time limit.
    float matchTime() const { return m_matchTime; }
    // The results screen has been up for its ten seconds.
    bool matchFinished() const { return m_matchState == MatchState::Ending && m_matchTime == 0; }
    // A new match in the same room: scores to zero, everybody respawned
    // (the main player back to the intro when there is one).
    void restartMatch();
    // CameraSequence.Intro: the room's fly-through. The match then starts
    // with the main player unspawned, watching it until FIRE (or the respawn
    // wait) spawns it; it comes back behind "GAME OVER" on a tie or a dead
    // winner, and behind the results.
    void setIntro(std::optional<CameraSequence> intro);
    // The intro's camera while it plays.
    std::optional<CameraView> introCamera() const;
    // CameraSequence.Current?.IsIntro while the match is on: the rules are up.
    bool introPlaying() const { return m_introActive && m_matchState == MatchState::InProgress; }
    // GameState.ResultSlots: the players best first; Standings: each one's rank, ties sharing it.
    const std::vector<int>& resultSlots() const { return m_resultSlots; }
    int standing(size_t slot) const { return m_slots.at(slot).standing; }
    // GameState.TeamStandings: the rank inside the player's team.
    int teamStanding(size_t slot) const { return m_slots.at(slot).teamStanding; }
    bool resultTie() const;
    // PlayerCamera.UpdateMatchEndCamera: while "GAME OVER" is up, the main
    // player watches the winner from the front; nothing on a tie or when the
    // winner is dead.
    std::optional<CameraPose> matchEndCamera() const;
    int points(size_t slot) const { return m_slots.at(slot).points; }
    int kills(size_t slot) const { return m_slots.at(slot).kills; }
    int deaths(size_t slot) const { return m_slots.at(slot).deaths; }
    // GameState.Time: how long a player has survived, -1 for "MAX"
    // (Survival), has been the Prime Hunter, or has held the ring (Defender).
    float survivalTime(size_t slot) const { return m_slots.at(slot).time; }
    // The modes' objects, for the bots and the HUD.
    const std::vector<OctolithFlag>& octolithFlags() const { return m_octolithFlags; }
    const std::vector<NodeDefense>& nodeDefenses() const { return m_nodeDefenses; }
    // Survival: out of spare lives, never to respawn.
    bool eliminated(size_t slot) const;
    size_t activeBeams() const;
    size_t activeBeams(size_t slot) const;
    Effects& effects() { return *m_effects; }
    // Online: the server runs the match. Nobody respawns or scores here, and
    // the health pickups are the server's; NetGame drives the rest. Before setMode.
    void setNetworked(bool networked) { m_networked = networked; }
    bool networked() const { return m_networked; }
    // Online: the network's say in each hit (every player, now and to come),
    // and the world-frame a shot fired by `slot` is aimed in (NetUnlagged.LaunchFrameFor).
    void setNetDamageHooks(NetDamageHooks* hooks);
    void setLaunchFrameSource(std::function<uint32_t(int slot)> source) { m_launchFrameFor = std::move(source); }
    // The suit a player wears (0-3) outside the team modes, which dress the teams.
    void setSuit(size_t slot, int suit);
    // A slot somebody plays (online: the server's roster holds it), and the name shown for it.
    bool slotActive(size_t slot) const { return slot < m_slots.size() && m_slots[slot].active; }
    std::string slotName(size_t slot) const;
    // What happened to or was done by the main player since the last call.
    std::vector<HudEvent> takeHudEvents() { return std::exchange(m_hudEvents, {}); }

private:
    // A beam trail's or particle's texture: a model's material, and its size.
    struct BeamTexture {
        const Model* model = nullptr;
        int textureId = -1, paletteId = -1;
        RepeatMode xRepeat = RepeatMode::Clamp, yRepeat = RepeatMode::Clamp;
        int width = 1, height = 1;
    };
    struct PlayerSlot {
        std::unique_ptr<Player> player;
        std::optional<size_t> gun, biped, alt, turret;
        int gunWeapon = -1;
        int points = 0, kills = 0, deaths = 0;
        int standing = 0, teamStanding = 0;
        int killStreak = 0; // GameState.KillStreak
        int recolor = 0;    // the slot's own suit, without teams
        float time = 0; // GameState.Time
        std::array<BeamProjectile, 16> beams{}; // EquipInfo.Beams (16, the game has 5)
        std::array<std::unique_ptr<SoundSource>, 16> beamSounds; // each beam's BeamProjectileEntity._soundSource
        std::array<int, 3> syluxBombs{-1, -1, -1}; // indices into m_bombs
        int syluxBombCount = 0;
        std::unique_ptr<PlayerAi> ai; // a bot's
        int spineNode = -1; // the biped's "Spine_1"
        std::vector<Mat4> bipedNodes; // the biped's node matrices last tick, in the world
        ModelInstance::Lights lights{};       // DynamicLightEntityBase: the player's
        ModelInstance::Lights turretLights{}; // and its Halfturret's
        bool turretLit = false;               // the Halfturret's took the player's when it came out
        bool active = true; // online: somebody holds this slot (off the scoreboard otherwise)
        std::string name;   // online: the player's name, from the server's roster
    };

    struct ForceField {
        ForceFieldObstacle obstacle;
        bool active;
    };
    // BombEntity: Samus's bombs, Kanden's stinglarva, Sylux's Lockjaw.
    struct Bomb {
        bool active = false;
        int type = BombType::MorphBall;
        int owner = -1;
        int index = 0; // BombIndex, among the owner's Lockjaw bombs
        bool exploding = false, exploded = false;
        Vec3 position{};
        Mat4 transform = Mat4::identity();
        int countdown = 0;
        float radius = 0, selfRadius = 0;
        int damage = 0;
        int target = -1; // a player slot
        bool targetTurret = false; // that player's Halfturret
        Vec3 speed{};
        std::optional<size_t> instance; // the stinglarva's model
        unsigned long long visualTick = 0;
        Effects::Handle effect = -1;
        std::shared_ptr<SoundSource> sound = std::make_shared<SoundSource>();
    };

    // BeamEffectEntity: the ice wave and the Imperialist's beam, as models.
    struct BeamEffect {
        size_t instance;
        int lifespan = 0; // ticks
    };

    PlayerSlot& createSlot(int hunter);
    void processDoors();
    void processJumpPads();
    void processTeleporters();
    void processMorphCameras();
    // DynamicLightEntityBase.UpdateLightSources: `lights` drift toward the
    // light sources `position` is in, else the room's.
    void updateLights(ModelInstance::Lights& lights, const Vec3& position) const;
    ModelInstance::Lights roomLights() const;
    void processItems();
    void processPlayers(const PlayerInput& input, bool movePlayer);
    void respawn(PlayerSlot& slot);
    // GameState.ProcessFrame and UpdateTime, for a battle.
    void processMatchState();
    // GameState.EnsureIntroCamSeq
    void ensureIntro();
    // Everything that happens in a tick while the match is on.
    void simulate(const PlayerInput& input, bool movePlayer);
    // GameState.UpdateSurvival: the survivors' time, the end with one left.
    void updateSurvival();
    // GameState.ModeStatePrimeHunter: the Prime Hunter loses a point of
    // energy every 20 ticks and counts prime time toward the goal.
    void updatePrimeHunter();
    // PlayerHud.ProcessModeHud: the arrows toward what the main player should find.
    void addLocators(HudContext& context) const;

    // AreaVolumes.cpp: AreaVolumeEntity.Process for the players.
    void processAreaVolumes();
    void areaVolumeTrigger(AreaVolume& volume, Player& player);
    void areaVolumeInside(AreaVolume& volume, Player& player);
    void areaVolumeExit(AreaVolume& volume, Player& player);
    bool prioritizeGravity(AreaVolume& volume, const Vec3& position, int slot);
    void sendAreaMessage(uint32_t message, int param1, Player& player);

    // Modes.cpp: the objects of Capture, Bounty, Nodes and Defender.
    // The OctolithFlag, FlagBase and NodeDefense entities the mode uses, with their models.
    void setUpModeEntities();
    void processModeEntities();
    void processOctolithFlag(OctolithFlag& flag);
    bool octolithTouched(OctolithFlag& flag, Player& player);
    void octolithAtBase(OctolithFlag& flag);
    void octolithReset(OctolithFlag& flag);
    void octolithDropped(OctolithFlag& flag, bool reset);
    void octolithCaptured(OctolithFlag& flag);
    void processFlagBase(FlagBase& base);
    void processDefender(NodeDefense& defense);
    void processNodes(NodeDefense& defense);
    // Complete: `value1` 4 when the main player's team lost it, 2 when it took it, else `value2` 1.
    void completeNode(NodeDefense& defense, int& value1, int& value2);
    void updateModeModels();
    // PlayerHud.ProcessHudBounty, Capture, Defender, Nodes: the arrows toward the objectives.
    void addModeLocators(HudContext& context) const;
    // NodeDefenseEntity.GetDrawInfo: a node's color for the main player.
    std::array<float, 3> nodeColor(const NodeDefense& defense) const;
    // Metadata.TeamColors / TeamVisuals.ObjectiveColor; white for no team.
    static std::array<float, 3> teamColor(int team);
    void resetModeEntities();
    // QueueHudMessage for the main player: a message of Text/HudMessagesMP.
    void mainMessage(int messageId, float duration, int category, float y = 133, bool red = false);
    bool octolithMode() const { return m_mode == GameMode::Capture || m_mode == GameMode::Bounty || m_mode == GameMode::BountyTeams; }
    bool nodesMode() const { return m_mode == GameMode::Nodes || m_mode == GameMode::NodesTeams; }
    bool survivalMode() const { return m_mode == GameMode::Survival || m_mode == GameMode::SurvivalTeams; }
    bool defenderMode() const { return m_mode == GameMode::Defender || m_mode == GameMode::DefenderTeams; }
    // GameState.UpdateState: the teams' totals, then the standings.
    void updateState();
    // PlayerProcess, Survival: a player that stays near where it spawned is
    // shown to everybody after ten seconds (five with two players).
    void processHiding(Player& player);
    // PlayerProcess.GetTimeUntilRespawn: ticks before a dead player respawns without pressing FIRE.
    int timeUntilRespawn(const Player& player) const;
    // GameState.UpdateStandings with ComparePlayers.
    void updateStandings();
    int comparePlayers(size_t slot1, size_t slot2) const;
    int compareTeams(int team1, int team2) const;
    // TeamRules.ChooseTeam for even teams, and TeamVisuals.Apply: the suit colors.
    void assignTeams();
    void applyTeamVisuals(PlayerSlot& slot);
    void onDamage(Player& victim, const DamageSource& source, bool died, uint32_t flags);
    void updatePlayerModels(PlayerSlot& slot, bool main);
    void updateTurretModel(PlayerSlot& slot);
    void animateBiped(PlayerSlot& slot);
    void dropItem(ItemType type, const Vec3& position, int despawnTime);
    void drawDeathParticles();
    // Renderer.AddSingleParticle: a camera-facing quad of one texture.
    void addSingleParticle(const BeamTexture& tex, const Vec3& position, const Vec3& color, float alpha, float scale);

    // Bombs.cpp
    bool spawnBomb(Player& owner, const Mat4& transform);
    void processBombs();
    bool processBomb(size_t index);
    void lockjawCheckTargeting(Bomb& bomb, Player& player, int& hitPlayer, bool& hitTurret);
    bool lockjawCheckSnare(const Bomb& bomb, const Vec3& position) const;
    void processBombTargeting(Bomb& bomb);
    void destroyBomb(size_t index);
    void drawBombs();
    void drawEffects();

    // Beams.cpp
    int spawnBeam(int owner, EquipInfo& equip, const WeaponInfo& weapon, const Vec3& position, const Vec3& direction, int spawnFlags,
        const BeamProjectile* parent);
    void processBeams();
    bool processBeam(BeamProjectile& beam);
    void checkBeamCollision(BeamProjectile& beam);
    void onBeamCollision(BeamProjectile& beam, const CollisionResult& result, int hitPlayer);
    void processRicochet(BeamProjectile& beam, const CollisionResult& result);
    void checkSplashDamage(BeamProjectile& beam, int hitPlayer);
    void spawnIceWave(BeamProjectile& beam, float chargePct, const WeaponInfo& weapon);
    void spawnBeamEffect(const char* model, const Mat4& transform);
    void spawnCollisionEffect(const BeamProjectile& beam, const CollisionResult& res, bool noSplat);
    void endBeamEffect(BeamProjectile& beam);
    Vec3 damageDirection(const BeamProjectile& beam, const Vec3& beamPos, const Vec3& targetPos) const;
    void damagePlayer(Player& victim, BeamProjectile& beam, int damage, uint32_t flags, const Vec3& direction);
    void processBeamEffects();
    void drawBeams();
    // BeamProjectileEntity's sounds.
    SoundSource& beamSound(const BeamProjectile& beam);
    void playBeamHitSfx(BeamProjectile& beam);
    void playRicochetSfx(BeamProjectile& beam);
    void stopHomingSfx(BeamProjectile& beam);

    Scene& m_scene;
    std::filesystem::path m_root;
    std::unique_ptr<RoomCollision> m_collision;
    std::vector<PlayerSlot> m_slots;
    std::vector<Player*> m_players; // the same, for Player::setPlayers
    std::vector<PlayerSpawn> m_spawns;
    std::vector<int> m_spawnCooldowns;
    std::vector<Door> m_doors;
    std::vector<JumpPad> m_jumpPads;
    std::vector<Teleporter> m_teleporters;
    std::vector<LightSource> m_lightSources;
    std::vector<MorphCamera> m_morphCameras;
    std::vector<ItemSpawn> m_items;
    std::vector<ItemInstance> m_droppedItems;
    std::vector<AreaVolume> m_areaVolumes;
    std::vector<Entity> m_modeEntities; // OctolithFlag, FlagBase and NodeDefense, set up with the mode
    std::vector<OctolithFlag> m_octolithFlags;
    std::vector<FlagBase> m_flagBases;
    std::vector<NodeDefense> m_nodeDefenses;
    bool m_octolithReset = false;
    std::array<float, 16> m_teamTime{}; // GameState.TeamTime: Defender's ring time, Survival's best, by team
    std::array<int, 16> m_teamPoints{}, m_teamKills{}, m_teamDeaths{};
    std::array<int, 16> m_voicePoints{}, m_voiceDeaths{}; // the totals updateState last saw, for the voices
    float m_lastAlarmTime = 0;
    bool m_tempoChanged = false;
    int m_nextAlarmIndex = 0;
    std::unique_ptr<NodeData> m_nodeData;
    std::unique_ptr<AiPersonality> m_aiPersonality;
    AiGlobalState m_aiGlobals;
    void processBots(); // the ammo killed players drop, a few of each kind
    std::vector<ForceField> m_forceFields;
    std::vector<BeamEffect> m_beamEffects;
    std::vector<Bomb> m_bombs;
    std::unique_ptr<Effects> m_effects;
    std::vector<HudEvent> m_hudEvents;
    std::vector<size_t> m_ownedInstances; // animated by their owners, not by tick()
    long long m_ticks = 0;
    GameMode m_mode = GameMode::Battle;
    bool m_radarPlayers = false; // GameState.RadarPlayers: the last two survivors face off
    MatchState m_matchState = MatchState::InProgress;
    int m_pointGoal = 7;
    float m_timeGoal = 0;
    int m_primeHunter = -1;
    float m_matchTime = 7 * 60;
    float m_matchLength = 7 * 60;
    long long m_matchEndTick = 0; // m_globalTicks when "GAME OVER" came up
    long long m_globalTicks = 0;  // GlobalElapsedTime: counts on after the match ends
    std::vector<int> m_resultSlots;
    std::optional<CameraSequence> m_intro;
    bool m_introActive = false;
    long long m_introStartTick = 0; // m_globalTicks when it started
    bool m_showPlayer = false;
    bool m_networked = false;
    NetDamageHooks* m_netHooks = nullptr;
    int m_hitMarkerTimer = 0; // NetHitPrediction's mark: frames left
    std::function<uint32_t(int slot)> m_launchFrameFor;
    bool m_server = false;
    Authority m_authority;
    std::function<PlayerInput(size_t slot, long long tick)> m_otherInput;
    BeamTexture beamTexture(const Model* model, int materialId) const;
    BeamTexture m_trail, m_electroTrail, m_arcWelder, m_arcWelder1, m_fuzzball, m_deathParticle;
};

} // namespace fp
