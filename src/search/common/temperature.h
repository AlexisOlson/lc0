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

#pragma once

namespace lczero {

// Computes the effective temperature (tau) for move selection based on game ply.
// This function centralizes the temperature calculation logic that was previously
// duplicated across search implementations.
//
// @param ply: Current game ply (half-moves played)
// @param initial_temperature: Base temperature value
// @param cutoff_move: Move number after which endgame temperature is used (0 = disabled)
// @param decay_delay_moves: Number of moves to delay before starting temperature decay
// @param decay_moves: Number of moves over which temperature decays to 0
// @param endgame_temperature: Temperature used in endgame or as minimum decay value
//
// @return: The effective temperature to use for move selection
//
// Note: This function maintains the exact same behavior as the original code in
// classic/search.cc and dag_classic/search.cc, but centralizes the logic.
float EffectiveTau(int ply,
                   float initial_temperature,
                   int cutoff_move,
                   int decay_delay_moves,
                   int decay_moves,
                   float endgame_temperature);

}  // namespace lczero