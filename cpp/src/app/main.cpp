#include "formats/Entities.h"
#include "formats/Metadata.h"
#include "formats/Model.h"
#include "formats/Rooms.h"
#include "app/GameWindow.h"
#include "app/Launcher.h"
#include "app/Gamepad.h"
#include "app/Settings.h"
#include "game/Collision.h"
#include "game/Player.h"
#include "game/SceneBuilder.h"
#include "game/PlayerAi.h"
#include "game/World.h"
#include "game/NetGame.h"
#include "game/MatchRoom.h"
#include "game/ServerGame.h"
#include <atomic>
#include <csignal>
#include "net/NetServer.h"
#include "net/NetMaster.h"
#include "audio/Mixer.h"
#include "audio/Sfx.h"
#include "audio/Sequencer.h"
#include "audio/Music.h"
#include "net/NetClient.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTextStream>
#include <QThread>
#include <QVulkanInstance>

#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <cstdio>
#include <numbers>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace {

// paths.txt holds one "VERSION=DIR" line per game dump, as the C# build writes it.
std::optional<fs::path> filesFromPathsTxt(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const qsizetype eq = line.indexOf('=');
        if (eq != 5) {
            continue;
        }
        const QString value = line.mid(eq + 1).trimmed();
        if (!value.isEmpty() && QDir(value).exists()) {
            return fs::path(value.toStdString());
        }
    }
    return std::nullopt;
}

std::optional<fs::path> locateGameFiles(const QString& explicitDir)
{
    if (!explicitDir.isEmpty()) {
        return fs::path(explicitDir.toStdString());
    }
    if (const QByteArray env = qgetenv("FP_FILES"); !env.isEmpty()) {
        return fs::path(env.toStdString());
    }
    for (const QString& candidate : {QCoreApplication::applicationDirPath() + "/paths.txt", QDir::currentPath() + "/paths.txt"}) {
        if (auto found = filesFromPathsTxt(candidate)) {
            return found;
        }
    }
    return std::nullopt;
}

fp::Model loadRoomModel(const fs::path& root, const fp::RoomMetadata& room)
{
    const fs::path modelFile = fp::resolveCaseInsensitive(root, room.modelPath());
    const fs::path textureFile = fp::resolveCaseInsensitive(root, room.texturePath());
    fs::path animationFile;
    if (const std::string anim = room.animationPath(); !anim.empty()) {
        animationFile = fp::resolveCaseInsensitive(root, anim);
        if (!fs::exists(animationFile)) {
            animationFile.clear();
        }
    }
    return fp::Model::loadRoom(modelFile, textureFile, animationFile);
}

// Loads every MPH room and decodes every texture it references, without a window.
int checkAllRooms(const fs::path& root)
{
    int ok = 0;
    int failed = 0;
    int skipped = 0;
    for (const fp::RoomMetadata& room : fp::allRooms()) {
        if (room.firstHunt || room.hybrid) {
            skipped++;
            continue;
        }
        try {
            QElapsedTimer timer;
            timer.start();
            fp::Model model = loadRoomModel(root, room);
            size_t textures = 0;
            for (const fp::Material& m : model.materials()) {
                if (m.textureId >= 0 && m.textureId < static_cast<int>(model.textureCount())) {
                    const fp::Image image = model.decodeTexture(m.textureId, m.paletteId);
                    if (image.rgba.size() != static_cast<size_t>(image.width) * image.height) {
                        throw std::runtime_error("texture " + std::to_string(m.textureId) + " decoded to wrong size");
                    }
                    textures++;
                }
            }
            size_t entityCount = 0;
            if (!room.entityPath().empty()) {
                entityCount = fp::loadEntities(fp::resolveCaseInsensitive(root, room.entityPath()), -1).size();
            }
            std::printf("ok   %-32s %6zu vertices %4zu materials %4zu textures %4zu entities  %4lld ms\n", room.name,
                model.vertices().size(), model.materials().size(), textures, entityCount, static_cast<long long>(timer.elapsed()));
            ok++;
        } catch (const std::exception& e) {
            std::printf("FAIL %-32s %s\n", room.name, e.what());
            failed++;
        }
    }
    std::printf("\n%d rooms loaded, %d failed, %d First Hunt rooms skipped\n", ok, failed, skipped);
    return failed == 0 ? 0 : 1;
}

// Loads every model in the metadata table and decodes every texture of every recolor.
int checkAllModels(const fs::path& root)
{
    int ok = 0;
    int failed = 0;
    for (const fp::ModelMetadata& meta : fp::modelTable()) {
        try {
            fp::Model model = fp::Model::load(root, meta);
            size_t images = 0;
            for (size_t r = 0; r < model.recolorCount(); r++) {
                for (const fp::Material& m : model.materials()) {
                    if (m.textureId >= 0 && m.textureId < static_cast<int>(model.textureCount())) {
                        model.decodeTexture(m.textureId, m.paletteId, static_cast<int>(r));
                        images++;
                    }
                }
            }
            ok++;
            (void)images;
        } catch (const std::exception& e) {
            std::printf("FAIL %-32s %s\n", meta.name, e.what());
            failed++;
        }
    }
    std::printf("%d models loaded, %d failed\n", ok, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace

// --sound: the sound effects (or voice streams) one after the other, on the
// device or into a WAV file; prints what the sound files hold.
int soundTest(const fs::path& root, const QString& ids, const QString& wavPath)
{
    std::unique_ptr<fp::Mixer> mixer = fp::Mixer::open(wavPath.isEmpty());
    if (!mixer) {
        return 1;
    }
    fp::Sfx& sfx = fp::Sfx::instance();
    if (!sfx.load(root, mixer.get())) {
        return 1;
    }
    const fp::SoundData& data = sfx.data();
    std::printf("%zu samples, %zu ranges, %zu scripts, %zu DGN, %zu streams; output %d Hz\n", data.samples.size(), data.ranges.size(),
        data.scripts.size(), data.dgns.size(), data.streams.size(), mixer->sampleRate());
    std::vector<float> wav;
    auto run = [&](float seconds) {
        for (int tick = 0; tick < seconds * 60; tick++) {
            sfx.update(1 / 60.0f, {0, 0, 0}, {0, 0, -1}, {0, 1, 0});
            if (wavPath.isEmpty()) {
                QThread::msleep(16);
            } else {
                const int frames = mixer->sampleRate() / 60;
                const size_t at = wav.size();
                wav.resize(at + static_cast<size_t>(frames) * 2);
                mixer->render(wav.data() + at, frames);
            }
        }
    };
    std::unique_ptr<fp::SoundArchive> archive;
    fp::Sequencer sequencer;
    std::mutex sequencerLock;
    mixer->setMusic([&](float* out, int frames) {
        std::lock_guard lock(sequencerLock);
        sequencer.render(out, frames, 1);
    });
    for (const QString& item : ids.split(',', Qt::SkipEmptyParts)) {
        float length = 1;
        if (item.startsWith('s')) {
            // A sequence of sound_data.sdat, ten seconds of it.
            if (!archive) {
                archive = std::make_unique<fp::SoundArchive>(fp::readFile(fp::resolveCaseInsensitive(root, "data/sound/sound_data.sdat")));
            }
            fp::SoundArchive::Sequence seq;
            if (archive->sequence(item.mid(1).toInt(), seq)) {
                std::printf("sequence %d: %zu bytes, volume %d, channels %04x\n", item.mid(1).toInt(), seq.data.size(), seq.volume, seq.channelMask);
                std::lock_guard lock(sequencerLock);
                sequencer.start(seq, static_cast<uint32_t>(mixer->sampleRate()));
            }
            run(10);
            std::lock_guard lock(sequencerLock);
            sequencer.stop();
            continue;
        }
        if (item.startsWith('v')) {
            const int id = item.mid(1).toInt();
            if (id >= 0 && id < static_cast<int>(data.streams.size())) {
                const fp::SoundStream& stream = data.streams[id];
                length = static_cast<float>(stream.pcm->frames) / stream.pcm->sampleRate;
                std::printf("stream %d %s: %d channels, %d Hz, %.2f s\n", id, stream.name.c_str(), stream.pcm->channels,
                    stream.pcm->sampleRate, length);
            }
            sfx.playFreeStream(static_cast<fp::VoiceId>(id));
        } else {
            const int id = item.toInt(nullptr, 0);
            const int index = id & 0x3FFF;
            if ((id & 0xC000) == 0 && index < static_cast<int>(data.samples.size()) && data.samples[index].pcm) {
                const fp::SoundSample& sample = data.samples[index];
                length = static_cast<float>(sample.pcm->frames) / sample.pcm->sampleRate;
                std::printf("sample %d %s: %d Hz, %.2f s, loop %d (%d-%d), volume %.2f\n", index, sample.name.c_str(), sample.pcm->sampleRate,
                    length, sample.loop, sample.pcm->loopStart, sample.pcm->loopEnd, sample.volume);
            } else if ((id & 0x4000) && index < static_cast<int>(data.scripts.size())) {
                length = 2;
                std::printf("script %d %s: %zu entries\n", index, data.scripts[index].name.c_str(), data.scripts[index].entries.size());
            }
            fp::SoundSource source;
            source.update({0, 0, -2}, 0);
            if (id & 0x8000) {
                std::printf("DGN %d %s\n", index, index < static_cast<int>(data.dgns.size()) ? data.dgns[index].name.c_str() : "?");
                source.playSfx(id, false, false, -1, false, false, 0x7FFF, 0x7FFF);
                run(1);
                continue;
            }
            sfx.playFreeSfx(id);
        }
        run(std::min(length, 5.0f) + 0.2f);
    }
    sfx.unload();
    mixer->setMusic({});
    if (!wavPath.isEmpty()) {
        QFile file(wavPath);
        if (!file.open(QIODevice::WriteOnly)) {
            return 1;
        }
        // 16-bit stereo PCM.
        const uint32_t rate = static_cast<uint32_t>(mixer->sampleRate());
        const uint32_t bytes = static_cast<uint32_t>(wav.size() * 2);
        QByteArray out;
        auto put32 = [&](uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
        auto put16 = [&](uint16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); };
        out.append("RIFF");
        put32(36 + bytes);
        out.append("WAVEfmt ");
        put32(16);
        put16(1);
        put16(2);
        put32(rate);
        put32(rate * 4);
        put16(4);
        put16(16);
        out.append("data");
        put32(bytes);
        float peak = 0;
        for (float v : wav) {
            peak = std::max(peak, std::abs(v));
            put16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767)));
        }
        file.write(out);
        std::printf("wrote %s: %.2f s, peak %.3f\n", qPrintable(wavPath), wav.size() / 2.0 / rate, peak);
    }
    return 0;
}

// "host", "host:port": a server address (NetConfig.DefaultPort when none).
bool parseServerAddress(const QString& text, std::string& host, uint16_t& port)
{
    host = text.toStdString();
    port = fp::net::DefaultPort;
    if (const qsizetype colon = text.lastIndexOf(':'); colon > 0) {
        bool ok = false;
        const int value = text.mid(colon + 1).toInt(&ok);
        if (!ok || value <= 0 || value > 65535) {
            return false;
        }
        host = text.left(colon).toStdString();
        port = static_cast<uint16_t>(value);
    }
    return !host.empty();
}

