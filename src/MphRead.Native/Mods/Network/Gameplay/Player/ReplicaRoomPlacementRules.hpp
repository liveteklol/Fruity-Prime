#pragma once

#include "Formats/Culling.hpp"
#include "Mods/Network/Domain/NetworkVectorRules.hpp"

#include <cstddef>

namespace MphRead::Mods::Network
{
    // Pure rules shared by room-geometry placement and its deterministic tests.
    class ReplicaRoomPlacementRules final
    {
    public:
        enum class State
        {
            Resolved,
            Ambiguous,
            Unknown
        };

        struct Resolution
        {
            State state = State::Unknown;
            Formats::Culling::NodeRef node = Formats::Culling::NodeRef::None;
        };

        enum class BoundsResult
        {
            Inside,
            Outside,
            Invalid
        };

        class Candidates final
        {
        public:
            void Consider(Formats::Culling::NodeRef candidate)
            {
                using Formats::Culling::NodeRef;
                if (_state == State::Ambiguous || candidate == NodeRef::None) return;
                if (_state == State::Resolved)
                {
                    _state = State::Ambiguous;
                    _node = NodeRef::None;
                    return;
                }
                _state = State::Resolved;
                _node = candidate;
            }

            [[nodiscard]] bool IsAmbiguous() const noexcept
            {
                return _state == State::Ambiguous;
            }

            [[nodiscard]] Resolution Get() const
            {
                using Formats::Culling::NodeRef;
                return { _state, _state == State::Resolved ? _node : NodeRef::None };
            }

        private:
            State _state = State::Unknown;
            Formats::Culling::NodeRef _node = Formats::Culling::NodeRef::None;
        };

        ReplicaRoomPlacementRules() = delete;

        [[nodiscard]] static bool StopsProbeFallback(State state) noexcept
        {
            return state == State::Ambiguous;
        }

        [[nodiscard]] static bool IsInsidePortalSide(
            OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Vector4 plane, bool opposite) noexcept
        {
            float distance = OpenTK::Mathematics::Vector3::Dot(position, plane.Xyz()) - plane.W;
            if (opposite) distance *= -1.0F;
            return distance >= 0.0F;
        }
        [[nodiscard]] static bool IsFinite(OpenTK::Mathematics::Vector3 value) noexcept
        {
            return Domain::IsFiniteVector3(value);
        }
        [[nodiscard]] static BoundsResult CheckBounds(
            OpenTK::Mathematics::Vector3 position,
            OpenTK::Mathematics::Vector3 minimum,
            OpenTK::Mathematics::Vector3 maximum,
            OpenTK::Mathematics::Vector3 offset) noexcept
        {
            minimum = minimum + offset;
            maximum = maximum + offset;
            if (!Domain::IsFiniteVector3(minimum) || !Domain::IsFiniteVector3(maximum)
                || minimum.X > maximum.X || minimum.Y > maximum.Y || minimum.Z > maximum.Z)
                return BoundsResult::Invalid;

            constexpr float tolerance = 1.0F / 4096.0F;
            const bool contains = position.X >= minimum.X - tolerance && position.X <= maximum.X + tolerance
                && position.Y >= minimum.Y - tolerance && position.Y <= maximum.Y + tolerance
                && position.Z >= minimum.Z - tolerance && position.Z <= maximum.Z + tolerance;
            return contains ? BoundsResult::Inside : BoundsResult::Outside;
        }
        [[nodiscard]] static bool IsValidNodeRef(Formats::Culling::NodeRef nodeRef,
            std::int32_t roomPartCount, std::size_t connectorCount,
            std::size_t collisionCount) noexcept
        {
            if (nodeRef.PartIndex < 0 || nodeRef.PartIndex >= roomPartCount
                || nodeRef.NodeIndex < 0 || nodeRef.ModelIndex < 0)
                return false;
            if (nodeRef.ModelIndex == 0) return true;
            const auto modelIndex = static_cast<std::size_t>(nodeRef.ModelIndex);
            return modelIndex <= connectorCount && modelIndex < collisionCount;
        }
    };
}
