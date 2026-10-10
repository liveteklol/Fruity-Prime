#include "NetUnlagged.hpp"

#include "NetPlayerBridge.hpp"
#include "NetHitClaims.hpp"
#include "NetShotEvents.hpp"
#include "NetPlayerLifecycle.hpp"
#include "NetSession.hpp"
#include "NetShotDiagnostics.hpp"
#include "NetSmoothing.hpp"
#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Metadata/Weapons.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/System/Number.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::TestFlag;

namespace MphRead::Mods::Network::Detail
{
    void NetSessionNetUnlaggedReset()
    {
        NetUnlagged::Reset();
    }

    void NetSessionNetUnlaggedRecord(std::uint32_t frame)
    {
        NetUnlagged::Record(frame);
    }
}

namespace MphRead::Mods::Network
{
    namespace
    {
        namespace Runtime = ::MphRead::NativeRuntime;

        [[nodiscard]] std::size_t At(std::uint32_t frame) noexcept
        {
            return static_cast<std::size_t>(frame % static_cast<std::uint32_t>(NetUnlagged::HistoryFrames));
        }

        [[nodiscard]] std::int32_t PlayerCount()
        {
            return static_cast<std::int32_t>(Entities::PlayerEntity::Players().size());
        }

        [[nodiscard]] Entities::PlayerEntity& PlayerAt(std::int32_t slot)
        {
            return RequireReference(Entities::PlayerEntity::Players().at(static_cast<std::size_t>(slot)));
        }
    }

    bool NetUnlagged::ConfigureMaxRewind(const std::optional<std::string>& value)
    {
        std::int32_t frames = 0;
        if (!value.has_value() || !Runtime::Int32TryParseInvariant(*value, frames)
            || frames < 1 || frames > MaxRewindCeiling)
        {
            return false;
        }
        _maxRewindFrames = frames;
        return true;
    }

    void NetUnlagged::ResetSlot(std::int32_t slot)
    {
        if (slot < 0 || slot >= Slots)
        {
            return;
        }
        const auto s = static_cast<std::size_t>(slot);
        for (std::size_t i = 0; i < HistoryFrames; i++)
        {
            _inPlay[s][i] = false;
            _life[s][i] = 0;
            _generation[s][i] = 0;
        }
        _moved[s] = false;
    }

    void NetUnlagged::Reset()
    {
        _stamp.fill(0);
        _moved.fill(false);
        _newest = 0;
        _reconciled = false;
        _inProgress = false;
        _shooter = nullptr;
        _rewind = 0;
        _shotsCompensated = 0;
        _rewindsSkipped = 0;
        _framesRewound = 0;
        _worstRewind = 0;
        _shotsClamped = 0;
        _framesRefused = 0;
        _worstRequested = 0;
        _stalePresses = 0;
        _stalePressFrames = 0;
        ClampedByShooter.fill(0);
        _clampErrorSamples = 0;
        _clampErrorSum = 0;
        _clampErrorVerticalSum = 0;
        _clampErrorWorst = 0;
        _clampErrorWorstVertical = 0;
        DepthHistogram.fill(0);
        _catchUpSteps = 0;
        _catchUpHits = 0;
        _historyMisses = 0;
    }

    void NetUnlagged::Record(std::uint32_t frame)
    {
        if (!Enabled())
        {
            return;
        }
        Restore();
        _inProgress = false;
        const std::size_t index = At(frame);
        _stamp[index] = frame;
        _newest = frame;
        for (std::int32_t i = 0; i < Slots; ++i)
        {
            const auto s = static_cast<std::size_t>(i);
            if (i >= PlayerCount())
            {
                _inPlay[s][index] = false;
                continue;
            }
            Entities::PlayerEntity& player = PlayerAt(i);
            const bool active = TestFlag(player.LoadFlags(), Entities::LoadFlags::Active) && player.ModIsInPlay();
            _life[s][index] = NetPlayerLifecycle::Get(i);
            _generation[s][index] = NetPlayerLifecycle::Generation(i);
            _inPlay[s][index] = active;
            if (active)
            {
                _position[s][index] = player.Position;
                _altForm[s][index] = player.IsAltForm();
            }
        }
    }

