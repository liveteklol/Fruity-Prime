#pragma once

namespace MphRead::Mods
{
    // A process that makes no sound at all: the scripted clients a test
    // session runs beside somebody's own game, whose music and effects would
    // otherwise play over it. Effects get no device (Sound::Sfx::Load) and
    // music plays at nothing (Music::Volume), whatever the settings say.
    class AudioMute final
    {
    public:
        AudioMute() = delete;

        [[nodiscard]] static bool Active() noexcept { return _active; }
        static void Enter() noexcept { _active = true; }

    private:
        inline static bool _active = false;
    };
}
