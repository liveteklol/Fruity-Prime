#pragma once

#include "Formats/Types.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

namespace MphRead
{
    class Scene;

    namespace Entities
    {
        class PlayerEntity;
    }
}

namespace MphRead::Mods::Network
{
    class NetLog final
    {
    public:
        NetLog() = delete;

        static const double Interval;

        [[nodiscard]] static bool Enabled() noexcept;
        static void Open(const std::string& clientName);
        static void Close();
        static void CollisionRange(
            std::int32_t slot,
            const std::string& label,
            OpenTK::Mathematics::Vector3 prev,
            OpenTK::Mathematics::Vector3 current);
        static void Event(const std::string& message);
        static void Snapshot(double time);
        static void Snapshot(double time, MphRead::Scene& scene);

    private:
        static double ReadInterval();
        static void SnapshotInternal(double time, MphRead::Scene* scene);
        [[nodiscard]] static bool InScene(
            MphRead::Scene* scene, Entities::PlayerEntity& player);
        [[nodiscard]] static std::string DescribeNodeRef(
            Entities::PlayerEntity& player);
        static void Line(const std::string& text);
        static void HitReg();

        static std::unique_ptr<std::ofstream> _writer;
        static double _lastWrite;
        static bool _failed;
        static bool _enabled;
    };
}
