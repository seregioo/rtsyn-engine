/**
 * @file main.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine host entry point.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <rtsyn/engine.h>
#include <rtsyn/runtime/config.h>
#include <rtsyn/spsc/defaults.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t rtsyn_engine_main_stop_requested = 0;

static void
rtsyn_engine_main_signal_handler(int signum)
{
    (void)signum;
    rtsyn_engine_main_stop_requested = 1;
}

static bool
rtsyn_engine_main_install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = rtsyn_engine_main_signal_handler;
    sigemptyset(&action.sa_mask);

    return sigaction(SIGINT, &action, NULL) == 0 && sigaction(SIGTERM, &action, NULL) == 0;
}

static bool
rtsyn_engine_main_block_process_signals(sigset_t *previous_mask)
{
    sigset_t all_signals;
    sigfillset(&all_signals);
    return pthread_sigmask(SIG_SETMASK, &all_signals, previous_mask) == 0;
}

static bool
rtsyn_engine_main_unblock_stop_signals(void)
{
    sigset_t stop_signals;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    return pthread_sigmask(SIG_UNBLOCK, &stop_signals, NULL) == 0;
}

static const char *
rtsyn_engine_main_env_or_default(const char *name, const char *default_value)
{
    const char *value = getenv(name);
    return value && value[0] != '\0' ? value : default_value;
}

static void
rtsyn_engine_main_close_shared_queues(rtsyn_spsc_command_shared_t *command_shared,
                                      rtsyn_spsc_result_shared_t *result_shared,
                                      rtsyn_spsc_telemetry_shared_t *telemetry_shared,
                                      rtsyn_spsc_telemetry_values_shared_t *values_shared)
{
    rtsyn_spsc_command_shared_close(command_shared);
    rtsyn_spsc_result_shared_close(result_shared);
    rtsyn_spsc_telemetry_shared_close(telemetry_shared);
    rtsyn_spsc_telemetry_values_shared_close(values_shared);
}

static void
rtsyn_engine_main_unlink_shared_queues(const char *command_name, const char *result_name,
                                       const char *telemetry_name, const char *values_name)
{
    (void)rtsyn_spsc_command_shared_unlink(command_name);
    (void)rtsyn_spsc_result_shared_unlink(result_name);
    (void)rtsyn_spsc_telemetry_shared_unlink(telemetry_name);
    (void)rtsyn_spsc_telemetry_values_shared_unlink(values_name);
}

int
main(void)
{
    sigset_t previous_mask;
    if (!rtsyn_engine_main_block_process_signals(&previous_mask)
        || !rtsyn_engine_main_install_signal_handlers())
    {
        return EXIT_FAILURE;
    }

    rtsyn_runtime_config_t runtime_config = {0};
    rtsyn_runtime_config_init(&runtime_config);

    rtsyn_runtime_t *runtime = rtsyn_runtime_create(&runtime_config);
    if (!runtime)
    {
        return EXIT_FAILURE;
    }

    const char *command_name = rtsyn_engine_main_env_or_default(
        RTSYN_SPSC_ENV_COMMAND_QUEUE, RTSYN_SPSC_DEFAULT_COMMAND_QUEUE);
    const char *result_name =
        rtsyn_engine_main_env_or_default(RTSYN_SPSC_ENV_RESULT_QUEUE,
                                         RTSYN_SPSC_DEFAULT_RESULT_QUEUE);
    const char *telemetry_name = rtsyn_engine_main_env_or_default(
        RTSYN_SPSC_ENV_TELEMETRY_QUEUE, RTSYN_SPSC_DEFAULT_TELEMETRY_QUEUE);
    const char *values_name = rtsyn_engine_main_env_or_default(
        RTSYN_SPSC_ENV_TELEMETRY_VALUES_QUEUE, RTSYN_SPSC_DEFAULT_TELEMETRY_VALUES_QUEUE);

    rtsyn_engine_main_unlink_shared_queues(command_name, result_name, telemetry_name,
                                           values_name);

    rtsyn_spsc_command_shared_t command_shared = {0};
    rtsyn_spsc_result_shared_t result_shared = {0};
    rtsyn_spsc_telemetry_shared_t telemetry_shared = {0};
    rtsyn_spsc_telemetry_values_shared_t values_shared = {0};
    if (rtsyn_spsc_command_shared_create(&command_shared, command_name) != 0
        || rtsyn_spsc_result_shared_create(&result_shared, result_name) != 0
        || rtsyn_spsc_telemetry_shared_create(&telemetry_shared, telemetry_name) != 0
        || rtsyn_spsc_telemetry_values_shared_create(&values_shared, values_name) != 0)
    {
        rtsyn_engine_main_close_shared_queues(&command_shared, &result_shared, &telemetry_shared,
                                              &values_shared);
        rtsyn_engine_main_unlink_shared_queues(command_name, result_name, telemetry_name,
                                               values_name);
        rtsyn_runtime_destroy(runtime);
        return EXIT_FAILURE;
    }

    rtsyn_engine_config_t engine_config = {0};
    rtsyn_engine_config_init(&engine_config);
    engine_config.runtime = runtime;
    engine_config.command_queue = command_shared.queue;
    engine_config.result_queue = result_shared.queue;
    engine_config.telemetry_queue = telemetry_shared.queue;
    engine_config.telemetry_values = values_shared.values;

    rtsyn_engine_t *engine = rtsyn_engine_create(&engine_config);
    if (!engine)
    {
        rtsyn_engine_main_close_shared_queues(&command_shared, &result_shared, &telemetry_shared,
                                              &values_shared);
        rtsyn_engine_main_unlink_shared_queues(command_name, result_name, telemetry_name,
                                               values_name);
        rtsyn_runtime_destroy(runtime);
        return EXIT_FAILURE;
    }

    if (!rtsyn_engine_start(engine))
    {
        rtsyn_engine_destroy(engine);
        rtsyn_engine_main_close_shared_queues(&command_shared, &result_shared, &telemetry_shared,
                                              &values_shared);
        rtsyn_engine_main_unlink_shared_queues(command_name, result_name, telemetry_name,
                                               values_name);
        rtsyn_runtime_destroy(runtime);
        return EXIT_FAILURE;
    }

    if (!rtsyn_engine_main_unblock_stop_signals())
    {
        rtsyn_engine_request_stop(engine);
    }

    while (!rtsyn_engine_main_stop_requested)
    {
        pause();
    }

    rtsyn_engine_request_stop(engine);
    const bool joined = rtsyn_engine_join(engine);

    (void)pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
    rtsyn_engine_destroy(engine);
    rtsyn_engine_main_close_shared_queues(&command_shared, &result_shared, &telemetry_shared,
                                          &values_shared);
    rtsyn_engine_main_unlink_shared_queues(command_name, result_name, telemetry_name, values_name);
    rtsyn_runtime_destroy(runtime);

    return joined ? EXIT_SUCCESS : EXIT_FAILURE;
}
