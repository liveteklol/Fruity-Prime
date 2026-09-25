#pragma once

#include <cstdint>

namespace fp {

// Rng: the game's two LCGs. GetRandomInt1 drives effects and visuals,
// GetRandomInt2 gameplay (the camera shake, the bots). Both return [0, max).
uint32_t randomInt1(uint32_t max);
uint32_t randomInt2(uint32_t max);
// Rng.Rng1 and Rng2: the generators' state, which a server's snapshots carry.
uint32_t rngState1();
uint32_t rngState2();

} // namespace fp
