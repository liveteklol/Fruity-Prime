#pragma once

#include "Player.h"
#include "formats/HudAssets.h"
#include "formats/Metadata.h"
#include "formats/Rooms.h"
#include "render/Overlay.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fp {

// What TakeDamage tells the main player's HUD (QueueHudMessage): kills,
// deaths and headshots. `hunter` is the other player's.
struct HudEvent {
    enum Type {
        Headshot, KilledBy, HeadshotKilledBy, SelfDestructed, YouKilled, YourHeadshotKilled,
        NewPrimeHunter, // `hunter` is the new Prime Hunter
        PrimeHunterDead,
        KilledTeammate, // `hunter` is the teammate
        KillStreak,     // five kills in a row: the main player's (hunter -1) or `hunter`'s
        Message,       // QueueHudMessage(128, y, duration, category, messageId)
        ClearMessages, // ClearHudMessage(category)
    } type;
    int hunter = -1;
    int beam = -1; // what killed the main player, -1 for none
    // "(what killed you)": a WeaponNames.bin entry ('W' weapons, 'A' alt
    // attacks) or a HUD message ('M'); 0 for none.
    char killedByTable = 0;
    int killedById = 0;
    // Message: a HudMessagesMP entry, where and for how long; red instead of the usual green.
    int messageId = 0;
    float y = 133, duration = 0;
    int category = 0;
    bool red = false;
};

// GameState.MatchState: playing, then three seconds on the winner ("GAME
// OVER"), then the results.
enum class MatchState { InProgress, GameOver, Ending };

// Everything the HUD reads that is not the player's own.
struct HudContext {
    struct Blip {
        Vec3 position;
        bool weapon;
    };
    std::vector<Blip> items;
    int points = 0;
    int pointGoal = 7;
    float time = 0;      // the main player's GameState.Time (prime time)
    float timeGoal = 0;  // GameState.TimeGoal
    int primeHunter = -1; // GameState.PrimeHunter, a slot
    GameMode mode = GameMode::Battle;
    MatchState matchState = MatchState::InProgress;
    float matchTime = -1;       // GameState.MatchTime
    float elapsed = 0;          // Scene.ElapsedTime: stops with the match
    bool showScoreboard = false; // the main player holding the scoreboard button
    int respawnWait = 0;         // ticks before the main player respawns without FIRE
    bool intro = false;          // the room's intro is playing (CameraSequence.Current?.IsIntro)
    float introTime = 0;         // seconds since it started, for the rules' typing
    bool eliminated = false;     // Survival: the main player is out of lives
    bool faceOff = false;        // Survival: two survivors left (GameState.RadarPlayers)
    bool revealed = false;       // Survival: the main player hid too long (RadarReveal)
    bool cowardDetected = false; // Survival: another player was just revealed
    // The scoreboard's rows, best first (GameState.ResultSlots).
    struct Score {
        int slot;
        int hunter;
        int points, kills, deaths;
        float time; // GameState.Time, -1 for MAX
        int team = 0;
        int standing = 0;
        std::string name; // online, from the server's roster; "PlayerN" otherwise
    };
    std::vector<Score> scores;
    // Online: the chat's last lines, and a line about the connection when it has trouble.
    std::vector<std::string> chat;
    std::string netStatus;
    // GameState.Teams: the teams' totals, by team.
    bool teams = false;
    int teamCount = 2;
    struct TeamScore {
        int points = 0, kills = 0, deaths = 0;
        float time = 0;
    };
    std::array<TeamScore, 4> teamScores{};
    int deaths = 0;                // the main player's team's deaths (Survival's lives)
    bool teamCarryingOctolith = false; // a teammate carries an Octolith (Capture)
    // AddLocatorInfo: arrows toward players and the modes' objects, in the world.
    struct Locator {
        Vec3 position;
        std::array<float, 3> color;
        float alpha;
        enum Kind { Player, Node, Octolith } kind = Player; // hud_icon_player, _nodes, _octolith
    };
    std::vector<Locator> locators;
    int teamIndex = 0;             // the main player's TeamIndex
    bool carryingOctolith = false; // the main player's PlayerEntity.OctolithFlag
    // Nodes: every node in the room's order, for the icons and the bonuses.
    struct Node {
        int currentTeam = -1, occupyingTeam = -1; // NodeDefenseEntity.NoTeam: -1
        bool blinking = false;
        bool occupiedByMain = false; // OccupiedBy[main slot]
        float progress = 0;          // seconds of the ten it takes
    };
    std::vector<Node> nodes;
    // The camera the scene is drawn with, for placing the locators on screen.
    CameraPose camera{};
    float fovY = 78;
    float hitMarker = 0; // the hit mark's opacity, 0 for none
};

