#include "Hud.h"
#include "PlayerAiUtil.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace fp {

namespace {

uint32_t packColor(float r, float g, float b, float a)
{
    auto c = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}

// PlayerInput._weaponOrder: the order the weapons cycle in.
constexpr int kWeaponOrder[9] = {0, 2, 1, 3, 4, 5, 6, 7, 8};

// PlayerHud._weaponListColors, by beam.
constexpr uint8_t kWeaponListColors[9][3] = {
    {0xF8, 0x28, 0x28}, {0xF8, 0xF8, 0x08}, {0xF8, 0x28, 0x28}, {0x20, 0xC0, 0x20}, {0xD0, 0x18, 0x18},
    {0x98, 0x38, 0xC0}, {0xF8, 0xB0, 0x18}, {0x50, 0x98, 0xD0}, {0xD0, 0xD0, 0xD0},
};

// PlayerEntityProHud's colours.
constexpr float kProWarn = 60 / 99.0f;
constexpr float kProDanger = 33 / 99.0f;

// TeamVisuals: each team's label color (Color) and objective color (ObjectiveColor, Metadata.TeamColors).
constexpr std::array<std::array<float, 3>, 4> kTeamLabelColors{{
    {1.0f, 156 / 255.0f, 0}, {0, 1.0f, 0}, {41 / 255.0f, 156 / 255.0f, 1.0f}, {222 / 255.0f, 66 / 255.0f, 1.0f}}};
constexpr std::array<std::array<float, 3>, 4> kTeamObjectiveColors{{
    {1.0f, 19 / 31.0f, 0}, {0, 1.0f, 0}, {5 / 31.0f, 19 / 31.0f, 1.0f}, {27 / 31.0f, 8 / 31.0f, 1.0f}}};

std::string twoDigits(int value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

} // namespace

Hud::Hud(const std::filesystem::path& root, int hunter)
    : m_root(root)
    , m_hunter(static_cast<Hunter>(hunter))
    , m_objects(hudHunterObjects().at(hunter))
    , m_mainMeter(hudMainHealthbars().at(hunter))
    , m_subMeter(hudSubHealthbars().at(hunter))
    , m_ammoMeter(hudAmmoBars().at(hunter))
    , m_strings(root)
{
    Image white;
    white.width = white.height = 1;
    white.rgba = {0xFFFFFFFFu};
    addTexture(std::move(white));
    m_font = loadFont(root);
    if (!m_font.valid()) {
        qWarning("HUD: no font in %s/_bin/arm9.bin", root.string().c_str());
    }
    try {
        m_helmetTexture = addTexture(loadCharMap(root, m_objects.Helmet));
        m_helmetDropTexture = addTexture(loadCharMap(root, m_objects.HelmetDrop));
        m_visorTexture = addTexture(loadCharMap(root, m_objects.Visor, 0, 0, 0, 32));
    } catch (const std::exception& e) {
        qWarning("HUD helmet: %s", e.what());
    }
    if (Sprite* ammo = sprite(m_objects.AmmoBar)) {
        m_textPalette = ammo->object.palette;
    }
    try {
        // FrontendMeta.HudModels: "damage", model and textures in one file.
        static const ModelMetadata damage{"damage", "hud/damage_Model.bin", nullptr, nullptr,
            {{"default", "hud/damage_Model.bin", "hud/damage_Model.bin", "hud/damage_Model.bin", nullptr, {}}}};
        m_damageModel = std::make_unique<Model>(Model::load(root, damage));
        if (qEnvironmentVariableIsSet("FP_DEBUG_HUD")) {
            const Model& m = *m_damageModel;
            qInfo("damage model: scale %.3f, %zu nodes, %zu meshes", m.scale(), m.nodes().size(), m.meshes().size());
            for (const Node& node : m.nodes()) {
                if (node.meshCount <= 0) {
                    qInfo("  node %s: no mesh", node.name.c_str());
                    continue;
                }
                const DrawRange& r = m.dlistRange(m.meshes()[m.firstMesh(node)].dlistId);
                float minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
                for (uint32_t k = 0; k < r.vertexCount; k++) {
                    const Vertex& vert = m.vertices()[r.firstVertex + k];
                    minX = std::min(minX, vert.pos[0]);
                    maxX = std::max(maxX, vert.pos[0]);
                    minY = std::min(minY, vert.pos[1]);
                    maxY = std::max(maxY, vert.pos[1]);
                }
                qInfo("  node %s: %u vertices, x %.2f..%.2f y %.2f..%.2f", node.name.c_str(), r.vertexCount, minX, maxX, minY, maxY);
            }
        }
        if (!m_damageModel->materials().empty()) {
            const Material& material = m_damageModel->materials()[0];
            m_damageTexture = addTexture(m_damageModel->decodeTexture(material.textureId, material.paletteId));
        }
    } catch (const std::exception& e) {
        qWarning("HUD damage indicator: %s", e.what());
        m_damageModel.reset();
    }
    // FrontendMeta.HudModels: the locators, model and texture in one file each.
    auto loadIcon = [&](const char* name, const char* path, std::unique_ptr<Model>& model, int& texture) {
        try {
            const ModelMetadata meta{name, path, nullptr, nullptr, {{"default", path, path, path, nullptr, {}}}};
            model = std::make_unique<Model>(Model::load(root, meta));
            if (model->materials().empty() || model->meshes().empty()) {
                model.reset();
                return;
            }
            const Material& material = model->materials()[0];
            texture = addTexture(model->decodeTexture(material.textureId, material.paletteId));
        } catch (const std::exception& e) {
            qWarning("HUD %s: %s", name, e.what());
            model.reset();
        }
    };
    loadIcon("hud_icon_player", "hud/hud_icon_player_Model.bin", m_playerLocator, m_playerLocatorTexture);
    loadIcon("hud_icon_arrow", "hud/hud_icon_arrow_Model.bin", m_arrowLocator, m_arrowLocatorTexture);
    loadIcon("hud_icon_nodes", "hud/hud_icon_nodes_Model.bin", m_nodeLocator, m_nodeLocatorTexture);
    loadIcon("hud_icon_octolith", "hud/hud_icon_octolith_Model.bin", m_octolithLocator, m_octolithLocatorTexture);
    if (const ModelMetadata* filter = findModel("filter")) {
        // PlayerHud.SetUpHud: the screen filter behind the scoreboard.
        try {
            const Model model = Model::load(root, *filter);
            if (!model.materials().empty()) {
                const Material& material = model.materials()[0];
                m_filterTexture = addTexture(model.decodeTexture(material.textureId, material.paletteId));
                m_filterAlpha = material.alpha;
            }
        } catch (const std::exception& e) {
            qWarning("HUD filter: %s", e.what());
        }
    }
    if (Sprite* list = sprite(m_objects.WeaponSelect)) {
        const HudObject& o = list->object;
        for (int i = 0; i < 9; i++) {
            int minX = o.width, minY = o.height, maxX = -1, maxY = -1;
            for (int y = 0; y < o.height; y++) {
                for (int x = 0; x < o.width; x++) {
                    if (i < o.frameCount() && o.index(i, x, y) != 0) {
                        minX = std::min(minX, x);
                        maxX = std::max(maxX, x);
                        minY = std::min(minY, y);
                        maxY = std::max(maxY, y);
                    }
                }
            }
            if (maxX < minX) {
                minX = minY = 0;
                maxX = o.width - 1;
                maxY = o.height - 1;
            }
            m_iconBounds[i] = {float(minX), float(minY), float(maxX), float(maxY)};
        }
    }
    m_healthbarYOffset = m_objects.HealthOffsetY;
}

int Hud::addTexture(Image image)
{
    m_textures.push_back(std::move(image));
    return static_cast<int>(m_textures.size()) - 1;
}

Hud::Sprite* Hud::sprite(const std::string& file)
{
    auto it = m_sprites.find(file);
    if (it == m_sprites.end()) {
        std::unique_ptr<Sprite> loaded;
        try {
            loaded = std::make_unique<Sprite>();
            loaded->object = loadHudObject(m_root, file);
            loaded->columns = std::clamp(loaded->object.frameCount(), 1, 16);
        } catch (const std::exception& e) {
            qWarning("HUD %s: %s", file.c_str(), e.what());
            loaded.reset();
        }
        it = m_sprites.emplace(file, std::move(loaded)).first;
    }
    return it->second.get();
}

// Every frame of a sheet in one texture, a grid `columns` wide, in one palette
// (or as a white mask for DrawText2D's and SmoothHudIcon's flat colours).
int Hud::spriteTexture(Sprite& s, int palette)
{
    if (auto it = s.textures.find(palette); it != s.textures.end()) {
        return it->second;
    }
    const HudObject& o = s.object;
    const int frames = std::max(o.frameCount(), 1);
    const int rows = (frames + s.columns - 1) / s.columns;
    Image image;
    image.width = s.columns * o.width;
    image.height = rows * o.height;
    image.opaque = false;
    image.rgba.assign(static_cast<size_t>(image.width) * image.height, 0);
    for (int f = 0; f < o.frameCount(); f++) {
        const int ox = f % s.columns * o.width;
        const int oy = f / s.columns * o.height;
        for (int y = 0; y < o.height; y++) {
            for (int x = 0; x < o.width; x++) {
                const uint8_t index = o.index(f, x, y);
                if (index == 0) {
                    continue;
                }
                uint32_t color = 0xFFFFFFFFu;
                if (palette >= 0) {
                    const size_t p = palette * 16 + index;
                    color = p < o.palette.size() ? o.palette[p] : 0xFFFF00FFu;
                }
                image.rgba[(oy + y) * image.width + ox + x] = color;
            }
        }
    }
    const int texture = addTexture(std::move(image));
    s.textures[palette] = texture;
    return texture;
}

int Hud::fontTexture(int palette)
{
    if (auto it = m_fontTextures.find(palette); it != m_fontTextures.end()) {
        return it->second;
    }
    const int glyphs = std::max(m_font.glyphCount(), 1);
    Image image;
    image.width = 32 * 8;
    image.height = (glyphs + 31) / 32 * 8;
    image.opaque = false;
    image.rgba.assign(static_cast<size_t>(image.width) * image.height, 0);
    for (int g = 0; g < m_font.glyphCount(); g++) {
        for (int i = 0; i < 64; i++) {
            const uint8_t index = m_font.indices[g * 64 + i];
            if (index == 0) {
                continue;
            }
            uint32_t color = 0xFFFFFFFFu;
            if (palette >= 0) {
                const size_t p = palette * 16 + index;
                color = p < m_textPalette.size() ? m_textPalette[p] : 0xFFFFFFFFu;
            }
            image.rgba[(g / 32 * 8 + i / 8) * image.width + g % 32 * 8 + i % 8] = color;
        }
    }
    const int texture = addTexture(std::move(image));
    m_fontTextures[palette] = texture;
    return texture;
}

void Hud::quad(int texture, bool linear, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
    uint32_t color)
{
    auto& batches = m_out->batches;
    auto& v = m_out->vertices;
    if (batches.empty() || batches.back().texture != texture || batches.back().linear != linear) {
        batches.push_back({texture, linear, static_cast<uint32_t>(v.size()), 0});
    }
    const HudVertex a{x0, y0, u0, v0, color}, b{x1, y0, u1, v0, color}, c{x0, y1, u0, v1, color}, d{x1, y1, u1, v1, color};
    v.insert(v.end(), {a, b, c, b, d, c});
    batches.back().vertexCount += 6;
}

void Hud::flatBox(float left, float top, float right, float bottom, const Color& color)
{
    quad(0, false, px(left), py(top), px(right), py(bottom), 0, 0, 1, 1, packColor(color.r, color.g, color.b, color.a));
}

void Hud::flatTriangle(float x0, float y0, float x1, float y1, float x2, float y2, const Color& color)
{
    auto& batches = m_out->batches;
    auto& v = m_out->vertices;
    if (batches.empty() || batches.back().texture != 0) {
        batches.push_back({0, false, static_cast<uint32_t>(v.size()), 0});
    }
    const uint32_t c = packColor(color.r, color.g, color.b, color.a);
    v.insert(v.end(), {HudVertex{x0, y0, 0, 0, c}, HudVertex{x1, y1, 0, 0, c}, HudVertex{x2, y2, 0, 0, c}});
    batches.back().vertexCount += 3;
}

void Hud::flatQuad(const float (&x)[4], const float (&y)[4], const Color& color)
{
    flatTriangle(x[0], y[0], x[1], y[1], x[2], y[2], color);
    flatTriangle(x[1], y[1], x[3], y[3], x[2], y[2], color);
}

void Hud::drawSprite(Sprite& s, int frame, int palette, float posX, float posY, int mode, float scale, bool center,
    float alpha, const Color* tint, bool linear)
{
    const HudObject& o = s.object;
    if (o.frameCount() == 0) {
        return;
    }
    frame = std::clamp(frame, 0, o.frameCount() - 1);
    float width = static_cast<float>(o.width);
    float height = static_cast<float>(o.height);
    if (mode == 2) {
        width = width / 256 * m_width;
        height = height / 192 * m_height;
    } else if (mode == 1) {
        const float aspect = height / width;
        height = height / 192 * m_height;
        width = height / aspect;
    } else {
        const float aspect = width / height;
        width = width / 256 * m_width;
        height = width / aspect;
    }
    width *= scale;
    height *= scale;
    const float left = posX * m_width - (center ? width / 2 : 0);
    const float top = posY * m_height - (center ? height / 2 : 0);
    const int texture = spriteTexture(s, tint ? -1 : palette);
    const Image& image = m_textures[texture];
    const float u0 = static_cast<float>(frame % s.columns * o.width) / image.width;
    const float v0 = static_cast<float>(frame / s.columns * o.height) / image.height;
    const float u1 = u0 + static_cast<float>(o.width) / image.width;
    const float v1 = v0 + static_cast<float>(o.height) / image.height;
    const uint32_t color = tint ? packColor(tint->r, tint->g, tint->b, tint->a * alpha) : packColor(1, 1, 1, alpha);
    quad(texture, linear, left, top, left + width, top + height, u0, v0, u1, v1, color);
}

// Renderer.DrawHudLayer: a full-screen layer `scale` screens across, shifted
// in clip space.
void Hud::drawLayer(int texture, float alpha, float scaleX, float scaleY, float shiftX, float shiftY)
{
    if (texture < 0 || alpha <= 0) {
        return;
    }
    const float cx = m_width / 2 * (1 + shiftX);
    const float cy = m_height / 2 * (1 - shiftY);
    const float hx = m_width / 2 * scaleX;
    const float hy = m_height / 2 * scaleY;
    quad(texture, false, cx - hx, cy - hy, cx + hx, cy + hy, 0, 0, 1, 1, packColor(1, 1, 1, alpha));
}

float Hud::textWidth(const std::string& text) const
{
    float width = 0;
    for (char c : text) {
        width += m_font.widths[m_font.glyph(static_cast<uint8_t>(c))] * aspectFix();
    }
    return width;
}

std::pair<float, float> Hud::drawText(float x, float y, Align align, int palette, const std::string& text,
    const Color* color, float alpha, float scale, float spacingY)
{
    if (!m_font.valid() || text.empty()) {
        return {x, y};
    }
    const float fix = aspectFix();
    const int texture = fontTexture(color ? -1 : palette);
    const Image& image = m_textures[texture];
    const uint32_t tint = color ? packColor(color->r, color->g, color->b, color->a * alpha) : packColor(1, 1, 1, alpha);
    // One glyph as _textInst draws it: 8x8, mode 1, at (x, y + offset).
    auto glyph = [&](int index, float gx) {
        const float top = (m_font.offsets[index] * scale + y) / 192.0f * m_height;
        const float size = 8.0f / 192.0f * m_height * scale;
        const float left = gx / 256.0f * m_width;
        const float u0 = static_cast<float>(index % 32 * 8) / image.width;
        const float v0 = static_cast<float>(index / 32 * 8) / image.height;
        quad(texture, false, left, top, left + size, top + size, u0, v0, u0 + 8.0f / image.width,
            v0 + 8.0f / image.height, tint);
    };
    // Each line on its own; '\n' starts the next `spacingY` units down (12 by default).
    spacingY *= scale;
    size_t start = 0;
    while (true) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = text.substr(start, end - start);
        float lineWidth = 0;
        for (char c : line) {
            lineWidth += m_font.widths[m_font.glyph(static_cast<uint8_t>(c))] * scale;
        }
        float cursor = x;
        if (align == Align::Right) {
            cursor = x - lineWidth * fix;
        } else if (align == Align::Center) {
            cursor = x - std::floor(lineWidth / 2) * fix;
        }
        const float lineStart = cursor;
        for (char c : line) {
            const int index = m_font.glyph(static_cast<uint8_t>(c));
            if (c != ' ') {
                glyph(index, cursor);
            }
            cursor += m_font.widths[index] * scale * fix;
        }
        if (end >= text.size()) {
            return {align == Align::Right ? lineStart : cursor, y};
        }
        start = end + 1;
        y += spacingY;
    }
}

