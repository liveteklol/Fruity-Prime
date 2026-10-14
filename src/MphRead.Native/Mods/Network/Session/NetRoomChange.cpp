#include "Mods/Network/Session/NetRoomChange.hpp"
#include "Mods/Network/Bots/NetBotRoles.hpp"
#include "Mods/Network/Gameplay/Claims/Hit/NetHitClaims.hpp"
#include "Mods/Network/Gameplay/Replication/NetSmoothing.hpp"

#include "GameState.hpp"
#include "Scene.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerBridge.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "Mods/Network/Session/NetSlotManager.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/ExceptionText.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include "Mods/Network/Gameplay/Combat/NetDamage.hpp"
#include "Mods/Network/Gameplay/Combat/NetHitPrediction.hpp"
#include "Mods/Network/Integration/Launch/NetLaunch.hpp"
#include "Mods/Network/Diagnostics/NetLog.hpp"
#include "Mods/Network/Session/NetMatchEnd.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerSetup.hpp"
#include "Entities/CamSeq/CameraSequence.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/MapGen/MapCatalog.hpp"
#include "Mods/MapGen/CustomRooms.hpp"
#include "Mods/Chat/ChatBox.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace MphRead::Mods::Network
{
    std::string NetRoomChange::_requested{};
    std::uint32_t NetRoomChange::_requestedFrame = 0;
    std::string NetRoomChange::_fetchWanted{};
    std::shared_ptr<std::atomic<int>> NetRoomChange::_fetchState{};
    bool NetRoomChange::_loadPending = false;
    std::uint16_t NetRoomChange::_requestedMatch = 0;
    std::uint16_t NetRoomChange::_loadedMatch = 0;
    std::uint32_t NetRoomChange::_loadedFrame = 0;

    bool NetRoomChange::GameplayReady()
    {
        return !_loadPending
            && (_loadedMatch == 0 || _loadedMatch == NetSession::CurrentMatchId());
    }

    bool NetRoomChange::Settling()
    {
        return _loadedFrame != 0
            && NetSession::NetFrame() - _loadedFrame < SettleFrames;
    }

    std::int32_t NetRoomChange::RoomPlayerCount()
    {
        return NetSession::Active() ? NetLaunch::RoomPlayerCount() : 0;
    }

    bool NetRoomChange::Rebuilding()
    {
        return NetSession::Active();
    }

    void NetRoomChange::Reset()
    {
        _loadPending = false;
        _requested = "";
        _requestedFrame = 0;
        _fetchWanted.clear();
        _fetchState.reset();
        _requestedMatch = 0;
        _loadedMatch = 0;
        _loadedFrame = 0;
    }

    void NetRoomChange::Sync(Scene& scene)
    {
        if (!NetSession::Active()
            || !(scene.Room() != nullptr)
            || GameState::InRoomTransition())
        {
            return;
        }
        std::optional<MatchStatePacket> state
            = NetSession::ServerMatch();
        std::string wanted;
        if (state.has_value() && state->RoomKey.has_value())
        {
            wanted = state->RoomKey.value();
        }
        if (wanted.length() == 0)
        {
            return;
        }
        const std::uint16_t match = state.value().MatchId;
        if (_loadPending)
        {
            return;
        }
        const RoomMetadata* currentMeta = Metadata::GetRoomById(
            scene.RoomId(), true);
        const std::string current = currentMeta == nullptr
            ? std::string{}
            : currentMeta->Name;
        if (current == wanted && (_loadedMatch == 0 || _loadedMatch == match))
        {
            _loadedMatch = match;
            _requested = "";
            return;
        }
        if (_requested == wanted && _loadedMatch == match
            && NetSession::NetFrame() - _requestedFrame < 300U)
        {
            return;
        }
        auto [meta, ignored] = Metadata::GetRoomByName(wanted);
        static_cast<void>(ignored);
        if (meta == nullptr)
        {
            // A map this device does not have: its pack is fetched in the
            // background (the game goes on meanwhile) and listed here, on the
            // thread that loads rooms, once it is on disk.
            if (_fetchWanted != wanted)
            {
                _fetchWanted = wanted;
                _fetchState = std::make_shared<std::atomic<int>>(0);
                std::thread([wanted, state = _fetchState]()
                {
                    bool arrived = false;
                    try
                    {
                        arrived = Mods::MapGen::MapCatalog::FetchForRoom(wanted);
                    }
                    catch (...)
                    {
                    }
                    state->store(arrived ? 1 : 2);
                }).detach();
                NativeRuntime::ConsoleWriteLine(("[net] server switched to \"" + wanted
                    + "\", which this device does not have: fetching it"));
                NetLog::Event("fetching server map \"" + wanted + "\"");
                return;
            }
            const int fetched = _fetchState ? _fetchState->load() : 2;
            if (fetched == 0)
            {
                return;
            }
            if (fetched == 1 && Mods::MapGen::MapCatalog::EnsureRoom(wanted))
            {
                meta = Metadata::GetRoomByName(wanted).first;
            }
            if (meta == nullptr)
            {
                NativeRuntime::ConsoleWriteLine(("[net] server switched to \"" + wanted
                    + "\", which this build does not know"));
                NetLog::Event("unknown server map \"" + wanted + "\"");
                _requested = wanted;
                _requestedFrame = NetSession::NetFrame();
                return;
            }
        }
        // A custom map's room is made from its level the first time it is
        // wanted (the launcher does it before a match, Scene::AddRoom would do
        // it too, but too late to say so). Build it now, and if it cannot be
        // built say why rather than loading a room whose files are not there:
        // that dies with "Could not find a part of the path ...Collision.bin"
        // and leaves the player in a black room.
        Mods::MapGen::CustomRooms::EnsureReady(wanted);
        if (const std::optional<std::string> why = Mods::MapGen::CustomRooms::WhyUnplayable(wanted))
        {
            static std::string reported;
            if (reported != wanted)
            {
                reported = wanted;
                Mods::Chat::ChatBox::System("Cannot load " + wanted + ": " + *why);
            }
            NativeRuntime::ConsoleWriteLine("[net] server switched to \"" + wanted + "\", which cannot be played here: " + *why);
            NetLog::Event("cannot play server map \"" + wanted + "\": " + *why);
            _requested = wanted;
            _requestedFrame = NetSession::NetFrame();
            return;
        }
        _requested = wanted;
        _requestedFrame = NetSession::NetFrame();
        _loadPending = true;
        _requestedMatch = match;
        NativeRuntime::ConsoleWriteLine((current == wanted
            ? "[net] server started a new match on " + wanted + "; loading it"
            : "[net] server rotated to " + wanted + "; loading it"));
        NetLog::Event("loading " + wanted + " for match "
            + ::MphRead::NativeRuntime::ToString(match));
        GameState::TransitionRoomId(meta->Id);
        scene.SetFade(FadeType::FadeOutBlack, 10.0F / 30.0F, true, AfterFade::LoadRoom);
    }

    std::shared_ptr<Entities::PlayerEntity> NetRoomChange::RebuildPlayers(
        Scene& scene, Hunter hunter, std::int32_t recolor)
    {
        static_cast<void>(scene);
        // Loading is synchronous from here through AfterRebuild. Allow the
        // authority's initial Spawn while constructing the requested room.
        _loadedMatch = _requestedMatch;
        _loadPending = false;
        const std::int32_t localSlot
            = std::max(NetSession::LocalSlot(), 0);
        for (std::int32_t slot = 0; slot < Entities::PlayerEntity::MaxPlayers(); ++slot)
        {
            const Hunter slotHunter = slot == localSlot
                ? hunter
                : SessionSlots().HunterAt(static_cast<std::size_t>(slot));
            std::shared_ptr<Entities::PlayerEntity> created
                = Entities::PlayerEntity::Create(
                    slotHunter, slot == localSlot ? recolor : 0);
            if (created == nullptr)
            {
                continue;
            }
            created->SetLoadFlags(created->LoadFlags() | Entities::LoadFlags::SlotActive);
            created->SetLoadFlags(created->LoadFlags() | Entities::LoadFlags::Active);
            created->SetLoadFlags(created->LoadFlags() | Entities::LoadFlags::Initial);
            created->NodeRef = Formats::Culling::NodeRef::None;
            created->CameraInfo()->NodeRef = Formats::Culling::NodeRef::None;
            NetBotRoles::Assign(*created);
            const bool occupied = slot == localSlot
                || (slot < static_cast<std::int32_t>(SessionRoster::SlotCapacity)
                    && SessionSlots().IsOccupied(slot));
            if (!occupied)
            {
                created->SetLoadFlags(
                    created->LoadFlags() & ~Entities::LoadFlags::Active);
            }
        }
        Entities::PlayerEntity::SetPlayerCount(1);
        Entities::PlayerEntity::SetMainPlayerIndex(localSlot);
        NetSlotManager::Reset();
        NetPlayerSetup::Reset();
        NetDamage::ResetForRoomChange();
        NetHitPrediction::ForgetPending();
        NetHitClaims::ForgetPending();
        ResetScores();
        NativeRuntime::ConsoleWriteLine(("[net] player slots rebuilt for the new room, main player = slot "
            + ::MphRead::NativeRuntime::ToString(localSlot)));
        return ::MphRead::NativeRuntime::ManagedListAt(
            Entities::PlayerEntity::Players(), localSlot);
    }

    void NetRoomChange::AfterRebuild(Scene& scene)
    {
        _loadedMatch = _requestedMatch;
        _loadedFrame = std::max(NetSession::NetFrame(), 1U);
        _loadPending = false;
        _requested = "";
        NetPlayerBridge::NoteRoomChanged();
        NetSmoothing::NoteRoomChanged();
        NetLaunch::DisableCheatsForMatch();
        ReloadIntroCamSeq(scene);
        for (std::int32_t slot = 0;
            slot < static_cast<std::int32_t>(
                Entities::PlayerEntity::Players().size()); ++slot)
        {
            std::shared_ptr<Entities::PlayerEntity> player
                = Entities::PlayerEntity::Players()[static_cast<std::size_t>(slot)];
            if (slot == Entities::PlayerEntity::MainPlayerIndex())
            {
                continue;
            }
            if ((player->LoadFlags() & Entities::LoadFlags::SlotActive)
                == Entities::LoadFlags::None)
            {
                NetLog::Event("slot "
                    + ::MphRead::NativeRuntime::ToString(slot)
                    + " skipped on rebuild: flags="
                    + ::MphRead::Entities::ToString(player->LoadFlags()));
                continue;
            }
            scene.InsertEntity(player);
            player->Initialize();
            scene.InitEntity(player);
            scene.InitEntity(player->Halfturret());
            NetLog::Event("slot "
                + ::MphRead::NativeRuntime::ToString(slot)
                + " re-inserted into the new room");
        }
    }

    void NetRoomChange::ReloadIntroCamSeq(Scene& scene)
    {
        Formats::CameraSequence::Current(nullptr);
        Formats::CameraSequence::Intro(nullptr);
        if (!GameState::Multiplayer()
            || Entities::PlayerEntity::PlayerCount() == 0)
        {
            return;
        }
        const std::int32_t seqId = scene.RoomId() - 93 + 172;
        if (seqId < 172 || seqId >= 199)
        {
            return;
        }
        try
        {
            std::shared_ptr<Formats::CameraSequence> intro
                = Formats::CameraSequence::Load(seqId, &scene);
            intro->Initialize();
            intro->Flags(static_cast<Formats::CamSeqFlags>(
                static_cast<std::int32_t>(intro->Flags())
                | static_cast<std::int32_t>(Formats::CamSeqFlags::Loop)));
            Formats::CameraSequence::Intro(intro.get());
        }
        catch (...)
        {
            const std::string message
                = ::MphRead::NativeRuntime::ExceptionMessage(std::current_exception());
            const std::int32_t consoleRoomId
                = scene.RoomId();
            NativeRuntime::ConsoleWriteLine(("[net] no intro camera for room "
                + ::MphRead::NativeRuntime::ToString(consoleRoomId)
                + ": " + message));
            const std::int32_t logRoomId
                = scene.RoomId();
            NetLog::Event(
                "no intro camera for room "
                + ::MphRead::NativeRuntime::ToString(logRoomId)
                + ": " + message);
        }
    }

    void NetRoomChange::ResetScores()
    {
        for (std::int32_t i = 0; i < Entities::PlayerEntity::SlotCapacity; ++i)
        {
            GameState::Points()[i] = 0;
            GameState::TeamPoints()[i] = 0;
            GameState::Kills()[i] = 0;
            GameState::TeamKills()[i] = 0;
            GameState::Deaths()[i] = 0;
            GameState::TeamDeaths()[i] = 0;
            GameState::Standings()[i] = 0;
            GameState::TeamStandings()[i] = 0;
            GameState::DamageCount()[i] = 0;
            GameState::KillStreak()[i] = 0;
        }
        GameState::ResetMatchProgress();
        NetMatchEnd::Reset();
    }
}
