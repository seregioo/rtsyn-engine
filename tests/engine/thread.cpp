#include "test_utils.h"

extern "C" {
#include <rtsyn/thread.h>
}

namespace {

bool
wait_for_result(rtsyn_spsc_result_queue_t *queue, rtsyn_spsc_result_message_t *result,
                uint64_t seq)
{
    for (uint32_t i = 0; i < 5000; i++)
    {
        while (rtsyn_spsc_result_try_pop(queue, result))
        {
            if (result->seq == seq)
            {
                return true;
            }
        }
        rtsyn_thread_sleep(1000000);
    }

    return false;
}

} // namespace

TEST_F(EngineTest, StartRunsThreadsAndJoinReturnsAfterStopRequested)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.max_commands_per_cycle = 4;
    engine_ = rtsyn_engine_create(&config);
    ASSERT_NE(engine_, nullptr);

    ASSERT_TRUE(rtsyn_engine_start(engine_));
    EXPECT_TRUE(rtsyn_engine_is_running(engine_));

    rtsyn_engine_request_stop(engine_);
    EXPECT_TRUE(rtsyn_engine_join(engine_));
    EXPECT_FALSE(rtsyn_engine_is_running(engine_));
}

TEST_F(EngineTest, MeasurementActualPeriodIncludesCycleSleep)
{
    rtsyn_runtime_destroy(runtime_);
    rtsyn_runtime_config_t runtime_config = {};
    rtsyn_runtime_config_init(&runtime_config);
    runtime_config.period_ns = 1000000;
    runtime_ = rtsyn_runtime_create(&runtime_config);
    ASSERT_NE(runtime_, nullptr);

    rtsyn_engine_config_t config = MakeConfig();
    engine_ = rtsyn_engine_create(&config);
    ASSERT_NE(engine_, nullptr);

    ASSERT_TRUE(rtsyn_engine_start(engine_));
    rtsyn_thread_sleep(5000000);
    rtsyn_engine_request_stop(engine_);
    ASSERT_TRUE(rtsyn_engine_join(engine_));

    bool found_measurement = false;
    rtsyn_spsc_telemetry_message_t message = {};
    while (rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &message))
    {
        if (message.type != RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT)
        {
            continue;
        }

        found_measurement = true;
        EXPECT_EQ(message.data.measurement.period_ns, 1000000U);
        EXPECT_GE(message.data.measurement.actual_period_ns, 500000U);
        EXPECT_LT(message.data.measurement.devices_read_ns,
                  message.data.measurement.actual_period_ns);
        break;
    }

    EXPECT_TRUE(found_measurement);
}

#ifdef RTSYN_RTHYBRID_TEST_MODULE_PATH
TEST_F(EngineTest, RunningEngineLoadsAndAddsRTHybridPluginNode)
{
    rtsyn_runtime_destroy(runtime_);
    rtsyn_runtime_config_t runtime_config = {};
    rtsyn_runtime_config_init(&runtime_config);
    runtime_config.period_ns = 1000000;
    runtime_ = rtsyn_runtime_create(&runtime_config);
    ASSERT_NE(runtime_, nullptr);

    rtsyn_engine_config_t config = MakeConfig();
    config.max_commands_per_cycle = 4;
    engine_ = rtsyn_engine_create(&config);
    ASSERT_NE(engine_, nullptr);
    ASSERT_TRUE(rtsyn_engine_start(engine_));

    rtsyn_spsc_command_message_t load = {};
    load.seq = 501;
    load.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    load.data.load_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(load.data.load_node.module_path, sizeof(load.data.load_node.module_path), "%s",
             RTSYN_RTHYBRID_TEST_MODULE_PATH);
    ASSERT_TRUE(rtsyn_spsc_command_try_push(&command_queue_, &load));

    rtsyn_spsc_result_message_t result = {};
    ASSERT_TRUE(wait_for_result(&result_queue_, &result, 501));
    ASSERT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);

    rtsyn_spsc_command_message_t add = {};
    add.seq = 502;
    add.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    add.data.add_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(add.data.add_node.node_name, sizeof(add.data.add_node.node_name),
             "rthybrid_hindmarsh_rose_1984_neuron_v2");
    ASSERT_TRUE(rtsyn_spsc_command_try_push(&command_queue_, &add));

    ASSERT_TRUE(wait_for_result(&result_queue_, &result, 502));
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_NE(result.node_id, RTSYN_NODE_ID_INVALID);

    rtsyn_engine_request_stop(engine_);
    ASSERT_TRUE(rtsyn_engine_join(engine_));
}
#endif