void Hud::queueMessage(float x, float y, float duration, int category, const std::string& text, bool red)
{
    // Reuses the message closest to expiring; stacks or replaces the rest.
    Message* message = &m_messages[0];
    for (Message& existing : m_messages) {
        if (existing.lifetime > 0) {
            if (category & existing.category & 14) {
                existing.y -= 8; // one line of the 8-unit font
            } else if (existing.y == y) {
                existing.lifetime = 0;
            }
        }
        if (existing.lifetime < message->lifetime) {
            message = &existing;
        }
    }
    message->x = x;
    message->y = y;
    message->lifetime = duration;
    message->category = category;
    message->text = text;
    message->red = red;
}

void Hud::clearMessages(int mask)
{
    // ClearHudMessage
    for (Message& message : m_messages) {
        if (mask & message.category) {
            message.lifetime = 0;
        }
    }
}

bool Hud::messageQueued(int mask) const
{
    // IsHudMessageQueued
    for (const Message& message : m_messages) {
        if ((mask & message.category) && message.lifetime > 0) {
            return true;
        }
    }
    return false;
}

void Hud::onEvent(const HudEvent& event)
{
    // PlayerEntity.TakeDamage's messages for a multiplayer battle.
    auto name = [&](int hunter) { return hunter >= 0 ? m_strings.message('H', hunter + 1, "WeaponNames.bin") : std::string(); };
    auto replace = [](std::string text, const std::string& value) {
        if (const size_t at = text.find("%s"); at != std::string::npos) {
            text.replace(at, 2, value);
        }
        return text;
    };
    switch (event.type) {
    case HudEvent::Headshot:
        queueMessage(128, 40, 20 / 30.0f, 0, m_strings.hudMessage(228)); // HEADSHOT!
        break;
    case HudEvent::SelfDestructed:
    case HudEvent::KilledBy:
    case HudEvent::HeadshotKilledBy:
        if (event.type == HudEvent::SelfDestructed) {
            queueMessage(128, 70, 90 / 30.0f, 2, m_strings.hudMessage(235)); // YOU SELF-DESTRUCTED!
        } else {
            // %s's HEADSHOT KILLED YOU! / %s KILLED YOU!
            queueMessage(128, 70, 90 / 30.0f, 2,
                replace(m_strings.hudMessage(event.type == HudEvent::HeadshotKilledBy ? 236 : 237), name(event.hunter)));
        }
        if (event.killedByTable == 'M') {
            queueMessage(128, 70, 90 / 30.0f, 2, "(" + m_strings.hudMessage(event.killedById) + ")");
        } else if (event.killedByTable != 0) {
            queueMessage(128, 70, 90 / 30.0f, 2, "(" + m_strings.message(event.killedByTable, event.killedById, "WeaponNames.bin") + ")");
        }
        break;
    case HudEvent::YouKilled:
    case HudEvent::YourHeadshotKilled:
        // YOUR HEADSHOT KILLED %s! / YOU KILLED %s!
        queueMessage(128, 70, 60 / 30.0f, 2,
            replace(m_strings.hudMessage(event.type == HudEvent::YourHeadshotKilled ? 239 : 238), name(event.hunter)));
        break;
    case HudEvent::NewPrimeHunter:
        queueMessage(128, 70, 90 / 30.0f, 2, replace(m_strings.hudMessage(241), name(event.hunter))); // %s is the new prime hunter!
        break;
    case HudEvent::PrimeHunterDead:
        queueMessage(128, 70, 90 / 30.0f, 2, m_strings.hudMessage(242)); // the prime hunter is dead!
        break;
    case HudEvent::KilledTeammate:
        queueMessage(128, 70, 60 / 30.0f, 2, replace(m_strings.hudMessage(240), name(event.hunter))); // YOU KILLED A TEAMMATE, (%s)!
        break;
    case HudEvent::KillStreak:
        // YOU KILLED 5 IN A ROW! / %s KILLED 5 IN A ROW!
        queueMessage(128, 70, 90 / 30.0f, 2, event.hunter < 0 ? m_strings.hudMessage(254) : replace(m_strings.hudMessage(255), name(event.hunter)));
        break;
    case HudEvent::Message:
        queueMessage(128, event.y, event.duration, event.category, m_strings.hudMessage(event.messageId), event.red);
        break;
    case HudEvent::ClearMessages:
        clearMessages(event.category);
        break;
    }
}

