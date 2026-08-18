#include "../test_utils.h"

static rtsyn_spsc_command_message_t
MakeGlobalCommand(rtsyn_engine_global_command_t command)
{
    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND;
    message.data.global_command.command = command;
    return message;
}

TEST_F(EngineTest, GlobalCommandNoneDoesNothing)
{
    rtsyn_spsc_command_message_t message = MakeGlobalCommand(RTSYN_ENGINE_GLOBAL_COMMAND_NONE);
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_global_command(engine_, &message));
    EXPECT_FALSE(rtsyn_engine_is_stop_requested(engine_));
}

TEST_F(EngineTest, GlobalCommandStopRequestsStop)
{
    rtsyn_spsc_command_message_t message = MakeGlobalCommand(RTSYN_ENGINE_GLOBAL_COMMAND_STOP);
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_global_command(engine_, &message));
    EXPECT_TRUE(rtsyn_engine_is_stop_requested(engine_));
}

TEST_F(EngineTest, GlobalCommandPauseAndResumeTogglePausedState)
{
    rtsyn_spsc_command_message_t pause = MakeGlobalCommand(RTSYN_ENGINE_GLOBAL_COMMAND_PAUSE);
    rtsyn_spsc_command_message_t resume = MakeGlobalCommand(RTSYN_ENGINE_GLOBAL_COMMAND_RESUME);
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_global_command(engine_, &pause));
    EXPECT_TRUE(engine_->paused.load(std::memory_order_acquire));

    EXPECT_TRUE(rtsyn_engine_event_global_command(engine_, &resume));
    EXPECT_FALSE(engine_->paused.load(std::memory_order_acquire));
}

TEST_F(EngineTest, GlobalCommandRejectsInvalidInputsAndUnknownCommands)
{
    rtsyn_spsc_command_message_t message = MakeGlobalCommand(RTSYN_ENGINE_GLOBAL_COMMAND_COUNT);
    rtsyn_spsc_command_message_t not_global = {};
    not_global.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_NONE;
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_FALSE(rtsyn_engine_event_global_command(nullptr, &message));
    EXPECT_FALSE(rtsyn_engine_event_global_command(engine_, nullptr));
    EXPECT_FALSE(rtsyn_engine_event_global_command(engine_, &not_global));
    EXPECT_FALSE(rtsyn_engine_event_global_command(engine_, &message));
}
