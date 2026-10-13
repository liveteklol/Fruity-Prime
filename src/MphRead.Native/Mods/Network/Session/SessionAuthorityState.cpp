#include "Mods/Network/Session/SessionAuthorityState.hpp"

#include "NativeRuntime/System/Managed.hpp"

namespace MphRead::Mods::Network
{
    void SessionAuthorityState::BecomeServerAuthority() noexcept
    {
        _isAuthority = true;
    }

    bool SessionAuthorityState::BecomeClientAuthority() noexcept
    {
        if (_isAuthority)
        {
            return false;
        }
        _isAuthority = true;
        _needsStateSync = true;
        return true;
    }

    bool SessionAuthorityState::StandDown() noexcept
    {
        if (!_isAuthority)
        {
            return false;
        }
        _isAuthority = false;
        ::MphRead::NativeRuntime::IncrementInPlace(_standDowns);
        return true;
    }

    bool SessionAuthorityState::ConsumeStateSync() noexcept
    {
        if (!_needsStateSync)
        {
            return false;
        }
        _needsStateSync = false;
        return true;
    }

    void SessionAuthorityState::RecordServerFrame() noexcept
    {
        RecordFrame();
    }

    void SessionAuthorityState::RecordFrame() noexcept
    {
        ::MphRead::NativeRuntime::IncrementInPlace(_frames);
    }

    void SessionAuthorityState::RecordAuthorityFrame() noexcept
    {
        if (_isAuthority)
        {
            RecordFrame();
        }
    }

    void SessionAuthorityState::Reset() noexcept
    {
        _isAuthority = false;
        _needsStateSync = false;
        _standDowns = 0;
        _frames = 0;
    }
}
