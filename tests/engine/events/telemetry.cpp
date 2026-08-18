#include "../test_utils.h"

extern "C" {
#include <rtsyn/node/instance/plugin.h>
}

namespace {

extern "C" rtsyn_abi_status_t
telemetry_test_create(void **out_instance)
{
    if (!out_instance)
    {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    *out_instance = nullptr;
    return RTSYN_ABI_STATUS_OK;
}

extern "C" rtsyn_abi_status_t
telemetry_test_read_state(const void *, uint32_t, void *)
{
    return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
}

extern "C" rtsyn_abi_status_t
telemetry_test_step(void *)
{
    return RTSYN_ABI_STATUS_OK;
}

extern "C" rtsyn_abi_status_t
telemetry_test_process(void *, const rtsyn_abi_runtime_context_t *)
{
    return RTSYN_ABI_STATUS_OK;
}

extern "C" void
telemetry_test_destroy(void *)
{
}

rtsyn_abi_node_descriptor_t
make_telemetry_test_descriptor(const char *name)
{
    rtsyn_abi_node_descriptor_t descriptor = {};
    descriptor.name = name;
    descriptor.node_type = RTSYN_ABI_NODE_PLUGIN;
    descriptor.callbacks.create = telemetry_test_create;
    descriptor.callbacks.set_param = nullptr;
    descriptor.callbacks.read_state = telemetry_test_read_state;
    descriptor.callbacks.start = telemetry_test_step;
    descriptor.callbacks.process = telemetry_test_process;
    descriptor.callbacks.stop = telemetry_test_step;
    descriptor.callbacks.destroy = telemetry_test_destroy;
    return descriptor;
}

} // namespace

TEST_F(EngineTest, PushTelemetryAssignsSequenceAndPublishesMessage)
{
    rtsyn_spsc_telemetry_message_t message = {};
    message.timestamp_ns = 100;
    message.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN;
    ASSERT_NE(CreateEngine(), nullptr);

    ASSERT_TRUE(rtsyn_engine_event_push_telemetry(engine_, &message));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.seq, 1u);
    EXPECT_EQ(popped.timestamp_ns, 100u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN);
}

TEST_F(EngineTest, PublishCycleBeginWritesCycleBeginTelemetry)
{
    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 7;

    ASSERT_TRUE(rtsyn_engine_event_publish_cycle_begin(engine_, 1234));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.seq, 1u);
    EXPECT_EQ(popped.timestamp_ns, 1234u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN);
    EXPECT_EQ(popped.data.cycle_begin.cycle_id, 7u);
    EXPECT_EQ(popped.data.cycle_begin.scheduled_timestamp_ns, 1234u);
}

TEST_F(EngineTest, PublishCycleEndWritesCycleEndTelemetry)
{
    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 8;

    ASSERT_TRUE(rtsyn_engine_event_publish_cycle_end(engine_, 10, 20));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.seq, 1u);
    EXPECT_EQ(popped.timestamp_ns, 20u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_END);
    EXPECT_EQ(popped.data.cycle_end.cycle_id, 8u);
    EXPECT_EQ(popped.data.cycle_end.started_at_ns, 10u);
    EXPECT_EQ(popped.data.cycle_end.finished_at_ns, 20u);
}

TEST_F(EngineTest, PublishDroppedSkipsWhenNoDropsWereAccounted)
{
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_FALSE(rtsyn_engine_event_publish_dropped(engine_, 30));
    EXPECT_EQ(rtsyn_spsc_telemetry_size(&telemetry_queue_), 0u);
}

TEST_F(EngineTest, PublishDroppedWritesDroppedTelemetry)
{
    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 9;
    rtsyn_engine_account_dropped_event(engine_);
    rtsyn_engine_account_dropped_values(engine_, 3);

    ASSERT_TRUE(rtsyn_engine_event_publish_dropped(engine_, 30));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.seq, 1u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_DROPPED);
    EXPECT_EQ(popped.data.dropped.cycle_id, 9u);
    EXPECT_EQ(popped.data.dropped.dropped_event_count, 1u);
    EXPECT_EQ(popped.data.dropped.dropped_value_count, 3u);
}

