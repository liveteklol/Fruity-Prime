#include "WeaponDps.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../NativeRuntime/OpenTK/GL.hpp"

#include "../../GameState.hpp"
#include "../../Scene.hpp"
#include "MapAudit.hpp"

#include <typeinfo>
#include "../../NativeRuntime/System/Console.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include "NetLaunch.hpp"
#include "NetTestScript.hpp"
#include "../../Entities/BeamProjectileEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Formats/Culling.hpp"
#include "../../Scene.hpp"
#include "../../NativeRuntime/System/ExceptionText.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/Rhi/BackendFactory.hpp"
#include "../../Formats/Types.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using ::MphRead::NativeRuntime::ExceptionTypeName;
using ::MphRead::NativeRuntime::IncrementInPlace;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::OpenTK::Mathematics::Add;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Negate;
using ::OpenTK::Mathematics::Normalize;
using ::OpenTK::Mathematics::Scale;
using ::OpenTK::Mathematics::Subtract;

namespace
{
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] std::string BeamName(MphRead::BeamType value)
    {
        return ::MphRead::ToString(value);
    }

    [[nodiscard]] float ClampDistance(float value) noexcept
    {
        if (value < 0.5F)
        {
            return 0.5F;
        }
        if (value > 40.0F)
        {
            return 40.0F;
        }
        return value;
    }
}

namespace
{
    // Exception.StackTrace, which C++ exceptions do not carry.
    [[nodiscard]] std::optional<std::string> ExceptionStackTrace(const std::exception& ex)
    {
        (void)ex;
        return std::nullopt;
    }
}

namespace MphRead::Mods::Network
{
    using OpenTK::Mathematics::Vector2i;
    using OpenTK::Mathematics::Vector3;

    std::int32_t WeaponDps::FullHealth(Entities::PlayerEntity& player)
    {
        return std::max<std::int32_t>(1, player.HealthMax());
    }

    RendererPlatform::WindowSettings WeaponDps::GameSettings()
    {
        RendererPlatform::WindowSettings settings{};
        settings.UpdateFrequency = 60;
        return settings;
    }

    RendererPlatform::WindowSettings WeaponDps::WindowSettings()
    {
        RendererPlatform::WindowSettings settings = GameSettings();
        settings.ClientSize = Vector2i(320, 180);
        settings.Title = "MphRead weapon probe";
        settings.Profile = RendererPlatform::WindowSettings::ContextProfile::Compatability;
        settings.Flags = RendererPlatform::WindowSettings::ContextFlags::Default;
        settings.ApiMajor = 3;
        settings.ApiMinor = 2;
        settings.StartVisible = false;
        settings.GraphicsMode = RendererPlatform::GraphicsWindowMode::OpenGL;
        return settings;
    }

    void WeaponDps::Run()
    {
        _window->Run(*this);
    }

    OpenTK::Mathematics::Vector2i WeaponDps::ClientSize() const
    {
        return _window->Size();
    }

    void WeaponDps::Close()
    {
        _window->Close();
    }

    void WeaponDps::Present()
    {
        _swapchain->Present();
    }

