#include "radio_hub.h"
#include <gtest/gtest.h>
using namespace esphome;
using namespace esphome::elero;

class RadioDeliveryTest : public ::testing::Test {
 protected:
  Elero hub;
  t_elero_command command{};
  void SetUp() override {
    test_now = 100;
    command.remote_addr = 0x123456; command.num_dests = 1; command.dest_addrs[0] = 0x111111;
  }
  void queue() {
    ASSERT_TRUE(hub.send_command_internal_(&command, test_now));
    hub.active_tx_transaction_id_ = 1;
  }
  std::vector<uint8_t> packet(uint8_t counter) {
    std::vector<uint8_t> p(32); p[0] = 29; p[1] = counter; p[31] = 0x80; return p;
  }
  void receive(uint8_t counter) {
    auto p = packet(counter); hub.fifo.insert(hub.fifo.end(), p.begin(), p.end()); hub.rx_ready_ = true;
  }
};

TEST_F(RadioDeliveryTest, StxIsActuallyIssuedFromRxAfterRssiListenNotFromIdle) {
  queue();
  EXPECT_EQ(hub.tx_state_, TxState::CCA);
  hub.advance_tx(); EXPECT_EQ(hub.marc, CC1101_MARCSTATE_RX);
  test_now++; hub.advance_tx();
  ASSERT_EQ(hub.marc, CC1101_MARCSTATE_TX);
  EXPECT_TRUE(hub.stx_from_rx);
  EXPECT_TRUE(hub.completions.empty());
  hub.finish_tx(); hub.advance_tx();
  ASSERT_EQ(hub.completions.size(), 1u); EXPECT_TRUE(hub.completions[0].success);
  EXPECT_FALSE(hub.illegal_flush); EXPECT_FALSE(hub.illegal_fifo_read);
}

TEST_F(RadioDeliveryTest, BusyChannelReturnsBoundedFailureWithoutRxFlushOrFictitiousSuccess) {
  hub.channel_clear = false; queue();
  for (int i = 0; i < 51; i++) { test_now++; hub.advance_tx(); }
  ASSERT_EQ(hub.completions.size(), 1u); EXPECT_FALSE(hub.completions[0].success);
  EXPECT_EQ(std::count(hub.strobes.begin(), hub.strobes.end(), CC1101_STX), 0);
  EXPECT_EQ(std::count(hub.strobes.begin(), hub.strobes.end(), CC1101_SFRX), 0);
  EXPECT_EQ(hub.marc, CC1101_MARCSTATE_RX);
}

TEST_F(RadioDeliveryTest, HardwareCcaRejectionKeepsTxBytesAndRxOwnershipDuringBackoff) {
  hub.reject_stx = true; queue();
  const auto loaded = hub.tx_fifo;
  test_now++; hub.advance_tx();
  EXPECT_EQ(hub.tx_state_, TxState::CCA);
  EXPECT_EQ(hub.radio_mode_, static_cast<uint8_t>(RadioMode::RX));
  EXPECT_EQ(hub.tx_fifo, loaded); EXPECT_TRUE(hub.completions.empty());
  receive(7);
  for (int i = 0; i < 51; i++) { test_now++; hub.advance_tx(); }
  ASSERT_EQ(hub.received.size(), 1u); EXPECT_EQ(hub.received[0], packet(7));
  ASSERT_EQ(hub.completions.size(), 1u); EXPECT_FALSE(hub.completions[0].success);
  EXPECT_TRUE(hub.stx_from_rx); EXPECT_FALSE(hub.illegal_flush);
}

TEST_F(RadioDeliveryTest, FeedbackDuringCooldownIsDrainedBeforeNextTxPreparation) {
  queue(); test_now++; hub.advance_tx(); hub.finish_tx(); hub.advance_tx();
  receive(10); receive(11);
  test_now++; hub.advance_tx();
  ASSERT_EQ(hub.received.size(), 2u);
  EXPECT_EQ(hub.received[0], packet(10)); EXPECT_EQ(hub.received[1], packet(11));
  EXPECT_TRUE(hub.fifo.empty()); EXPECT_FALSE(hub.illegal_fifo_read);
  ASSERT_TRUE(hub.send_command_internal_(&command, test_now));
  EXPECT_EQ(hub.received.size(), 2u);
}

TEST_F(RadioDeliveryTest, CompletionFenceExcludesAlreadyBufferedStatusBeforeCoreOneDispatch) {
  queue(); test_now++; hub.advance_tx();
  hub.finish_tx(); receive(4); hub.advance_tx();
  ASSERT_EQ(hub.metadata.size(), 1u); ASSERT_EQ(hub.completions.size(), 1u);
  auto old = hub.metadata[0]; old.source = command.dest_addrs[0];
  EXPECT_FALSE(old.after(hub.completions[0].rx_cutoff, old.source));
  test_now += 10; receive(5); hub.advance_tx();
  ASSERT_EQ(hub.metadata.size(), 2u);
  auto fresh = hub.metadata[1]; fresh.source = command.dest_addrs[0];
  EXPECT_TRUE(fresh.after(hub.completions[0].rx_cutoff, fresh.source));
}