void Hud::drawMessages()
{
    // DrawQueuedHudMessages, in the default message color.
    static const Color color{15 / 31.0f, 1.0f, 15 / 31.0f, 1.0f};
    static const Color red{1.0f, 0.0f, 0.0f, 1.0f}; // ColorRgba(31)
    for (const Message& message : m_messages) {
        if (message.lifetime > 0 && (!(message.category & 1) || (m_ticks & (7 * 2)) <= 3 * 2)) {
            drawText(message.x, message.y, Align::Center, 0, message.text, message.red ? &red : &color);
        }
    }
}

void Hud::tick(const Player& player, const HudContext& context)
{
    m_ticks++;
    // ProcessHudPrimeHunter: the icon starts over each time the main player becomes it.
    const bool primeHunter = context.mode == GameMode::PrimeHunter && context.primeHunter == 0;
    if (primeHunter && !m_isPrimeHunter) {
        m_primeHunterStart = m_ticks;
    }
    m_isPrimeHunter = primeHunter;
    for (Message& message : m_messages) {
        message.lifetime -= 1 / 60.0f;
    }
    if (context.matchState != MatchState::InProgress) {
        return;
    }
    if (context.mode == GameMode::Nodes || context.mode == GameMode::NodesTeams) {
        processNodes(context);
    }
    if (context.eliminated) {
        // PlayerProcess: YOU LOST ALL YOUR LIVES! YOU'RE OUT OF THE GAME.
        queueMessage(128, 152, 1 / 1000.0f, 0, m_strings.hudMessage(243));
    } else if (player.dead()) {
        // PlayerProcess: press FIRE to begin (during the intro) or to respawn,
        // and the countdown to the forced respawn.
        queueMessage(128, 162, 1 / 1000.0f, 0, m_strings.hudMessage(context.intro ? 245 : 244));
        const int time = context.respawnWait;
        if (time < 150 * 2) {
            std::string text = m_strings.hudMessage(246); // SPAWNING IN %d...
            if (const size_t at = text.find("%d"); at != std::string::npos) {
                text.replace(at, 2, std::to_string((std::max(time, 0) + 30 * 2) / (30 * 2)));
            }
            queueMessage(128, 152, 1 / 1000.0f, 0, text);
        }
    }
    if (context.faceOff && player.health() > 0) {
        queueMessage(128, 170, 1 / 1000.0f, 0, m_strings.hudMessage(247)); // FACE OFF!
    }
    if (context.cowardDetected) {
        queueMessage(128, 150, 60 / 30.0f, 0, m_strings.hudMessage(234)); // COWARD DETECTED!
    }
    if (context.revealed && (m_ticks & (8 * 2)) == 0) {
        queueMessage(128, 150, 1 / 1000.0f, 0, m_strings.hudMessage(248)); // position revealed!
        queueMessage(128, 160, 1 / 1000.0f, 0, m_strings.hudMessage(249)); // RETURN TO BATTLE!
    }
    if (player.halfturret().active) {
        // HalfturretEntity.Process: its energy, under the crosshair.
        std::string text = m_strings.hudMessage(233); // turret energy: %d
        if (const size_t at = text.find("%d"); at != std::string::npos) {
            text.replace(at, 2, std::to_string(player.halfturret().health));
        }
        queueMessage(128, 150, 1 / 1000.0f, 0, text);
    }
    const bool alt = player.isAltForm() || player.isMorphing();
    // UpdateHealthbars / UpdateBoostBombs: half a unit and a unit a tick.
    const float healthTarget = m_objects.HealthOffsetY + (alt ? m_objects.HealthOffsetYAlt : 0);
    if (m_healthbarYOffset > healthTarget) {
        m_healthbarYOffset = std::max(healthTarget, m_healthbarYOffset - 0.5f);
    } else if (m_healthbarYOffset < healthTarget) {
        m_healthbarYOffset = std::min(healthTarget, m_healthbarYOffset + 0.5f);
    }
    const float boostTarget = alt ? 160.0f : 208.0f;
    if (m_boostBombsYOffset > boostTarget) {
        m_boostBombsYOffset -= 1;
    } else if (m_boostBombsYOffset < boostTarget) {
        m_boostBombsYOffset += 1;
    }
}

void Hud::drawMeter(float x, float y, int baseAmount, int curAmount, int palette, const HudMeterInfo& meter, Sprite& bar,
    int tankAmount, bool text, float alpha)
{
    // Multiplayer: no tanks, and the bar holds at most one tank's worth.
    const int barAmount = std::min(baseAmount, curAmount);
    int tiles = (meter.Length + 7) / 8;
    int filledTiles = 100000 * barAmount / (99000 * tankAmount / std::max(meter.Length, 1));
    if (filledTiles == 0 && barAmount > 0) {
        filledTiles = 1;
    }
    if (text) {
        drawText(x + meter.BarOffsetX, y + meter.BarOffsetY, static_cast<Align>(meter.Align), palette, twoDigits(curAmount),
            nullptr, alpha);
        if (meter.MessageId > 0) {
            drawText(x + meter.TextOffsetX, y + meter.TextOffsetY, Align::Left, palette, m_strings.hudMessage(meter.MessageId),
                nullptr, alpha);
        }
    }
    auto tile = [&](int frame) {
        drawSprite(bar, frame, palette, x / 256.0f, y / 192.0f, 2, 1, false, alpha);
        if (meter.Horizontal) {
            x += 8;
        } else {
            y -= 8;
        }
    };
    for (int i = 0; i < filledTiles / 8; i++) {
        tile(0);
        tiles--;
    }
    if (tiles > 0) {
        tile(8 - (filledTiles & 7));
        tiles--;
        for (int i = 0; i < tiles; i++) {
            tile(8);
        }
    }
}

void Hud::drawHealthbars(const Player& player)
{
    Sprite* main = sprite(m_objects.HealthBarA);
    Sprite* sub = sprite(m_objects.HealthBarB);
    if (!main || !sub) {
        return;
    }
    const int tank = player.values().EnergyTank;
    const int health = player.health();
    const int palette = health < 25 ? 2 : 0;
    drawMeter(m_objects.HealthMainPosX, m_objects.HealthMainPosY + m_healthbarYOffset, tank - 1, health, palette, m_mainMeter,
        *main, tank, true, 1);
    const int extra = health >= tank ? health - tank : 0;
    drawMeter(m_objects.HealthSubPosX, m_objects.HealthSubPosY + m_healthbarYOffset, tank - 1, extra, palette, m_subMeter,
        *sub, tank, false, 1);
}

void Hud::drawAmmoBar(const Player& player)
{
    const int beam = player.currentWeapon();
    const WeaponInfo& info = weaponsMP().at(beam);
    Sprite* bar = sprite(m_objects.AmmoBar);
    Sprite* icon = sprite(m_objects.WeaponIcon);
    if (info.ammoCost != 0 && bar) {
        const int amount = player.ammo(info.ammoType);
        drawMeter(m_objects.AmmoBarPosX, m_objects.AmmoBarPosY, amount, amount, 0, m_ammoMeter, *bar,
            player.ammoMax(info.ammoType) + 1, false, 1);
        const std::string text = twoDigits(amount / info.ammoCost);
        float textX = m_objects.AmmoBarPosX + m_ammoMeter.BarOffsetX;
        const float textY = m_objects.AmmoBarPosY + m_ammoMeter.BarOffsetY;
        // ModAmmoTextX: out from under the weapon icon.
        if (icon) {
            const float iconLeft = m_objects.WeaponIconPosX;
            const float iconTop = m_objects.WeaponIconPosY;
            const float iconRight = iconLeft + icon->object.width;
            const float iconBottom = iconTop + icon->object.height / std::max(aspectFix(), 0.0001f);
            const float width = textWidth(text);
            const Align align = static_cast<Align>(m_ammoMeter.Align);
            const float left = align == Align::Right ? textX - width : align == Align::Center ? textX - width / 2 : textX;
            const float right = left + width;
            if (!(textY + 12 <= iconTop || textY >= iconBottom) && !(right <= iconLeft || left >= iconRight)) {
                const float shift = right - iconLeft + 2;
                textX = left - shift >= 2 ? textX - shift : textX + (iconRight - left + 2);
            }
        }
        drawText(textX, textY, static_cast<Align>(m_ammoMeter.Align), 0, text);
    }
    if (icon) {
        // HudOnWeaponSwitch leaves the icon on AnimFrames[beam].
        const auto& frames = icon->object.animFrames;
        const int frame = beam < static_cast<int>(frames.size()) ? frames[beam] : beam;
        drawSprite(*icon, frame, 0, m_objects.WeaponIconPosX / 256.0f, m_objects.WeaponIconPosY / 192.0f);
    }
}