    WeaponDps::WeaponDps(
        std::string room,
        Hunter hunter,
        BeamType beam,
        double seconds,
        float distance,
        bool bombs)
        : _window(RendererPlatform::CreateWindow(WindowSettings())),
          _room(std::move(room)),
          _hunter(hunter),
          _beam(beam),
          _seconds(seconds),
          _distance(distance),
          _bombs(bombs),
          _scene(nullptr)
    {
        NativeRuntime::Rhi::SwapchainDesc swapchainDesc{};
        const Vector2i framebufferSize = _window->Size();
        swapchainDesc.width = static_cast<std::uint32_t>(std::max(framebufferSize.X, 1));
        swapchainDesc.height = static_cast<std::uint32_t>(std::max(framebufferSize.Y, 1));
        _swapchain = NativeRuntime::Rhi::BackendFactory::CreateSwapchain(
            NativeRuntime::Rhi::GraphicsBackend::OpenGl, *_window, swapchainDesc);
        Entities::PlayerEntity::SetMaxPlayers(
            std::max<std::int32_t>(Entities::PlayerEntity::MaxPlayers(), 2));
        MapAudit::ForceEveryone(true);
        _scene = std::make_unique<MphRead::Scene>(ClientSize(), _window->Keyboard(), _window->Mouse(),
            [](std::string) {}, [this]() { Close(); });

        _scene->AddPlayer(Hunter::Samus, 0, -1);
        _scene->AddPlayer(hunter, 0, -1);

        const auto& players = Entities::PlayerEntity::Players();
        for (std::int32_t i = 2;
            i < static_cast<std::int32_t>(players.size());
            i = UncheckedAdd(i, 1))
        {
            Entities::PlayerEntity& player
                = RequireReference(players[static_cast<std::size_t>(i)]);
            player.SetLoadFlags(player.LoadFlags() & ~Entities::LoadFlags::Active);
        }
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(players.size());
            i = UncheckedAdd(i, 1))
        {
            RequireReference(players[static_cast<std::size_t>(i)]).SetIsBot(false);
        }

