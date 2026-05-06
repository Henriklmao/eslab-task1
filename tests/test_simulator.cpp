#include "simulator.h"
#include <gtest/gtest.h>

TEST(SimulatorDelayIndexTest, ZeroDelayUsesCurrentIndex) {
  EXPECT_EQ(calculate_delay_index(42, 100, 0.0001, 0, 0), 42);
}

TEST(SimulatorDelayIndexTest, DelayUsesPreviousBufferIndex) {
  EXPECT_EQ(calculate_delay_index(42, 100, 0.0001, 100, 0), 41);
}

TEST(SimulatorDelayIndexTest, JitterAddsToDelay) {
  EXPECT_EQ(calculate_delay_index(42, 100, 0.0001, 100, 200), 39);
}

TEST(SimulatorDelayIndexTest, WrapsAroundCircularBuffer) {
  EXPECT_EQ(calculate_delay_index(1, 100, 0.0001, 300, 0), 98);
}

TEST(SimulatorDelayIndexTest, ClampsDelayToOldestBufferedSample) {
  EXPECT_EQ(calculate_delay_index(5, 10, 0.001, 1'000'000, 0), 6);
}

TEST(SimulatorDelayIndexTest, InvalidTimingFallsBackToZeroIndex) {
  EXPECT_EQ(calculate_delay_index(5, 10, 0, 100, 0), 0);
  EXPECT_EQ(calculate_delay_index(5, 0, 0.0001, 100, 0), 0);
}