// --net-probe: join a server without a room or a window and report what it
// says -- the admission, the match, the roster, the snapshots -- sending
// intents that stand this player where the server spawned it.
int netProbe(const QString& address, const QString& name, int hunter, double seconds)
{
    std::string host;
    uint16_t port;
    if (!parseServerAddress(address, host, port)) {
        std::fprintf(stderr, "bad server address \"%s\"\n", qPrintable(address));
        return 1;
    }
    fp::net::Client client;
    if (!client.connect(host, port, name.toStdString(), hunter, 0)) {
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    int lastSlot = -1;
    uint16_t lastMatch = 0;
    uint32_t lastRoster = 0;
    uint16_t lastLife = 0;
    long long frames = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds)) {
        next += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next);
        client.update();
        frames++;
        client.smoothing().tick();
        if (client.refused()) {
            std::printf("refused: %s\n", client.refusedReason().c_str());
            return 2;
        }
        if (!client.connected()) {
            std::printf("disconnected\n");
            return 2;
        }
        if (client.localSlot() != lastSlot) {
            lastSlot = client.localSlot();
            std::printf("t=%.2f admitted as slot %d\n", frames / 60.0, lastSlot);
        }
        if (client.match() && client.matchId() != lastMatch) {
            lastMatch = client.matchId();
            const fp::net::MatchState& m = *client.match();
            std::printf("t=%.2f match %u: %s mode %d, %.0f s left, goal %u, flags %x, next %s\n", frames / 60.0, m.matchId, m.roomKey.c_str(),
                m.mode, m.timeRemaining, m.pointGoal, m.flags, m.nextRoomKey.c_str());
        }
        if (client.rosterRevision() != lastRoster) {
            lastRoster = client.rosterRevision();
            std::printf("t=%.2f roster %u:", frames / 60.0, lastRoster);
            for (int i = 0; i < fp::net::SlotCapacity; i++) {
                const auto& s = client.slotInfo()[i];
                if (s.occupied) {
                    std::printf(" [%d %s hunter %d suit %d team %d ping %d gen %u]", i, s.name.c_str(), s.hunter, s.color, s.team, s.ping,
                        client.life(i).generation());
                }
            }
            std::printf("\n");
        }
        for (const fp::net::Chat& chat : client.takeChat()) {
            std::printf("t=%.2f chat %d: %s: %s\n", frames / 60.0, chat.kind, chat.name.c_str(), chat.text.c_str());
        }
        const int slot = client.localSlot();
        fp::net::Intent intent;
        if (slot >= 0) {
            if (const fp::net::PlayerState* own = client.state(slot)) {
                if (own->lifeId != lastLife) {
                    lastLife = own->lifeId;
                    std::printf("t=%.2f life %u: health %u at (%.2f %.2f %.2f) flags %x\n", frames / 60.0, own->lifeId, own->health,
                        own->position[0], own->position[1], own->position[2], own->flags);
                }
                if (own->health > 0 && (own->flags & fp::net::PlayerState::FlagSpawned)) {
                    intent.position = own->position;
                    intent.aim = own->facing;
                    intent.buttons |= fp::net::Buttons::InPlayState;
                    intent.weaponSelect = own->currentWeapon;
                    intent.ammoUa = 0;
                    intent.ammoMissiles = 50;
                }
            }
            client.noteStatesApplied();
        }
        client.sendIntent(intent);
        if (frames % 300 == 0) {
            std::printf("t=%.1f snapshots %lld (last frame %u, age %u), smoothing delay %d starved %lld, intents sent %lld, out of order %lld, "
                        "refused states %lld\n",
                frames / 60.0, client.snapshotsReceived(), client.lastSnapshotFrame(), client.snapshotAge(), client.smoothing().delay(),
                client.smoothing().starved(), client.intentsSent(), client.outOfOrder(), client.refusedStates());
            for (int i = 0; i < fp::net::SlotCapacity; i++) {
                if (const fp::net::PlayerState* s = client.state(i)) {
                    fp::Vec3 smoothed{};
                    bool alt = false;
                    const bool sampled = client.smoothing().sample(i, client.lives(), smoothed, alt);
                    std::printf("  slot %d life %u/%u health %u weapon %u pts %d k %u d %u at (%.2f %.2f %.2f) smoothed %s (%.2f %.2f %.2f)%s\n", i,
                        s->slotGeneration, s->lifeId, s->health, s->currentWeapon, s->points, s->kills, s->deaths, s->position[0],
                        s->position[1], s->position[2], sampled ? "yes" : "no", smoothed[0], smoothed[1], smoothed[2],
                        client.intent(i) ? " (intents)" : "");
                }
            }
            std::fflush(stdout);
        }
    }
    client.disconnect();
    return 0;
}

// --lobby-probe: join a server without a room or a window and walk its
// session: the lobby (as its owner, set the match up and start it; else get
// ready), the load barrier, the match (standing where it is put), the
// results' ballot and the way back. Prints each phase and what the server says.
//   actions (comma separated): update (--room/--mode), ready, start, pick:ROOM, vote:ROOM
int lobbyProbe(const QString& address, const QString& name, int hunter, double seconds, const QStringList& actions,
    const std::string& room, int wireMode, const QString& ownerToken)
{
    std::string host;
    uint16_t port;
    if (!parseServerAddress(address, host, port)) {
        std::fprintf(stderr, "bad server address \"%s\"\n", qPrintable(address));
        return 1;
    }
    fp::net::Client client;
    if (!ownerToken.isEmpty()) {
        std::array<uint8_t, 16> token{};
        if (!fp::net::parseGuid(ownerToken.toStdString(), token)) {
            std::fprintf(stderr, "bad owner token\n");
            return 1;
        }
        client.setOwnerToken(token);
    }
    if (!client.connect(host, port, name.toStdString(), hunter, 0)) {
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    long long frames = 0;
    int lastPhase = -1;
    uint16_t lastRevision = 0, lastMatch = 0;
    std::string lastMessage;
    bool updated = false, readied = false, started = false, picked = false, voted = false;
    auto has = [&](const char* action) { return actions.contains(QLatin1String(action)); };
    auto valueOf = [&](const char* prefix) {
        for (const QString& a : actions) {
            if (a.startsWith(QLatin1String(prefix))) {
                return a.mid(static_cast<int>(std::strlen(prefix))).toStdString();
            }
        }
        return std::string();
    };
    static const char* phases[] = {"lobby", "starting", "in match", "post match"};
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds)) {
        next += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(next);
        client.update();
        frames++;
        client.smoothing().tick();
        const double t = frames / 60.0;
        if (client.refused()) {
            std::printf("refused: %s\n", client.refusedReason().c_str());
            return 2;
        }
        if (!client.connected()) {
            std::printf("disconnected\n");
            return 2;
        }
        for (const fp::net::Chat& chat : client.takeChat()) {
            std::printf("t=%.2f chat: %s%s%s\n", t, chat.name.c_str(), chat.name.empty() ? "" : ": ", chat.text.c_str());
        }
        const auto& session = client.session();
        if (!session || client.localSlot() < 0) {
            continue;
        }
        if (session->phase != lastPhase || session->revision != lastRevision || session->matchId != lastMatch) {
            if (session->phase != lastPhase || session->matchId != lastMatch) {
                std::printf("t=%.2f %s (%s): match %u on %s, mode %d, format %d, %u s, goal %u, owner slot %d%s, expected %02X loaded %02X, "
                            "layers %d\n",
                    t, phases[std::min<int>(session->phase, 3)], session->policy ? "lobby" : "continuous", session->matchId,
                    session->roomKey.c_str(), session->mode, session->format, session->timeLimit, session->pointGoal,
                    session->ownerSlot == 0xFF ? -1 : session->ownerSlot, client.lobbyOwner() ? " (me)" : "", session->expectedParticipants,
                    session->loadedParticipants, session->entityLayerPlayers);
            }
            lastPhase = session->phase;
            lastRevision = session->revision;
            lastMatch = session->matchId;
        }
        if (client.lobbyMessage() != lastMessage) {
            lastMessage = client.lobbyMessage();
            std::printf("t=%.2f lobby: %s\n", t, lastMessage.empty() ? "ok" : lastMessage.c_str());
        }
        if (session->phase == fp::net::SessionState::PhaseLobby && !client.lobbyCommandPending() && frames % 30 == 0) {
            if (has("update") && client.lobbyOwner() && !updated) {
                fp::net::SessionState config = *session;
                if (!room.empty()) {
                    config.roomKey = room;
                }
                if (wireMode >= 0) {
                    config.mode = static_cast<uint8_t>(wireMode);
                    config.format = fp::net::SessionState::FormatAuto;
                }
                updated = client.sendLobbyCommand(fp::net::LobbyCommand::UpdateMatch, 0xFF, -1, false, &config);
                std::printf("t=%.2f update: %s mode %d\n", t, config.roomKey.c_str(), config.mode);
            } else if (has("ready") && !readied && (updated || !has("update") || !client.lobbyOwner())) {
                readied = client.sendLobbyCommand(fp::net::LobbyCommand::SetReady, 0xFF, -1, true);
                std::printf("t=%.2f ready\n", t);
            } else if (has("start") && client.lobbyOwner() && readied && !started) {
                bool all = true;
                int count = 0;
                for (const auto& info : client.slotInfo()) {
                    if (info.occupied) {
                        count++;
                        all = all && info.lobbyReady;
                    }
                }
                if (all && count >= 2) {
                    started = client.sendLobbyCommand(fp::net::LobbyCommand::StartMatch);
                    std::printf("t=%.2f start (%d players ready)\n", t, count);
                }
            }
        }
        if (session->phase == fp::net::SessionState::PhaseStarting && client.shouldLoadMatch()) {
            client.markMatchLoaded();
        }
        if (session->phase == fp::net::SessionState::PhaseInMatch) {
            readied = started = false;
            if (const std::string map = valueOf("vote:"); !map.empty() && !voted && t > 3) {
                voted = true;
                client.sendVote(fp::net::Vote::KindPropose, map);
                std::printf("t=%.2f proposed %s\n", t, map.c_str());
            }
            if (const auto& vote = client.vote(); vote && vote->state == fp::net::VoteState::StateRunning && !client.voteAnswered()) {
                client.sendVote(fp::net::Vote::KindYes);
                std::printf("t=%.2f vote on %s by %s: yes\n", t, vote->roomKey.c_str(), vote->proposer.c_str());
            }
        }
        if (const auto& choices = client.mapChoices(); choices && choices->open) {
            if (const std::string map = valueOf("pick:"); !map.empty() && !picked) {
                picked = true;
                client.sendMapPick(map);
                std::printf("t=%.2f picked %s\n", t, map.c_str());
            }
            static std::string lastTally;
            std::string tally;
            for (const auto& [key, votes] : choices->choices) {
                tally += " " + key + "=" + std::to_string(votes);
            }
            if (tally != lastTally) {
                lastTally = tally;
                std::printf("t=%.2f ballot (%u eligible):%s\n", t, choices->eligible, tally.c_str());
            }
        } else {
            picked = false;
        }
        // Stand where the server put this player, and be ready after the results.
        fp::net::Intent intent;
        if (const fp::net::PlayerState* own = client.state(client.localSlot())) {
            if (own->health > 0 && (own->flags & fp::net::PlayerState::FlagSpawned)) {
                intent.position = own->position;
                intent.aim = own->facing;
                intent.buttons |= fp::net::Buttons::InPlayState;
                intent.weaponSelect = own->currentWeapon;
                intent.ammoMissiles = 50;
            }
        }
        if (client.match() && client.match()->ending()) {
            intent.buttons |= fp::net::Buttons::ReadyState;
        }
        if (has("spectate")) {
            intent.buttons |= fp::net::Buttons::SpectatingState;
        }
        if (frames % 120 == 0) {
            // Who the server says is watching rather than playing.
            std::string watching;
            for (int i = 0; i < fp::net::SlotCapacity; i++) {
                if (const fp::net::PlayerState* st = client.state(i); st && (st->flags & fp::net::PlayerState::FlagSpectating)) {
                    watching += " " + std::to_string(i);
                }
            }
            if (!watching.empty()) {
                std::printf("t=%.2f spectating:%s\n", t, watching.c_str());
            }
        }
        client.noteStatesApplied();
        client.sendIntent(intent);
    }
    client.disconnect();
    return 0;
}

