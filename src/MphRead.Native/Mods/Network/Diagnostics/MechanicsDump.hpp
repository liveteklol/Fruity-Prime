#pragma once

#include <string>

namespace MphRead::Mods::Network
{
    class MechanicsDump final
    {
    public:
        MechanicsDump() = delete;
        MechanicsDump(const MechanicsDump&) = delete;
        MechanicsDump& operator=(const MechanicsDump&) = delete;
        MechanicsDump(MechanicsDump&&) = delete;
        MechanicsDump& operator=(MechanicsDump&&) = delete;

        static void Run();

    private:
        static void Weapons(std::string& text);
        static void DamageRules(std::string& text);
        static void Hunters(std::string& text);
        static void Movement(std::string& text);
        static void States(std::string& text);
        static void SpawnRules(std::string& text);
        static void Modes(std::string& text);
        static void World(std::string& text);
        static void Items(std::string& text);
        static void Bots(std::string& text);
        static void Networking(std::string& text);
    };
}
