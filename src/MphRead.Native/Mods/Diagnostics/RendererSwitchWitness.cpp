#include "RendererSwitchWitness.hpp"
#include "../../Renderer.hpp"
#include "../../Entities/BombEntity.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../Formats/Effects.hpp"
#include "../../NativeRuntime/Rhi/SceneBackend.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace MphRead::Mods::Diagnostics
{
    namespace
    {
        using Bits = std::uint32_t;
        Bits Bit(float value) { return std::bit_cast<Bits>(value); }
        auto Position(OpenTK::Mathematics::Vector3 position)
        { return std::array{Bit(position.X), Bit(position.Y), Bit(position.Z)}; }
        struct Particle final
        {
            std::shared_ptr<Effects::EffectParticle> Object;
            std::array<Bits, 3> PositionBits;
            std::array<Bits, 3> SpeedBits;
            std::array<Bits, 18> Values;
            std::array<int, 4> Indices;
            std::shared_ptr<Effects::EffectElementEntry> Owner;
            bool Drawable;
            bool operator==(const Particle&) const = default;
        };
        struct Element final
        {
            std::shared_ptr<Effects::EffectElementEntry> Object;
            std::vector<int> Bindings;
            std::vector<Particle> Particles;
            bool operator==(const Element&) const = default;
        };
        struct Bomb final
        {
            std::shared_ptr<Entities::BombEntity> Object;
            Entities::PlayerEntity* Owner;
            Entities::BombFlags Flags;
            int Countdown;
            std::array<Bits, 3> PositionBits;
            std::shared_ptr<Effects::EffectEntry> Effect;
            std::vector<Element> Elements;
            bool operator==(const Bomb&) const = default;
        };
        struct Player final
        {
            std::shared_ptr<Entities::PlayerEntity> Object;
            int Health;
            std::array<Bits, 3> PositionBits;
            bool operator==(const Player&) const = default;
        };
        struct Texture final
        {
            std::uint32_t Width, Height, Layers, Mips;
            NativeRuntime::Rhi::TextureFormat Format;
            bool operator==(const Texture&) const = default;
        };
    }
    struct RendererSwitchWitness::State final
    {
        Scene* World;
        std::uint64_t Frame;
        Bits Elapsed;
        std::vector<Player> Players;
        std::vector<Bomb> Bombs;
        std::map<int, Texture> Textures;
        explicit State(Scene& scene) : World(&scene), Frame(scene.FrameCount()), Elapsed(Bit(scene.ElapsedTime()))
        {
            auto players = scene.GetPlayerEntities().GetEnumerator();
            while (players.MoveNext())
            {
                const auto player = players.Current();
                Players.push_back({player, player->Health(), Position(player->Position)});
            }
            auto bombs = scene.GetBombEntities().GetEnumerator();
            while (bombs.MoveNext())
            {
                const auto bomb = bombs.Current();
                Bomb state{bomb, bomb->Owner(), bomb->Flags(), bomb->Countdown(), Position(bomb->Position), bomb->Effect(), {}};
                if (state.Effect)
                    for (const auto& element : *state.Effect->Elements)
                    {
                        Element entry{element, *element->TextureBindingIds, {}};
                        for (const auto& particle : *element->Particles)
                            entry.Particles.push_back({particle, Position(particle->Position), Position(particle->Speed),
                                {Bit(particle->CreationTime), Bit(particle->ExpirationTime), Bit(particle->Lifespan),
                                    Bit(particle->Scale), Bit(particle->Rotation), Bit(particle->Red), Bit(particle->Green),
                                    Bit(particle->Blue), Bit(particle->Alpha), Bit(particle->PortionTotal),
                                    Bit(particle->RoField1), Bit(particle->RoField2), Bit(particle->RoField3), Bit(particle->RoField4),
                                    Bit(particle->RwField1), Bit(particle->RwField2), Bit(particle->RwField3), Bit(particle->RwField4)},
                                {particle->ParticleId, particle->MaterialId, particle->SetVecsId, particle->DrawId},
                                particle->Owner, particle->ShouldDraw()});
                        for (const auto binding : entry.Bindings)
                        {
                            if (binding <= 0) continue;
                            const auto* image = NativeRuntime::Rhi::SceneDevice().FindTexture({binding});
                            if (!image) throw std::runtime_error("Live bomb particle refers to an absent GPU texture.");
                            const auto& desc = image->Desc();
                            Textures[binding] = {desc.width, desc.height, desc.arrayLayers, desc.mipLevels, desc.format};
                        }
                        state.Elements.push_back(std::move(entry));
                    }
                Bombs.push_back(std::move(state));
            }
        }
    };
    RendererSwitchWitness::RendererSwitchWitness(Scene& scene) : _state(std::make_unique<State>(scene)) {}
    RendererSwitchWitness::~RendererSwitchWitness() = default;
    void RendererSwitchWitness::Validate(Scene& scene, bool report) const
    {
        const State after{scene};
        if (after.World != _state->World || after.Frame != _state->Frame || after.Elapsed != _state->Elapsed
            || after.Players != _state->Players || after.Bombs != _state->Bombs || after.Textures != _state->Textures)
            throw std::runtime_error("Renderer transition changed simulation, actors, bombs, particle data or texture bindings.");
        if (report) std::cout << "[switch witness] PASS; frame=" << after.Frame << "; actors=" << after.Players.size()
            << "; bombs=" << after.Bombs.size() << "; particle textures=" << after.Textures.size()
            << "; world/clock/health/positions/bombs/particles/bindings unchanged\n";
    }
    void RendererSwitchWitness::CheckRejections(Scene& scene) const
    {
        unsigned rejected = 0;
        bool checkedAlpha = false, checkedBinding = false;
        const auto check = [&](auto perturb, auto restore) {
            perturb();
            bool failed = false;
            try { Validate(scene, false); }
            catch (const std::runtime_error&) { failed = true; }
            catch (...) { restore(); throw; }
            restore();
            if (!failed) throw std::runtime_error("Renderer witness accepted a perturbed world/particle/binding.");
            ++rejected;
        };
        if (_state->Players.empty()) throw std::runtime_error("Witness negative control needs an actor.");
        const auto player = _state->Players.front().Object;
        const auto health = player->Health();
        check([&] { player->SetHealth(health ? 0 : 1); }, [&] { player->SetHealth(health); });
        for (const auto& bomb : _state->Bombs)
            for (const auto& element : bomb.Elements)
            {
                if (!checkedAlpha && !element.Particles.empty())
                {
                    const auto particle = element.Particles.front().Object;
                    const auto alpha = particle->Alpha;
                    check([&] { particle->Alpha = alpha == 0 ? 1.0F : 0.0F; }, [&] { particle->Alpha = alpha; });
                    checkedAlpha = true;
                }
                if (!checkedBinding && !element.Bindings.empty())
                {
                    const auto binding = element.Object->TextureBindingIds->front();
                    check([&] { element.Object->TextureBindingIds->front() = INT32_MAX; },
                        [&] { element.Object->TextureBindingIds->front() = binding; });
                    checkedBinding = true;
                }
                if (rejected == 3 && checkedAlpha && checkedBinding)
                {
                    Validate(scene, false); // All original fields must be restored.
                    std::cout << "[switch witness] negative controls PASS; health/particle alpha/texture binding rejected and restored\n";
                    return;
                }
            }
        throw std::runtime_error("Witness negative controls need live particles and texture bindings.");
    }
}