void Hud::drawBoostBombs(const Player& player)
{
    const float posY = m_boostBombsYOffset;
    const Hunter h = player.hunter();
    if (h == Hunter::Samus || h == Hunter::Sylux) {
        if (Sprite* bombs = sprite(hudBombsFile())) {
            float posX = 244;
            for (int i = 3; i > 0; i--) {
                drawSprite(*bombs, player.bombAmmo() < i ? 1 : 0, 0, (posX - bombs->object.width / 2) / 256.0f, posY / 192.0f, 2);
                posX -= 14;
            }
            drawText(230, posY + 18, Align::Center, 0, m_strings.hudMessage(1));
        }
    }
    if (h == Hunter::Samus) {
        if (Sprite* boost = sprite(hudBoostFile())) {
            const int frame = player.altAttackCooldown() == 0 ? 0 : 1;
            drawSprite(*boost, frame, 0, (29 - boost->object.width / 2) / 256.0f, (posY - 16) / 192.0f, 2);
            drawText(29, posY + 18, Align::Center, 0, m_strings.hudMessage(2));
        }
    }
}

void Hud::drawModeScore(const HudContext& context)
{
    // DrawHudBattle / DrawHudSurvival: "POINTS" or "LIVES LEFT" and the
    // score under it, 9 units apart.
    const float x = m_objects.ScorePosX;
    const float y = m_objects.ScorePosY;
    const Align align = static_cast<Align>(m_objects.ScoreAlign);
    drawText(x, y, align, 0, m_strings.hudMessage(modeScoreLabel(context.mode)));
    drawText(x, y + 9, align, 0, modeScore(context));
}

void Hud::drawPrimeHunter()
{
    // DrawHudPrimeHunter: the icon blinking between its two frames, and
    // "PRIME HUNTER" typed out beside it over three seconds.
    if (!m_isPrimeHunter) {
        return;
    }
    const float posX = m_objects.PrimePosX;
    const float posY = m_objects.PrimePosY;
    const long long elapsed = m_ticks - m_primeHunterStart;
    if (Sprite* icon = sprite(m_objects.PrimeHunter)) {
        // SetAnimation(0, 1, 20 frames, loop): frame 1 for the second half of each 20/30 s.
        const int frame = elapsed % (20 * 2) >= 10 * 2 ? 1 : 0;
        drawSprite(*icon, frame, 0, (posX - 16) / 256.0f, (posY - 16) / 192.0f);
    }
    if (elapsed < 90 * 2) {
        const size_t length = static_cast<size_t>(std::ceil(elapsed / 2.0));
        const std::string message = m_strings.hudMessage(11); // prime hunter
        drawText(posX + m_objects.PrimeTextPosX, posY + m_objects.PrimeTextPosY, static_cast<Align>(m_objects.PrimeAlign), 0,
            message.substr(0, length), nullptr, 1, 1, 8);
    }
}

void Hud::drawOctolith(const HudContext& context)
{
    // DrawOctolithInst: blinking while the main player carries it -- Bounty's
    // frame, or its team's in Capture.
    // With teams, half transparent while a teammate carries one.
    float alpha = 1;
    if (context.carryingOctolith) {
        if ((m_ticks & (16 * 2)) == 0) {
            return;
        }
    } else if (context.teams && context.teamCarryingOctolith) {
        alpha = 0.5f;
    } else {
        return;
    }
    if (Sprite* icon = sprite("_archives/commonMP/radar_octolithLARGE.bin")) {
        const int frame = context.mode == GameMode::Capture ? (context.teamIndex == 0 ? 4 : 3) : 0;
        drawSprite(*icon, frame, 0, m_objects.OctolithPosX / 256.0f, m_objects.OctolithPosY / 192.0f, 0, 1, false, alpha);
    }
}

void Hud::processNodes(const HudContext& context)
{
    // ProcessHudNodes: how many nodes each team holds untouched (two or more
    // is a bonus), and "acquiring node" then the capture bar while the main
    // player stands in one.
    m_nodeBonusOpponent = -1;
    m_mainNodeBonus = false;
    m_teamNodeCounts.fill(0);
    bool showBar = false;
    for (const HudContext::Node& node : context.nodes) {
        if (node.currentTeam >= 0 && node.currentTeam < 16 && node.occupyingTeam < 0) {
            const int count = ++m_teamNodeCounts[node.currentTeam];
            if (count > 1) {
                if (node.currentTeam == context.teamIndex) {
                    m_mainNodeBonus = true;
                } else if (m_nodeBonusOpponent == -1 || count > m_teamNodeCounts[m_nodeBonusOpponent]) {
                    m_nodeBonusOpponent = node.currentTeam;
                }
            }
        }
        if (node.occupiedByMain) {
            showBar = true;
            if (m_nodesHudState == 0) {
                queueMessage(128, 133, 45 / 30.0f, 17, m_strings.hudMessage(205)); // acquiring node
                m_nodesProgressAmount = 0;
                m_nodesHudState = 1;
            } else if (m_nodesHudState == 1) {
                m_nodesProgressAmount = static_cast<int>(std::round(40 * node.progress / (300 / 30.0f)));
            }
        }
    }
    if (!showBar && m_nodesHudState != 0) {
        clearMessages(16);
        m_nodesHudState = 0;
    }
}

void Hud::drawNodes(const HudContext& context)
{
    Sprite* icons = sprite("hud/rad_NodesRB.bin");
    const std::string bonus = m_strings.hudMessage(210); // bonus
    if (context.teams) {
        // With teams: every team holding two or more, as text in its color,
        // and each node a box of its holder's color with its letter.
        float y = m_objects.NodeBonusPosY;
        for (int team = 0; team < context.teamCount && team < 4; team++) {
            if (m_teamNodeCounts[team] < 2) {
                continue;
            }
            const auto& c = kTeamLabelColors[team];
            const Color color{c[0], c[1], c[2], 1};
            drawText(m_objects.NodeBonusPosX, y, Align::Left, 0,
                std::string("Team ") + static_cast<char>('A' + team) + " x " + std::to_string(m_teamNodeCounts[team]), &color, 1, 0.8f);
            y += 10;
        }
        const int nodeCount = static_cast<int>(context.nodes.size());
        const float startX = nodeCount < 4 ? static_cast<float>(16 * nodeCount / 2 - 12) : 12.0f;
        float posX = 0;
        const Color black{0, 0, 0, 1};
        for (const HudContext::Node& node : context.nodes) {
            const int owner = node.blinking ? node.occupyingTeam : node.currentTeam;
            const auto color = owner >= 0 && owner < 4 ? kTeamObjectiveColors[owner] : std::array<float, 3>{1, 1, 1};
            const float x = m_objects.NodeIconPosX + startX - posX;
            const float y2 = m_objects.NodeIconPosY - 8.0f;
            flatBox(x, y2, x + 12, y2 + 12, {color[0], color[1], color[2], 1});
            drawText(x + 2, y2 + 2, Align::Left, 0, owner < 0 ? std::string("-") : std::string(1, static_cast<char>('A' + owner)), &black);
            posX += 16;
        }
        drawText(m_objects.NodeTextPosX, m_objects.NodeTextPosY, Align::Center, 0, m_strings.hudMessage(8)); // NODES
    } else {
        drawNodesFfa(context, icons, bonus);
    }
    drawNodeProgress();
}

void Hud::drawNodesFfa(const HudContext& context, Sprite* icons, const std::string& bonus)
{
    // DrawNodesBonuses: the main player's bonus, and the leading opponent's blinking in red.
    if (m_mainNodeBonus && icons != nullptr) {
        drawSprite(*icons, 4, 0, m_objects.NodeBonusPosX / 256.0f, m_objects.NodeBonusPosY / 192.0f);
        drawText(m_objects.NodeBonusPosX + 12, m_objects.NodeBonusPosY + 2, Align::Left, 0,
            "x " + std::to_string(m_teamNodeCounts[context.teamIndex & 15]));
        drawText(m_objects.NodeBonusPosX, m_objects.NodeBonusPosY + 10, Align::Left, 0, bonus);
    }
    if (m_nodeBonusOpponent != -1 && std::fmod(context.elapsed, 16 / 30.0f) < 12 / 30.0f && icons != nullptr) {
        drawSprite(*icons, 2, 0, m_objects.EnemyBonusPosX / 256.0f, m_objects.EnemyBonusPosY / 192.0f);
        drawText(m_objects.EnemyBonusPosX + 12, m_objects.EnemyBonusPosY + 2, Align::Left, 2,
            "x " + std::to_string(m_teamNodeCounts[m_nodeBonusOpponent]));
        drawText(m_objects.EnemyBonusPosX, m_objects.EnemyBonusPosY + 10, Align::Left, 2, bonus);
    }
    // DrawNodesIcons: one icon a node, right to left from the corner --
    // blue when the main player holds it, red for anyone else, grey when
    // nobody does, blinking to whoever is taking it.
    const int nodeCount = static_cast<int>(context.nodes.size());
    const float startX = nodeCount < 4 ? static_cast<float>(16 * nodeCount / 2 - 12) : 12.0f;
    float posX = 0;
    for (const HudContext::Node& node : context.nodes) {
        int frame;
        if (node.currentTeam < 0) {
            frame = node.blinking ? (node.occupyingTeam == context.teamIndex ? 4 : 2) : 0;
        } else if (node.currentTeam == context.teamIndex) {
            frame = !node.blinking || node.occupyingTeam == context.teamIndex ? 4 : 2;
        } else {
            frame = node.blinking && node.occupyingTeam == context.teamIndex ? 4 : 2;
        }
        if (icons != nullptr) {
            drawSprite(*icons, frame, 0, (m_objects.NodeIconPosX + startX - posX) / 256.0f, (m_objects.NodeIconPosY - 8) / 192.0f);
        }
        posX += 16;
    }
    drawText(m_objects.NodeTextPosX, m_objects.NodeTextPosY, Align::Center, 0, m_strings.hudMessage(8)); // NODES
}

