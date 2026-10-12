#include "Q3Movers.hpp"

#include "CustomRooms.hpp"
#include "Q3Bsp.hpp"
#include "Q3Convert.hpp"
#include "../../Formats/Formats.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../../NativeRuntime/System/Number.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

namespace MphRead::Mods::MapGen
{
    namespace
    {
        using OpenTK::Mathematics::Matrix4;
        using OpenTK::Mathematics::Vector3;

        enum class Kind
        {
            Bob,
            Rotate,
            Pendulum,
            Door,
        };

        enum class DoorState
        {
            Closed,
            Opening,
            Open,
            Closing,
        };

        struct Mover
        {
            std::int32_t Model = 0;
            Kind Type = Kind::Bob;
            // In Quake's space and units.
            Vector3 Origin{};
            // Its brush model is stored about its origin (q3map moves an
            // entity with an origin there), not where it stands.
            bool Relative = false;
            Vector3 Axis{0.0F, 0.0F, 1.0F};
            float Height = 0.0F;
            float Speed = 0.0F;
            float Phase = 0.0F;
            float Frequency = 0.0F;
            float Yaw = 0.0F;
            // A door: where it moves, how far, how long it stays open.
            float Distance = 0.0F;
            float Wait = 2.0F;
            bool StartOpen = false;
            std::array<float, 3> TriggerMin{};
            std::array<float, 3> TriggerMax{};
            DoorState State = DoorState::Closed;
            float Open = 0.0F;
            float Hold = 0.0F;
        };

        struct Cache
        {
            std::int32_t Room = -1;
            std::vector<Mover> Movers;
            std::uint64_t Frame = 0;
            float Seconds = 0.0F;
        };

        [[nodiscard]] Cache& Movers()
        {
            static Cache cache;
            return cache;
        }

        [[nodiscard]] const std::string* Value(const Q3Entity& entity, std::string_view key)
        {
            for (const auto& [name, value] : entity)
            {
                if (Q3StringEqual{}(name, std::string(key)))
                {
                    return &value;
                }
            }
            return nullptr;
        }

        [[nodiscard]] float Number(const Q3Entity& entity, std::string_view key, float fallback)
        {
            float value = fallback;
            if (const std::string* text = Value(entity, key))
            {
                if (!::MphRead::NativeRuntime::SingleTryParseInvariant(*text, value))
                {
                    value = fallback;
                }
            }
            return value;
        }

        [[nodiscard]] std::optional<Vector3> Point(const Q3Entity& entity, std::string_view key)
        {
            const std::string* text = Value(entity, key);
            if (text == nullptr)
            {
                return std::nullopt;
            }
            std::array<float, 3> values{};
            std::size_t found = 0;
            std::size_t start = 0;
            while (found < 3 && start < text->size())
            {
                const std::size_t end = std::min(text->find(' ', start), text->size());
                if (end > start && ::MphRead::NativeRuntime::SingleTryParseInvariant(
                        std::string_view(*text).substr(start, end - start), values[found]))
                {
                    ++found;
                }
                start = end + 1;
            }
            if (found < 3)
            {
                return std::nullopt;
            }
            return Vector3(values[0], values[1], values[2]);
        }

