#include "../test_utils.h"

#include <cstdio>
#include <string>

extern "C" {
#include <rtsyn/node/instance/plugin.h>
}

namespace {

int engine_param_value = 0;

std::string
DeviceModulePath()
{
    std::string path = RTSYN_TEST_MODULE_PATH;
    const std::string plugin_name = "librtsyn-mock.so";
    const std::size_t position = path.rfind(plugin_name);
    if (position != std::string::npos)
    {
        path.replace(position, plugin_name.size(), "librtsyn-mock-device.so");
    }
    return path;
}

extern "C" rtsyn_abi_status_t
engine_test_create(void **out_instance)
{
    if (!out_instance)
    {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    *out_instance = &engine_param_value;
    return RTSYN_ABI_STATUS_OK;
}

extern "C" rtsyn_abi_status_t
engine_test_set_param(void *instance, uint32_t param_index, const void *value)
{
    if (!instance || param_index != 0 || !value)
    {
        return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
    }
    engine_param_value = *(const int *)value;
    return RTSYN_ABI_STATUS_OK;
}

extern "C" rtsyn_abi_status_t
engine_test_read_state(const void *, uint32_t, void *)
{
    return RTSYN_ABI_STATUS_INVALID_ARGUMENT;
}

extern "C" rtsyn_abi_status_t
engine_test_step(void *)
{
    return RTSYN_ABI_STATUS_OK;
}

extern "C" rtsyn_abi_status_t
engine_test_process(void *, const rtsyn_abi_runtime_context_t *)
{
    return RTSYN_ABI_STATUS_OK;
}

extern "C" void
engine_test_destroy(void *)
{
}

rtsyn_abi_node_descriptor_t
make_engine_test_descriptor()
{
    rtsyn_abi_node_descriptor_t descriptor = {};
    descriptor.name = "engine-test-param";
    descriptor.node_type = RTSYN_ABI_NODE_PLUGIN;
    descriptor.callbacks.create = engine_test_create;
    descriptor.callbacks.set_param = engine_test_set_param;
    descriptor.callbacks.read_state = engine_test_read_state;
    descriptor.callbacks.start = engine_test_step;
    descriptor.callbacks.process = engine_test_process;
    descriptor.callbacks.stop = engine_test_step;
    descriptor.callbacks.destroy = engine_test_destroy;
    return descriptor;
}

} // namespace

TEST_F(EngineTest, DispatchCommandAcceptsNone)
{
    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_NONE;
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
}

TEST_F(EngineTest, DispatchCommandForwardsGlobalCommand)
{
    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND;
    message.data.global_command.command = RTSYN_ENGINE_GLOBAL_COMMAND_STOP;
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_TRUE(rtsyn_engine_is_stop_requested(engine_));
}

TEST_F(EngineTest, DispatchCommandUpdatesRuntimePriority)
{
    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PRIORITY;
    message.data.set_runtime_priority.priority = 42;
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_->config.rt_thread.priority, 42);

    message.data.set_runtime_priority.priority = 100;
    EXPECT_FALSE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_->config.rt_thread.priority, 42);
}

TEST_F(EngineTest, DispatchCommandUpdatesDeadlineTolerance)
{
    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_DEADLINE_TOLERANCE;
    message.data.set_runtime_deadline_tolerance.tolerance_ns = 25000;
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_->config.deadline_tolerance_ns, 25000u);
}

