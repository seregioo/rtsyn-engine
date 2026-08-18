#include "engine/test_utils.h"

#include <rtsyn/engine/defaults.h>

#include <string>

#ifndef RTSYN_TEST_MODULE_PATH
#error RTSYN_TEST_MODULE_PATH must name the module loader test fixture
#endif

namespace {

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

} // namespace

TEST(EngineConfigTest, ConfigInitSetsThreadDefaultsAndCommandBudget)
{
    rtsyn_engine_config_t config = {};
    rtsyn_engine_config_init(&config);

#if defined(RTSYN_ENGINE_THREAD_CORE_PREEMPT_RT) || defined(RTSYN_ENGINE_THREAD_CORE_XENOMAI)
    EXPECT_EQ(config.rt_thread.priority, RTSYN_ENGINE_DEFAULT_RT_THREAD_PRIORITY);
    EXPECT_EQ(config.rt_thread.policy, RTSYN_ENGINE_DEFAULT_RT_THREAD_POLICY);
    EXPECT_EQ(config.rt_thread.inheritsched, RTSYN_ENGINE_DEFAULT_RT_THREAD_INHERITSCHED);
#else
    EXPECT_EQ(config.rt_thread.priority, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_PRIORITY);
    EXPECT_EQ(config.rt_thread.policy, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_POLICY);
    EXPECT_EQ(config.rt_thread.inheritsched, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_INHERITSCHED);
#endif
    EXPECT_EQ(config.wait_thread.priority, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_PRIORITY);
    EXPECT_EQ(config.wait_thread.policy, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_POLICY);
    EXPECT_EQ(config.wait_thread.inheritsched, RTSYN_ENGINE_DEFAULT_WAIT_THREAD_INHERITSCHED);
    EXPECT_EQ(config.max_commands_per_cycle, RTSYN_ENGINE_DEFAULT_COMMAND_BUDGET);
    EXPECT_EQ(config.runtime, nullptr);
    EXPECT_EQ(config.command_queue, nullptr);
    EXPECT_EQ(config.result_queue, nullptr);
}

TEST_F(EngineTest, CreateRejectsNullConfig)
{
    EXPECT_EQ(rtsyn_engine_create(nullptr), nullptr);
}

TEST_F(EngineTest, CreateRejectsMissingRuntime)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.runtime = nullptr;

    EXPECT_EQ(rtsyn_engine_create(&config), nullptr);
}

TEST_F(EngineTest, CreateRejectsMissingCommandQueue)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.command_queue = nullptr;

    EXPECT_EQ(rtsyn_engine_create(&config), nullptr);
}

TEST_F(EngineTest, CreateRejectsInvalidThreadSchedulingInheritance)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.rt_thread.inheritsched = -1;

    EXPECT_EQ(rtsyn_engine_create(&config), nullptr);
}

TEST_F(EngineTest, CreateStoresConfigAndNormalizesZeroCommandBudget)
{
    rtsyn_engine_config_t config = MakeConfig();
    config.max_commands_per_cycle = 0;

    engine_ = rtsyn_engine_create(&config);

    ASSERT_NE(engine_, nullptr);
    EXPECT_EQ(engine_->config.runtime, runtime_);
    EXPECT_EQ(engine_->config.command_queue, &command_queue_);
    EXPECT_EQ(engine_->config.result_queue, &result_queue_);
    EXPECT_EQ(engine_->config.telemetry_queue, &telemetry_queue_);
    EXPECT_EQ(engine_->config.telemetry_values, &telemetry_values_);
    EXPECT_EQ(engine_->config.max_commands_per_cycle, RTSYN_ENGINE_DEFAULT_COMMAND_BUDGET);
    EXPECT_FALSE(rtsyn_engine_is_running(engine_));
    EXPECT_FALSE(rtsyn_engine_is_stop_requested(engine_));
}

TEST_F(EngineTest, RequestStopMarksStopRequested)
{
    ASSERT_NE(CreateEngine(), nullptr);

    rtsyn_engine_request_stop(engine_);

    EXPECT_TRUE(rtsyn_engine_is_stop_requested(engine_));
}

TEST_F(EngineTest, LoadNodeStoresDescriptorByName)
{
    ASSERT_NE(CreateEngine(), nullptr);

    ASSERT_TRUE(rtsyn_engine_load_node(engine_, RTSYN_TEST_MODULE_PATH));

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(engine_->loaded_nodes,
                                                           (void *)"test-module");
    ASSERT_NE(loaded_node, nullptr);
    ASSERT_NE(loaded_node->descriptor, nullptr);
    EXPECT_STREQ(loaded_node->descriptor->name, "test-module");
    EXPECT_EQ(loaded_node->descriptor->node_type, RTSYN_ABI_NODE_PLUGIN);
}

TEST_F(EngineTest, LoadNodeReplacesPreviousDescriptorWithSameName)
{
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_TRUE(rtsyn_engine_load_node(engine_, RTSYN_TEST_MODULE_PATH));
    EXPECT_TRUE(rtsyn_engine_load_node(engine_, RTSYN_TEST_MODULE_PATH));

    rtsyn_engine_loaded_node_t *loaded_node =
        (rtsyn_engine_loaded_node_t *)rtsyn_collection_get(engine_->loaded_nodes,
                                                           (void *)"test-module");
    ASSERT_NE(loaded_node, nullptr);
    ASSERT_NE(loaded_node->descriptor, nullptr);
    EXPECT_STREQ(loaded_node->descriptor->name, "test-module");
}