        [[nodiscard]] const Q3Entity* EntityOf(const Q3Bsp* bsp, std::int32_t model)
        {
            const std::string name = "*" + std::to_string(model);
            for (const std::shared_ptr<Q3Entity>& entity : bsp->Entities())
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (const std::string* value = Value(*entity, "model"); value != nullptr && *value == name)
                {
                    return entity.get();
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::string ClassOf(const Q3Entity& entity)
        {
            const std::string* value = Value(entity, "classname");
            std::string result = value == nullptr ? std::string() : *value;
            std::transform(result.begin(), result.end(), result.begin(),
                [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
            return result;
        }

        [[nodiscard]] std::vector<Mover> Load(const Q3Bsp* bsp)
        {
            std::vector<Mover> movers;
            std::map<std::string, std::vector<std::size_t>> teams;
            for (std::size_t m = 1; m < bsp->Models().size(); ++m)
            {
                const std::int32_t model = static_cast<std::int32_t>(m);
                if (!Q3Movers::IsMover(bsp, model))
                {
                    continue;
                }
                const Q3Entity& entity = *EntityOf(bsp, model);
                const std::string kind = ClassOf(entity);
                const Q3Model& brushes = *bsp->Models()[m];
                const std::vector<float>& mins = *brushes.Mins();
                const std::vector<float>& maxs = *brushes.Maxs();
                const Vector3 low(mins[0], mins[1], mins[2]);
                const Vector3 high(maxs[0], maxs[1], maxs[2]);
                const Vector3 centre = OpenTK::Mathematics::Multiply(low + high, 0.5F);
                const int flags = static_cast<int>(Number(entity, "spawnflags", 0.0F));

                Mover mover;
                mover.Model = model;
                const std::optional<Vector3> origin = Point(entity, "origin");
                mover.Origin = origin.value_or(Vector3(0.0F, 0.0F, 0.0F));
                mover.Relative = Q3Movers::IsRelative(bsp, model);
                if (kind == "func_bobbing")
                {
                    mover.Type = Kind::Bob;
                    mover.Height = Number(entity, "height", 32.0F);
                    mover.Speed = std::max(0.1F, Number(entity, "speed", 4.0F));
                    mover.Phase = Number(entity, "phase", 0.0F);
                    mover.Axis = (flags & 1) != 0 ? Vector3(1.0F, 0.0F, 0.0F)
                        : ((flags & 2) != 0 ? Vector3(0.0F, 1.0F, 0.0F) : Vector3(0.0F, 0.0F, 1.0F));
                }
                else if (kind == "func_rotating")
                {
                    mover.Type = Kind::Rotate;
                    mover.Speed = Number(entity, "speed", 100.0F);
                    mover.Axis = (flags & 4) != 0 ? Vector3(1.0F, 0.0F, 0.0F)
                        : ((flags & 8) != 0 ? Vector3(0.0F, 1.0F, 0.0F) : Vector3(0.0F, 0.0F, 1.0F));
                }
                else if (kind == "func_pendulum")
                {
                    mover.Type = Kind::Pendulum;
                    mover.Speed = Number(entity, "speed", 30.0F);
                    mover.Phase = Number(entity, "phase", 0.0F);
                    mover.Yaw = Number(entity, "angle", 0.0F);
                    const float length = std::max(8.0F, std::abs(mins[2]));
                    mover.Frequency = std::sqrt(800.0F / (3.0F * length)) / (2.0F * std::numbers::pi_v<float>);
                }
                else
                {
                    mover.Type = Kind::Door;
                    const float angle = Number(entity, "angle", 0.0F);
                    if (angle == -1.0F)
                    {
                        mover.Axis = Vector3(0.0F, 0.0F, 1.0F);
                    }
                    else if (angle == -2.0F)
                    {
                        mover.Axis = Vector3(0.0F, 0.0F, -1.0F);
                    }
                    else
                    {
                        const float radians = angle * std::numbers::pi_v<float> / 180.0F;
                        mover.Axis = Vector3(std::cos(radians), std::sin(radians), 0.0F);
                    }
                    mover.Speed = Number(entity, "speed", 0.0F);
                    if (mover.Speed <= 0.0F)
                    {
                        mover.Speed = 400.0F;
                    }
                    mover.Wait = Number(entity, "wait", 0.0F);
                    if (mover.Wait == 0.0F)
                    {
                        mover.Wait = 2.0F;
                    }
                    const Vector3 size = high - low;
                    const float lip = Number(entity, "lip", 8.0F);
                    mover.Distance = std::max(1.0F, std::abs(mover.Axis.X) * size.X + std::abs(mover.Axis.Y) * size.Y
                        + std::abs(mover.Axis.Z) * size.Z - lip);
                    mover.StartOpen = (flags & 1) != 0;
                    mover.TriggerMin = {low.X, low.Y, low.Z};
                    mover.TriggerMax = {high.X, high.Y, high.Z};
                    const std::string* team = Value(entity, "team");
                    teams[team == nullptr ? "#" + std::to_string(model) : *team].push_back(movers.size());
                }
                movers.push_back(mover);
            }
            // A door's trigger (Think_SpawnNewDoorTrigger): its team's bounds,
            // 120 wider on their thinnest side, opened by any player whose
            // box (Quake's 30 by 30 by 56) reaches into it.
            for (const auto& [team, members] : teams)
            {
                std::array<float, 3> low{1.0e9F, 1.0e9F, 1.0e9F};
                std::array<float, 3> high{-1.0e9F, -1.0e9F, -1.0e9F};
                for (const std::size_t i : members)
                {
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        low[k] = std::min(low[k], movers[i].TriggerMin[k]);
                        high[k] = std::max(high[k], movers[i].TriggerMax[k]);
                    }
                }
                std::size_t best = 0;
                for (std::size_t k = 1; k < 3; ++k)
                {
                    if (high[k] - low[k] < high[best] - low[best])
                    {
                        best = k;
                    }
                }
                low[best] -= 120.0F;
                high[best] += 120.0F;
                const std::array<float, 3> playerLow{-15.0F, -15.0F, -24.0F};
                const std::array<float, 3> playerHigh{15.0F, 15.0F, 32.0F};
                for (const std::size_t i : members)
                {
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        movers[i].TriggerMin[k] = low[k] - playerHigh[k];
                        movers[i].TriggerMax[k] = high[k] - playerLow[k];
                    }
                }
            }
            return movers;
        }

        // Quake's space (x, y, z) is this game's (x, z, -y), its units
        // Q3Convert::UnitsPerUnit of this game's.
        [[nodiscard]] Vector3 Game(Vector3 quake)
        {
            const float unit = Q3Convert::UnitsPerUnit;
            return Vector3(quake.X / unit, quake.Z / unit, -quake.Y / unit);
        }

        [[nodiscard]] Vector3 GameDirection(Vector3 quake)
        {
            return Vector3(quake.X, quake.Z, -quake.Y);
        }

        [[nodiscard]] Matrix4 Turned(const Mover& mover, Matrix4 rotation)
        {
            const Vector3 pivot = Game(mover.Origin);
            if (mover.Relative)
            {
                return OpenTK::Mathematics::Multiply(rotation, OpenTK::Mathematics::CreateTranslation(pivot));
            }
            return OpenTK::Mathematics::Multiply(
                OpenTK::Mathematics::Multiply(OpenTK::Mathematics::CreateTranslation(-pivot), rotation),
                OpenTK::Mathematics::CreateTranslation(pivot));
        }

        [[nodiscard]] Matrix4 Moved(const Mover& mover, Vector3 offset)
        {
            const Vector3 base = mover.Relative ? Game(mover.Origin) : Vector3(0.0F, 0.0F, 0.0F);
            return OpenTK::Mathematics::CreateTranslation(base + Game(offset));
        }

        [[nodiscard]] Matrix4 Where(const Mover& mover, float seconds)
        {
            constexpr float Turn = 2.0F * std::numbers::pi_v<float>;
            constexpr float Degree = std::numbers::pi_v<float> / 180.0F;
            switch (mover.Type)
            {
            case Kind::Bob:
                return Moved(mover, OpenTK::Mathematics::Multiply(mover.Axis, mover.Height * std::sin(Turn * (seconds / mover.Speed - mover.Phase))));
            case Kind::Rotate:
                return Turned(mover, OpenTK::Mathematics::CreateFromAxisAngle(
                    GameDirection(mover.Axis), mover.Speed * seconds * Degree));
            case Kind::Pendulum:
            {
                const float roll = mover.Speed * std::sin(Turn * (seconds * mover.Frequency - mover.Phase));
                const Matrix4 swing = OpenTK::Mathematics::CreateFromAxisAngle(
                    GameDirection(Vector3(1.0F, 0.0F, 0.0F)), roll * Degree);
                const Matrix4 facing = OpenTK::Mathematics::CreateFromAxisAngle(
                    GameDirection(Vector3(0.0F, 0.0F, 1.0F)), mover.Yaw * Degree);
                return Turned(mover, OpenTK::Mathematics::Multiply(swing, facing));
            }
            case Kind::Door:
            default:
                return Moved(mover, OpenTK::Mathematics::Multiply(mover.Axis, mover.Distance * (mover.StartOpen ? 1.0F - mover.Open : mover.Open)));
            }
        }

        void Step(Mover& door, float seconds, bool someone)
        {
            const float rate = door.Speed / door.Distance;
            switch (door.State)
            {
            case DoorState::Closed:
                if (someone)
                {
                    door.State = DoorState::Opening;
                }
                break;
            case DoorState::Opening:
                door.Open = std::min(1.0F, door.Open + seconds * rate);
                if (door.Open >= 1.0F)
                {
                    door.State = DoorState::Open;
                    door.Hold = door.Wait;
                }
                break;
            case DoorState::Open:
                if (someone)
                {
                    door.Hold = door.Wait;
                }
                else if (door.Wait >= 0.0F)
                {
                    door.Hold -= seconds;
                    if (door.Hold <= 0.0F)
                    {
                        door.State = DoorState::Closing;
                    }
                }
                break;
            case DoorState::Closing:
                if (someone)
                {
                    door.State = DoorState::Opening;
                    break;
                }
                door.Open = std::max(0.0F, door.Open - seconds * rate);
                if (door.Open <= 0.0F)
                {
                    door.State = DoorState::Closed;
                }
                break;
            }
        }
    }

    bool Q3Movers::IsMover(const Q3Bsp* bsp, std::int32_t model)
    {
        if (bsp == nullptr || model <= 0)
        {
            return false;
        }
        const Q3Entity* entity = EntityOf(bsp, model);
        if (entity == nullptr)
        {
            return false;
        }
        const std::string kind = ClassOf(*entity);
        if (kind == "func_bobbing" || kind == "func_rotating" || kind == "func_pendulum")
        {
            return true;
        }
        return kind == "func_door" && Value(*entity, "targetname") == nullptr && Value(*entity, "health") == nullptr;
    }

    bool Q3Movers::IsRelative(const Q3Bsp* bsp, std::int32_t model)
    {
        if (bsp == nullptr || model <= 0 || static_cast<std::size_t>(model) >= bsp->Models().size())
        {
            return false;
        }
        const Q3Entity* entity = EntityOf(bsp, model);
        const std::optional<Vector3> origin = entity == nullptr ? std::nullopt : Point(*entity, "origin");
        if (!origin.has_value())
        {
            return false;
        }
        const Q3Model& brushes = *bsp->Models()[static_cast<std::size_t>(model)];
        const std::vector<float>& mins = *brushes.Mins();
        const std::vector<float>& maxs = *brushes.Maxs();
        const Vector3 centre((mins[0] + maxs[0]) * 0.5F, (mins[1] + maxs[1]) * 0.5F, (mins[2] + maxs[2]) * 0.5F);
        return OpenTK::Mathematics::LengthSquared(centre)
            < OpenTK::Mathematics::LengthSquared(centre - *origin);
    }

    void Q3Movers::Update(
        std::int32_t roomId,
        std::uint64_t frame,
        const std::vector<OpenTK::Mathematics::Vector3>& players)
    {
        Cache& cache = Movers();
        if (cache.Room != roomId)
        {
            cache.Room = roomId;
            cache.Movers.clear();
            cache.Frame = frame;
            if (const std::shared_ptr<Q3Bsp> bsp = CustomRooms::Q3LevelBsp(roomId))
            {
                cache.Movers = Load(bsp.get());
            }
        }
        else if (frame == cache.Frame)
        {
            return;
        }
        const float elapsed = std::clamp(static_cast<float>(frame - std::min(frame, cache.Frame)) / 60.0F, 0.0F, 0.25F);
        cache.Frame = frame;
        cache.Seconds = static_cast<float>(frame) / 60.0F;

        const float unit = Q3Convert::UnitsPerUnit;
        for (Mover& mover : cache.Movers)
        {
            if (mover.Type != Kind::Door)
            {
                continue;
            }
            bool someone = false;
            for (const Vector3& player : players)
            {
                const std::array<float, 3> at{player.X * unit, -player.Z * unit, player.Y * unit};
                bool inside = true;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    inside = inside && at[k] >= mover.TriggerMin[k] && at[k] <= mover.TriggerMax[k];
                }
                someone = someone || inside;
            }
            Step(mover, elapsed, someone);
        }
    }

    std::optional<OpenTK::Mathematics::Matrix4> Q3Movers::Motion(std::int32_t roomId, std::int32_t model)
    {
        const Cache& cache = Movers();
        if (cache.Room != roomId)
        {
            return std::nullopt;
        }
        for (const Mover& mover : cache.Movers)
        {
            if (mover.Model == model)
            {
                return Where(mover, cache.Seconds);
            }
        }
        return std::nullopt;
    }

    void Q3Movers::Apply(
        std::int32_t roomId,
        const std::vector<std::shared_ptr<MphRead::Node>>& nodes)
    {
        for (const std::shared_ptr<MphRead::Node>& nodeValue : nodes)
        {
            if (nodeValue == nullptr || nodeValue->Name.rfind("q3model", 0) != 0)
            {
                continue;
            }
            if (const auto motion = Motion(roomId, std::atoi(nodeValue->Name.c_str() + 7)))
            {
                nodeValue->Animation = OpenTK::Mathematics::Multiply(nodeValue->Animation, *motion);
            }
        }
    }
}