void Hud::drawNodeProgress()
{
    // The capture bar, once "acquiring node" is gone.
    if (m_nodesHudState == 1 && !messageQueued(16)) {
        if (Sprite* bar = sprite("_archives/commonMP/hud_systemload.bin")) {
            // HudElements.NodeProgressBar, its tanks 40 long.
            static const HudMeterInfo meter{true, 40, 8, 1, -8, 15, 6, 0, -7, 2, 0, 40, 0};
            drawMeter(108, 143, m_nodesProgressAmount, m_nodesProgressAmount, 0, meter, *bar, 40, false, 1);
        }
        drawText(128, 133, Align::Center, 0, m_strings.hudMessage(204)); // progress
    }
}

void Hud::drawWeaponList(const Player& player)
{
    Sprite* sheet = sprite(m_objects.WeaponSelect);
    if (!sheet) {
        return;
    }
    const float scale = std::clamp(m_pro ? 1.7f : 1.0f, 0.6f, 2.0f);
    const float fix = aspectFix();
    const float panelX = 2 * fix;
    const float rowHeight = 8 * scale;
    const float panelWidth = 26 * scale * fix;
    const float iconBox = rowHeight - 1 * scale;
    const float iconBoxX = iconBox * fix;
    const float ammoRightX = panelX + panelWidth - 1.5f * scale * fix;
    float y = 46;
    for (int beam : kWeaponOrder) {
        if (!player.hasWeapon(beam)) {
            continue;
        }
        const bool equipped = beam == player.currentWeapon();
        flatBox(panelX, y, panelX + panelWidth, y + rowHeight - 1 * scale,
            equipped ? Color{0.45f, 0.4f, 0.2f, 0.72f} : Color{0, 0, 0, 0.42f});
        const auto& b = m_iconBounds[beam];
        const float bw = b[2] - b[0] + 1, bh = b[3] - b[1] + 1;
        const float cx = (b[0] + b[2] + 1) / 2, cy = (b[1] + b[3] + 1) / 2;
        const float iconScale = (iconBox - 1 * scale) / std::max(bw, bh);
        const Color tint{kWeaponListColors[beam][0] / 255.0f, kWeaponListColors[beam][1] / 255.0f,
            kWeaponListColors[beam][2] / 255.0f, 1};
        drawSprite(*sheet, beam, 0, (panelX + iconBoxX / 2 - cx * iconScale * fix) / 256.0f,
            (y + iconBox / 2 - cy * iconScale) / 192.0f, 1, iconScale, false, 1, &tint, true);
        const WeaponInfo& info = weaponsMP().at(beam);
        const int amount = player.ammo(info.ammoType);
        const std::string ammo = info.ammoCost > 0 && amount >= 0 ? std::to_string(amount / info.ammoCost) : "--";
        const Color ink{230 / 255.0f, 234 / 255.0f, 242 / 255.0f, 1};
        drawText(ammoRightX, y + 1.6f * scale, Align::Right, 0, ammo, &ink, 1, 0.42f * scale);
        y += rowHeight;
    }
}

void Hud::drawCustomCrosshair(const Player& player)
{
    // GetCrosshairColor, then Crosshair's default: a medium cross.
    const int health = player.health();
    const Color color = health > 60 ? Color{0, 1, 0, 1} : health > 33 ? Color{1, 0.65f, 0, 1} : Color{1, 0, 0, 1};
    const float cx = m_width / 2, cy = m_height / 2;
    const float arm = 9, thickness = 3, gap = 3;
    const float offset = gap + arm / 2;
    const float bars[4][4] = {{0, offset, thickness, arm}, {0, -offset, thickness, arm}, {-offset, 0, arm, thickness},
        {offset, 0, arm, thickness}};
    for (const auto& bar : bars) {
        // Crosshair.EdgesOf: snapped outward to whole pixels, y up.
        const float left = std::floor(bar[0] - bar[2] / 2), right = std::ceil(bar[0] + bar[2] / 2);
        const float bottom = std::floor(bar[1] - bar[3] / 2), top = std::ceil(bar[1] + bar[3] / 2);
        quad(0, false, cx + left, cy - top, cx + right, cy - bottom, 0, 0, 1, 1,
            packColor(color.r, color.g, color.b, color.a));
    }
}

void Hud::drawHitMarker(float alpha)
{
    const float cx = m_width / 2, cy = m_height / 2;
    constexpr float gap = 4, length = 7, thickness = 2;
    const float diagonal = std::sqrt(0.5f);
    const Color color{1, 1, 1, alpha};
    for (int i = 0; i < 4; i++) {
        const float dx = ((i & 1) == 0 ? -1 : 1) * diagonal;
        const float dy = ((i & 2) == 0 ? -1 : 1) * diagonal;
        const float x0 = dx * gap, y0 = dy * gap, x1 = dx * (gap + length), y1 = dy * (gap + length);
        const float hx = -dy * thickness / 2, hy = dx * thickness / 2;
        const float x[4] = {cx + x0 + hx, cx + x0 - hx, cx + x1 + hx, cx + x1 - hx};
        const float y[4] = {cy - (y0 + hy), cy - (y0 - hy), cy - (y1 + hy), cy - (y1 - hy)};
        flatQuad(x, y, color);
    }
}

void Hud::drawRadar(const Player& player, const HudContext& context)
{
    const float u = m_height / 192.0f;
    const float dialGrow = 1.3f * 0.8f;
    const float blipGrow = 1.3f * 1.2f;
    const float radius = 19.44f * dialGrow * u;
    // Centre in pixels, local offsets y up as the C# has them.
    const float ox = m_width - 5 * u - radius;
    const float oy = 10 * u + radius;
    auto ring = [&](float lx, float ly, float r, float thickness, const Color& color) {
        const int segments = 48;
        const float inner = r - thickness / 2, outer = r + thickness / 2;
        for (int i = 0; i < segments; i++) {
            const float a0 = 2 * std::numbers::pi_v<float> * i / segments;
            const float a1 = 2 * std::numbers::pi_v<float> * (i + 1) / segments;
            const float x[4] = {ox + lx + outer * std::cos(a0), ox + lx + inner * std::cos(a0), ox + lx + outer * std::cos(a1),
                ox + lx + inner * std::cos(a1)};
            const float y[4] = {oy - ly - outer * std::sin(a0), oy - ly - inner * std::sin(a0), oy - ly - outer * std::sin(a1),
                oy - ly - inner * std::sin(a1)};
            flatQuad(x, y, color);
        }
    };
    auto line = [&](float ax, float ay, float bx, float by, float thickness, const Color& color) {
        const float dx = bx - ax, dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.0001f) {
            return;
        }
        const float nx = -dy / len * thickness / 2, ny = dx / len * thickness / 2;
        const float x[4] = {ox + ax + nx, ox + ax - nx, ox + bx + nx, ox + bx - nx};
        const float y[4] = {oy - (ay + ny), oy - (ay - ny), oy - (by + ny), oy - (by - ny)};
        flatQuad(x, y, color);
    };
    const Color ringColor{0.6f, 0.85f, 0.9f, 1};
    ring(0, 0, radius, 0.35f * dialGrow * u, ringColor);
    ring(0, 0, radius * 0.55f, 0.25f * dialGrow * u, ringColor);
    const float cone = 55 * std::numbers::pi_v<float> / 180;
    line(0, 0, -radius * std::sin(cone), radius * std::cos(cone), 0.25f * dialGrow * u, ringColor);
    line(0, 0, radius * std::sin(cone), radius * std::cos(cone), 0.25f * dialGrow * u, ringColor);

    // Heading up: the camera's facing is straight up on the dial.
    const CameraPose pose = player.camera();
    float fx = pose.target[0] - pose.position[0];
    float fz = pose.target[2] - pose.position[2];
    const float faceLen = std::sqrt(fx * fx + fz * fz);
    if (faceLen < 0.0001f) {
        fx = 0;
        fz = 1;
    } else {
        fx /= faceLen;
        fz /= faceLen;
    }
    const float rx = -fz, rz = fx;
    const float worldToPixel = radius / 24.0f;
    for (const HudContext::Blip& blip : context.items) {
        const float dx = blip.position[0] - player.position()[0];
        const float dz = blip.position[2] - player.position()[2];
        const float sx = dx * rx + dz * rz;
        const float sy = dx * fx + dz * fz;
        if (sx * sx + sy * sy < 0.0004f) {
            continue;
        }
        float bx = sx * worldToPixel, by = sy * worldToPixel;
        const float len = std::sqrt(bx * bx + by * by);
        if (len > radius) {
            bx *= radius / len;
            by *= radius / len;
        }
        if (blip.weapon) {
            const float d = 0.49f * blipGrow * u;
            const float x[4] = {ox + bx, ox + bx + d, ox + bx - d, ox + bx};
            const float y[4] = {oy - by - d, oy - by, oy - by, oy - by + d};
            flatQuad(x, y, {1, 0.65f, 0.2f, 1});
        } else {
            const float r = 0.39f * blipGrow * u;
            for (int i = 0; i < 32; i++) {
                const float a0 = 2 * std::numbers::pi_v<float> * i / 32, a1 = 2 * std::numbers::pi_v<float> * (i + 1) / 32;
                flatTriangle(ox + bx, oy - by, ox + bx + r * std::cos(a0), oy - by - r * std::sin(a0),
                    ox + bx + r * std::cos(a1), oy - by - r * std::sin(a1), {1, 0.4f, 0.8f, 1});
            }
        }
    }
    const float tri = 1.25f * u;
    flatTriangle(ox, oy - tri, ox - tri * 0.75f, oy + tri * 0.7f, ox + tri * 0.75f, oy + tri * 0.7f, {0.92f, 0.94f, 0.98f, 1});
}

