#include "Mods/Network/Gameplay/Claims/Hit/HitClaimMetrics.hpp"

namespace MphRead::Mods::Network
{
    void HitClaimMetrics::Increment(Counter counter) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(counter);
        if (index < _counters.size())
        {
            ++_counters[index];
        }
    }

    void HitClaimMetrics::Add(Counter counter, std::int64_t amount) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(counter);
        if (index < _counters.size())
        {
            _counters[index] += amount;
        }
    }

    std::int64_t HitClaimMetrics::Value(Counter counter) const noexcept
    {
        const std::size_t index = static_cast<std::size_t>(counter);
        return index < _counters.size() ? _counters[index] : 0;
    }

    bool HitClaimMetrics::RecordDamageAgreement(
        std::size_t bucket, std::int32_t claimed, std::int32_t resolved) noexcept
    {
        if (bucket >= _damageAgreement.size() || resolved <= 0)
        {
            return false;
        }
        DamageAgreement& agreement = _damageAgreement[bucket];
        agreement.Claimed += claimed;
        agreement.Resolved += resolved;
        if (claimed == resolved)
        {
            ++agreement.Agreed;
            return false;
        }
        ++agreement.Differed;
        return true;
    }

    HitClaimMetrics::DamageAgreement HitClaimMetrics::AgreementAt(std::size_t bucket) const noexcept
    {
        return bucket < _damageAgreement.size() ? _damageAgreement[bucket] : DamageAgreement{};
    }

    bool HitClaimMetrics::TakeDisagreementLogSlot() noexcept
    {
        if (_disagreementsLogged >= 20)
        {
            return false;
        }
        ++_disagreementsLogged;
        return true;
    }

    void HitClaimMetrics::Reset() noexcept
    {
        _counters.fill(0);
        _damageAgreement.fill(DamageAgreement{});
        _disagreementsLogged = 0;
    }
}
