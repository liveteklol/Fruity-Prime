#include "Mods/Network/Gameplay/Pickups/PredictionBook.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    bool PredictionBook::HasRoom() const noexcept
    {
        return std::any_of(_predictions.begin(), _predictions.end(), [](const Prediction& p) { return !p.Live; });
    }

    void PredictionBook::Add(std::uint16_t claimId, PowerUp kind, std::int16_t spawnId, std::uint16_t spawnCount,
        std::uint16_t life, std::uint32_t now, std::uint16_t remainingBefore) noexcept
    {
        const auto free = std::find_if(_predictions.begin(), _predictions.end(), [](const Prediction& p) { return !p.Live; });
        if (free == _predictions.end())
        {
            return;
        }
        *free = Prediction{ true, claimId, kind, spawnId, spawnCount, life, now, ++_nextGeneration, false, false, 0,
            _seen[IndexOf(kind)], remainingBefore };
    }

    bool PredictionBook::Holds(std::int16_t spawnId, std::uint16_t spawnCount) const noexcept
    {
        return std::any_of(_predictions.begin(), _predictions.end(), [&](const Prediction& p)
        {
            return p.Live && p.SpawnId == spawnId && p.SpawnCount == spawnCount;
        });
    }

    bool PredictionBook::Awaiting(PowerUp kind) const noexcept
    {
        return std::any_of(_predictions.begin(), _predictions.end(), [&](const Prediction& p)
        {
            return p.Live && !p.Confirmed && p.Kind == kind;
        });
    }

    PredictionBook::Prediction* PredictionBook::Find(std::uint16_t claimId) noexcept
    {
        const auto found = std::find_if(_predictions.begin(), _predictions.end(), [&](const Prediction& p)
        {
            return p.Live && p.ClaimId == claimId;
        });
        return found == _predictions.end() ? nullptr : &*found;
    }

    PredictionBook::Prediction* PredictionBook::Successor(const Prediction& prediction) noexcept
    {
        Prediction* next = nullptr;
        for (Prediction& other : _predictions)
        {
            if (other.Live && &other != &prediction && other.Kind == prediction.Kind && other.Life == prediction.Life
                && other.Generation > prediction.Generation && (next == nullptr || other.Generation < next->Generation))
            {
                next = &other;
            }
        }
        return next;
    }

    std::optional<PredictionBook::Rollback> PredictionBook::Settle(std::uint16_t claimId, bool granted, std::uint8_t sequence,
        std::uint32_t now, std::uint16_t life, bool alive) noexcept
    {
        Prediction* prediction = Find(claimId);
        if (prediction == nullptr || prediction->Granted)
        {
            return std::nullopt;
        }
        if (granted)
        {
            prediction->Granted = true;
            prediction->Sequence = sequence;
            prediction->Confirmed = _seen[IndexOf(prediction->Kind)] == sequence;
            return std::nullopt;
        }
        prediction->Live = false;
        if (prediction->Life != life)
        {
            // Another life's pickup: whatever it showed went with that life.
            return std::nullopt;
        }
        if (Prediction* next = Successor(*prediction))
        {
            // A later pickup of the same kind is still waiting and owns what
            // is shown; what it would roll back to was this one's prediction,
            // so it takes this one's "before" instead.
            const std::uint32_t between = next->Since - prediction->Since;
            next->RemainingBefore = static_cast<std::uint16_t>(
                prediction->RemainingBefore > between ? prediction->RemainingBefore - between : 0U);
            next->SeenBefore = prediction->SeenBefore;
            return std::nullopt;
        }
        if (!alive || _seen[IndexOf(prediction->Kind)] != prediction->SeenBefore)
        {
            // Dead, the effect is gone already; and a grant shown since is
            // the authority's, which the snapshots rule on.
            return std::nullopt;
        }
        const std::uint32_t elapsed = now - prediction->Since;
        return Rollback{ prediction->Kind,
            static_cast<std::uint16_t>(prediction->RemainingBefore > elapsed ? prediction->RemainingBefore - elapsed : 0U) };
    }

    void PredictionBook::Seen(PowerUp kind, std::uint8_t sequence) noexcept
    {
        _seen[IndexOf(kind)] = sequence;
        for (Prediction& p : _predictions)
        {
            if (p.Live && p.Granted && p.Kind == kind && p.Sequence == sequence)
            {
                p.Confirmed = true;
            }
        }
    }

    void PredictionBook::BeginLife(std::uint16_t life) noexcept
    {
        for (Prediction& p : _predictions)
        {
            if (p.Live && p.Life != life)
            {
                p.Live = false;
            }
        }
    }

    void PredictionBook::Reset() noexcept
    {
        _predictions = {};
        _seen = {};
        _nextGeneration = 0;
    }
}