// PlayerHud (the stock helmet HUD) and PlayerEntityProHud (Pro mode) for the
// main player in a multiplayer battle, drawn in the DS's 256x192 HUD space
// the way the C# lays it out and scaled to the window.
class Hud {
public:
    Hud(const std::filesystem::path& root, int hunter);

    void setProMode(bool pro) { m_pro = pro; }
    bool proMode() const { return m_pro; }
    // UpdateHud's per-tick parts: the meters sliding between biped and alt
    // form, message lifetimes, and "press FIRE to respawn" while dead.
    void tick(const Player& player, const HudContext& context);
    void onEvent(const HudEvent& event);
    // PlayerHud.QueueHudMessage: a line of text centered on (x, y) in HUD
    // space for `duration` seconds. Category bit 0 blinks; messages sharing
    // one of bits 1-3 stack, the newest at the bottom.
    void queueMessage(float x, float y, float duration, int category, const std::string& text, bool red = false);
    void build(const Player& player, const HudContext& context, int width, int height, HudDrawList& out);

    // Texture 0 is a single white texel, for flat fills. Images are never
    // changed once added, so the renderer uploads each id once.
    const std::vector<Image>& textures() const { return m_textures; }

private:
    enum class Align { Left = 0, Right = 1, Center = 2 };
    struct Sprite {
        HudObject object;
        std::map<int, int> textures; // palette (-1: white mask) -> texture
        int columns = 1;
    };
    struct Color {
        float r, g, b, a;
    };

    Sprite* sprite(const std::string& file);
    int spriteTexture(Sprite& sprite, int palette);
    int fontTexture(int palette);
    int addTexture(Image image);