void Hud::proBar(float x, float y, float width, float height, float fill, const Color& color)
{
    const float fix = aspectFix();
    flatBox(x - fix, y - 1, x + (width + 1) * fix, y + height + 1, {0, 0, 0, 0.55f});
    flatBox(x, y, x + width * fix, y + height, {1, 1, 1, 0.16f});
    if (fill > 0) {
        flatBox(x, y, x + width * fill * fix, y + height, color);
    }
}

void Hud::proNumber(float x, float y, Align align, const std::string& text, const Color& color, float scale)
{
    const Color shadow{0, 0, 0, 1};
    drawText(x + 0.8f * aspectFix(), y + 0.8f, align, 0, text, &shadow, 1, scale);
    drawText(x, y, align, 0, text, &color, 1, scale);
}

void Hud::drawProHud(const Player& player, const HudContext& context)
{
    const Color good{0.24f, 0.85f, 0.32f, 1}, warn{1, 0.68f, 0.1f, 1}, danger{0.95f, 0.18f, 0.18f, 1};
    const Color panel{0, 0, 0, 0.5f};
    const float fix = aspectFix();
    // Energy: the left foot of the screen.
    const float healthFraction =
        std::clamp(player.health() / static_cast<float>(std::max(player.values().EnergyTank - 1, 1)), 0.0f, 1.0f);
    const Color health = healthFraction > kProWarn ? good : healthFraction > kProDanger ? warn : danger;
    flatBox(2 * fix, 170, 46 * fix, 190, panel);
    proNumber(6 * fix, 172, Align::Left, std::to_string(player.health()), health, 1.5f);
    proBar(4 * fix, 186, 40, 3, healthFraction, health);
    // Ammo: the right foot, for weapons that use it and not in alt form.
    const int beam = player.currentWeapon();
    const WeaponInfo& info = weaponsMP().at(beam);
    if (!player.isAltForm() && !player.isMorphing() && !player.isUnmorphing() && info.ammoCost != 0) {
        const int amount = player.ammo(info.ammoType);
        const int full = 100;
        const Color color = amount < 0 || amount >= full / 2 ? good : amount >= full / 5 ? warn : danger;
        const float width = 58;
        const float right = 256 - 2 * fix;
        const float left = right - width * fix;
        flatBox(left, 170, right, 190, panel);
        if (Sprite* sheet = sprite(m_objects.WeaponSelect)) {
            const float side = 8 * 1.5f;
            const auto& b = m_iconBounds[beam];
            const float scale = side / std::max(b[2] - b[0] + 1, b[3] - b[1] + 1);
            const float cx = (b[0] + b[2] + 1) / 2, cy = (b[1] + b[3] + 1) / 2;
            const Color tint{kWeaponListColors[beam][0] / 255.0f, kWeaponListColors[beam][1] / 255.0f,
                kWeaponListColors[beam][2] / 255.0f, 1};
            const float x = left + 2 * fix, y = 172;
            drawSprite(*sheet, beam, 0, (x + side * fix / 2 - cx * scale * fix) / 256.0f, (y + side / 2 - cy * scale) / 192.0f, 1,
                scale, false, 1, &tint, true);
        }
        proNumber(right - 4 * fix, 172, Align::Right, amount < 0 ? "--" : std::to_string(amount / info.ammoCost), color, 1.5f);
        proBar(left + 2 * fix, 186, width - 4, 3, amount < 0 ? 1.0f : std::clamp(amount / float(full), 0.0f, 1.0f), color);
    }
    // The score, top left.
    proNumber(4 * fix, 12, Align::Left, m_strings.hudMessage(modeScoreLabel(context.mode)),
        {178 / 255.0f, 186 / 255.0f, 200 / 255.0f, 1}, 0.55f);
    proNumber(4 * fix, 20, Align::Left, modeScore(context),
        {235 / 255.0f, 238 / 255.0f, 245 / 255.0f, 1}, 1.1f);
}

void Hud::drawDamageIndicators(const Player& player)
{
    // PlayerHud.UpdateDamageIndicators and Renderer.DrawHudDamageModel: an
    // arrow node per direction, blinking while its timer runs, drawn in the
    // DS's 256x192 screen space (the model's scale takes its 4x3 there).
    if (m_damageModel == nullptr || m_damageTexture < 0) {
        return;
    }
    const Model& model = *m_damageModel;
    const auto& nodes = model.nodes();
    const auto& timers = player.damageIndicators();
    auto& batches = m_out->batches;
    auto& v = m_out->vertices;
    const float scale = model.scale();
    static constexpr const char* names[8] = {"north", "ne", "east", "se", "south", "sw", "west", "nw"};
    for (size_t i = 0; i < 8; i++) {
        if ((timers[i] & (4 * 2)) == 0) {
            continue;
        }
        const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.name == names[i]; });
        if (node == nodes.end() || node->meshCount <= 0) {
            continue;
        }
        const int meshIndex = model.firstMesh(*node);
        if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes().size())) {
            continue;
        }
        const DrawRange& range = model.dlistRange(model.meshes()[meshIndex].dlistId);
        if (batches.empty() || batches.back().texture != m_damageTexture || batches.back().linear) {
            batches.push_back({m_damageTexture, false, static_cast<uint32_t>(v.size()), 0});
        }
        for (uint32_t k = 0; k < range.vertexCount; k++) {
            const Vertex& vert = model.vertices()[range.firstVertex + k];
            const float x = vert.pos[0] * scale / 256.0f * m_width;
            const float y = m_height - vert.pos[1] * scale / 192.0f * m_height;
            v.push_back({x, y, vert.uv[0], vert.uv[1], 0xFFFFFFFFu});
        }
        batches.back().vertexCount += range.vertexCount;
    }
}

void Hud::drawIconModel(const Model& model, int texture, float x, float y, float angle, const std::array<float, 3>& color,
    float alpha)
{
    // Renderer.DrawIconModel: the model's first mesh at (x, y) in window
    // pixels, turned `angle` degrees counterclockwise, one DS unit a
    // 192th of the window's height either way, tinted by `color`.
    const auto& meshes = model.meshes();
    if (meshes.empty() || texture < 0 || alpha <= 0) {
        return;
    }
    const DrawRange& range = model.dlistRange(meshes[0].dlistId);
    auto& batches = m_out->batches;
    auto& v = m_out->vertices;
    if (batches.empty() || batches.back().texture != texture || batches.back().linear) {
        batches.push_back({texture, false, static_cast<uint32_t>(v.size()), 0});
    }
    const float scale = model.scale() * m_height / 192.0f;
    const float radians = angle * 3.14159265f / 180;
    const float c = std::cos(radians), s = std::sin(radians);
    const uint32_t tint = packColor(color[0], color[1], color[2], alpha);
    for (uint32_t k = 0; k < range.vertexCount; k++) {
        const Vertex& vert = model.vertices()[range.firstVertex + k];
        const float rx = vert.pos[0] * c - vert.pos[1] * s;
        const float ry = vert.pos[0] * s + vert.pos[1] * c;
        v.push_back({x + rx * scale, y - ry * scale, vert.uv[0], vert.uv[1], tint});
    }
    batches.back().vertexCount += range.vertexCount;
}

void Hud::drawLocators(const HudContext& context)
{
    // PlayerHud.DrawLocatorIcon: in the view, the icon over the position;
    // outside a 200x120 box around (128, 106), or behind the camera, an
    // arrow on the box's edge pointing toward it.
    if (m_playerLocator == nullptr || m_arrowLocator == nullptr) {
        return;
    }
    auto W = [&](float value) { return value / 256.0f * m_width; };
    auto H = [&](float value) { return value / 192.0f * m_height; };
    const CameraPose& cam = context.camera;
    const Vec3 forward = ai::normalized(cam.target - cam.position);
    const Vec3 right = ai::normalized(ai::cross(forward, cam.up));
    const Vec3 up = ai::cross(right, forward);
    const float tanHalf = std::tan(context.fovY * 3.14159265f / 360);
    const float aspect = m_width / m_height;
    for (const HudContext::Locator& info : context.locators) {
        // Vec3MultMtx4 with the view matrix: x right, y up, z toward the camera.
        const Vec3 between = info.position - cam.position;
        const float viewX = dot(between, right);
        const float viewY = dot(between, up);
        const float viewZ = -dot(between, forward);
        float x, y;
        float projX = 0, projY = 0; // in pixels
        bool behind = false;
        if (viewZ < -1) {
            const float w = -viewZ;
            projX = (viewX / (w * tanHalf * aspect) + 1) / 2 * m_width;
            projY = (1 - viewY / (w * tanHalf)) / 2 * m_height;
            x = projX - W(128);
            y = projY - H(106);
        } else {
            x = W(viewX);
            y = -H(viewY);
            behind = true;
        }
        const float absX = std::abs(x);
        const float absY = std::abs(y);
        if (behind || absX > W(100) || absY > H(60)) {
            if (absY >= 1 / 4096.0f) {
                const float v15 = absX + std::trunc((H(60) - absY) * absX / absY);
                if (v15 > W(100)) {
                    const float v17 = absY + std::trunc((W(100) - absX) * absY / absX);
                    projX = x <= 0 ? W(28) : W(228);
                    projY = y <= 0 ? H(106) - v17 : v17 + H(106);
                } else {
                    projX = x <= 0 ? W(128) - v15 : v15 + W(128);
                    projY = y <= 0 ? H(46) : H(166);
                }
            } else {
                // Level with the box's middle. (The C# keeps the projected
                // height here, in the wrong units, which puts it at the top.)
                projX = x <= 0 ? W(28) : W(228);
                projY = H(106);
            }
            const float angle = std::atan2(-y, x) * 180 / 3.14159265f;
            drawIconModel(*m_arrowLocator, m_arrowLocatorTexture, projX, projY, angle, info.color, info.alpha);
        } else {
            const Model* icon = m_playerLocator.get();
            int texture = m_playerLocatorTexture;
            if (info.kind == HudContext::Locator::Node && m_nodeLocator != nullptr) {
                icon = m_nodeLocator.get();
                texture = m_nodeLocatorTexture;
            } else if (info.kind == HudContext::Locator::Octolith && m_octolithLocator != nullptr) {
                icon = m_octolithLocator.get();
                texture = m_octolithLocatorTexture;
            }
            drawIconModel(*icon, texture, projX, projY, 0, info.color, info.alpha);
        }
    }
}

