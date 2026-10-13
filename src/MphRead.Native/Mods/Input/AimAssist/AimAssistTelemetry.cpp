#include "AimAssistTelemetry.hpp"

#include "AimAssistDebug.hpp"
#include "../AimInputSourceTracker.hpp"
#include "../../SpectatorMode.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "../../../Entities/Players/PlayerEntity.hpp"
#include "../../../Formats/Enums.hpp"
#include "../../../NativeRuntime/System/AppDomain.hpp"
#include "../../../NativeRuntime/System/Console.hpp"
#include "../../../NativeRuntime/System/Exceptions.hpp"
#include "../../../NativeRuntime/System/Globalization.hpp"
#include "../../../NativeRuntime/System/IO.hpp"
#include "../../../NativeRuntime/System/Json.hpp"
#include "../../../NativeRuntime/System/Managed.hpp"
#include "../../../NativeRuntime/System/Number.hpp"

#include <algorithm>

namespace MphRead::Mods::Input::AimAssist
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::size_t Beam(BeamType weapon) noexcept
        {
            return static_cast<std::size_t>(std::clamp(static_cast<std::int32_t>(weapon), 0, 15));
        }

        // double.ToString("R"), which is what System.Text.Json writes.
        [[nodiscard]] Runtime::JsonPtr Number(double value)
        {
            return Runtime::JsonValue::MakeNumber(Runtime::ToStringInvariant(value, "R"));
        }
    }

    void AimAssistTelemetry::Shot(BeamType weapon)
    {
        if (_path.has_value() && _current != nullptr)
        {
            Runtime::IncrementInPlace(_current->Shots);
            LastShot[Beam(weapon)] = _current;
        }
    }

    void AimAssistTelemetry::Hit(const Entities::PlayerEntity* attacker, BeamType weapon, std::uint32_t damage)
    {
        if (_path.has_value() && attacker != nullptr && attacker->IsMainPlayer() && !attacker->IsBot()
            && !SpectatorMode::IsSpectating() && damage > 0
            && (!Network::NetSession::Active() || Network::NetSession::IsAuthority()))
        {
            if (const std::shared_ptr<Bucket>& bucket = LastShot[Beam(weapon)])
            {
                Runtime::IncrementInPlace(bucket->HitEvents);
                bucket->ObservedDamage = Runtime::UncheckedAdd(bucket->ObservedDamage,
                    static_cast<std::int64_t>(damage));
            }
        }
    }

    void AimAssistTelemetry::Configure(const std::optional<std::string>& path)
    {
        if (!path.has_value() || Runtime::StringIsNullOrWhiteSpace(*path))
        {
            return;
        }
        _path = Runtime::PathGetFullPath(*path);
        Runtime::AppDomainAddProcessExitHandler([]() { Save(); });
    }

    void AimAssistTelemetry::Record(BeamType weapon, const AimAssistTarget& target, const AimAssistResult& result,
        float correction, float velocity)
    {
        if (!_path.has_value())
        {
            return;
        }
        const std::int32_t input = AimInputSourceTracker::Current() == AimInputSource::Gamepad
            ? AimAssistDebug::UnassistedArm ? 1 : 2 : 0;
        const std::size_t beam = Beam(weapon);
        const std::int32_t range = result.TargetSlot() < 0 ? 3 : target.Distance() < 5 ? 0 : target.Distance() < 25 ? 1 : 2;
        std::shared_ptr<Bucket>& slot = Buckets[static_cast<std::size_t>(input)][beam][static_cast<std::size_t>(range)];
        if (slot == nullptr)
        {
            slot = std::make_shared<Bucket>();
            slot->Input = input == 0 ? "mouse-or-touch" : input == 1 ? "controller-baseline" : "controller-assisted";
            slot->Weapon = ToString(weapon);
            constexpr const char* Distances[] = {"close", "mid", "far", "no-target"};
            slot->Distance = Distances[range];
        }
        _current = slot;
        Runtime::IncrementInPlace(slot->Samples);
        slot->FrictionSum += result.Friction();
        slot->CorrectionSum += correction;
        if (result.TargetSlot() >= 0)
        {
            Runtime::IncrementInPlace(slot->TargetSamples);
            slot->SecondsOnTarget += 1.0 / 60;
            slot->ErrorSum += target.BodyError().Length();
            slot->VelocitySum += velocity;
            if (result.TargetSlot() != _lastTarget)
            {
                Runtime::IncrementInPlace(slot->Switches);
            }
        }
        if (result.RotationStrength() > 0)
        {
            slot->AssistSeconds += 1.0 / 60;
        }
        if (result.HeadBlend() > 0)
        {
            slot->HeadSeconds += 1.0 / 60;
        }
        _lastTarget = result.TargetSlot();
    }

    void AimAssistTelemetry::Save()
    {
        try
        {
            Runtime::JsonPtr output = Runtime::JsonValue::MakeArray();
            // foreach over a [,,] array: row-major.
            for (const auto& byInput : Buckets)
            {
                for (const auto& byBeam : byInput)
                {
                    for (const std::shared_ptr<Bucket>& bucket : byBeam)
                    {
                        if (bucket == nullptr)
                        {
                            continue;
                        }
                        Runtime::JsonPtr item = Runtime::JsonValue::MakeObject();
                        item->Set("Input", Runtime::JsonValue::MakeString(bucket->Input));
                        item->Set("Weapon", Runtime::JsonValue::MakeString(bucket->Weapon));
                        item->Set("Distance", Runtime::JsonValue::MakeString(bucket->Distance));
                        item->Set("Samples", Runtime::JsonValue::MakeNumber(std::to_string(bucket->Samples)));
                        item->Set("TargetSamples", Runtime::JsonValue::MakeNumber(std::to_string(bucket->TargetSamples)));
                        item->Set("Shots", Runtime::JsonValue::MakeNumber(std::to_string(bucket->Shots)));
                        item->Set("HitEvents", Runtime::JsonValue::MakeNumber(std::to_string(bucket->HitEvents)));
                        item->Set("ObservedDamage", Runtime::JsonValue::MakeNumber(std::to_string(bucket->ObservedDamage)));
                        item->Set("HitEventsPerShot", Number(bucket->HitEventsPerShot()));
                        item->Set("SecondsOnTarget", Number(bucket->SecondsOnTarget));
                        item->Set("AssistSeconds", Number(bucket->AssistSeconds));
                        item->Set("HeadSeconds", Number(bucket->HeadSeconds));
                        item->Set("ErrorSum", Number(bucket->ErrorSum));
                        item->Set("FrictionSum", Number(bucket->FrictionSum));
                        item->Set("CorrectionSum", Number(bucket->CorrectionSum));
                        item->Set("VelocitySum", Number(bucket->VelocitySum));
                        item->Set("Switches", Runtime::JsonValue::MakeNumber(std::to_string(bucket->Switches)));
                        item->Set("MeanError", Number(bucket->MeanError()));
                        item->Set("MeanFriction", Number(bucket->MeanFriction()));
                        output->Items().push_back(item);
                    }
                }
            }
            Runtime::FileWriteAllText(*_path, Runtime::JsonWriteIndented(output));
        }
        catch (const System::IO::IOException& ex)
        {
            Runtime::ConsoleErrorWriteLine(std::string("Aim diagnostics could not be saved: ") + ex.what());
        }
        catch (const System::UnauthorizedAccessException& ex)
        {
            Runtime::ConsoleErrorWriteLine(std::string("Aim diagnostics could not be saved: ") + ex.what());
        }
    }
}
