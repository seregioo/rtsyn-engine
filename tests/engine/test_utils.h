#ifndef RTSYN_ENGINE_TEST_UTILS_H
#define RTSYN_ENGINE_TEST_UTILS_H

#include <gtest/gtest.h>

extern "C" {
#include <rtsyn/engine.h>
#include <rtsyn/internal/engine.h>
#include <rtsyn/internal/engine/events.h>
#include <rtsyn/runtime/config.h>
}

class EngineTest : public ::testing::Test {
  protected:
    void
    SetUp() override
    {
        rtsyn_runtime_config_t runtime_config = {};
        rtsyn_runtime_config_init(&runtime_config);
        runtime_config.period_ns = 0;
        runtime_ = rtsyn_runtime_create(&runtime_config);
        ASSERT_NE(runtime_, nullptr);

        rtsyn_spsc_command_init(&command_queue_);
        rtsyn_spsc_result_init(&result_queue_);
        rtsyn_spsc_telemetry_init(&telemetry_queue_);
        rtsyn_spsc_telemetry_values_init(&telemetry_values_);
    }

    void
    TearDown() override
    {
        rtsyn_engine_destroy(engine_);
        engine_ = nullptr;
        rtsyn_runtime_destroy(runtime_);
        runtime_ = nullptr;
    }

    rtsyn_engine_config_t
    MakeConfig()
    {
        rtsyn_engine_config_t config = {};
        rtsyn_engine_config_init(&config);
        config.runtime = runtime_;
        config.command_queue = &command_queue_;
        config.result_queue = &result_queue_;
        config.telemetry_queue = &telemetry_queue_;
        config.telemetry_values = &telemetry_values_;
        return config;
    }

    rtsyn_engine_t *
    CreateEngine()
    {
        rtsyn_engine_config_t config = MakeConfig();
        engine_ = rtsyn_engine_create(&config);
        return engine_;
    }

    rtsyn_runtime_t *runtime_ = nullptr;
    rtsyn_engine_t *engine_ = nullptr;
    rtsyn_spsc_command_queue_t command_queue_ = {};
    rtsyn_spsc_result_queue_t result_queue_ = {};
    rtsyn_spsc_telemetry_queue_t telemetry_queue_ = {};
    rtsyn_spsc_telemetry_values_t telemetry_values_ = {};
};

#endif /* RTSYN_ENGINE_TEST_UTILS_H */