    bool NetUnlagged::PositionAt(std::int32_t slot, std::uint32_t frame, std::uint16_t expectedGeneration,
        std::uint16_t expectedLife, OpenTK::Mathematics::Vector3& position)
    {
        position = OpenTK::Mathematics::Vector3::Zero;
        if (slot < 0 || slot >= Slots || frame == 0)
        {
            return false;
        }
        const std::size_t index = At(frame);
        const auto s = static_cast<std::size_t>(slot);
        if (_stamp[index] != frame || !_inPlay[s][index]
            || _life[s][index] != expectedLife || _generation[s][index] != expectedGeneration)
        {
            return false;
        }
        position = _position[s][index];
        return std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z);
    }

    double NetUnlagged::RewindFor(std::int32_t slot, std::int32_t& requested)
    {
        requested = 0;
        if (slot < 0 || slot >= Slots || slot == NetSession::LocalSlot())
        {
            return 0;
        }
        const auto s = static_cast<std::size_t>(slot);
        if (!NetSession::RemoteIntentValid[s])
        {
            return 0;
        }
        const std::uint32_t ack = NetSession::RemoteIntents[s].AckFrame;
        const std::uint32_t now = NetSession::NetFrame();
        if (ack == 0 || ack >= now)
        {
            return 0;
        }
        double depth = static_cast<double>(now - ack);
        depth -= NetSession::RemoteIntents[s].AckSubFrame / 256.0;
        if (_pressAgeEnabled && slot < static_cast<std::int32_t>(NetPlayerBridge::ShootPressAge.size()))
        {
            const std::int32_t age = NetPlayerBridge::ShootPressAge[s];
            if (age > 0)
            {
                depth += age;
                Runtime::IncrementInPlace(_stalePresses);
                _stalePressFrames = Runtime::UncheckedAdd(_stalePressFrames, age);
            }
        }
        requested = Runtime::ConvertToInt32Net9(
            Runtime::MathMin(Runtime::RoundToEven(depth), static_cast<double>(HistoryFrames)));
        if (depth > _maxRewindFrames)
        {
            depth = _maxRewindFrames;
        }
        return depth < 0 ? 0 : depth;
    }

    bool NetUnlagged::AnyBotInPlay()
    {
        for (const std::shared_ptr<Entities::PlayerEntity>& player : Entities::PlayerEntity::Players())
        {
            if (player != nullptr && player->IsBot()
                && ::MphRead::TestFlag(player->LoadFlags(), Entities::LoadFlags::Active))
            {
                return true;
            }
        }
        return false;
    }

    bool NetUnlagged::Simulating()
    {
        return NetSession::Active() && (NetSession::Role() == NetRole::Host || NetSession::IsAuthority());
    }

    void NetUnlagged::BeginShot(Entities::PlayerEntity& shooter)
    {
        if (_inProgress)
        {
            return;
        }
        _shooter = nullptr;
        _rewind = 0;
        if (!Enabled() || !Simulating() || shooter.IsBot())
        {
            return;
        }
        // Shooter-authoritative hits: a human's shot at another human is the
        // claim, and the copy simulated here is thrown away. Rewinding every
        // player (and catching the projectile up) for it is server time spent
        // on nothing -- unless a bot is in play, which only this copy can hit.
        if (NetHitClaims::ShooterHits() && !AnyBotInPlay())
        {
            Runtime::IncrementInPlace(_rewindsSkipped);
            return;
        }
        const std::int32_t slot = shooter.SlotIndex();
        std::int32_t requested = 0;
        const double rewind = RewindFor(slot, requested);
        if (requested > 0 && requested < static_cast<std::int32_t>(DepthHistogram.size()))
        {
            Runtime::IncrementInPlace(DepthHistogram[static_cast<std::size_t>(requested)]);
        }
        if (requested > _worstRequested)
        {
            _worstRequested = requested;
        }
        const std::int32_t served = Runtime::ConvertToInt32Net9(Runtime::RoundToEven(rewind));
        const auto weapon = static_cast<std::size_t>(NetShotDiagnostics::Bucket(shooter.CurrentWeapon()));
        Runtime::IncrementInPlace(NetShotDiagnostics::RewindSamples[weapon]);
        NetShotDiagnostics::RewindFrames[weapon] = Runtime::UncheckedAdd(
            NetShotDiagnostics::RewindFrames[weapon], served);
        if (requested > served)
        {
            Runtime::IncrementInPlace(NetShotDiagnostics::RewindClamps[weapon]);
        }
        if (requested > served)
        {
            Runtime::IncrementInPlace(_shotsClamped);
            _framesRefused = Runtime::UncheckedAdd(_framesRefused, requested - served);
            MeasureClampError(slot, requested, served);
        }
        if (rewind <= 0)
        {
            return;
        }
        const double target = NetSession::NetFrame() - rewind;
        if (!Reconcile(slot, target))
        {
            Runtime::IncrementInPlace(_historyMisses);
            return;
        }
        _shooter = &shooter;
        _rewind = Runtime::ConvertToInt32Net9(std::ceil(rewind));
        Runtime::IncrementInPlace(_shotsCompensated);
        _framesRewound = Runtime::UncheckedAdd(_framesRewound, served);
        if (served > _worstRewind)
        {
            _worstRewind = served;
        }
        auto& beams = RequireReference(RequireReference(shooter.EquipInfo()).Beams);
        const std::int32_t length = beams.Length();
        if (static_cast<std::int32_t>(_beamsBefore.size()) < length)
        {
            _beamsBefore = std::vector<bool>(static_cast<std::size_t>(length));
        }
        for (std::int32_t i = 0; i < length; ++i)
        {
            _beamsBefore[static_cast<std::size_t>(i)] = RequireReference(beams[i]).Lifespan() > 0;
        }
        _inProgress = true;
    }

    std::uint32_t NetUnlagged::LaunchFrameFor(Entities::PlayerEntity& shooter)
    {
        const std::int32_t slot = shooter.SlotIndex();
        // A copy firing a shot event: the world that shot was aimed in.
        if (const auto event = NetShotEvents::FiringEvent(shooter); event.has_value() && event->AckFrame != 0
            && event->AckFrame <= NetSession::NetFrame())
        {
            return event->AckFrame;
        }
        if (Simulating() && slot != NetSession::LocalSlot() && !shooter.IsBot()
            && slot >= 0 && slot < Slots && NetSession::RemoteIntentValid[static_cast<std::size_t>(slot)])
        {
            const std::uint32_t ack = NetSession::RemoteIntents[static_cast<std::size_t>(slot)].AckFrame;
            if (ack != 0 && ack <= NetSession::NetFrame())
            {
                return ack;
            }
        }
        std::uint32_t read = 0;
        std::uint8_t subFrame = 0;
        if (NetSmoothing::AckPoint(read, subFrame))
        {
            return read;
        }
        return NetSession::AppliedSnapshotFrame() != 0 ? NetSession::AppliedSnapshotFrame() : NetSession::NetFrame();
    }

    void NetUnlagged::MeasureClampError(std::int32_t shooterSlot, std::int32_t requested, std::int32_t served)
    {
        if (shooterSlot >= 0 && shooterSlot < Slots)
        {
            Runtime::IncrementInPlace(ClampedByShooter[static_cast<std::size_t>(shooterSlot)]);
        }
        const std::uint32_t now = NetSession::NetFrame();
        if (now < static_cast<std::uint32_t>(requested))
        {
            return;
        }
        const std::size_t wanted = At(now - static_cast<std::uint32_t>(requested));
        const std::size_t got = At(now - static_cast<std::uint32_t>(served));
        if (_stamp[wanted] != now - static_cast<std::uint32_t>(requested)
            || _stamp[got] != now - static_cast<std::uint32_t>(served))
        {
            return;
        }
        for (std::int32_t i = 0; i < Slots && i < PlayerCount(); i++)
        {
            const auto s = static_cast<std::size_t>(i);
            if (i == shooterSlot || !_inPlay[s][wanted] || !_inPlay[s][got])
            {
                continue;
            }
            const OpenTK::Mathematics::Vector3 error = _position[s][got] - _position[s][wanted];
            if (!std::isfinite(error.X) || !std::isfinite(error.Y) || !std::isfinite(error.Z))
            {
                continue;
            }
            const float length = OpenTK::Mathematics::Length(error);
            const float vertical = std::abs(error.Y);
            Runtime::IncrementInPlace(_clampErrorSamples);
            _clampErrorSum += length;
            _clampErrorVerticalSum += vertical;
            if (length > _clampErrorWorst)
            {
                _clampErrorWorst = length;
            }
            if (vertical > _clampErrorWorstVertical)
            {
                _clampErrorWorstVertical = vertical;
            }
        }
    }

    bool NetUnlagged::Reconcile(std::int32_t exceptSlot, double targetFrame)
    {
        const auto frame = static_cast<std::uint32_t>(std::floor(targetFrame));
        const auto fraction = static_cast<float>(targetFrame - frame);
        const std::size_t index = At(frame);
        if (_stamp[index] != frame || frame == 0)
        {
            return false;
        }
        const std::size_t next = At(frame + 1);
        const bool haveNext = fraction > 0.0001F && _stamp[next] == frame + 1;
        Restore();
        for (std::int32_t i = 0; i < Slots && i < PlayerCount(); ++i)
        {
            const auto s = static_cast<std::size_t>(i);
            if (i == exceptSlot || !_inPlay[s][index]
                || !NetPlayerLifecycle::Matches(i, _generation[s][index], _life[s][index]))
            {
                continue;
            }
            Entities::PlayerEntity& player = PlayerAt(i);
            if (!TestFlag(player.LoadFlags(), Entities::LoadFlags::Active) || !player.ModIsInPlay())
            {
                continue;
            }
            OpenTK::Mathematics::Vector3 was = _position[s][index];
            if (!std::isfinite(was.X) || !std::isfinite(was.Y) || !std::isfinite(was.Z))
            {
                continue;
            }
            if (haveNext && _inPlay[s][next] && _altForm[s][next] == _altForm[s][index]
                && _life[s][next] == _life[s][index] && _generation[s][next] == _generation[s][index])
            {
                const OpenTK::Mathematics::Vector3 then = _position[s][next];
                const OpenTK::Mathematics::Vector3 travel = then - was;
                if (std::isfinite(travel.X) && std::isfinite(travel.Y) && std::isfinite(travel.Z)
                    && OpenTK::Mathematics::LengthSquared(travel) <= 16.0F)
                {
                    was = was + OpenTK::Mathematics::Multiply(travel, fraction);
                }
            }
            _restore[s] = player.Position;
            _moved[s] = true;
            player.ModPlaceAt(NetPlayerBridge::InFormFor(player, was, _altForm[s][index]));
        }
        _reconciled = true;
        return true;
    }

    void NetUnlagged::Restore()
    {
        if (!_reconciled)
        {
            return;
        }
        for (std::int32_t i = 0; i < Slots && i < PlayerCount(); ++i)
        {
            const auto s = static_cast<std::size_t>(i);
            if (!_moved[s])
            {
                continue;
            }
            _moved[s] = false;
            PlayerAt(i).ModPlaceAt(_restore[s]);
        }
        _reconciled = false;
    }

    void NetUnlagged::EndShot(Entities::PlayerEntity& shooter)
    {
        if (_shooter != &shooter || _rewind <= 0)
        {
            Restore();
            _shooter = nullptr;
            _inProgress = false;
            return;
        }
        const std::int32_t slot = shooter.SlotIndex();
        auto& beams = RequireReference(RequireReference(shooter.EquipInfo()).Beams);
        const std::int32_t length = beams.Length();
        std::int32_t newCount = 0;
        for (std::int32_t i = 0; i < length; ++i)
        {
            const auto b = static_cast<std::size_t>(i);
            if (!_beamsBefore[b] && RequireReference(beams[i]).Lifespan() > 0)
            {
                _beamsBefore[b] = true; // reused as "this one is ours, and still going"
                ++newCount;
            }
            else
            {
                _beamsBefore[b] = false;
            }
        }
        if (newCount == 0)
        {
            Restore();
            _shooter = nullptr;
            _inProgress = false;
            return;
        }
        for (std::int32_t step = 1; step <= _rewind && newCount > 0; ++step)
        {
            const std::uint32_t frame = NetSession::NetFrame() - static_cast<std::uint32_t>(_rewind - step);
            if (!Reconcile(slot, frame))
            {
                if (frame <= _newest)
                {
                    Runtime::IncrementInPlace(_historyMisses);
                    break;
                }
                Restore();
            }
            for (std::int32_t i = 0; i < length; ++i)
            {
                const auto b = static_cast<std::size_t>(i);
                if (!_beamsBefore[b])
                {
                    continue;
                }
                Entities::BeamProjectileEntity& beam = RequireReference(beams[i]);
                const bool hit = TestFlag(beam.Flags(), Entities::BeamFlags::Collided);
                Runtime::IncrementInPlace(_catchUpSteps);
                if (!beam.Process() || TestFlag(beam.Flags(), Entities::BeamFlags::Collided))
                {
                    if (!hit && TestFlag(beam.Flags(), Entities::BeamFlags::Collided))
                    {
                        Runtime::IncrementInPlace(_catchUpHits);
                    }
                    _beamsBefore[b] = false;
                    --newCount;
                }
            }
        }
        Restore();
        _shooter = nullptr;
        _rewind = 0;
        _inProgress = false;
    }

    std::string NetUnlagged::Describe()
    {
        if (!Enabled())
        {
            return "lag compensation: off";
        }
        if (_shotsCompensated == 0)
        {
            return "lag compensation: on, nothing to compensate (history misses "
                + std::to_string(_historyMisses) + ", " + std::to_string(_rewindsSkipped)
                + " human shots not rewound: their hits are the shooter's claims)";
        }
        const double mean = static_cast<double>(_framesRewound) / static_cast<double>(_shotsCompensated);
        std::string text = "lag compensation: " + std::to_string(_shotsCompensated)
            + " shots rewound, mean " + Runtime::ToString(mean, "F1")
            + " frames (" + Runtime::ToString(mean * 1000.0 / 60.0, "F0")
            + " ms), worst " + std::to_string(_worstRewind)
            + ", catch-up " + std::to_string(_catchUpSteps)
            + " steps / " + std::to_string(_catchUpHits)
            + " hits, history misses " + std::to_string(_historyMisses);
        text += "; ceiling " + std::to_string(_maxRewindFrames) + " frames ("
            + std::to_string(_maxRewindFrames * 1000 / 60) + " ms), clamped " + std::to_string(_shotsClamped);
        if (_shotsClamped > 0)
        {
            text += " (" + Runtime::ToString(static_cast<double>(_shotsClamped) * 100.0
                    / static_cast<double>(std::max<std::int64_t>(1, _shotsCompensated)), "F1")
                + "% of shots, mean "
                + Runtime::ToString(static_cast<double>(_framesRefused) / static_cast<double>(_shotsClamped), "F1")
                + " frames refused)";
        }
        text += ", worst asked " + std::to_string(_worstRequested);
        if (_shotsClamped > 0)
        {
            std::string by = ", clamped by slot";
            for (std::size_t i = 0; i < ClampedByShooter.size(); i++)
            {
                if (ClampedByShooter[i] > 0)
                {
                    by += " " + std::to_string(i) + ":" + std::to_string(ClampedByShooter[i]);
                }
            }
            text += by;
        }
        if (_clampErrorSamples > 0)
        {
            const auto samples = static_cast<double>(_clampErrorSamples);
            text += ", error " + Runtime::ToString(_clampErrorSum / samples, "F2") + " units mean ("
                + Runtime::ToString(_clampErrorVerticalSum / samples, "F2") + " vertical), worst "
                + Runtime::ToString(_clampErrorWorst, "F2") + " ("
                + Runtime::ToString(_clampErrorWorstVertical, "F2") + " vertical)";
        }
        if (_pressAgeEnabled)
        {
            text += "; stale presses " + std::to_string(_stalePresses);
            if (_stalePresses > 0)
            {
                text += " (+" + Runtime::ToString(static_cast<double>(_stalePressFrames)
                    / static_cast<double>(_stalePresses), "F1") + " frames each)";
            }
        }
        return text;
    }

    std::string NetUnlagged::DescribeDepths()
    {
        std::string text = "rewind depths asked (frames: shots):";
        bool any = false;
        for (std::size_t i = 0; i < DepthHistogram.size(); i++)
        {
            if (DepthHistogram[i] == 0)
            {
                continue;
            }
            any = true;
            text += " " + std::to_string(i) + ":" + std::to_string(DepthHistogram[i]);
            if (static_cast<std::int32_t>(i) == _maxRewindFrames)
            {
                text += "<-ceiling";
            }
        }
        return any ? text : std::string("rewind depths asked: none");
    }
}