// --demo-info: what a demo holds -- records, frames, a packet histogram, the
// compression -- and with --replay, how the packets land frame by frame.
int demoInfo(const QString& path, bool replay)
{
    auto reader = fp::net::DemoReader::open(path.toStdString());
    if (!reader) {
        std::printf("%s: not a demo this build reads\n", qPrintable(path));
        return 1;
    }
    std::map<int, std::pair<long long, long long>> byType; // count, bytes
    long long records = 0, bytes = 0;
    uint32_t lastFrame = 0;
    while (auto record = reader->next()) {
        records++;
        bytes += static_cast<long long>(record->data.size());
        lastFrame = record->frame;
        auto& entry = byType[record->data.empty() ? -1 : record->data[0]];
        entry.first++;
        entry.second += static_cast<long long>(record->data.size());
    }
    const double seconds = lastFrame / 60.0;
    const long long fileBytes = static_cast<long long>(QFileInfo(path).size());
    std::printf("%s: protocol %u, %lld records over %u frames (%.1f s), %.1f KiB of packets in %.1f KiB (%.1fx, %.1f KiB/s)\n",
        qPrintable(path), reader->protocol(), records, lastFrame, seconds, bytes / 1024.0, fileBytes / 1024.0,
        fileBytes > 0 ? static_cast<double>(bytes) / fileBytes : 0.0, seconds > 0 ? fileBytes / 1024.0 / seconds : 0.0);
    static const std::map<int, const char*> names = {{4, "Snapshot"}, {8, "MatchState"}, {9, "MapChange"}, {10, "Roster"}, {13, "SlotIntent"},
        {23, "Chat"}, {27, "VoteState"}, {28, "MapChoices"}, {31, "HitVerdict"}, {36, "SessionState"}, {6, "Ping"}, {2, "Welcome"}};
    for (const auto& [type, entry] : byType) {
        const auto name = names.find(type);
        std::printf("  %-12s %7lld records %9.1f KiB\n", name != names.end() ? name->second : std::to_string(type).c_str(), entry.first,
            entry.second / 1024.0);
    }
    if (byType.find(4) == byType.end()) {
        std::printf("no snapshots: this demo would play an empty room\n");
        return 1;
    }
    if (!replay) {
        return 0;
    }
    fp::net::Client client;
    std::string error;
    if (!client.openDemo(path.toStdString(), error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    long long frames = 0, one = 0, none = 0, burst = 0;
    int gap = 0, worstGap = 0;
    long long lastCount = client.snapshotsReceived();
    while (!client.playbackEnded() && frames < 60LL * 60 * 60) {
        client.update();
        frames++;
        const long long got = client.snapshotsReceived() - lastCount;
        lastCount = client.snapshotsReceived();
        if (got == 0) {
            none++;
            worstGap = std::max(worstGap, ++gap);
        } else {
            gap = 0;
            (got == 1 ? one : burst)++;
        }
    }
    std::printf("replay: %lld frames, %.1f%% with exactly one snapshot, %lld with more, %lld with none, longest run with none %d\n", frames,
        frames > 0 ? 100.0 * one / frames : 0.0, burst, none, worstGap);
    return frames > 0 && (100.0 * burst / frames > 5 || worstGap > 10) ? 1 : 0;
}

// --launcher: the front screen, with a room flying its intro behind it.
// Everything a player does from here on is the Launcher's.
int runLauncher(const fs::path& root, const QString& sizeText, bool vsync, int frameCap, bool validate, bool mute,
    const QString& shot, int shotFrames, bool offscreen, const QString& screen)
{
    QVulkanInstance instance;
    const QVersionNumber supported = instance.supportedApiVersion();
    instance.setApiVersion(supported >= QVersionNumber(1, 3) ? QVersionNumber(1, 3) : supported);
    if (instance.supportedExtensions().contains("VK_EXT_swapchain_colorspace")) {
        instance.setExtensions({"VK_EXT_swapchain_colorspace"});
    }
    if (validate) {
        instance.setLayers({"VK_LAYER_KHRONOS_validation"});
    }
    if (!instance.create()) {
        std::fprintf(stderr, "Vulkan is not available (VkResult %d).\n", instance.errorCode());
        return 1;
    }

    fp::Settings settings;
    fp::GamepadInput gamepad;
    fp::GameWindow window(nullptr);
    fp::Launcher launcher(window, settings, gamepad, root);
    window.setGamepad(&gamepad);
    window.setGameFiles(root);
    window.setVSync(vsync);
    window.setFrameCap(frameCap);
    window.setVulkanInstance(&instance);
    const QStringList size = sizeText.split('x');
    window.resize(size.value(0).toInt() > 0 ? size.value(0).toInt() : 1280, size.value(1).toInt() > 0 ? size.value(1).toInt() : 720);
    // The room a server moves to, while a match on one is being played.
    window.setRoomLoader([&](const std::string& key, fp::GameMode mode) -> std::optional<fp::MatchRoom> {
        const fp::RoomMetadata* next = fp::findRoom(key);
        return next ? fp::loadMatchRoom(root, *next, mode, 8, std::clamp(settings.integer(QStringLiteral("player.hunter")), 0, 6), true)
                    : std::nullopt;
    });
    if (!mute) {
        if (std::unique_ptr<fp::Mixer> mixer = fp::Mixer::open(); mixer && fp::Sfx::instance().load(root, mixer.get())) {
            fp::Music::instance().load(root, mixer.get());
            window.setAudio(std::move(mixer));
        }
    }
    // The screens see the launcher and the settings by name.
    window.setUiEnabled(true);
    window.setUiSetup([&](fp::UiOverlay& ui) {
        ui.engine()->rootContext()->setContextProperty(QStringLiteral("launcher"), &launcher);
        ui.engine()->rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    });
    launcher.applySettings();
    launcher.showStart();
    if (screen.startsWith(QLatin1String("join:"))) {
        launcher.joinServer(screen.mid(5)); // --ui-screen join:HOST[:PORT], for the checks
    } else if (screen == QLatin1String("match")) {
        launcher.startOffline(); // --ui-screen match: straight into a match, for the checks
    } else if (!screen.isEmpty()) {
        launcher.setScreen(screen); // --ui-screen: photograph one screen rather than the first
    }
    if (!shot.isEmpty()) {
        window.screenshotAfter(std::max(1, shotFrames), shot);
    }
    if (offscreen) {
        if (!window.startOffscreen(QSize(window.width(), window.height()))) {
            std::fprintf(stderr, "Offscreen Vulkan setup failed.\n");
            return 1;
        }
    } else {
        window.show();
    }
    const int code = QGuiApplication::exec();
    window.deviceAboutToBeDestroyed(); // the screens let go before what they read does
    settings.save();
    return code;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Fruity Prime"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Fruity Prime — C++/Vulkan room viewer"));
    parser.addHelpOption();
    QCommandLineOption filesOpt(QStringLiteral("files"), QStringLiteral("Extracted game files (the AMHP1 directory)."), QStringLiteral("dir"));
    QCommandLineOption roomOpt(QStringLiteral("room"), QStringLiteral("Room to show, internal or in-game name."), QStringLiteral("name"),
        QStringLiteral("MP3 PROVING GROUND"));
    QCommandLineOption listOpt(QStringLiteral("list"), QStringLiteral("List the rooms and exit."));
    QCommandLineOption checkOpt(QStringLiteral("check-all"), QStringLiteral("Load every room without a window; exit 0 when all load."));
    QCommandLineOption shotOpt(QStringLiteral("shot"), QStringLiteral("Save a screenshot after --frames frames, then exit."), QStringLiteral("png"));
    QCommandLineOption framesOpt(QStringLiteral("frames"), QStringLiteral("Frames before --shot."), QStringLiteral("n"), QStringLiteral("30"));
    QCommandLineOption camOpt(QStringLiteral("cam"), QStringLiteral("Camera as x,y,z,yaw,pitch."), QStringLiteral("values"));
    QCommandLineOption playersOpt(QStringLiteral("players"), QStringLiteral("Player count for the node and entity layers (default: 1 + targets + bots, at least 2)."), QStringLiteral("n"));
    QCommandLineOption validateOpt(QStringLiteral("validate"), QStringLiteral("Enable the Vulkan validation layer."));
    QCommandLineOption dumpOpt(QStringLiteral("dump-textures"), QStringLiteral("Write the room's decoded textures as PNGs, then exit."), QStringLiteral("dir"));
    QCommandLineOption objOpt(QStringLiteral("dump-obj"), QStringLiteral("Write the room geometry as OBJ, then exit."), QStringLiteral("file"));
    QCommandLineOption flyOpt(QStringLiteral("fly"), QStringLiteral("Start with the free camera instead of walking."));
    QCommandLineOption hunterOpt(QStringLiteral("hunter"), QStringLiteral("Hunter index 0-7 for movement values (0 Samus)."), QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption soundOpt(QStringLiteral("sound"),
        QStringLiteral("Play sound effects by id (comma-separated; v3 for voice stream 3) and exit."), QStringLiteral("ids"));
    QCommandLineOption soundWavOpt(QStringLiteral("sound-wav"), QStringLiteral("With --sound: write the mix to this WAV file instead."),
        QStringLiteral("file"));
    QCommandLineOption muteOpt(QStringLiteral("mute"), QStringLiteral("No sound."));
    QCommandLineOption introOpt(QStringLiteral("intro"),
        QStringLiteral("Start the match on the room's intro, spawning on FIRE: on (default when playing in a window) or off."),
        QStringLiteral("on|off"));
    QCommandLineOption walkTestOpt(QStringLiteral("walk-test"), QStringLiteral("Simulate a scripted walk from the spawn without a window, print the path."));
    QCommandLineOption checkModelsOpt(QStringLiteral("check-models"), QStringLiteral("Load every model in the metadata table without a window."));
    QCommandLineOption entitiesOpt(QStringLiteral("entities"), QStringLiteral("List the room's entities for the match layer, then exit."));
    QCommandLineOption spawnOpt(QStringLiteral("spawn"), QStringLiteral("Start the camera at player spawn N (default 0)."), QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("Walk as the player even with --shot/--bench."));
    QCommandLineOption scriptOpt(QStringLiteral("script"),
        QStringLiteral("Scripted input, one step per half second, e.g. \"w;w;m;.\" (see InputScript.h). Implies --play; --shot waits for it."),
        QStringLiteral("steps"));
    QCommandLineOption hudOpt(QStringLiteral("hud"), QStringLiteral("HUD while walking: stock, pro or off."), QStringLiteral("mode"),
        QStringLiteral("stock"));
    QCommandLineOption offscreenOpt(QStringLiteral("offscreen"), QStringLiteral("Render without a window or presentation (with --bench/--shot)."));
    QCommandLineOption vsyncOpt(QStringLiteral("vsync"), QStringLiteral("on (default) or off."), QStringLiteral("on|off"), QStringLiteral("on"));
    QCommandLineOption capOpt(QStringLiteral("fps-cap"), QStringLiteral("Frame rate cap, 0 for none."), QStringLiteral("fps"), QStringLiteral("0"));
    QCommandLineOption benchOpt(QStringLiteral("bench"), QStringLiteral("Render for N seconds after a 3 s warmup, print stats, exit."), QStringLiteral("seconds"));
    QCommandLineOption noCullOpt(QStringLiteral("nocull"), QStringLiteral("Disable face culling (debug)."));
    QCommandLineOption targetsOpt(QStringLiteral("targets"),
        QStringLiteral("Other hunters standing at the spawn points to shoot at (no AI yet)."), QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption botsOpt(QStringLiteral("bots"), QStringLiteral("Bots (PlayerAi) in the match, each a different hunter."),
        QStringLiteral("n"), QStringLiteral("0"));
    QCommandLineOption botLevelOpt(QStringLiteral("bot-level"), QStringLiteral("Bot difficulty: 0 easy, 1 medium, 2 hard, 3 insane."),
        QStringLiteral("n"), QStringLiteral("2"));
    QCommandLineOption modeOpt(QStringLiteral("mode"),
        QStringLiteral("Game mode: battle (default), survival, primehunter, capture, bounty, nodes or defender; "
                       "battle, survival, bounty, nodes and defender also as teams (battleteams, ...)."),
        QStringLiteral("mode"),
        QStringLiteral("battle"));
    QCommandLineOption pointGoalOpt(QStringLiteral("point-goal"),
        QStringLiteral("Points that win (battle 7, bounty 3, nodes 70), or spare lives in survival (2); 0 for none."), QStringLiteral("n"));
    QCommandLineOption timeLimitOpt(QStringLiteral("time-limit"),
        QStringLiteral("Match length in minutes (battle 7, the other modes 15), 0 for none."), QStringLiteral("minutes"));
    QCommandLineOption timeGoalOpt(QStringLiteral("time-goal"),
        QStringLiteral("Prime time (Prime Hunter) or ring time (Defender) that wins, in seconds (90); 0 for none."), QStringLiteral("seconds"));
    QCommandLineOption teamsOpt(QStringLiteral("teams"), QStringLiteral("Number of teams in a team mode, 2-4 (Capture: 2)."),
        QStringLiteral("n"), QStringLiteral("2"));
    QCommandLineOption friendlyFireOpt(QStringLiteral("friendly-fire"), QStringLiteral("Teammates hurt each other."));
    QCommandLineOption allWeaponsOpt(QStringLiteral("all-weapons"), QStringLiteral("Start with every weapon and full ammo."));
    QCommandLineOption netProbeOpt(QStringLiteral("net-probe"),
        QStringLiteral("Join a server without a window and print what it sends (host[:port]); --seconds, --name, --hunter."), QStringLiteral("address"));
    QCommandLineOption netStatusOpt(QStringLiteral("net-status"),
        QStringLiteral("Ask a server what it is running, without joining (host[:port])."), QStringLiteral("address"));
    QCommandLineOption connectOpt(QStringLiteral("connect"),
        QStringLiteral("Join the match a server is running (host[:port]); --name and --hunter say who you are."), QStringLiteral("address"));
    QCommandLineOption nameOpt(QStringLiteral("name"), QStringLiteral("Player name online."), QStringLiteral("name"), QStringLiteral("Player"));
    QCommandLineOption secondsOpt(QStringLiteral("seconds"), QStringLiteral("How long --net-probe runs."), QStringLiteral("s"), QStringLiteral("20"));
    QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Window size WxH."), QStringLiteral("size"), QStringLiteral("1280x720"));
    QCommandLineOption serverOpt(QStringLiteral("server"),
        QStringLiteral("Run a dedicated server without a window: it runs the match (--port, --max-players, --rotation, --server-name; "
                       "without --rotation, --room and --mode with --time-limit and --point-goal)."));
    QCommandLineOption portOpt(QStringLiteral("port"), QStringLiteral("The server's UDP port."), QStringLiteral("n"), QStringLiteral("27888"));
    QCommandLineOption maxPlayersOpt(QStringLiteral("max-players"), QStringLiteral("The server's player limit, 2-8."), QStringLiteral("n"),
        QStringLiteral("4"));
    QCommandLineOption rotationOpt(QStringLiteral("rotation"),
        QStringLiteral("The server's map rotation: one match a line, \"ROOM KEY | mode | minutes | points\"."), QStringLiteral("file"));
    QCommandLineOption serverNameOpt(QStringLiteral("server-name"), QStringLiteral("What the server calls itself."), QStringLiteral("name"),
        QStringLiteral("Fruity Prime C++"));
    QCommandLineOption lobbyOpt(QStringLiteral("lobby"),
        QStringLiteral("Server: a persistent lobby -- players gather, the first one in (the owner) sets up and starts each match."));
    QCommandLineOption formatOpt(QStringLiteral("format"),
        QStringLiteral("Server: the team format, auto (default), ffa, 1v1, 2v2, 3v3, 4v4 or 2v2v2v2."), QStringLiteral("format"),
        QStringLiteral("auto"));
    QCommandLineOption noVotesOpt(QStringLiteral("no-votes"), QStringLiteral("Server: no map votes, mid-match or on the results screen."));
    QCommandLineOption ownerTokenOpt(QStringLiteral("owner-token"),
        QStringLiteral("Server: the lobby belongs to the client presenting this token (32 hex digits); client: present it."),
        QStringLiteral("hex"));
    QCommandLineOption lobbyProbeOpt(QStringLiteral("lobby-probe"),
        QStringLiteral("Join a server without a window and walk its session (host[:port]); --lobby-actions, --seconds."),
        QStringLiteral("address"));
    QCommandLineOption lobbyActionsOpt(QStringLiteral("lobby-actions"),
        QStringLiteral("--lobby-probe's actions: update (to --room/--mode), ready, start, pick:ROOM, vote:ROOM."), QStringLiteral("list"));
    QCommandLineOption masterOpt(QStringLiteral("master"),
        QStringLiteral("The server directory (host[:port], default net.livetek.fr:27889): where a server announces itself, what "
                       "--servers asks, who --host-game asks."),
        QStringLiteral("address"));
    QCommandLineOption noMasterOpt(QStringLiteral("no-master"), QStringLiteral("Server: announce to no directory."));
    QCommandLineOption hostPortsOpt(QStringLiteral("host-ports"),
        QStringLiteral("Server or directory: open games for players who cannot open a port, on ports A-B (\"none\" for none)."),
        QStringLiteral("A-B"));
    QCommandLineOption exitEmptyOpt(QStringLiteral("exit-when-empty"), QStringLiteral("Server: close once nobody has been on for S seconds."),
        QStringLiteral("S"));
    QCommandLineOption graceOpt(QStringLiteral("startup-grace"), QStringLiteral("Server: with --exit-when-empty, wait this long for the first player."),
        QStringLiteral("S"), QStringLiteral("180"));
    QCommandLineOption noJoinOpt(QStringLiteral("no-join-in-progress"), QStringLiteral("Server: nobody joins a match already running."));
    QCommandLineOption masterServerOpt(QStringLiteral("master-server"),
        QStringLiteral("Run the server directory (UDP --port, default 27889; --public HOST, --host-ports A-B)."));
    QCommandLineOption publicOpt(QStringLiteral("public"), QStringLiteral("Directory: the name to publish for servers on its own machine or network."),
        QStringLiteral("host"));
    QCommandLineOption serversOpt(QStringLiteral("servers"), QStringLiteral("Print the directory's server list, each server asked in turn."));
    QCommandLineOption hostGameOpt(QStringLiteral("host-game"),
        QStringLiteral("Ask the directory (--master) to run a match on ROOM (--mode, --time-limit, --point-goal, --max-players; "
                       "--maps \"A,B\" for more) and join it."),
        QStringLiteral("room"));
    QCommandLineOption hostLocalOpt(QStringLiteral("host-local"),
        QStringLiteral("Start a dedicated server on this machine for these maps (\"A,B,C\"), on the first free port from 27888, and print it."),
        QStringLiteral("maps"));
    QCommandLineOption mapsOpt(QStringLiteral("maps"), QStringLiteral("--host-game: the rest of the map cycle, comma separated."), QStringLiteral("list"));
    QCommandLineOption uiScreenOpt(QStringLiteral("ui-screen"),
        QStringLiteral("With --launcher: open on this screen (start, play, settings, pause)."), QStringLiteral("name"));
    parser.addOption(uiScreenOpt);
    QCommandLineOption launcherOpt(QStringLiteral("launcher"),
        QStringLiteral("Open the front screen: play against bots, join a server, settings. The default with no room given."));
    parser.addOption(launcherOpt);
    QCommandLineOption recordDemoOpt(QStringLiteral("record-demo"), QStringLiteral("Online: record the match to this .fpdemo file."),
        QStringLiteral("file"));
    QCommandLineOption demoOpt(QStringLiteral("demo"), QStringLiteral("Watch a recorded match (.fpdemo)."), QStringLiteral("file"));
    QCommandLineOption demoInfoOpt(QStringLiteral("demo-info"),
        QStringLiteral("Print what a demo holds (--replay: and how its packets land frame by frame)."), QStringLiteral("file"));
    QCommandLineOption replayOpt(QStringLiteral("replay"), QStringLiteral("With --demo-info: replay it through the client."));
    parser.addOptions({recordDemoOpt, demoOpt, demoInfoOpt, replayOpt});
    parser.addOptions({serverOpt, portOpt, maxPlayersOpt, rotationOpt, serverNameOpt, lobbyOpt, formatOpt, noVotesOpt, ownerTokenOpt,
        lobbyProbeOpt, lobbyActionsOpt, masterOpt, noMasterOpt, hostPortsOpt, exitEmptyOpt, graceOpt, noJoinOpt, masterServerOpt, publicOpt,
        serversOpt, hostGameOpt, hostLocalOpt, mapsOpt});
    parser.addOptions({filesOpt, roomOpt, listOpt, checkOpt, shotOpt, framesOpt, camOpt, playersOpt, validateOpt, sizeOpt, dumpOpt, noCullOpt, objOpt, benchOpt, vsyncOpt, capOpt, offscreenOpt, entitiesOpt, spawnOpt, checkModelsOpt, flyOpt, hunterOpt, muteOpt, soundOpt, soundWavOpt, introOpt, walkTestOpt, playOpt, scriptOpt, hudOpt, targetsOpt, allWeaponsOpt, botsOpt, botLevelOpt, modeOpt, pointGoalOpt, timeLimitOpt, timeGoalOpt, teamsOpt, friendlyFireOpt, netProbeOpt, netStatusOpt, connectOpt, nameOpt, secondsOpt});
    parser.process(app);

    if (parser.isSet(listOpt)) {
        for (const fp::RoomMetadata& room : fp::allRooms()) {
            std::printf("%3d  %-32s %s%s\n", room.id, room.name, room.inGameName ? room.inGameName : "",
                room.firstHunt || room.hybrid ? "  (First Hunt)" : "");
        }
        return 0;
    }

    if (parser.isSet(netStatusOpt)) {
        std::string host;
        uint16_t port;
        if (!parseServerAddress(parser.value(netStatusOpt), host, port)) {
            std::fprintf(stderr, "bad server address\n");
            return 1;
        }
        int latency = 0;
        const auto status = fp::net::queryStatus(host, port, 3000, latency);
        if (!status) {
            std::printf("%s:%u did not answer\n", host.c_str(), port);
            return 2;
        }
        std::printf("%s:%u \"%s\": %s, mode %d, %u/%u players, %.0f s left, protocol %u (this build %d)%s, %d ms\n", host.c_str(), port,
            status->name.c_str(), status->match.roomKey.c_str(), status->match.mode, status->match.playerCount, status->maxPlayers,
            status->match.timeRemaining, status->protocol, fp::net::ProtocolVersion,
            status->protocol == fp::net::ProtocolVersion ? "" : " -- CANNOT JOIN", latency);
        return status->protocol == fp::net::ProtocolVersion ? 0 : 3;
    }
    // --master: the directory, host[:port].
    std::string masterHost = fp::net::MasterDefaultHost;
    uint16_t masterPort = fp::net::MasterDefaultPort;
    if (parser.isSet(masterOpt)) {
        const QString value = parser.value(masterOpt);
        masterHost = value.section(':', 0, 0).toStdString();
        if (value.contains(':')) {
            masterPort = static_cast<uint16_t>(value.section(':', 1).toUInt());
        }
    }
    auto hostPorts = [&](uint16_t& first, uint16_t& last) {
        first = last = 0;
        const QString value = parser.value(hostPortsOpt);
        if (!value.isEmpty() && value != QStringLiteral("none")) {
            first = static_cast<uint16_t>(value.section('-', 0, 0).toUInt());
            last = static_cast<uint16_t>(value.section('-', 1, 1).toUInt());
        }
    };
    if (parser.isSet(demoInfoOpt)) {
        return demoInfo(parser.value(demoInfoOpt), parser.isSet(replayOpt));
    }
    if (parser.isSet(serversOpt)) {
        const fp::net::MasterListResult list = fp::net::queryMaster(masterHost, masterPort);
        if (!list.answered) {
            std::printf("the directory %s:%u did not answer\n", masterHost.c_str(), masterPort);
            return 2;
        }
        std::printf("%s:%u lists %zu server(s)%s\n", masterHost.c_str(), masterPort, list.servers.size(),
            !list.canHost ? " (too old to say whether it starts games)" : *list.canHost ? ", and starts games" : ", and does not start games");
        for (const fp::net::MasterListing& server : list.servers) {
            int latency = 0;
            const auto status = fp::net::queryStatus(server.address, server.port, 1500, latency);
            if (status) {
                std::printf("  %s:%u \"%s\": %s, mode %d, %u/%u players, %d ms%s%s\n", server.address.c_str(), server.port, status->name.c_str(),
                    status->match.roomKey.c_str(), status->match.mode, status->match.playerCount, status->maxPlayers, latency,
                    status->protocol != fp::net::ProtocolVersion ? " -- another version" : "", (status->flags & 1) ? ", opens games" : "");
            } else {
                std::printf("  %s:%u \"%s\": %s, %d/%d players -- did not answer\n", server.address.c_str(), server.port, server.name.c_str(),
                    server.roomKey.c_str(), server.players, server.maxPlayers);
            }
        }
        return 0;
    }
    if (parser.isSet(lobbyProbeOpt)) {
        int wire = -1;
        if (parser.isSet(modeOpt)) {
            static const char* modes[] = {"battle", "battleteams", "survival", "survivalteams", "capture", "bounty", "bountyteams", "nodes",
                "nodesteams", "defender", "defenderteams", "primehunter"};
            for (int i = 0; i < 12; i++) {
                if (parser.value(modeOpt).toLower() == QLatin1String(modes[i])) {
                    wire = fp::net::wireMode(i);
                }
            }
        }
        return lobbyProbe(parser.value(lobbyProbeOpt), parser.value(nameOpt), std::clamp(parser.value(hunterOpt).toInt(), 0, 6),
            parser.value(secondsOpt).toDouble(), parser.value(lobbyActionsOpt).split(',', Qt::SkipEmptyParts),
            parser.isSet(roomOpt) ? parser.value(roomOpt).toStdString() : std::string(), wire, parser.value(ownerTokenOpt));
    }
    if (parser.isSet(netProbeOpt)) {
        return netProbe(parser.value(netProbeOpt), parser.value(nameOpt), std::clamp(parser.value(hunterOpt).toInt(), 0, 6),
            parser.value(secondsOpt).toDouble());
    }

    const auto root = locateGameFiles(parser.value(filesOpt));
    if (!root) {
        std::fprintf(stderr, "Game files not found: pass --files DIR, set FP_FILES, or put paths.txt next to the binary.\n");
        return 1;
    }

    if (parser.isSet(masterServerOpt)) {
        fp::net::MasterServer::Config config;
        config.port = parser.isSet(portOpt) ? static_cast<uint16_t>(parser.value(portOpt).toUInt()) : fp::net::MasterDefaultPort;
        config.publicHost = parser.value(publicOpt).toStdString();
        config.gameFiles = *root;
        if (parser.isSet(hostPortsOpt)) {
            hostPorts(config.hostFirst, config.hostLast);
        }
        static std::atomic<bool> stopRequested{false};
        std::signal(SIGINT, [](int) { stopRequested = true; });
        std::signal(SIGTERM, [](int) { stopRequested = true; });
        fp::net::MasterServer master(std::move(config));
        return master.run(&stopRequested) ? 0 : 1;
    }
    if (parser.isSet(hostLocalOpt)) {
        // LocalServer: a server of this player's own, in a process of its own, left running.
        const QStringList maps = parser.value(hostLocalOpt).split(',', Qt::SkipEmptyParts);
        QString modeName = parser.value(modeOpt);
        modeName[0] = modeName[0].toUpper();
        const QString rotationPath = QDir::temp().filePath(QStringLiteral("fruityprime-rotation-%1.txt").arg(QCoreApplication::applicationPid()));
        QFile rotation(rotationPath);
        if (!rotation.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(rotationPath));
            return 1;
        }
        const double minutes = parser.isSet(timeLimitOpt) ? parser.value(timeLimitOpt).toDouble() : 7;
        const int points = parser.isSet(pointGoalOpt) ? parser.value(pointGoalOpt).toInt() : 7;
        for (const QString& map : maps) {
            rotation.write(QStringLiteral("%1 | %2 | %3 | %4\n").arg(map.trimmed(), parser.value(modeOpt), QString::number(minutes), QString::number(points)).toUtf8());
        }
        rotation.close();
        uint16_t port = 0;
        for (uint16_t candidate = fp::net::DefaultPort; candidate < fp::net::DefaultPort + 100 && port == 0; candidate++) {
            fp::net::Transport probe;
            if (probe.open(candidate)) {
                port = candidate;
            }
        }
        QStringList args{QStringLiteral("--server"), QStringLiteral("--files"), QString::fromStdString(root->string()), QStringLiteral("--port"),
            QString::number(port), QStringLiteral("--max-players"), parser.value(maxPlayersOpt), QStringLiteral("--rotation"), rotationPath,
            QStringLiteral("--server-name"), parser.value(serverNameOpt)};
        if (parser.isSet(noMasterOpt)) {
            args << QStringLiteral("--no-master");
        } else if (parser.isSet(masterOpt)) {
            args << QStringLiteral("--master") << parser.value(masterOpt);
        }
        if (parser.isSet(lobbyOpt)) {
            args << QStringLiteral("--lobby");
        }
        qint64 pid = 0;
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args, QString(), &pid)) {
            std::fprintf(stderr, "could not start the server\n");
            return 1;
        }
        for (int i = 0; i < 100; i++) {
            int latency = 0;
            if (const auto status = fp::net::queryStatus("127.0.0.1", port, 100, latency)) {
                std::printf("server %lld listening on 127.0.0.1:%u: \"%s\", %s\n", static_cast<long long>(pid), port, status->name.c_str(),
                    status->match.roomKey.c_str());
                return 0;
            }
        }
        std::fprintf(stderr, "the server (pid %lld) did not answer on %u\n", static_cast<long long>(pid), port);
        return 2;
    }
    if (parser.isSet(serverOpt)) {
        fp::net::Server::Config config;
        config.port = static_cast<uint16_t>(parser.value(portOpt).toUInt());
        config.maxPlayers = parser.value(maxPlayersOpt).toInt();
        config.name = parser.value(serverNameOpt).toStdString();
        config.friendlyFire = parser.isSet(friendlyFireOpt);
        config.lobby = parser.isSet(lobbyOpt);
        config.allowJoinInProgress = !parser.isSet(noJoinOpt);
        config.masterHost = parser.isSet(noMasterOpt) ? std::string() : masterHost;
        config.masterPort = masterPort;
        config.gameFiles = root->string();
        hostPorts(config.hostFirst, config.hostLast);
        config.exitWhenEmpty = parser.value(exitEmptyOpt).toDouble();
        config.startupGrace = parser.value(graceOpt).toDouble();
        if (parser.isSet(ownerTokenOpt) && !fp::net::parseGuid(parser.value(ownerTokenOpt).toStdString(), config.ownerToken)) {
            std::fprintf(stderr, "bad owner token: 32 hex digits\n");
            return 1;
        }
        config.allowMapVotes = !parser.isSet(noVotesOpt);
        {
            static const char* formats[] = {"auto", "ffa", "1v1", "2v2", "3v3", "4v4", "2v2v2v2"};
            const QString format = parser.value(formatOpt).toLower();
            bool known = false;
            for (int i = 0; i < 7; i++) {
                if (format == QLatin1String(formats[i])) {
                    config.format = static_cast<uint8_t>(i);
                    known = true;
                }
            }
            if (!known) {
                std::fprintf(stderr, "Unknown format \"%s\": auto, ffa, 1v1, 2v2, 3v3, 4v4 or 2v2v2v2.\n", qPrintable(format));
                return 1;
            }
        }
        if (parser.isSet(rotationOpt)) {
            std::string error;
            if (!fp::net::Rotation::load(parser.value(rotationOpt).toStdString(), config.rotation, error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
        } else {
            fp::net::RotationEntry entry;
            entry.roomKey = parser.value(roomOpt).toStdString();
            QString name = parser.value(modeOpt).toLower().remove('-').remove('_');
            static const char* modes[] = {"battle", "battleteams", "survival", "survivalteams", "capture", "bounty", "bountyteams", "nodes",
                "nodesteams", "defender", "defenderteams", "primehunter"};
            static const int goals[] = {7, 7, 2, 2, 5, 3, 3, 70, 70, 90, 90, 90}; // MatchGoalRules.DefaultValue
            for (int i = 0; i < 12; i++) {
                if (name == QLatin1String(modes[i])) {
                    entry.mode = i;
                }
            }
            entry.pointGoal = parser.isSet(pointGoalOpt) ? parser.value(pointGoalOpt).toInt() : goals[entry.mode];
            entry.timeLimit = parser.isSet(timeLimitOpt) ? static_cast<float>(parser.value(timeLimitOpt).toDouble() * 60)
                                                         : entry.mode == 0 || entry.mode == 1 ? 7 * 60 : 15 * 60;
            config.rotation = fp::net::Rotation::single(entry);
        }
        // Ctrl+C (or a service stop): the clients are told, rather than left to time out.
        static std::atomic<bool> stopRequested{false};
        std::signal(SIGINT, [](int) { stopRequested = true; });
        std::signal(SIGTERM, [](int) { stopRequested = true; });
        fp::net::Server server(std::move(config), std::make_unique<fp::ServerGame>(*root));
        return server.run(&stopRequested) ? 0 : 1;
    }
    // A bare invocation opens the front screen, as it does on the C#'s
    // Windows build: everything else here is a command somebody typed.
    const QStringList given = parser.optionNames();
    const bool bare = given.isEmpty() || (given.size() == 1 && given.first() == QLatin1String("files"));
    if (parser.isSet(launcherOpt) || bare) {
        return runLauncher(*root, parser.value(sizeOpt), parser.value(vsyncOpt) != QStringLiteral("off"),
            parser.value(capOpt).toInt(), parser.isSet(validateOpt), parser.isSet(muteOpt), parser.value(shotOpt),
            parser.value(framesOpt).toInt(), parser.isSet(offscreenOpt), parser.value(uiScreenOpt));
    }
    if (parser.isSet(soundOpt)) {
        return soundTest(*root, parser.value(soundOpt), parser.value(soundWavOpt));
    }
    if (parser.isSet(checkOpt)) {
        return checkAllRooms(*root);
    }
    if (parser.isSet(checkModelsOpt)) {
        return checkAllModels(*root);
    }

    // --host-game: the directory starts a match (a server that runs it) and this player joins it.
    std::optional<QString> hostedAddress;
    std::array<uint8_t, 16> hostedToken{};
    if (parser.isSet(hostGameOpt)) {
        fp::net::HostRequest request;
        request.roomKey = parser.value(hostGameOpt).toStdString();
        static const char* modes[] = {"battle", "battleteams", "survival", "survivalteams", "capture", "bounty", "bountyteams", "nodes",
            "nodesteams", "defender", "defenderteams", "primehunter"};
        for (int i = 0; i < 12; i++) {
            if (parser.value(modeOpt).toLower() == QLatin1String(modes[i])) {
                request.mode = static_cast<uint8_t>(fp::net::wireMode(i));
            }
        }
        request.maxPlayers = static_cast<uint8_t>(std::clamp(parser.value(maxPlayersOpt).toInt(), 2, 8));
        request.timeLimit = static_cast<uint16_t>((parser.isSet(timeLimitOpt) ? parser.value(timeLimitOpt).toDouble() : 7) * 60);
        request.pointGoal = static_cast<uint16_t>(parser.isSet(pointGoalOpt) ? parser.value(pointGoalOpt).toInt() : 7);
        request.serverName = parser.value(nameOpt).toStdString() + "'s game";
        request.rotation.emplace_back(request.roomKey, request.mode);
        for (const QString& map : parser.value(mapsOpt).split(',', Qt::SkipEmptyParts)) {
            request.rotation.emplace_back(map.trimmed().toStdString(), request.mode);
        }
        if (parser.isSet(lobbyOpt)) {
            request.policy = fp::net::SessionState::PolicyLobby;
        }
        const fp::net::HostReply reply = fp::net::requestGame(masterHost, masterPort, request);
        if (!reply.started) {
            std::fprintf(stderr, "the directory would not host: %s\n", reply.reason.c_str());
            return 2;
        }
        std::printf("the directory runs the game on port %u\n", reply.port);
        const auto master = fp::net::Endpoint::resolve(masterHost, reply.port);
        hostedAddress = QString::fromStdString((master ? master->toString() : masterHost + ":" + std::to_string(reply.port)));
        hostedToken = reply.ownerToken;
    }
    // --connect: the server says which room and which mode.
    std::unique_ptr<fp::net::Client> netClient;
    if (parser.isSet(demoOpt)) {
        netClient = std::make_unique<fp::net::Client>();
        std::string error;
        if (!netClient->openDemo(parser.value(demoOpt).toStdString(), error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    } else if (parser.isSet(connectOpt) || hostedAddress) {
        std::string host;
        uint16_t port;
        if (!parseServerAddress(hostedAddress ? *hostedAddress : parser.value(connectOpt), host, port)) {
            std::fprintf(stderr, "bad server address \"%s\"\n", qPrintable(parser.value(connectOpt)));
            return 1;
        }
        netClient = std::make_unique<fp::net::Client>();
        if (parser.isSet(ownerTokenOpt)) {
            fp::net::parseGuid(parser.value(ownerTokenOpt).toStdString(), hostedToken);
        }
        netClient->setOwnerToken(hostedToken);
        const int netHunter = std::clamp(parser.value(hunterOpt).toInt(), 0, 6);
        if (!netClient->connect(host, port, parser.value(nameOpt).toStdString(), netHunter, 0)) {
            return 1;
        }
        // Admitted, and told what is running (NetLaunch waits the same way).
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto next = std::chrono::steady_clock::now();
        while (netClient->localSlot() < 0 || !netClient->match()) {
            next += std::chrono::microseconds(16667);
            std::this_thread::sleep_until(next);
            netClient->update();
            if (netClient->refused()) {
                std::fprintf(stderr, "%s\n", netClient->refusedReason().c_str());
                return 2;
            }
            if (!netClient->connected() || std::chrono::steady_clock::now() > deadline) {
                std::fprintf(stderr, "%s did not answer: it may be off, full, or UDP may be blocked.\n", netClient->serverName().c_str());
                return 2;
            }
        }
        std::printf("joined %s as slot %d: %s\n", netClient->serverName().c_str(), netClient->localSlot(),
            netClient->match()->roomKey.c_str());
        if (parser.isSet(recordDemoOpt) && !netClient->startRecording(parser.value(recordDemoOpt).toStdString())) {
            std::fprintf(stderr, "cannot record to %s\n", qPrintable(parser.value(recordDemoOpt)));
        }
    }
    const fp::RoomMetadata* room
        = fp::findRoom(netClient ? netClient->match()->roomKey : parser.value(roomOpt).toStdString());
    if (room == nullptr) {
        std::fprintf(stderr, "Unknown room \"%s\". Use --list.\n", qPrintable(parser.value(roomOpt)));
        return 1;
    }
    if (room->firstHunt || room->hybrid) {
        std::fprintf(stderr, "First Hunt rooms are not supported yet.\n");
        return 1;
    }

    fp::Model model = [&] {
        try {
            return loadRoomModel(*root, *room);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Cannot load %s: %s\n", room->name, e.what());
            std::exit(1);
        }
    }();
    // SceneSetup: the room's layers go by how many play.
    const int players = netClient ? std::clamp(netClient->entityLayerPlayers() > 0 ? netClient->entityLayerPlayers() : 4, 2, 4)
        : parser.isSet(playersOpt)
        ? std::max(1, parser.value(playersOpt).toInt())
        : std::clamp(1 + parser.value(targetsOpt).toInt() + parser.value(botsOpt).toInt(), 2, 8);
    QString modeName = parser.value(modeOpt).toLower();
    modeName.remove('-').remove('_');
    static const std::pair<const char*, fp::GameMode> modeNames[] = {
        {"battle", fp::GameMode::Battle}, {"battleteams", fp::GameMode::BattleTeams}, {"survival", fp::GameMode::Survival},
        {"survivalteams", fp::GameMode::SurvivalTeams}, {"capture", fp::GameMode::Capture}, {"bounty", fp::GameMode::Bounty},
        {"bountyteams", fp::GameMode::BountyTeams}, {"nodes", fp::GameMode::Nodes}, {"nodesteams", fp::GameMode::NodesTeams},
        {"defender", fp::GameMode::Defender}, {"defenderteams", fp::GameMode::DefenderTeams}, {"primehunter", fp::GameMode::PrimeHunter},
    };
    fp::GameMode mode = fp::GameMode::Battle;
    bool knownMode = false;
    for (const auto& [name, value] : modeNames) {
        if (modeName == QLatin1String(name)) {
            mode = value;
            knownMode = true;
        }
    }
    if (netClient) {
        const int serverMode = fp::net::portMode(netClient->match()->mode);
        mode = serverMode < 0 ? fp::GameMode::Battle : static_cast<fp::GameMode>(serverMode);
        knownMode = true;
    }
    if (!knownMode) {
        std::fprintf(stderr, "Unknown mode \"%s\": battle, survival, primehunter, capture, bounty, nodes, defender, "
                             "battleteams, survivalteams, bountyteams, nodesteams or defenderteams.\n", qPrintable(modeName));
        return 1;
    }
    // SceneSetup.GetNodeLayer: arenas by player count; single-player rooms by their own layer.
    const int nodeLayerMask = room->multiplayer ? fp::multiplayerNodeLayerMask(players, mode == fp::GameMode::Capture)
                                                : (room->nodeLayer > 0 ? ((1 << room->nodeLayer) & 0xFF) << 6 : 0);
    model.filterNodes(nodeLayerMask);

    std::vector<fp::Entity> entities;
    if (!room->entityPath().empty()) {
        try {
            const int layer = room->multiplayer ? fp::multiplayerEntityLayer(mode, players) : 0;
            entities = fp::loadEntities(fp::resolveCaseInsensitive(*root, room->entityPath()), layer);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Cannot load entities for %s: %s\n", room->name, e.what());
        }
    }
    std::unique_ptr<fp::RoomCollision> collision;
    if (const std::string colPath = room->collisionPath(); !colPath.empty()) {
        try {
            collision = std::make_unique<fp::RoomCollision>(
                fp::RoomCollision::load(fp::resolveCaseInsensitive(*root, colPath), nodeLayerMask));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Cannot load collision for %s: %s\n", room->name, e.what());
        }
    }
    const int hunter = std::clamp(parser.value(hunterOpt).toInt(), 0, static_cast<int>(fp::playerValuesTable().size()) - 1);
    const std::vector<fp::PlayerSpawn> spawns = fp::playerSpawns(entities);
    const fp::PlayerSpawn* spawn = spawns.empty() ? nullptr : &spawns[std::clamp<size_t>(parser.value(spawnOpt).toUInt(), 0, spawns.size() - 1)];

    auto buildWorld = [&](fp::Scene& scene) {
        auto world = std::make_unique<fp::World>(scene, *root, std::move(collision), entities, hunter);
        world->setNetworked(netClient != nullptr);
        world->setMode(mode);
        world->setTeamCount(parser.value(teamsOpt).toInt());
        world->setFriendlyFire(parser.isSet(friendlyFireOpt));
        if (parser.isSet(pointGoalOpt) || parser.isSet(timeLimitOpt)) {
            world->setRules(parser.isSet(pointGoalOpt) ? parser.value(pointGoalOpt).toInt() : world->pointGoal(),
                parser.isSet(timeLimitOpt) ? static_cast<float>(parser.value(timeLimitOpt).toDouble() * 60) : world->matchTime());
        }
        if (parser.isSet(timeGoalOpt)) {
            world->setTimeGoal(static_cast<float>(parser.value(timeGoalOpt).toDouble()));
        }
        const std::string nodePath = fp::matchNodePath(*room, mode);
        if (!nodePath.empty()) {
            if (const fs::path file = fp::resolveCaseInsensitive(*root, nodePath); fs::exists(file)) {
                world->loadNodeData(file);
            }
        }
        if (spawn != nullptr) {
            world->spawnPlayer(spawn->position, spawn->facing);
        }
        if (parser.isSet(allWeaponsOpt)) {
            world->player()->giveAllWeapons();
        }
        if (netClient) {
            return world; // the server says who else is here
        }
        // Before the renderer exists: their models join the vertex buffer.
        const int targets = std::clamp(parser.value(targetsOpt).toInt(), 0, 7);
        for (int i = 0; i < targets; i++) {
            world->addPlayer((hunter + 1 + i) % 7);
        }
        const int bots = std::clamp(parser.value(botsOpt).toInt(), 0, 7 - targets);
        const int botLevel = std::clamp(parser.value(botLevelOpt).toInt(), 0, 3);
        for (int i = 0; i < bots; i++) {
            world->addBot((hunter + 1 + targets + i) % 7, botLevel);
        }
        if (const QStringList at = qEnvironmentVariable("FP_TARGET_AT").split(','); at.size() == 3 && world->playerCount() > 1) {
            // x,y,z: the first target stands there, facing the main player.
            const std::array<float, 3> pos{at[0].toFloat(), at[1].toFloat(), at[2].toFloat()};
            const auto& me = world->player()->position();
            world->player(1)->spawn(pos, {me[0] - pos[0], 0, me[2] - pos[2]});
        }
        if (bool ok = false; mode == fp::GameMode::PrimeHunter) {
            // FP_PRIME_HUNTER=slot: the match starts with a Prime Hunter.
            const int slot = qEnvironmentVariableIntValue("FP_PRIME_HUNTER", &ok);
            if (ok && slot >= 0 && slot < static_cast<int>(world->playerCount())) {
                world->setPrimeHunter(slot);
            }
        }
        if (const QString text = qEnvironmentVariable("FP_TARGET_SCRIPT"); !text.isEmpty() && world->playerCount() > 1) {
            // The first target plays this script (InputScript.h; turns and weapon picks apply to the main player).
            auto targetScript = std::make_shared<fp::InputScript>(text.toStdString());
            fp::World* w = world.get();
            world->setOtherInput([targetScript, w](size_t slot, long long tick) {
                return slot == 1 ? targetScript->input(static_cast<int>(tick - 1), *w) : fp::PlayerInput{};
            });
        }
        return world;
    };

    if (parser.isSet(walkTestOpt)) {
        if (spawn == nullptr) {
            std::fprintf(stderr, "walk test needs a player spawn\n");
            return 1;
        }
        auto scene = fp::buildScene(*root, *room, std::move(model), entities);
        auto world = buildWorld(*scene);
        if (world->collision() == nullptr) {
            std::fprintf(stderr, "walk test needs room collision\n");
            return 1;
        }
        // Online, the walk runs against the server's clock: one tick every 1/60 s.
        std::unique_ptr<fp::NetGame> net;
        if (netClient) {
            fp::NetGame::preloadModels(*scene, *root);
            const std::string roomKey = netClient->match()->roomKey;
            net = std::make_unique<fp::NetGame>(std::move(netClient));
            net->attach(*world, roomKey);
        }
        auto nextTick = std::chrono::steady_clock::now();
        // Online: what this machine saw each player do, for the report at the end.
        struct Seen {
            int altFrames = 0, beamFrames = 0, hits = 0, deaths = 0, weaponChanges = 0, lastHealth = 0, lastWeapon = 0;
        };
        std::vector<Seen> seen;
        fp::Player& player = *world->player();
        // FP_DEBUG_SOUND: the sounds run (without a device) so their log shows what plays.
        std::unique_ptr<fp::Mixer> silentMixer;
        if (qEnvironmentVariableIsSet("FP_DEBUG_SOUND")) {
            silentMixer = fp::Mixer::open(false);
            fp::Sfx::instance().load(*root, silentMixer.get());
        }
        if (const QStringList start = qEnvironmentVariable("FP_WALK_START").split(','); start.size() == 4) {
            // x,y,z,yaw: start elsewhere than the spawn.
            player.spawn({start[0].toFloat(), start[1].toFloat(), start[2].toFloat()}, {0, 0, -1});
            player.setAim(start[3].toFloat(), 0);
            if (parser.isSet(allWeaponsOpt)) {
                player.giveAllWeapons(); // spawning again took them away
            }
        }
        if (parser.value(introOpt) == QStringLiteral("on")) {
            if (auto intro = fp::CameraSequence::loadIntro(*root, room->id)) {
                std::printf("intro %s: %zu keyframes, %.2f s\n", intro->name().c_str(), intro->keyframes().size(), intro->length());
                world->setIntro(std::move(intro));
            }
        }
        std::printf("%zu collision faces; spawn (%.2f %.2f %.2f); hunter %d\n", world->collision()->faceCount(), spawn->position[0],
            spawn->position[1], spawn->position[2], hunter);
        // FP_WALK_SCRIPT or --script: see InputScript.h.
        const QString scriptText = parser.isSet(scriptOpt) ? parser.value(scriptOpt) : qEnvironmentVariable("FP_WALK_SCRIPT");
        const QStringList script = scriptText.split(';', Qt::SkipEmptyParts);
        const fp::InputScript walkScript(scriptText.toStdString());
        // Default script: 1 s still, 3 s forward, a jump, turn right 90 degrees, 3 s forward, 1 s strafe left.
        int lastHealth = player.health();
        std::array<int, 2> lastAmmo{player.ammo(0), player.ammo(1)};
        const int totalTicks = net && parser.isSet(secondsOpt) ? static_cast<int>(parser.value(secondsOpt).toDouble() * 60)
            : script.isEmpty()                             ? 60 * 9
                                                           : 60 * script.size() / 2;
        for (int tick = 0; tick < totalTicks; tick++) {
            fp::Player& player = *world->player(); // online, a room change replaces the world
            fp::PlayerInput input;
            const double t = tick / 60.0;
            if (script.isEmpty()) {
                input.forward = (t >= 1 && t < 4) || (t >= 4.5 && t < 7.5);
                input.jumpPressed = tick == 60 * 2;
                input.left = t >= 8;
                if (tick == static_cast<int>(60 * 4.25)) {
                    player.setAim(player.yaw() - 90.0f, 0);
                }
            } else {
                input = walkScript.input(tick, *world);
            }
            std::vector<int> healthBefore;
            for (size_t i = 0; i < world->playerCount(); i++) {
                healthBefore.push_back(world->player(i)->health());
            }
            const fp::MatchState stateBefore = world->matchState();
            if (net) {
                nextTick += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(nextTick);
                net->tick(input, true);
                if (net->roomChanged()) {
                    std::printf("t=%4.2fs the server moved to %s\n", (tick + 1) / 60.0, net->serverRoom().c_str());
                    const fp::RoomMetadata* next = fp::findRoom(net->serverRoom());
                    std::optional<fp::MatchRoom> loaded
                        = next ? fp::loadMatchRoom(*root, *next, net->serverMode(), players, hunter, true) : std::nullopt;
                    if (!loaded) {
                        break;
                    }
                    world.reset();
                    scene = std::move(loaded->scene);
                    world = std::move(loaded->world);
                    net->attach(*world, net->serverRoom());
                    seen.clear();
                    continue;
                }
                seen.resize(world->playerCount());
                for (size_t i = 0; i < world->playerCount(); i++) {
                    const fp::Player& other = *world->player(i);
                    Seen& s = seen[i];
                    s.altFrames += other.health() > 0 && other.isAltForm();
                    s.beamFrames += static_cast<int>(world->activeBeams(i));
                    s.hits += other.health() > 0 && other.health() < s.lastHealth;
                    s.deaths += other.health() == 0 && s.lastHealth > 0;
                    s.weaponChanges += other.health() > 0 && other.currentWeapon() != s.lastWeapon;
                    s.lastHealth = other.health();
                    s.lastWeapon = other.currentWeapon();
                }
                if (tick % 60 == 59) {
                    std::printf("%s\n", net->status().c_str());
                    for (size_t i = 0; i < world->playerCount(); i++) {
                        const fp::Player& other = *world->player(i);
                        std::printf("  world slot %zu hunter %d health %d at (%.2f %.2f %.2f) %s weapon %d pts %d k %d d %d\n", i,
                            static_cast<int>(other.hunter()), other.health(), other.position()[0], other.position()[1],
                            other.position()[2], other.isAltForm() ? "alt" : "biped", other.currentWeapon(), world->points(i),
                            world->kills(i), world->deaths(i));
                    }
                }
            } else {
                world->tick(input, true);
            }
            if (silentMixer) {
                const fp::CameraPose cam = player.camera();
                fp::Vec3 facing{cam.target[0] - cam.position[0], cam.target[1] - cam.position[1], cam.target[2] - cam.position[2]};
                const float length = std::sqrt(fp::dot(facing, facing));
                facing = length > 0 ? fp::Vec3{facing[0] / length, facing[1] / length, facing[2] / length} : fp::Vec3{0, 0, -1};
                fp::Sfx::instance().update(1 / 60.0f, cam.position, facing, cam.up);
                std::vector<float> discard(1600);
                silentMixer->render(discard.data(), 800); // the voices advance as they would on a device
            }
            if (world->matchState() != stateBefore) {
                static const char* names[] = {"in progress", "game over", "results"};
                std::printf("t=%4.2fs match %s\n", (tick + 1) / 60.0, names[static_cast<int>(world->matchState())]);
                if (world->matchState() == fp::MatchState::GameOver) {
                    for (const int slot : world->resultSlots()) {
                        std::printf("  #%d slot %d (hunter %d): %d points, %d kills, %d deaths, time %.1f s\n",
                            world->standing(slot) + 1, slot, static_cast<int>(world->player(slot)->hunter()), world->points(slot),
                            world->kills(slot), world->deaths(slot), world->survivalTime(slot));
                    }
                    if (const auto cam = world->matchEndCamera()) {
                        std::printf("  winner camera at (%.2f %.2f %.2f) looking at (%.2f %.2f %.2f)\n", cam->position[0],
                            cam->position[1], cam->position[2], cam->target[0], cam->target[1], cam->target[2]);
                    } else {
                        std::printf("  no winner camera%s\n", world->resultTie() ? " (tie)" : "");
                    }
                }
            }
            if (!net && world->matchFinished()) {
                std::printf("t=%4.2fs results done, new match\n", (tick + 1) / 60.0);
                world->restartMatch();
            }
            for (size_t i = 1; i < world->playerCount() && i < healthBefore.size(); i++) {
                const fp::Player& target = *world->player(i);
                if (target.health() != healthBefore[i]) {
                    std::printf("t=%4.2fs slot %zu (hunter %d) at (%.2f %.2f %.2f) health %d -> %d%s | points %d kills %d | beams %zu\n",
                        (tick + 1) / 60.0, i, static_cast<int>(target.hunter()), target.position()[0], target.position()[1],
                        target.position()[2], healthBefore[i], target.health(), target.dead() ? " DEAD" : "", world->points(0),
                        world->kills(0), world->activeBeams());
                }
            }
            const auto& p = player.position();
            if (player.health() != lastHealth || player.ammo(0) != lastAmmo[0] || player.ammo(1) != lastAmmo[1]) {
                std::printf("t=%4.2fs change at (%.2f %.2f %.2f): health %d -> %d, UA %d -> %d, missiles %d -> %d, weapon %d\n", (tick + 1) / 60.0,
                    p[0], p[1], p[2], lastHealth, player.health(), lastAmmo[0], player.ammo(0), lastAmmo[1], player.ammo(1),
                    player.currentWeapon());
                lastHealth = player.health();
                lastAmmo = {player.ammo(0), player.ammo(1)};
            }
            if (tick % 60 == 59 && qEnvironmentVariableIsSet("FP_DEBUG_BOTS")) {
                for (size_t i = 1; i < world->playerCount(); i++) {
                    fp::PlayerAi* ai = world->ai(i);
                    if (ai == nullptr) {
                        continue;
                    }
                    const fp::Player& bot = *world->player(i);
                    std::printf("  bot %zu (hunter %d) at (%6.2f %6.2f %6.2f) health %3d weapon %d %s pts %d | %s\n", i,
                        static_cast<int>(bot.hunter()), bot.position()[0], bot.position()[1], bot.position()[2], bot.health(),
                        bot.currentWeapon(), bot.isAltForm() ? "alt" : "biped", world->points(i), ai->pathDescription().c_str());
                }
            }
            if (tick % 30 == 29) {
                const auto& s = player.speed();
                const auto cam = player.camera();
                std::printf("t=%4.1fs pos=(%7.3f %7.3f %7.3f) speed=(%6.3f %6.3f %6.3f) %s %s cam=(%6.2f %6.2f %6.2f) fx %zu/%lld\n",
                    (tick + 1) / 60.0, p[0], p[1], p[2], s[0], s[1], s[2], player.standing() ? "standing" : "airborne",
                    player.isAltForm() ? "alt" : player.isMorphing() ? "morphing" : player.isUnmorphing() ? "unmorphing" : "biped",
                    cam.position[0], cam.position[1], cam.position[2], world->effects().particleCount(),
                    world->effects().particlesSpawned());
                if (const auto intro = world->introCamera()) {
                    std::printf("         intro cam=(%6.2f %6.2f %6.2f) target=(%6.2f %6.2f %6.2f) up=(%5.2f %5.2f %5.2f) fov %.1f\n",
                        intro->position[0], intro->position[1], intro->position[2], intro->target[0], intro->target[1],
                        intro->target[2], intro->up[0], intro->up[1], intro->up[2], intro->fov);
                }
            }
        }
        if (net) {
            std::printf("%s\n", net->predictionReport().c_str());
        }
        for (size_t i = 0; i < seen.size(); i++) {
            std::printf("seen world slot %zu (hunter %d): alt form %d frames, beams %d beam-frames, hits taken %d, deaths %d, weapon changes %d\n",
                i, static_cast<int>(world->player(i)->hunter()), seen[i].altFrames, seen[i].beamFrames, seen[i].hits, seen[i].deaths,
                seen[i].weaponChanges);
        }
        fp::Sfx::instance().unload();
        return 0;
    }

    if (parser.isSet(entitiesOpt)) {
        for (const fp::Entity& e : entities) {
            std::printf("%4d  %-14s node=%-14s pos=(%7.2f %7.2f %7.2f) facing=(%5.2f %5.2f %5.2f)", e.id, fp::entityTypeName(e.type),
                e.nodeName.c_str(), e.position[0], e.position[1], e.position[2], e.facing[0], e.facing[1], e.facing[2]);
            if (e.type == fp::EntityType::AreaVolume && e.data.size() >= 152) {
                // AreaVolumeEntityData: the volume, then what it sends on entering and leaving.
                std::printf(" vol=%u active=%u multi=%u in=%u(%d,%d) parent=%d out=%u(%d,%d) child=%d cooldown=%u prio=%u flags=%x",
                    e.u32(40), e.u8(106), e.u8(108), e.u32(112), e.i32(116), e.i32(120), static_cast<int16_t>(e.u32(124) & 0xFFFF),
                    e.u32(128), e.i32(132), e.i32(136), static_cast<int16_t>(e.u32(140) & 0xFFFF), e.u32(140) >> 16, e.u32(144),
                    e.u32(148));
            }
            if (e.type == fp::EntityType::MorphCamera && e.data.size() >= 104) {
                // RawCollisionVolume: a box's three vectors, corner and lengths
                std::printf(" vol=%u v1=(%.2f %.2f %.2f) v2=(%.2f %.2f %.2f) v3=(%.2f %.2f %.2f) at=(%.2f %.2f %.2f) len=(%.2f %.2f %.2f)", e.u32(40),
                    e.i32(44) / 4096.0, e.i32(48) / 4096.0, e.i32(52) / 4096.0, e.i32(56) / 4096.0, e.i32(60) / 4096.0, e.i32(64) / 4096.0,
                    e.i32(68) / 4096.0, e.i32(72) / 4096.0, e.i32(76) / 4096.0, e.i32(80) / 4096.0, e.i32(84) / 4096.0, e.i32(88) / 4096.0,
                    e.i32(92) / 4096.0, e.i32(96) / 4096.0, e.i32(100) / 4096.0);
            }
            if (e.type == fp::EntityType::LightSource && e.data.size() >= 136) {
                // LightSourceEntityData: volume type, then the two lights
                std::printf(" vol=%u light1=%u (%u %u %u) (%.2f %.2f %.2f) light2=%u (%u %u %u) (%.2f %.2f %.2f)", e.u32(40), e.u8(104),
                    e.u8(105), e.u8(106), e.u8(107), e.i32(108) / 4096.0, e.i32(112) / 4096.0, e.i32(116) / 4096.0, e.u8(120), e.u8(121),
                    e.u8(122), e.u8(123), e.i32(124) / 4096.0, e.i32(128) / 4096.0, e.i32(132) / 4096.0);
            }
            if (e.type == fp::EntityType::Teleporter && e.data.size() >= 92) {
                // TeleporterEntityData
                std::printf(" artifact=%u active=%u invisible=%u target=(%.2f %.2f %.2f)", e.u8(42), e.u8(43), e.u8(44),
                    e.i32(64) / 4096.0, e.i32(68) / 4096.0, e.i32(72) / 4096.0);
            }
            std::printf("\n");
        }
        std::printf("%zu entities\n", entities.size());
        return 0;
    }

    if (parser.isSet(objOpt)) {
        FILE* f = std::fopen(parser.value(objOpt).toUtf8().constData(), "w");
        for (const fp::Vertex& v : model.vertices()) {
            std::fprintf(f, "v %.9g %.9g %.9g\n", v.pos[0], v.pos[1], v.pos[2]);
        }
        for (size_t i = 0; i + 2 < model.vertices().size(); i += 3) {
            std::fprintf(f, "f %zu %zu %zu\n", i + 1, i + 2, i + 3);
        }
        std::fclose(f);
        return 0;
    }
    if (parser.isSet(dumpOpt)) {
        QDir().mkpath(parser.value(dumpOpt));
        for (size_t i = 0; i < model.materials().size(); i++) {
            const fp::Material& m = model.materials()[i];
            if (m.textureId < 0 || m.textureId >= static_cast<int>(model.textureCount())) {
                continue;
            }
            const fp::Image image = model.decodeTexture(m.textureId, m.paletteId);
            QImage out(reinterpret_cast<const uchar*>(image.rgba.data()), image.width, image.height, QImage::Format_RGBA8888);
            const QString file = QStringLiteral("%1/%2_t%3_p%4.png").arg(parser.value(dumpOpt)).arg(i, 3, 10, QChar('0')).arg(m.textureId).arg(m.paletteId);
            out.copy().save(file);
            std::printf("%s  %dx%d  %s  render=%d poly=%d alpha=%.2f cull=%d texgen=%d light=%d repeat=%d,%d\n", qPrintable(file),
                image.width, image.height, m.name.c_str(), static_cast<int>(m.renderMode), static_cast<int>(m.polygonMode), m.alpha,
                static_cast<int>(m.culling), static_cast<int>(m.texgenMode), m.lighting, static_cast<int>(m.xRepeat),
                static_cast<int>(m.yRepeat));
        }
        return 0;
    }

    QVulkanInstance instance;
    const QVersionNumber supported = instance.supportedApiVersion();
    instance.setApiVersion(supported >= QVersionNumber(1, 3) ? QVersionNumber(1, 3) : supported);
    if (instance.supportedExtensions().contains("VK_EXT_swapchain_colorspace")) {
        instance.setExtensions({"VK_EXT_swapchain_colorspace"});
    }
    if (parser.isSet(validateOpt)) {
        instance.setLayers({"VK_LAYER_KHRONOS_validation"});
    }
    if (!instance.create()) {
        std::fprintf(stderr, "Vulkan is not available (VkResult %d).\n", instance.errorCode());
        return 1;
    }

    auto scene = fp::buildScene(*root, *room, std::move(model), entities);
    auto world = buildWorld(*scene);
    std::unique_ptr<fp::NetGame> net;
    if (netClient) {
        fp::NetGame::preloadModels(*scene, *root);
        const std::string roomKey = netClient->match()->roomKey;
        net = std::make_unique<fp::NetGame>(std::move(netClient));
        net->attach(*world, roomKey);
    }
    fp::GameWindow window(std::move(scene));
    window.setUiEnabled(qEnvironmentVariableIsSet("FP_UI"));
    // The settings: the bindings, the mouse, the pad, the view.
    fp::Settings settings;
    fp::InputBindings bindings;
    bindings.load(settings);
    window.setBindings(bindings);
    window.setMouse(static_cast<float>(settings.number(QStringLiteral("mouse.sensitivity"))), settings.flag(QStringLiteral("mouse.invertX")),
        settings.flag(QStringLiteral("mouse.invertY")));
    window.setFov(static_cast<float>(settings.number(QStringLiteral("display.fov"))));
    fp::GamepadInput gamepad;
    gamepad.setOptions({settings.flag(QStringLiteral("pad.enabled")), static_cast<float>(settings.number(QStringLiteral("pad.sensitivity"))),
        static_cast<float>(settings.number(QStringLiteral("pad.deadzone"))), settings.flag(QStringLiteral("pad.invertY")),
        settings.flag(QStringLiteral("pad.aimAssist"))});
    window.setGamepad(&gamepad);
    // Without the screens, Escape leaves the game as it always has.
    QObject::connect(&window, &fp::GameWindow::menuRequested, &window, [&window] {
        if (window.ui() == nullptr) {
            window.close();
        }
    });
    window.setNoCull(parser.isSet(noCullOpt));
    window.setVSync(parser.value(vsyncOpt) != QStringLiteral("off"));
    window.setFrameCap(parser.value(capOpt).toInt());
    window.setVulkanInstance(&instance);
    const QStringList size = parser.value(sizeOpt).split('x');
    window.resize(size.value(0).toInt() > 0 ? size.value(0).toInt() : 1280, size.value(1).toInt() > 0 ? size.value(1).toInt() : 720);
    if (spawn != nullptr && !parser.isSet(camOpt)) {
        fp::Camera cam;
        constexpr float eyeHeight = 3686 / 4096.0f; // PlayerValues.AimYOffset
        cam.position = QVector3D(spawn->position[0], spawn->position[1] + eyeHeight, spawn->position[2]);
        cam.yaw = static_cast<float>(std::atan2(-spawn->facing[0], -spawn->facing[2]) * 180.0 / std::numbers::pi);
        cam.pitch = 0.0f;
        window.setInitialCamera(cam);
    }
    const bool play = parser.isSet(playOpt) || parser.isSet(scriptOpt);
    const bool walking = spawn != nullptr && !parser.isSet(flyOpt) && !parser.isSet(camOpt)
        && (play || (!parser.isSet(benchOpt) && !parser.isSet(shotOpt)));
    const bool useIntro = parser.isSet(introOpt) ? parser.value(introOpt) == QStringLiteral("on") : walking && !parser.isSet(scriptOpt);
    if (useIntro && walking && !net) {
        world->setIntro(fp::CameraSequence::loadIntro(*root, room->id));
    }
    window.setWorld(std::move(world), walking);
    window.setNet(std::move(net));
    window.setRoomLoader([&](const std::string& key, fp::GameMode serverMode) -> std::optional<fp::MatchRoom> {
        const fp::RoomMetadata* next = fp::findRoom(key);
        return next ? fp::loadMatchRoom(*root, *next, serverMode, players, hunter, true) : std::nullopt;
    });
    if (!parser.isSet(muteOpt) && !parser.isSet(offscreenOpt)) {
        if (std::unique_ptr<fp::Mixer> mixer = fp::Mixer::open(); mixer && fp::Sfx::instance().load(*root, mixer.get())) {
            if (fp::Music::instance().load(*root, mixer.get())) {
                fp::Music::instance().playRoomMusic(room->id, 0); // SceneSetup: TryPlayRoomMusic
            }
            window.setAudio(std::move(mixer));
        }
    }
    window.setGameFiles(*root);
    if (const QString hud = parser.value(hudOpt); hud != QStringLiteral("off")) {
        auto overlay = std::make_unique<fp::Hud>(*root, hunter);
        overlay->setProMode(hud == QStringLiteral("pro"));
        window.setHud(std::move(overlay));
    }
    int scriptFrames = 0;
    if (parser.isSet(scriptOpt)) {
        const fp::InputScript script(parser.value(scriptOpt).toStdString());
        scriptFrames = script.ticks();
        window.setScript(script);
    }
    // Screenshots and scripts step the simulation once a frame.
    window.setFixedStep(parser.isSet(scriptOpt) || (play && parser.isSet(shotOpt)));
    if (const QStringList v = parser.value(camOpt).split(','); v.size() == 5) {
        fp::Camera cam;
        cam.position = QVector3D(v[0].toFloat(), v[1].toFloat(), v[2].toFloat());
        cam.yaw = v[3].toFloat();
        cam.pitch = v[4].toFloat();
        window.setInitialCamera(cam);
    }
    if (parser.isSet(benchOpt)) {
        window.benchmark(3.0, std::max(1.0, parser.value(benchOpt).toDouble()));
    }
    if (parser.isSet(shotOpt)) {
        const int frames = parser.isSet(framesOpt) || scriptFrames == 0 ? parser.value(framesOpt).toInt() : scriptFrames + 1;
        window.screenshotAfter(std::max(1, frames), parser.value(shotOpt));
    }
    if (parser.isSet(offscreenOpt)) {
        if (!window.startOffscreen(QSize(window.width(), window.height()))) {
            std::fprintf(stderr, "Offscreen Vulkan setup failed.\n");
            return 1;
        }
    } else {
        window.show();
    }

    return app.exec();
}