TEST_F(EngineTest, DispatchCommandHandlesTelemetryCommands)
{
    rtsyn_spsc_command_message_t message = {};
    ASSERT_NE(CreateEngine(), nullptr);

    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES;
    message.data.plugin_request_ports.plugin_id = 5;
    message.data.plugin_request_ports.send = true;
    message.data.plugin_request_ports.portsyn_mask = 0x5;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_TRUE(engine_->telemetry_subscriptions[0].used);
    EXPECT_EQ(engine_->telemetry_subscriptions[0].node_id, 5u);
    EXPECT_EQ(engine_->telemetry_subscriptions[0].port_values_mask, 0x5u);

    message.data.plugin_request_ports.send = false;
    message.data.plugin_request_ports.portsyn_mask = 0x1;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_->telemetry_subscriptions[0].port_values_mask, 0x4u);

    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES;
    message.data.plugin_request_variables.plugin_id = 5;
    message.data.plugin_request_variables.send = true;
    message.data.plugin_request_variables.variable_mask = 0x2;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_->telemetry_subscriptions[0].variable_mask, 0x2u);

    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES;
    message.data.plugin_request_ports.send = false;
    message.data.plugin_request_ports.portsyn_mask = 0x4;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_TRUE(engine_->telemetry_subscriptions[0].used);

    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES;
    message.data.plugin_request_variables.send = false;
    message.data.plugin_request_variables.variable_mask = 0x2;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_FALSE(engine_->telemetry_subscriptions[0].used);
}

TEST_F(EngineTest, DispatchCommandForwardsPluginUpdatesToRuntime)
{
    rtsyn_abi_node_descriptor_t descriptor = make_engine_test_descriptor();
    rtsyn_node_t *node = rtsyn_node_create_plugin(&descriptor);
    ASSERT_NE(node, nullptr);
    const rtsyn_node_id_t node_id = rtsyn_node_get_id(node);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, node));
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE;
    message.data.plugin_update.plugin_id = node_id;
    message.data.plugin_update.plugin_state = RTSYN_NODE_RUNTIME_STATE_PROCESS;

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(rtsyn_node_get_runtime_state(node), RTSYN_NODE_RUNTIME_STATE_PROCESS);
}

TEST_F(EngineTest, DispatchCommandLoadsAndAddsPluginNode)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t load = {};
    load.seq = 41;
    load.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    load.data.load_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(load.data.load_node.module_path, sizeof(load.data.load_node.module_path), "%s",
             RTSYN_TEST_MODULE_PATH);

    ASSERT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &load));
    EXPECT_NE(rtsyn_collection_get(engine_->loaded_nodes, (void *)"test-module"), nullptr);
    rtsyn_spsc_result_message_t result = {};
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 41U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "test-module");
    EXPECT_EQ(result.node.node_type, RTSYN_ABI_NODE_PLUGIN);

    rtsyn_spsc_command_message_t add = {};
    add.seq = 42;
    add.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    add.data.add_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(add.data.add_node.node_name, sizeof(add.data.add_node.node_name), "test-module");

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &add));
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 42U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "test-module");
    EXPECT_NE(result.node_id, RTSYN_NODE_ID_INVALID);
}

#ifdef RTSYN_RTHYBRID_TEST_MODULE_PATH
TEST_F(EngineTest, DispatchCommandLoadsAndAddsRTHybridPluginNode)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t load = {};
    load.seq = 141;
    load.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    load.data.load_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(load.data.load_node.module_path, sizeof(load.data.load_node.module_path), "%s",
             RTSYN_RTHYBRID_TEST_MODULE_PATH);

    ASSERT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &load));
    EXPECT_NE(rtsyn_collection_get(engine_->loaded_nodes,
                                   (void *)"rthybrid_hindmarsh_rose_1984_neuron_v2"),
              nullptr);
    rtsyn_spsc_result_message_t result = {};
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 141U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "rthybrid_hindmarsh_rose_1984_neuron_v2");
    EXPECT_EQ(result.node.node_type, RTSYN_ABI_NODE_PLUGIN);

    rtsyn_spsc_command_message_t add = {};
    add.seq = 142;
    add.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    add.data.add_node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(add.data.add_node.node_name, sizeof(add.data.add_node.node_name),
             "rthybrid_hindmarsh_rose_1984_neuron_v2");

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &add));
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 142U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "rthybrid_hindmarsh_rose_1984_neuron_v2");
    EXPECT_NE(result.node_id, RTSYN_NODE_ID_INVALID);
}
#endif

