#pragma once

// Isolated board-alive bootstrap. Runs before USB wait, module-store reads and
// timer-wake dispatch. It publishes no runtime capability and has no off path.
bool x4PrepareBootPower();
