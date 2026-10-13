#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"

#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::IntentPacket;
    using ::OpenTK::Mathematics::Vector3;

    bool passed = true;
    IntentPacket::ShotEvent event{};
    event.Direction = Vector3(0.0F, 0.0F, -1.0F);
    passed &= Check(event.HasRay(), "a finite unit direction is valid");
    event.Direction = Vector3(0.6F, 0.0F, 0.0F);
    passed &= Check(event.HasRay(), "a finite non-unit direction in range is valid");
    event.Direction = Vector3(0.0F, 0.0F, 0.0F);
    passed &= Check(!event.HasRay(), "a zero direction is invalid");
    event.Direction = Vector3(std::numeric_limits<float>::infinity(), 0.0F, 0.0F);
    passed &= Check(!event.HasRay(), "an infinite direction is invalid");
    event.Direction = Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F);
    passed &= Check(!event.HasRay(), "a NaN direction is invalid");
    event.Direction = Vector3(0.0F, 0.0F, 2.01F);
    passed &= Check(!event.HasRay(), "an oversized direction is invalid");
    event.Direction = Vector3(0.0F, 0.0F, 2.0F);
    passed &= Check(!event.HasRay(), "the upper direction boundary remains exclusive");
    event.Direction = Vector3(0.5F, 0.0F, 0.0F);
    passed &= Check(!event.HasRay(), "the lower direction boundary remains exclusive");

    const Vector3 shooterPosition(10.0F, 20.0F, 30.0F);
    passed &= Check(MphRead::Mods::Network::ShotRayValidation::IsPlausibleOrigin(
        Vector3(16.0F, 20.0F, 30.0F), shooterPosition),
        "a shot origin at the maximum muzzle offset is plausible");
    passed &= Check(!MphRead::Mods::Network::ShotRayValidation::IsPlausibleOrigin(
        Vector3(16.01F, 20.0F, 30.0F), shooterPosition),
        "a shot origin beyond the muzzle offset is rejected");
    passed &= Check(!MphRead::Mods::Network::ShotRayValidation::IsPlausibleOrigin(
        Vector3(std::numeric_limits<float>::quiet_NaN(), 20.0F, 30.0F), shooterPosition),
        "a NaN shot origin is rejected");
    passed &= Check(!MphRead::Mods::Network::ShotRayValidation::IsPlausibleOrigin(
        Vector3(10.0F, 20.0F, 30.0F),
        Vector3(10.0F, std::numeric_limits<float>::infinity(), 30.0F)),
        "a non-finite shooter position is rejected");
    passed &= Check(!MphRead::Mods::Network::ShotRayValidation::IsPlausibleOrigin(
        Vector3(0.0F, 0.0F, 0.0F), Vector3(0.0F, 0.0F, 0.0F),
        std::numeric_limits<float>::quiet_NaN()),
        "a non-finite muzzle offset is rejected");
    return passed ? 0 : 1;
}
