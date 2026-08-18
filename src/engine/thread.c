/**
 * @file engine/thread.c
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief RTSyn Engine thread entry points.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#include <stdatomic.h>

#include "rtsyn/internal/engine.h"
#include "rtsyn/internal/engine/events.h"
#include "rtsyn/thread.h"

static void
rtsyn_engine_drain_commands(rtsyn_engine_t *engine)
{
    rtsyn_spsc_command_message_t message = {0};
    for (uint32_t i = 0; i < engine->config.max_commands_per_cycle; i++)
    {
        if (!rtsyn_spsc_command_try_pop(engine->config.command_queue, &message))
        {
            return;
        }

        (void)rtsyn_engine_event_dispatch_command(engine, &message);
    }
}

void *
rtsyn_engine_rt_thread_main(void *arg)
{
    rtsyn_engine_t *engine = (rtsyn_engine_t *)arg;
    uint64_t next_deadline_ns = rtsyn_thread_get_time();

    while (!atomic_load_explicit(&engine->stop_requested, memory_order_acquire))
    {
        const uint64_t started_at_ns = rtsyn_thread_get_time();
        (void)rtsyn_engine_event_publish_cycle_begin(engine, started_at_ns);

        rtsyn_engine_drain_commands(engine);

        if (!atomic_load_explicit(&engine->paused, memory_order_acquire))
        {
            rtsyn_runtime_step(engine->config.runtime);
        }

        const uint64_t finished_at_ns = rtsyn_thread_get_time();
        const period_ns_t period_ns = rtsyn_runtime_get_config(engine->config.runtime).period_ns;
        (void)rtsyn_engine_event_publish_requested_values(engine, finished_at_ns);
        (void)rtsyn_engine_event_publish_cycle_end(engine, started_at_ns, finished_at_ns);
        (void)rtsyn_engine_event_publish_dropped(engine, finished_at_ns);

        uint64_t cycle_completed_at_ns = finished_at_ns;
        if (period_ns > 0)
        {
            next_deadline_ns += period_ns;
            rtsyn_thread_sleep_until(next_deadline_ns);
            cycle_completed_at_ns = rtsyn_thread_get_time();
        }

        rtsyn_measurement_metrics_t metrics = {0};
        if (rtsyn_measurement_tool_calculate_cycle(engine->measurement_tool, period_ns,
                                                  started_at_ns, cycle_completed_at_ns,
                                                  &metrics))
        {
            (void)rtsyn_measurement_tool_calculate_deadline(
                period_ns, cycle_completed_at_ns, next_deadline_ns,
                engine->config.deadline_tolerance_ns, &metrics);
            (void)rtsyn_engine_event_publish_measurement(engine, cycle_completed_at_ns, &metrics);
        }

        engine->cycle_id++;
    }

    return NULL;
}

void *
rtsyn_engine_wait_thread_main(void *arg)
{
    rtsyn_engine_t *engine = (rtsyn_engine_t *)arg;

    (void)rtsyn_thread_join(engine->rt_thread, NULL);
    atomic_store_explicit(&engine->rt_joined, true, memory_order_release);
    atomic_store_explicit(&engine->running, false, memory_order_release);

    return NULL;
}
