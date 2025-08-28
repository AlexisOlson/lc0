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

#include <gtest/gtest.h>
#include "search/common/temperature.h"

namespace lczero {

TEST(Temperature, EffectiveTauBasicTest) {
  // Test basic temperature with no cutoff or decay
  float temp = EffectiveTau(0, 1.0f, 0, 0, 0, 0.0f);
  EXPECT_FLOAT_EQ(temp, 1.0f);
  
  temp = EffectiveTau(2, 0.8f, 0, 0, 0, 0.0f);
  EXPECT_FLOAT_EQ(temp, 0.8f);
}

TEST(Temperature, EffectiveTauCutoffTest) {
  // Test cutoff logic: ply=0 -> move=1, ply=2 -> move=2, etc.
  
  // Move 1 (ply 0), cutoff at move 2 -> should use initial temp
  float temp = EffectiveTau(0, 1.0f, 2, 0, 0, 0.5f);
  EXPECT_FLOAT_EQ(temp, 1.0f);
  
  // Move 2 (ply 2), cutoff at move 2 -> should use endgame temp
  temp = EffectiveTau(2, 1.0f, 2, 0, 0, 0.5f);
  EXPECT_FLOAT_EQ(temp, 0.5f);
  
  // Move 3 (ply 4), cutoff at move 2 -> should use endgame temp
  temp = EffectiveTau(4, 1.0f, 2, 0, 0, 0.5f);
  EXPECT_FLOAT_EQ(temp, 0.5f);
}

TEST(Temperature, EffectiveTauDecayTest) {
  // Test decay logic
  // Move 1 (ply 0), delay 0, decay over 2 moves -> should get initial temp
  float temp = EffectiveTau(0, 1.0f, 0, 0, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 1.0f);
  
  // Move 2 (ply 2), delay 0, decay over 2 moves -> should get 0.5 * initial
  temp = EffectiveTau(2, 1.0f, 0, 0, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 0.5f);
  
  // Move 3 (ply 4), delay 0, decay over 2 moves -> should get 0.0
  temp = EffectiveTau(4, 1.0f, 0, 0, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 0.0f);
}

TEST(Temperature, EffectiveTauDecayDelayTest) {
  // Test decay with delay
  // Move 1 (ply 0), delay 1, decay over 2 moves -> should get initial temp
  float temp = EffectiveTau(0, 1.0f, 0, 1, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 1.0f);
  
  // Move 2 (ply 2), delay 1, decay over 2 moves -> should still get initial temp (delay)
  temp = EffectiveTau(2, 1.0f, 0, 1, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 1.0f);
  
  // Move 3 (ply 4), delay 1, decay over 2 moves -> should start decaying (0.5)
  temp = EffectiveTau(4, 1.0f, 0, 1, 2, 0.0f);
  EXPECT_FLOAT_EQ(temp, 0.5f);
}

TEST(Temperature, EffectiveTauEndgameMinimumTest) {
  // Test that temperature doesn't decay below endgame temperature
  float temp = EffectiveTau(4, 1.0f, 0, 0, 2, 0.3f);
  EXPECT_FLOAT_EQ(temp, 0.3f);  // Should not decay below 0.3
}

}  // namespace lczero