    // Renderer.DrawHudObject: position in fractions of the window, size by mode
    // (0: from the width, 1: from the height, 2: stretched with it).
    void drawSprite(Sprite& sprite, int frame, int palette, float posX, float posY, int mode = 0, float scale = 1,
        bool center = false, float alpha = 1, const Color* tint = nullptr, bool linear = false);
    void drawLayer(int texture, float alpha, float scaleX, float scaleY, float shiftX = 0, float shiftY = 0);
    void flatBox(float left, float top, float right, float bottom, const Color& color);
    void flatTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& color);
    void flatQuad(const float (&x)[4], const float (&y)[4], const Color& color);
    void quad(int texture, bool linear, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
        uint32_t color);
    // PlayerHud.DrawText2D; returns where the run ended.
    std::pair<float, float> drawText(float x, float y, Align align, int palette, const std::string& text,
        const Color* color = nullptr, float alpha = 1, float scale = 1, float spacingY = 12);
    float textWidth(const std::string& text) const;

    void drawMeter(float x, float y, int baseAmount, int curAmount, int palette, const HudMeterInfo& meter,
        Sprite& bar, int tankAmount, bool drawText, float alpha);
    void drawHealthbars(const Player& player);
    void drawChat(const HudContext& context);
    void drawAmmoBar(const Player& player);
    void drawBoostBombs(const Player& player);
    void drawModeScore(const HudContext& context);
    void drawWeaponList(const Player& player);
    void drawCustomCrosshair(const Player& player);
    // Renderer.DrawHitMarker: four white bars in an X around the crosshair.
    void drawHitMarker(float alpha);
    void drawRadar(const Player& player, const HudContext& context);
    void drawProHud(const Player& player, const HudContext& context);
    void proBar(float x, float y, float width, float height, float fill, const Color& color);
    void proNumber(float x, float y, Align align, const std::string& text, const Color& color, float scale);
    void drawMessages();
    void drawDamageIndicators(const Player& player);
    // DrawLocatorIcon: the icon over what it points at, or an arrow at the
    // edge of the view toward it.
    void drawLocators(const HudContext& context);
    void drawIconModel(const Model& model, int texture, float x, float y, float angle, const std::array<float, 3>& color, float alpha);
    // ProcessHudPrimeHunter / DrawHudPrimeHunter: the main player's icon while it is the Prime Hunter.
    void drawPrimeHunter();
    // DrawHudBounty / DrawHudCapture: the Octolith icon while carrying it.
    void drawOctolith(const HudContext& context);
    // ProcessHudNodes: the bonuses and the capture bar's state.
    void processNodes(const HudContext& context);
    // DrawHudNodes: the bonuses, a row of node icons, the capture bar.
    void drawNodes(const HudContext& context);
    void drawNodesFfa(const HudContext& context, Sprite* icons, const std::string& bonus);
    void drawNodeProgress();
    void clearMessages(int mask);
    bool messageQueued(int mask) const;
    // Renderer.DrawHudFilterModel: the "filter" model's texture over the whole screen.
    void drawFilter(float alpha);
    void drawMatchTime(const HudContext& context);
    void drawScoreboard(const HudContext& context);
    // PlayerEntityTeamScoreboard: the teams with their players under them.
    void drawTeamScoreboard(const HudContext& context);
    std::string modeScore(const HudContext& context) const;
    // LoadModeRules / DrawModeRules: the mode's rules typed out over the intro.
    void loadModeRules(GameMode mode);
    void drawModeRules(const HudContext& context);
    // WrapText: breaks at the last space before `maxWidth` DS units, else after the character that overran.
    std::string wrapText(const std::string& text, int maxWidth, int& lines) const;
    static int modeScoreLabel(GameMode mode);

    struct Message {
        float x = 0, y = 0, lifetime = 0;
        int category = 0;
        std::string text;
        bool red = false;
    };
    std::array<Message, 20> m_messages;
    long long m_ticks = 0;

    float aspectFix() const { return m_height / 192.0f * (256.0f / m_width); }
    float px(float hudX) const { return hudX / 256.0f * m_width; }
    float py(float hudY) const { return hudY / 192.0f * m_height; }

    std::filesystem::path m_root;
    [[maybe_unused]] Hunter m_hunter;
    const HudHunterObjects& m_objects;
    const HudMeterInfo& m_mainMeter;
    const HudMeterInfo& m_subMeter;
    const HudMeterInfo& m_ammoMeter;
    HudStrings m_strings;
    HudFont m_font;
    std::vector<Image> m_textures;
    std::map<std::string, std::unique_ptr<Sprite>> m_sprites;
    std::map<int, int> m_fontTextures;
    std::vector<uint32_t> m_textPalette; // the ammo bar's palettes, which HUD text is drawn in
    int m_helmetTexture = -1, m_helmetDropTexture = -1, m_visorTexture = -1;
    std::array<float, 4> m_iconBounds[9]{}; // ModIconBounds per weapon: min x, min y, max x, max y
    bool m_pro = false;
    std::unique_ptr<Model> m_damageModel; // hud/damage: the arrows around the screen's edge
    int m_damageTexture = -1;
    int m_filterTexture = -1;
    std::unique_ptr<Model> m_playerLocator, m_arrowLocator; // hud_icon_player, hud_icon_arrow
    std::unique_ptr<Model> m_nodeLocator, m_octolithLocator; // hud_icon_nodes, hud_icon_octolith
    int m_playerLocatorTexture = -1, m_arrowLocatorTexture = -1, m_nodeLocatorTexture = -1, m_octolithLocatorTexture = -1;
    // ProcessHudNodes
    int m_nodesHudState = 0, m_nodesProgressAmount = 0;
    int m_nodeBonusOpponent = -1;
    bool m_mainNodeBonus = false;
    std::array<int, 16> m_teamNodeCounts{};
    struct RulesLine {
        std::string text;
        int offset = 0;
        int length = 0;   // characters up to the end of this line, the header counting as 30
        int newlines = 0; // lines the wrapping added
    };
    std::vector<RulesLine> m_rules;
    std::optional<GameMode> m_rulesMode;
    int m_prevScrollingChars = 0;
    bool m_isPrimeHunter = false;
    long long m_primeHunterStart = 0; // m_ticks when the main player became the Prime Hunter
    float m_filterAlpha = 1; // its material's
    float m_healthbarYOffset = 0;
    float m_boostBombsYOffset = 208;

    // Per build.
    HudDrawList* m_out = nullptr;
    float m_width = 256, m_height = 192;
};

} // namespace fp