        Entities::PlayerEntity::SetPlayerCount(2);
        Entities::PlayerEntity::SetMainPlayerIndex(0);
        _scene->AddRoom(_room, GameMode::Battle, NetLaunch::RoomPlayerCount());
    }

    WeaponDps::~WeaponDps() = default;

    MphRead::Scene& WeaponDps::Scene() noexcept
    {
        return *_scene;
    }

    void WeaponDps::OnLoad()
    {
        _scene->Size(ClientSize());
        _scene->OnLoad();
        _window->BaseOnLoad();
        ::MphRead::NativeRuntime::Rhi::ResetWindowViewport(ClientSize().X, ClientSize().Y);
        _scene->OnResize();
    }

    void WeaponDps::OnRenderFrame(const RendererPlatform::FrameEventArgs& args)
    {
        GameState::ApplyPause();
        _scene->OnUpdateFrame();
        if (!_scene->OnRenderFrame())
        {
            return;
        }

        IncrementInPlace(_frame);
        Step();
        Present();
        _scene->AfterRenderFrame();
        _window->BaseOnRenderFrame(args);

        if (_placed
            && static_cast<double>(UncheckedSubtract(_frame, _placedFrame))
                >= _seconds * 60.0)
        {
            Close();
        }
        else if (static_cast<double>(_frame) > (_seconds + 20.0) * 60.0)
        {
            Close();
        }
    }

    bool WeaponDps::Alive(Entities::PlayerEntity& player)
    {
        const Entities::LoadFlags flags = player.LoadFlags();
        return (flags & Entities::LoadFlags::Active) == Entities::LoadFlags::Active
            && (flags & Entities::LoadFlags::Spawned) == Entities::LoadFlags::Spawned
            && player.Health() > 0;
    }

    void WeaponDps::Account(Entities::PlayerEntity& victim)
    {
        if (!_placed)
        {
            return;
        }

        if (_lastHealth >= 0 && victim.Health() < _lastHealth)
        {
            const std::int32_t drop
                = UncheckedSubtract(_lastHealth, victim.Health());
            _damage = UncheckedAdd(_damage, drop);
            IncrementInPlace(_hits);
            _lastHitFrame = _firingFrames;
        }

        if (_killFrames < 0 && _lastHealth > 0 && victim.Health() == 0)
        {
            _killFrames = _firingFrames;
        }

        _lastHealth = victim.Health();
    }

    void WeaponDps::Step()
    {
        const auto& players = Entities::PlayerEntity::Players();
        if (players.size() < 2)
        {
            return;
        }

        const std::shared_ptr<Entities::PlayerEntity> victimReference = players[0];
        const std::shared_ptr<Entities::PlayerEntity> shooterReference = players[1];
        Entities::PlayerEntity& victim = RequireReference(victimReference);
        Entities::PlayerEntity& shooter = RequireReference(shooterReference);

        Account(victim);
        if (!Alive(shooter) || !Alive(victim))
        {
            NetTestScript::Rest(shooterReference, true);
            NetTestScript::Rest(victimReference, true);
            return;
        }

        if (!_bombs
            && (shooter.IsAltForm() || shooter.IsMorphing() || shooter.IsUnmorphing()))
        {
            NetTestScript::Rest(shooterReference, true);
            NetTestScript::Rest(victimReference, true);
            return;
        }

        if (!_placed)
        {
            Vector3 facing = victim.FacingVector();
            facing = Vector3(facing.X, 0.0F, facing.Z);
            facing = LengthSquared(facing) < 0.001F
                ? Vector3(0.0F, 0.0F, 1.0F)
                : Normalize(facing);

            const Vector3 victimPosition
                = static_cast<Vector3>(victim.Position);
            const Vector3 spot = Add(
                victimPosition,
                Scale(facing, _bombs ? 0.6F : _distance));
            shooter.Teleport(
                spot,
                Negate(facing),
                _scene->GetNodeRefByPosition(spot));
            _placed = true;
            _placedFrame = _frame;
            _lastHealth = victim.Health();
            _startHealth = victim.Health();
        }

        if (_killFrames >= 0)
        {
            NetTestScript::Rest(shooterReference, true);
            NetTestScript::Rest(victimReference, true);
            return;
        }

        if (_bombs)
        {
            if (_hunter == Hunter::Sylux)
            {
                const float angle = DegreesToRadians(
                    static_cast<float>(_firingFrames) * 6.0F);
                const Vector3 offset(
                    std::cos(angle) * 2.4F,
                    0.0F,
                    std::sin(angle) * 2.4F);
                const Vector3 ring = Add(
                    static_cast<Vector3>(victim.Position), offset);
                shooter.Teleport(
                    ring,
                    Negate(Normalize(offset)),
                    _scene->GetNodeRefByPosition(ring));
            }

            NetTestScript::LayBombs(shooterReference, _frame);
            NetTestScript::Rest(victimReference, true);

            auto enumerator = _scene->Entities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                const std::shared_ptr<Entities::EntityBase> entityReference
                    = enumerator.Current();
                Entities::EntityBase& entity = RequireReference(entityReference);
                if (entity.Type == EntityType::Bomb)
                {
                    IncrementInPlace(_beamFrames);
                    break;
                }
            }

            _lastAmmo = (shooter).ModAmmo().first;
            HoldShooter(shooter);
            IncrementInPlace(_firingFrames);
            return;
        }

        if (static_cast<std::int32_t>(shooter.ShockCoilTimer()) > _worstShockCoilTimer)
        {
            _worstShockCoilTimer
                = static_cast<std::int32_t>(shooter.ShockCoilTimer());
        }

        (shooter).ModArmWeapon(_beam);

        const Vector3 toVictim = Subtract(
            AddY(static_cast<Vector3>(victim.Position), 0.5F),
            AddY(static_cast<Vector3>(shooter.Position), 0.5F));
        if (LengthSquared(toVictim) > 0.001F)
        {
            (shooter).ModSetAim(Normalize(toVictim));
        }

        NetTestScript::HoldFire(shooterReference, true);
        NetTestScript::Rest(victimReference, true);

        auto enumerator = _scene->Entities().GetEnumerator();
        while (enumerator.MoveNext())
        {
            const std::shared_ptr<Entities::EntityBase> entityReference
                = enumerator.Current();
            Entities::EntityBase& entity = RequireReference(entityReference);
            if (entity.Type == EntityType::BeamProjectile)
            {
                const std::shared_ptr<Entities::BeamProjectileEntity> shot
                    = std::dynamic_pointer_cast<Entities::BeamProjectileEntity>(
                        entityReference);
                if (shot && shot->Owner() == shooterReference)
                {
                    IncrementInPlace(_beamFrames);
                    break;
                }
            }
        }

        _lastAmmo = (shooter).ModAmmo().first;
        HoldShooter(shooter);
        IncrementInPlace(_firingFrames);
    }

    void WeaponDps::HoldShooter(Entities::PlayerEntity& shooter)
    {
        const std::int32_t floor = std::max<std::int32_t>(
            1,
            UncheckedSubtract(FullHealth(shooter), HealHeadroom));
        if (shooter.Health() > floor)
        {
            _healed = UncheckedAdd(
                _healed,
                UncheckedSubtract(shooter.Health(), floor));
        }
        shooter.SetHealth(floor);
    }

    std::int32_t WeaponDps::Report()
    {
        if (!_placed || _firingFrames == 0)
        {
            NativeRuntime::ConsoleWriteLine(("DPSFAIL " + _room + " | "
                + ::MphRead::ToString(_hunter) + " " + BeamName(_beam)
                + " | never got set up"));
            return 1;
        }

        const double seconds = static_cast<double>(_firingFrames) / 60.0;
        const double window = _killFrames > 0
            ? static_cast<double>(_killFrames) / 60.0
            : seconds;
        const std::string kill = _killFrames > 0
            ? "killed " + ::MphRead::NativeRuntime::ToString(_startHealth) + " hp in "
                + ::MphRead::NativeRuntime::ToString(static_cast<double>(_killFrames) / 60.0, "0.00")
                + " s"
            : "did not kill " + ::MphRead::NativeRuntime::ToString(_startHealth) + " hp in "
                + ::MphRead::NativeRuntime::ToString(seconds, "0.0")
                + " s";
        const std::string action = _bombs
            ? std::string("laying bombs")
            : "holding " + BeamName(_beam);

        NativeRuntime::ConsoleWriteLine(("DPS " + _room
            + " | " + ::MphRead::ToString(_hunter) + " " + action
            + " at "
            + ::MphRead::NativeRuntime::ToString(_bombs ? 0.6F : _distance, "0.0")
            + " units | " + kill
            + " | damage " + ::MphRead::NativeRuntime::ToString(_damage)
            + " | hits " + ::MphRead::NativeRuntime::ToString(_hits)
            + " | "
            + ::MphRead::NativeRuntime::ToString(static_cast<double>(_damage) / window, "0.0")
            + " per second | "
            + ::MphRead::NativeRuntime::ToString(_hits > 0
                    ? static_cast<double>(_damage) / static_cast<double>(_hits)
                    : 0.0, "0.0")
            + " per hit | "
            + ::MphRead::NativeRuntime::ToString(static_cast<double>(_hits) / window, "0.0")
            + " hits per second | beam alive on "
            + ::MphRead::NativeRuntime::ToString(_beamFrames) + " of "
            + ::MphRead::NativeRuntime::ToString(_firingFrames) + " frame(s)"
            + " | shockCoilTimer " + ::MphRead::NativeRuntime::ToString(_worstShockCoilTimer)
            + " (ramp needs 60 for +1, 240 for +4)"
            + " | victim ended on " + ::MphRead::NativeRuntime::ToString(_lastHealth)
            + " hp | healed shooter " + ::MphRead::NativeRuntime::ToString(_healed)
            + " hp | shooter ammo " + ::MphRead::NativeRuntime::ToString(_lastAmmo)
            + " | last hit on firing frame " + ::MphRead::NativeRuntime::ToString(_lastHitFrame)
            + " of " + ::MphRead::NativeRuntime::ToString(_firingFrames)));
        return 0;
    }

    std::int32_t WeaponDps::Run(
        std::string room,
        Hunter hunter,
        BeamType beam,
        double seconds,
        float distance,
        bool bombs)
    {
        std::unique_ptr<WeaponDps> window;
        std::int32_t result = 1;
        try
        {
            try
            {
                window = std::unique_ptr<WeaponDps>(new WeaponDps(
                    room,
                    hunter,
                    beam,
                    seconds,
                    ClampDistance(distance),
                    bombs));
                window->Run();
                result = window->Report();
            }
            catch (const std::exception& ex)
            {
                NativeRuntime::ConsoleWriteLine(("DPSCRASH " + room + " | "
                    + ExceptionTypeName(ex) + ": "
                    + std::string(ex.what())));
                NativeRuntime::ConsoleWriteLineNullable(ExceptionStackTrace(ex));
                result = 1;
            }
        }
        catch (...)
        {
            if (window)
            {
                window.reset();
            }
            throw;
        }

        if (window)
        {
            window.reset();
        }
        return result;
    }
}