TEST_F(EngineTest, DispatchCommandLoadsAndAddsDeviceNode)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t load = {};
    load.seq = 51;
    load.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    load.data.load_node.node_type = RTSYN_ABI_NODE_DEVICE;
    snprintf(load.data.load_node.module_path, sizeof(load.data.load_node.module_path), "%s",
             DeviceModulePath().c_str());

    ASSERT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &load));
    EXPECT_NE(rtsyn_collection_get(engine_->loaded_nodes, (void *)"test-device"), nullptr);
    rtsyn_spsc_result_message_t result = {};
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 51U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "test-device");
    EXPECT_EQ(result.node.node_type, RTSYN_ABI_NODE_DEVICE);

    rtsyn_spsc_command_message_t add = {};
    add.seq = 52;
    add.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    add.data.add_node.node_type = RTSYN_ABI_NODE_DEVICE;
    snprintf(add.data.add_node.node_name, sizeof(add.data.add_node.node_name), "test-device");

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &add));
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 52U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_STREQ(result.node.name, "test-device");
    EXPECT_NE(result.node_id, RTSYN_NODE_ID_INVALID);
}

TEST_F(EngineTest, DispatchCommandAddsAndRemovesConnection)
{
    const rtsyn_abi_port_descriptor_t descriptors[] = {
        {"input", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_IN},
        {"output", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_OUT}};
    rtsyn_abi_node_descriptor_t source_descriptor = make_engine_test_descriptor();
    source_descriptor.name = "engine-test-source";
    source_descriptor.port_count = 2;
    source_descriptor.ports = descriptors;
    rtsyn_abi_node_descriptor_t destination_descriptor = make_engine_test_descriptor();
    destination_descriptor.name = "engine-test-destination";
    destination_descriptor.port_count = 2;
    destination_descriptor.ports = descriptors;
    rtsyn_node_t *source = rtsyn_node_create_plugin(&source_descriptor);
    rtsyn_node_t *destination = rtsyn_node_create_plugin(&destination_descriptor);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    const rtsyn_node_id_t source_id = rtsyn_node_get_id(source);
    const rtsyn_node_id_t destination_id = rtsyn_node_get_id(destination);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, source));
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, destination));
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t add = {};
    add.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION;
    add.data.add_connection.connection_id = 33;
    add.data.add_connection.source_node_id = source_id;
    add.data.add_connection.source_port_id = 1;
    add.data.add_connection.destination_node_id = destination_id;
    add.data.add_connection.destination_port_id = 0;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &add));
    EXPECT_NE(rtsyn_runtime_get_connection(runtime_, 33), nullptr);

    rtsyn_spsc_command_message_t remove = {};
    remove.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION;
    remove.data.remove_connection.connection_id = 33;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &remove));
    EXPECT_EQ(rtsyn_runtime_get_connection(runtime_, 33), nullptr);
}