TEST_F(EngineTest, PublishMeasurementWritesMeasurementTelemetry)
{
    ASSERT_NE(CreateEngine(), nullptr);
    rtsyn_measurement_metrics_t metrics = {};
    metrics.cycle_id = 44;
    metrics.period_ns = 1000;
    metrics.actual_period_ns = 1250;
    metrics.latency_ns = 250;
    metrics.wake_lateness_ns = 50;
    metrics.skipped_cycle_count = 2;
    metrics.missed_cycle = true;
    metrics.deadline_missed = true;
    metrics.devices_read_ns = 100;
    metrics.plugins_time_ns = 900;
    metrics.devices_write_ns = 250;

    ASSERT_TRUE(rtsyn_engine_event_publish_measurement(engine_, 5000, &metrics));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.timestamp_ns, 5000u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT);
    EXPECT_EQ(popped.data.measurement.cycle_id, 44u);
    EXPECT_EQ(popped.data.measurement.period_ns, 1000u);
    EXPECT_EQ(popped.data.measurement.actual_period_ns, 1250u);
    EXPECT_EQ(popped.data.measurement.latency_ns, 250u);
    EXPECT_EQ(popped.data.measurement.wake_lateness_ns, 50u);
    EXPECT_EQ(popped.data.measurement.skipped_cycle_count, 2u);
    EXPECT_EQ(popped.data.measurement.missed_cycle, 1u);
    EXPECT_EQ(popped.data.measurement.deadline_missed, 1u);
    EXPECT_EQ(popped.data.measurement.devices_read_ns, 100u);
    EXPECT_EQ(popped.data.measurement.plugins_time_ns, 900u);
    EXPECT_EQ(popped.data.measurement.devices_write_ns, 250u);
}

TEST_F(EngineTest, PublishValuesWritesValuesAndValuesWrittenTelemetry)
{
    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 11;

    rtsyn_spsc_telemetry_value_t sample = {};
    sample.cycle_id = 11;
    sample.timestamp_ns = 1000;
    sample.node_id = 4;
    sample.value_id = 2;
    sample.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    sample.value_type = RTSYN_ABI_VALUE_F64;
    sample.data.f64 = 42.5;

    ASSERT_TRUE(rtsyn_engine_event_publish_values(engine_, 1000, 4,
                                                  RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN, &sample, 1));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.seq, 1u);
    EXPECT_EQ(popped.timestamp_ns, 1000u);
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN);
    EXPECT_EQ(popped.data.values_written.cycle_id, 11u);
    EXPECT_EQ(popped.data.values_written.node_id, 4u);
    EXPECT_EQ(popped.data.values_written.source, RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN);
    EXPECT_EQ(popped.data.values_written.value_count, 1u);

    rtsyn_spsc_telemetry_value_t stored = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_values_try_get(
        &telemetry_values_, popped.data.values_written.values_start_index, &stored));
    EXPECT_EQ(stored.node_id, sample.node_id);
    EXPECT_EQ(stored.value_id, sample.value_id);
    EXPECT_EQ(stored.value_type, sample.value_type);
    EXPECT_DOUBLE_EQ(stored.data.f64, sample.data.f64);
}

TEST_F(EngineTest, PublishRequestedValuesReadsRuntimePortValues)
{
    rtsyn_abi_port_descriptor_t descriptors[] = {
        {"input", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_IN},
        {"output", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_OUT}};
    rtsyn_abi_node_descriptor_t descriptor = make_telemetry_test_descriptor("telemetry-port-node");
    descriptor.port_count = 2;
    descriptor.ports = descriptors;
    rtsyn_node_t *node = rtsyn_node_create_plugin(&descriptor);
    ASSERT_NE(node, nullptr);
    const rtsyn_node_id_t node_id = rtsyn_node_get_id(node);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, node));

    rtsyn_port_t *port =
        rtsyn_runtime_get_node_port(runtime_, node_id, 1, RTSYN_ABI_PORT_DIRECTION_OUT);
    ASSERT_NE(port, nullptr);
    double value = 17.25;
    ASSERT_TRUE(rtsyn_port_set_internal_value_by_ptr(port, &value));

    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 12;
    engine_->telemetry_subscriptions[0].used = true;
    engine_->telemetry_subscriptions[0].node_id = node_id;
    engine_->telemetry_subscriptions[0].port_values_mask = UINT64_C(1) << 1;

    ASSERT_TRUE(rtsyn_engine_event_publish_requested_values(engine_, 2000));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN);
    EXPECT_EQ(popped.data.values_written.cycle_id, 12u);
    EXPECT_EQ(popped.data.values_written.node_id, node_id);
    EXPECT_EQ(popped.data.values_written.value_count, 1u);

    rtsyn_spsc_telemetry_value_t stored = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_values_try_get(
        &telemetry_values_, popped.data.values_written.values_start_index, &stored));
    EXPECT_EQ(stored.cycle_id, 12u);
    EXPECT_EQ(stored.timestamp_ns, 2000u);
    EXPECT_EQ(stored.node_id, node_id);
    EXPECT_EQ(stored.value_id, 1u);
    EXPECT_EQ(stored.value_kind, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT);
    EXPECT_EQ(stored.value_type, RTSYN_ABI_VALUE_F64);
    EXPECT_DOUBLE_EQ(stored.data.f64, value);
}