namespace {

// ColorRgba(ushort): a DS BGR555 colour.
constexpr std::array<float, 3> bgr555(uint16_t value)
{
    return {(value & 31) / 31.0f, (value >> 5 & 31) / 31.0f, (value >> 10 & 31) / 31.0f};
}

std::string formatTime(float seconds)
{
    // PlayerHud.FormatTime: minutes (hours folded in) and two-digit seconds,
    // TimeSpan-style, the seconds cut rather than rounded.
    const int total = std::max(static_cast<int>(seconds), 0);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

// HudElements.Hunters: the scoreboard's portraits (the Guardian has Samus's).
const char* hunterPortrait(int hunter)
{
    static constexpr const char* files[8] = {
        "_archives/common/enemy_samus.bin", "_archives/common/enemy_kanden.bin", "_archives/common/enemy_trace.bin",
        "_archives/common/enemy_sylux.bin", "_archives/common/enemy_noxus.bin", "_archives/common/enemy_spyre.bin",
        "_archives/common/enemy_weavel.bin", "_archives/common/enemy_samus.bin",
    };
    return files[std::clamp(hunter, 0, 7)];
}

} // namespace

int Hud::modeScoreLabel(GameMode mode)
{
    // ProScoreMessageId: what each mode's DrawHud* passes to DrawModeScore.
    switch (mode) {
    case GameMode::Survival:
    case GameMode::SurvivalTeams:
        return 213; // lives left
    case GameMode::PrimeHunter:
        return 214; // prime time
    case GameMode::Bounty:
    case GameMode::BountyTeams:
        return 215; // octoliths
    case GameMode::Capture:
        return 216; // octoliths
    case GameMode::Defender:
    case GameMode::DefenderTeams:
        return 217; // ring time
    case GameMode::Nodes:
    case GameMode::NodesTeams:
        return 218; // points
    default:
        return 212; // points
    }
}

std::string Hud::modeScore(const HudContext& context) const
{
    // PlayerHud.FormatModeScore
    if (context.mode == GameMode::PrimeHunter || context.mode == GameMode::Defender || context.mode == GameMode::DefenderTeams) {
        return formatTime(context.time) + "/" + formatTime(context.timeGoal);
    }
    if (context.mode == GameMode::Survival || context.mode == GameMode::SurvivalTeams) {
        return std::to_string(std::max(context.pointGoal - context.deaths, 0));
    }
    return std::to_string(context.points) + " / " + std::to_string(context.pointGoal);
}

void Hud::drawFilter(float alpha)
{
    drawLayer(m_filterTexture, m_filterAlpha * alpha, 1, 1);
}

void Hud::drawMatchTime(const HudContext& context)
{
    // PlayerHud.DrawMatchTime: "TIME" and what is left, red under ten seconds.
    if (context.matchTime < 0) {
        return;
    }
    const int palette = context.matchTime < 10 ? 2 : 0;
    const float posY = 10;
    drawText(128, posY, Align::Center, palette, m_strings.hudMessage(5));
    drawText(128, posY + 10, Align::Center, palette, formatTime(context.matchTime));
}

void Hud::drawTeamScoreboard(const HudContext& context)
{
    // ModDrawTeamScoreboard at the offline columns: the winners over the
    // top once the match is over, then each team's line and its players.
    constexpr float column1 = 160, column2 = 215, nameColumn = 60;
    const GameMode mode = context.mode;
    const bool timed = mode == GameMode::SurvivalTeams || mode == GameMode::DefenderTeams;
    const bool deaths = mode == GameMode::BattleTeams || mode == GameMode::SurvivalTeams;
    const Color ink{239 / 255.0f, 239 / 255.0f, 247 / 255.0f, 1};
    auto value = [&](float time, int points) {
        return !timed ? std::to_string(points) : time < 0 ? std::string("MAX") : formatTime(time);
    };
    float y = 16;
    if (context.matchState != MatchState::InProgress) {
        bool tie = false;
        const int leaderTeam = context.scores.empty() ? -1 : context.scores[0].team;
        for (size_t i = 1; i < context.scores.size(); i++) {
            tie |= context.scores[i].standing == 0 && context.scores[i].team != leaderTeam;
        }
        std::string winners = tie ? "TIE: " : "WINNER: ";
        int last = -1;
        for (const HudContext::Score& score : context.scores) {
            if (score.standing != 0 || last == score.team) {
                continue;
            }
            if (last != -1) {
                winners += " / ";
            }
            winners += static_cast<char>('A' + score.team);
            last = score.team;
        }
        drawText(nameColumn - 18, 4, Align::Left, 0, winners, &ink, 1, 0.75f);
    }
    drawText(nameColumn - 18, y, Align::Left, 0, "TEAMS", &ink, 1, 0.75f);
    drawText(column1, y, Align::Center, 0, timed ? "TIME" : "POINTS", &ink);
    drawText(column2, y, Align::Center, 0, deaths ? "DEATHS" : "KILLS", &ink);
    y += 14;
    int previous = -1;
    for (const HudContext::Score& score : context.scores) {
        const int team = score.team;
        if (team != previous && team >= 0 && team < 4) {
            const auto& c = kTeamLabelColors[team];
            const Color color{c[0], c[1], c[2], 1};
            const HudContext::TeamScore& totals = context.teamScores[team];
            drawText(nameColumn - 18, y, Align::Left, 0, std::string("Team ") + static_cast<char>('A' + team), &color, 1, 0.85f);
            drawText(column1, y, Align::Center, 0, value(totals.time, totals.points), &color);
            drawText(column2, y, Align::Center, 0, std::to_string(deaths ? totals.deaths : totals.kills), &color);
            previous = team;
            y += 12;
        }
        const std::string name = (score.slot == 0 ? "> " : "  ") + (score.name.empty() ? "Player" + std::to_string(score.slot + 1) : score.name);
        drawText(nameColumn - 18, y, Align::Left, 0, name, &ink, 1, 0.8f);
        drawText(column1, y, Align::Center, 0, value(score.time, score.points), &ink);
        drawText(column2, y, Align::Center, 0, std::to_string(deaths ? score.deaths : score.kills), &ink);
        y += 13;
    }
}

void Hud::drawScoreboard(const HudContext& context)
{
    if (context.teams) {
        drawTeamScoreboard(context);
        return;
    }
    // PlayerHud.DrawScoreboard for a battle without teams, at the offline
    // column positions (PlayerEntityNetHud: _scoreColumn1Solo/_scoreColumn2Solo).
    constexpr float startSpace = 13, playerSpace = 28, minPlayerSpace = 19;
    constexpr float column1 = 160, column2 = 215, nameColumn = 60;
    const bool ending = context.matchState == MatchState::Ending;
    const int rows = static_cast<int>(context.scores.size());
    // GetScoreboardRowSpace: tighter than 28 with more than four players.
    float rowSpace = playerSpace;
    if (rows > 4) {
        const float available = 168 - startSpace - (ending ? startSpace : 0);
        rowSpace = std::clamp(available / rows, minPlayerSpace, playerSpace);
    }
    const float height = startSpace * (ending ? 2 : 1) + rows * rowSpace;
    float posY = 104 - height / 2;
    if (ending) {
        const auto c = bgr555(0x53F4);
        const Color color{c[0], c[1], c[2], 1};
        drawText(128, posY, Align::Center, 0, m_strings.hudMessage(219), &color); // GAME OVER
        posY += startSpace;
    }
    const auto h = bgr555(0x3FEF);
    const Color headerColor{h[0], h[1], h[2], 1};
    const GameMode mode = context.mode;
    const bool survival = mode == GameMode::Survival || mode == GameMode::SurvivalTeams;
    const bool defender = mode == GameMode::Defender || mode == GameMode::DefenderTeams;
    const bool octoliths = mode == GameMode::Capture || mode == GameMode::Bounty || mode == GameMode::BountyTeams;
    const bool timed = survival || defender || mode == GameMode::PrimeHunter;
    const bool kills = !survival && mode != GameMode::Battle && mode != GameMode::BattleTeams;
    // points, octoliths or time; then deaths or kills
    drawText(column1, posY, Align::Center, 0, m_strings.hudMessage(timed ? 224 : octoliths ? 227 : 225), &headerColor);
    drawText(column2, posY, Align::Center, 0, m_strings.hudMessage(kills ? 220 : 223), &headerColor);
    posY += startSpace;
    Sprite* stars = sprite("_archives/commonMP/stars.bin");
    for (const HudContext::Score& score : context.scores) {
        const auto c = bgr555(0x7DEF);
        Color color{c[0], c[1], c[2], 1};
        if (score.slot == 0) {
            // The main player's row pulses between blue and white.
            const float pct = std::fmod(context.elapsed / (32 / 30.0f), 1.0f);
            const float rg = pct <= 0.5f ? pct * 2 : 1 - (pct - 0.5f) * 2;
            color = {rg, rg, 1, 1};
        }
        // DrawScoreboardPlayer: portrait, rank stars, nickname.
        if (Sprite* portrait = sprite(hunterPortrait(score.hunter))) {
            drawSprite(*portrait, 0, 0, (nameColumn - 40) / 256.0f, (posY - 13) / 192.0f, 2);
        }
        if (stars != nullptr) {
            constexpr int starCount = 0; // GameState.Stars: the license rank, none offline
            drawSprite(*stars, starCount * 2, 0, nameColumn / 256.0f, posY / 192.0f, 2);
            drawSprite(*stars, starCount * 2 + 1, 0, (nameColumn + 32) / 256.0f, posY / 192.0f, 2);
        }
        drawText(nameColumn + 32, posY - 9, Align::Center, 0, score.name.empty() ? "Player" + std::to_string(score.slot + 1) : score.name,
            &color);
        const std::string value1 = !timed ? std::to_string(score.points)
            : score.time < 0              ? m_strings.hudMessage(256) // MAX
                                          : formatTime(score.time);
        drawText(column1, posY, Align::Center, 0, value1, &color);
        drawText(column2, posY, Align::Center, 0, std::to_string(kills ? score.kills : score.deaths), &color);
        posY += rowSpace;
    }
}

std::string Hud::wrapText(const std::string& text, int maxWidth, int& lines) const
{
    lines = 1;
    if (maxWidth <= 0 || text.empty() || !m_font.valid()) {
        return text;
    }
    std::string dest;
    int lineWidth = 0;
    // Width already on the next line when it breaks at an earlier space.
    int widthAfterBreak = 0;
    size_t breakPos = 0;
    for (size_t i = 0; i < text.size(); i++) {
        const char ch = text[i];
        dest.push_back(ch);
        if (ch == '\n') {
            lineWidth = 0;
            breakPos = 0;
            widthAfterBreak = 0;
            lines++;
            continue;
        }
        if (ch == ' ') {
            breakPos = dest.size() - 1;
            widthAfterBreak = 0;
        }
        const int width = m_font.widths[m_font.glyph(static_cast<uint8_t>(ch))];
        lineWidth += width;
        if (ch != ' ') {
            widthAfterBreak += width;
        }
        if (i + 1 < text.size() && lineWidth > maxWidth) {
            if (breakPos == 0 && maxWidth >= 8) {
                // No space to break at: a new line after this character.
                dest.push_back(' ');
                breakPos = dest.size() - 1;
                widthAfterBreak = 0;
            }
            if (breakPos > 0) {
                dest[breakPos] = '\n';
                lineWidth = widthAfterBreak;
                widthAfterBreak = 0;
                breakPos = 0;
                lines++;
            }
        }
    }
    return dest;
}

void Hud::loadModeRules(GameMode mode)
{
    // HudElements.RulesInfo: the Text/HudMessagesMP 'S' entries of each
    // mode, the first one the header, and how far each line is indented.
    struct Rules {
        std::vector<int> ids;
        std::vector<int> offsets;
    };
    static const Rules battle{{1, 2, 3, 4}, {0, 0, 0, 0}};
    static const Rules survival{{11, 12, 13, 14}, {0, 0, 0, 0}};
    static const Rules primeHunter{{21, 22, 23, 24, 25, 26, 27}, {0, 0, 0, 0, 12, 12, 12}};
    static const Rules bounty{{31, 32, 33, 34, 35}, {0, 0, 0, 0, 0}};
    static const Rules capture{{41, 42, 43, 44, 45, 46}, {0, 0, 0, 0, 0, 0}};
    static const Rules defender{{51, 52, 53, 54}, {0, 0, 0, 0}};
    static const Rules nodes{{61, 62, 63, 64, 65, 66, 67, 68}, {0, 0, 0, 0, 0, 12, 12, 12}};
    const Rules* rules = &battle;
    switch (mode) {
    case GameMode::Survival:
    case GameMode::SurvivalTeams:
        rules = &survival;
        break;
    case GameMode::PrimeHunter:
        rules = &primeHunter;
        break;
    case GameMode::Bounty:
    case GameMode::BountyTeams:
        rules = &bounty;
        break;
    case GameMode::Capture:
        rules = &capture;
        break;
    case GameMode::Defender:
    case GameMode::DefenderTeams:
        rules = &defender;
        break;
    case GameMode::Nodes:
    case GameMode::NodesTeams:
        rules = &nodes;
        break;
    default:
        break;
    }
    m_rules.clear();
    m_rulesMode = mode;
    for (size_t i = 0; i < rules->ids.size(); i++) {
        RulesLine line;
        line.text = m_strings.message('S', rules->ids[i], "HudMessagesMP.bin");
        line.offset = rules->offsets[i];
        if (i == 0) {
            line.length = 30;
        } else {
            int lines = 1;
            line.text = wrapText(line.text, 244 - (line.offset + 12), lines);
            line.length = m_rules.back().length + static_cast<int>(line.text.size());
            line.newlines = lines - 1;
        }
        m_rules.push_back(std::move(line));
    }
}

void Hud::drawModeRules(const HudContext& context)
{
    if (m_rulesMode != context.mode) {
        loadModeRules(context.mode);
    }
    if (m_rules.empty()) {
        return;
    }
    const auto h = bgr555(0x7FDE);
    const Color header{h[0], h[1], h[2], 1};
    drawText(128, 10, Align::Center, 0, m_rules[0].text, &header);
    // Thirty characters a second, the header's thirty first.
    const int totalCharacters = static_cast<int>(context.introTime / (1 / 30.0f));
    if (totalCharacters < m_prevScrollingChars) {
        m_prevScrollingChars = 0; // the intro started over
    }
    if (totalCharacters > m_prevScrollingChars && totalCharacters > m_rules[0].length && totalCharacters <= m_rules.back().length) {
        // A blip for each letter typed.
        Sfx::instance().stopSoundById(SfxId::LETTER_BLIP);
        Sfx::instance().playFreeSfx(SfxId::LETTER_BLIP);
        m_prevScrollingChars = totalCharacters;
    }
    const auto c = bgr555(0x7F5A);
    const Color color{c[0], c[1], c[2], 1};
    float posY = 28;
    for (size_t i = 1; i < m_rules.size(); i++) {
        const int characters = totalCharacters - m_rules[i - 1].length;
        if (characters <= 0) {
            break;
        }
        const RulesLine& line = m_rules[i];
        drawText(static_cast<float>(line.offset + 12), posY, Align::Left, 0, line.text.substr(0, characters), &color, 1, 1, 8);
        posY += 13 + line.newlines * 8;
    }
}

void Hud::build(const Player& player, const HudContext& context, int width, int height, HudDrawList& out)
{
    m_out = &out;
    m_width = static_cast<float>(std::max(width, 1));
    m_height = static_cast<float>(std::max(height, 1));
    out.clear();
    const bool alt = player.isAltForm() || player.isMorphing() || player.isUnmorphing();
    // UpdateHud: the helmet, drawn back, visor, front -- not in alt form, and
    // not at all in Pro mode (HelmetOpacity and VisorOpacity are 0 there).
    if (!alt && !m_pro && context.matchState == MatchState::InProgress && !context.intro) {
        drawLayer(m_helmetDropTexture, 1, 2, 256 / 192.0f);
        drawLayer(m_visorTexture, 0.5f, 1, 256 / 192.0f);
        drawLayer(m_helmetTexture, 1, 2, 256 / 192.0f);
    }
    // DrawHudModels, then DrawHudObjects: once the match is over, or with
    // the scoreboard held, the screen dims under it and the rest goes.
    if (context.matchState == MatchState::GameOver) {
        drawFilter(12 / 31.0f);
        const auto c = bgr555(0x3FEF);
        const Color color{c[0], c[1], c[2], 1};
        drawText(128, 40, Align::Center, 0, m_strings.hudMessage(219), &color); // GAME OVER
        m_out = nullptr;
        return;
    }
    if (context.matchState == MatchState::Ending) {
        drawFilter(1);
        drawScoreboard(context);
        m_out = nullptr;
        return;
    }
    if (context.intro) {
        // The room's intro, dimmed, with the rules typed over it.
        drawFilter(15 / 31.0f);
        drawModeRules(context);
        drawMessages();
        m_out = nullptr;
        return;
    }
    if (context.showScoreboard) {
        drawFilter(1);
        drawMatchTime(context);
        drawScoreboard(context);
        m_out = nullptr;
        return;
    }
    if (player.health() > 0) {
        drawLocators(context);
        drawDamageIndicators(player);
    }
    drawRadar(player, context);
    if (player.health() > 0) {
        if (alt) {
            drawBoostBombs(player);
        } else {
            if (!m_pro) {
                drawAmmoBar(player);
                if (Sprite* reticle = sprite(m_objects.Reticle)) {
                    drawSprite(*reticle, 0, 0, 0.5f, 0.5f, 0, 1, true);
                }
            } else {
                drawCustomCrosshair(player);
            }
            if (context.hitMarker > 0) {
                drawHitMarker(context.hitMarker);
            }
            if (m_pro) {
                drawWeaponList(player);
            }
        }
        drawPrimeHunter();
        if (context.mode == GameMode::Bounty || context.mode == GameMode::BountyTeams || context.mode == GameMode::Capture) {
            drawOctolith(context);
        } else if (context.mode == GameMode::Nodes || context.mode == GameMode::NodesTeams) {
            drawNodes(context);
        }
        if (m_pro) {
            drawProHud(player, context);
        } else {
            drawModeScore(context);
            drawHealthbars(player);
        }
    }
    drawMessages();
    drawChat(context);
    m_out = nullptr;
}

void Hud::drawChat(const HudContext& context)
{
    // Online: the last lines of chat above the bottom left corner, and the
    // connection's trouble in red at the top.
    const Color ink{0.85f, 1.0f, 0.85f, 1};
    float y = 150;
    for (auto it = context.chat.rbegin(); it != context.chat.rend(); ++it) {
        drawText(6, y, Align::Left, 0, *it, &ink, 1, 0.75f);
        y -= 9;
    }
    if (!context.netStatus.empty()) {
        const Color red{1.0f, 0.35f, 0.3f, 1};
        drawText(128, 24, Align::Center, 0, context.netStatus, &red, 1, 0.8f);
    }
}

} // namespace fp
