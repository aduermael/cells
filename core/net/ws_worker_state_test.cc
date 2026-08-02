// Unit tests for WsWorkerState — shipped reconnect/abandon rules used by WinHTTP WS.

#include "core/net/windows/ws_worker_state.h"

#include <gtest/gtest.h>

using cells::net::WsWorkerState;

TEST(WsWorkerStateTest, BeginConnectSucceedsWhenIdle) {
    WsWorkerState state;
    EXPECT_FALSE(state.isStarted());
    EXPECT_FALSE(state.isClosing());
    ASSERT_TRUE(state.tryBeginConnect());
    EXPECT_TRUE(state.isStarted());
    EXPECT_FALSE(state.isClosing());
}

TEST(WsWorkerStateTest, DoubleBeginConnectRejectedUntilFinished) {
    WsWorkerState state;
    ASSERT_TRUE(state.tryBeginConnect());
    EXPECT_FALSE(state.tryBeginConnect());
    state.onWorkerFinished();
    EXPECT_FALSE(state.isStarted());
    ASSERT_TRUE(state.tryBeginConnect());
}

TEST(WsWorkerStateTest, ReconnectAfterCloseRequestAndFinish) {
    WsWorkerState state;
    ASSERT_TRUE(state.tryBeginConnect());
    state.requestClose();
    EXPECT_TRUE(state.isClosing());
    EXPECT_TRUE(state.isStarted());

    // Worker still "running" until finished — no second connect yet.
    EXPECT_FALSE(state.tryBeginConnect());

    state.onWorkerFinished();
    // New connect clears closing and allows reuse (reset()+connect path).
    ASSERT_TRUE(state.tryBeginConnect());
    EXPECT_TRUE(state.isStarted());
    EXPECT_FALSE(state.isClosing());
}

TEST(WsWorkerStateTest, AbandonNotifyCloseWhenClosing) {
    EXPECT_EQ(WsWorkerState::abandonNotify(true), WsWorkerState::AbandonNotify::Close);
}

TEST(WsWorkerStateTest, AbandonNotifyErrorWhenNotClosing) {
    EXPECT_EQ(WsWorkerState::abandonNotify(false), WsWorkerState::AbandonNotify::Error);
}

TEST(WsWorkerStateTest, MultipleCloseReconnectCycles) {
    WsWorkerState state;
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(state.tryBeginConnect()) << "cycle " << i;
        state.requestClose();
        EXPECT_TRUE(state.isClosing());
        state.onWorkerFinished();
        EXPECT_FALSE(state.isStarted());
    }
}