TEST_F(EngineTest, DispatchCommandRequestsRuntimeNodesIncludesConnections)
{
    const rtsyn_abi_port_descriptor_t descriptors[] = {
        {"input", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_IN},
        {"output", RTSYN_ABI_VALUE_F64, RTSYN_ABI_PORT_DIRECTION_OUT}};
    rtsyn_abi_node_descriptor_t source_descriptor = make_engine_test_descriptor();
    source_descriptor.name = "engine-snapshot-source";
    source_descriptor.port_count = 2;
    source_descriptor.ports = descriptors;
    rtsyn_abi_node_descriptor_t destination_descriptor = make_engine_test_descriptor();
    destination_descriptor.name = "engine-snapshot-destination";
    destination_descriptor.port_count = 2;
    destination_descriptor.ports = descriptors;
    rtsyn_node_t *source = rtsyn_node_create_plugin(&source_descriptor);
    rtsyn_node_t *destination = rtsyn_node_create_plugin(&destination_descriptor);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    const rtsyn_node_id_t source_id = rtsyn_node_get_id(source);
    const rtsyn_node_id_t destination_id = rtsyn_node_get_id(destination);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, source));
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, destination));
    ASSERT_TRUE(rtsyn_runtime_add_connection_between(runtime_, 71, source_id, 1, destination_id, 0));
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t message = {};
    message.seq = 99;
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES;
    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));

    rtsyn_spsc_result_message_t result = {};
    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.seq, 99U);
    EXPECT_EQ(result.command_type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES);
    EXPECT_EQ(result.status, RTSYN_SPSC_RESULT_STATUS_OK);
    EXPECT_EQ(result.node_id, RTSYN_NODE_ID_INVALID);
    EXPECT_EQ(result.connection.connection_id, 71U);
    EXPECT_EQ(result.connection.source_node_id, source_id);
    EXPECT_EQ(result.connection.source_port_id, 1U);
    EXPECT_EQ(result.connection.destination_node_id, destination_id);
    EXPECT_EQ(result.connection.destination_port_id, 0U);
    EXPECT_EQ(result.connection.timing, (uint32_t)RTSYN_CONNECTION_TIMING_CURRENT_CYCLE);

    ASSERT_TRUE(rtsyn_spsc_result_try_pop(&result_queue_, &result));
    EXPECT_EQ(result.status_code, UINT32_MAX);
    EXPECT_EQ(result.node_id, RTSYN_NODE_ID_INVALID);
}

TEST_F(EngineTest, DispatchCommandRejectsNodeTypeMismatch)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t load = {};
    load.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    load.data.load_node.node_type = RTSYN_ABI_NODE_DEVICE;
    snprintf(load.data.load_node.module_path, sizeof(load.data.load_node.module_path), "%s",
             RTSYN_TEST_MODULE_PATH);

    EXPECT_FALSE(rtsyn_engine_event_dispatch_command(engine_, &load));
}

TEST_F(EngineTest, DispatchCommandForwardsSetParamToRuntime)
{
    rtsyn_abi_param_descriptor_t param_descriptors[] = {
        {"gain", "some param", RTSYN_ABI_VALUE_I64}};
    rtsyn_abi_node_descriptor_t descriptor = make_engine_test_descriptor();
    descriptor.param_count = 1;
    descriptor.params = param_descriptors;
    rtsyn_node_t *node = rtsyn_node_create_plugin(&descriptor);
    ASSERT_NE(node, nullptr);
    const rtsyn_node_id_t node_id = rtsyn_node_get_id(node);
    ASSERT_TRUE(rtsyn_runtime_add_node(runtime_, node));
    ASSERT_TRUE(rtsyn_runtime_transition_node(runtime_, node_id, RTSYN_NODE_RUNTIME_STATE_INIT));
    ASSERT_EQ(rtsyn_node_step(node, nullptr), RTSYN_ABI_STATUS_OK);
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM;
    message.data.set_param.node_id = node_id;
    message.data.set_param.param_id = 0;
    message.data.set_param.value_type = RTSYN_ABI_VALUE_I64;
    message.data.set_param.value.i64 = 91;

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(engine_param_value, 91);
}

TEST_F(EngineTest, DispatchCommandSetsRuntimePeriod)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_spsc_command_message_t message = {};
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PERIOD;
    message.data.set_runtime_period.period_ns = 500000;

    EXPECT_TRUE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(rtsyn_runtime_get_config(runtime_).period_ns, 500000U);

    message.data.set_runtime_period.period_ns = 0;
    EXPECT_FALSE(rtsyn_engine_event_dispatch_command(engine_, &message));
    EXPECT_EQ(rtsyn_runtime_get_config(runtime_).period_ns, 500000U);
}

TEST_F(EngineTest, DispatchCommandRejectsInvalidArguments)
{
    rtsyn_spsc_command_message_t message = {};
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_FALSE(rtsyn_engine_event_dispatch_command(nullptr, &message));
    EXPECT_FALSE(rtsyn_engine_event_dispatch_command(engine_, nullptr));
}
