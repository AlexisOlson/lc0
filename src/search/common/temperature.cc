/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2024 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "search/common/temperature.h"

namespace lczero {

float EffectiveTau(int ply,
                   float initial_temperature,
                   int cutoff_move,
                   int decay_delay_moves,
                   int decay_moves,
                   float endgame_temperature) {
  float temperature = initial_temperature;
  
  // Convert ply to move number as specified: (ply / 2) + 1
  // This matches the logic from classic/search.cc lines 1068-1076
  const int moves = (ply / 2) + 1;

  // Apply temperature cutoff logic
  if (cutoff_move && moves >= cutoff_move) {
    temperature = endgame_temperature;
  } else if (temperature && decay_moves) {
    // Apply temperature decay logic
    if (moves >= decay_delay_moves + decay_moves) {
      temperature = 0.0;
    } else if (moves >= decay_delay_moves) {
      temperature *=
          static_cast<float>(decay_delay_moves + decay_moves - moves) /
          decay_moves;
    }
    // Don't allow temperature to decay below endgame temperature
    if (temperature < endgame_temperature) {
      temperature = endgame_temperature;
    }
  }

  return temperature;
}

}  // namespace lczero