TEST_F(EngineTest, LoadNodeAsRejectsUnexpectedNodeType)
{
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_FALSE(rtsyn_engine_load_node_as(engine_, RTSYN_TEST_MODULE_PATH, RTSYN_ABI_NODE_DEVICE));
    EXPECT_FALSE(rtsyn_collection_contains(engine_->loaded_nodes, (void *)"test-module"));
}

TEST_F(EngineTest, AddNodeCreatesRuntimeNodeFromLoadedDescriptor)
{
    ASSERT_NE(CreateEngine(), nullptr);
    ASSERT_TRUE(rtsyn_engine_load_node(engine_, RTSYN_TEST_MODULE_PATH));

    rtsyn_node_t *node = rtsyn_engine_add_node(engine_, "test-module");

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(rtsyn_runtime_get_node(runtime_, rtsyn_node_get_id(node)), node);
    EXPECT_EQ(rtsyn_node_get_type(node), RTSYN_ABI_NODE_PLUGIN);
}

TEST_F(EngineTest, AddNodeAsUsesDescriptorDefaults)
{
    ASSERT_NE(CreateEngine(), nullptr);
    ASSERT_TRUE(rtsyn_engine_load_node_as(engine_, RTSYN_TEST_MODULE_PATH, RTSYN_ABI_NODE_PLUGIN));

    rtsyn_node_t *node =
        rtsyn_engine_add_node_as(engine_, "test-module", RTSYN_ABI_NODE_PLUGIN);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(rtsyn_runtime_get_node(runtime_, rtsyn_node_get_id(node)), node);
    EXPECT_EQ(rtsyn_node_get_type(node), RTSYN_ABI_NODE_PLUGIN);
    EXPECT_EQ(rtsyn_engine_add_node_as(engine_, "test-module", RTSYN_ABI_NODE_DEVICE), nullptr);
}

TEST_F(EngineTest, AddDeviceNodeAsUsesDescriptorDefaults)
{
    ASSERT_NE(CreateEngine(), nullptr);
    ASSERT_TRUE(
        rtsyn_engine_load_node_as(engine_, DeviceModulePath().c_str(), RTSYN_ABI_NODE_DEVICE));

    rtsyn_node_t *node = rtsyn_engine_add_node_as(engine_, "test-device", RTSYN_ABI_NODE_DEVICE);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(rtsyn_runtime_get_node(runtime_, rtsyn_node_get_id(node)), node);
    EXPECT_EQ(rtsyn_node_get_type(node), RTSYN_ABI_NODE_DEVICE);
    EXPECT_EQ(rtsyn_engine_add_node_as(engine_, "test-device", RTSYN_ABI_NODE_PLUGIN), nullptr);
}

#ifdef RTSYN_RTHYBRID_TEST_MODULE_PATH
TEST_F(EngineTest, AddNodeAcceptsLoadedRTHybridDescriptor)
{
    ASSERT_NE(CreateEngine(), nullptr);
    ASSERT_TRUE(rtsyn_engine_load_node_as(engine_, RTSYN_RTHYBRID_TEST_MODULE_PATH,
                                         RTSYN_ABI_NODE_PLUGIN));

    rtsyn_node_t *node = rtsyn_engine_add_node_as(
        engine_, "rthybrid_hindmarsh_rose_1984_neuron_v2", RTSYN_ABI_NODE_PLUGIN);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(rtsyn_runtime_get_node(runtime_, rtsyn_node_get_id(node)), node);
    EXPECT_EQ(rtsyn_node_get_type(node), RTSYN_ABI_NODE_PLUGIN);
}
#endif

TEST_F(EngineTest, LoadNodeReplacementRemovesRuntimeNodesWithSameDescriptorName)
{
    ASSERT_NE(CreateEngine(), nullptr);
    ASSERT_TRUE(rtsyn_engine_load_node_as(engine_, RTSYN_TEST_MODULE_PATH, RTSYN_ABI_NODE_PLUGIN));
    rtsyn_node_t *node =
        rtsyn_engine_add_node_as(engine_, "test-module", RTSYN_ABI_NODE_PLUGIN);
    ASSERT_NE(node, nullptr);
    const rtsyn_node_id_t node_id = rtsyn_node_get_id(node);
    ASSERT_NE(rtsyn_runtime_get_node(runtime_, node_id), nullptr);

    ASSERT_TRUE(rtsyn_engine_load_node_as(engine_, RTSYN_TEST_MODULE_PATH, RTSYN_ABI_NODE_PLUGIN));

    EXPECT_EQ(rtsyn_runtime_get_node(runtime_, node_id), nullptr);
}

TEST_F(EngineTest, AddNodeRejectsMissingDescriptor)
{
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_EQ(rtsyn_engine_add_node(engine_, "missing"), nullptr);
}

TEST_F(EngineTest, DispatchCommandRejectsInvalidArguments)
{
    rtsyn_spsc_command_message_t message = {};
    ASSERT_NE(CreateEngine(), nullptr);

    EXPECT_FALSE(rtsyn_engine_dispatch_command(nullptr, &message));
    EXPECT_FALSE(rtsyn_engine_dispatch_command(engine_, nullptr));
}