TEST_F(EngineTest, PublishRequestedValuesReadsRuntimeNodeStates)
{
    rtsyn_abi_state_descriptor_t state_descriptors[] = {
        {"state_a", "some state", RTSYN_ABI_VALUE_I64}};
    rtsyn_abi_node_descriptor_t descriptor = make_telemetry_test_descriptor("telemetry-state-node");
    descriptor.state_count = 1;
    descriptor.states = state_descriptors;
    rtsyn_node_t *node = rtsyn_node_create_plugin(&descriptor);
    ASSERT_NE(node, nullptr);
    const rtsyn_node_id_t node_id = rtsyn_node_get_id(node);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, node));

    ASSERT_NE(CreateEngine(), nullptr);
    engine_->cycle_id = 13;
    engine_->telemetry_subscriptions[0].used = true;
    engine_->telemetry_subscriptions[0].node_id = node_id;
    engine_->telemetry_subscriptions[0].variable_mask = UINT64_C(1);

    ASSERT_TRUE(rtsyn_engine_event_publish_requested_values(engine_, 3000));

    rtsyn_spsc_telemetry_message_t popped = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_pop(&telemetry_queue_, &popped));
    EXPECT_EQ(popped.type, RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN);
    EXPECT_EQ(popped.data.values_written.cycle_id, 13u);
    EXPECT_EQ(popped.data.values_written.node_id, node_id);
    EXPECT_EQ(popped.data.values_written.source, RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN);
    EXPECT_EQ(popped.data.values_written.value_count, 1u);

    rtsyn_spsc_telemetry_value_t stored = {};
    ASSERT_TRUE(rtsyn_spsc_telemetry_values_try_get(
        &telemetry_values_, popped.data.values_written.values_start_index, &stored));
    EXPECT_EQ(stored.cycle_id, 13u);
    EXPECT_EQ(stored.timestamp_ns, 3000u);
    EXPECT_EQ(stored.node_id, node_id);
    EXPECT_EQ(stored.value_id, 0u);
    EXPECT_EQ(stored.value_kind, RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE);
    EXPECT_EQ(stored.source, RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN);
    EXPECT_EQ(stored.value_type, RTSYN_ABI_VALUE_I64);
    EXPECT_EQ(stored.data.i64, 0);
}

TEST_F(EngineTest, PublishRequestedValuesSkipsWhenNoSubscriptionsExist)
{
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_publish_requested_values(engine_, 2000));
    EXPECT_EQ(rtsyn_spsc_telemetry_size(&telemetry_queue_), 0u);
}

TEST_F(EngineTest, TelemetryPublishersRejectMissingQueues)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.telemetry_queue = nullptr;
    engine_ = rtsyn_engine_create(&config);
    ASSERT_NE(engine_, nullptr);

    rtsyn_spsc_telemetry_message_t message = {};
    rtsyn_spsc_telemetry_value_t sample = {};
    EXPECT_FALSE(rtsyn_engine_event_push_telemetry(engine_, &message));
    EXPECT_FALSE(rtsyn_engine_event_publish_cycle_begin(engine_, 1));
    EXPECT_FALSE(rtsyn_engine_event_publish_cycle_end(engine_, 1, 2));
    EXPECT_FALSE(rtsyn_engine_event_publish_dropped(engine_, 3));
    EXPECT_FALSE(rtsyn_engine_event_publish_values(
        engine_, 4, 1, RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN, &sample, 1));
}
