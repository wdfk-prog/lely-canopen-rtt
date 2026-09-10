# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
RUNTIME_C = REPO_ROOT / "port" / "rtthread" / "src" / "runtime.c"
TIMER_C = REPO_ROOT / "port" / "rtthread" / "src" / "timer.c"
MASTER_SDO_C = REPO_ROOT / "port" / "rtthread" / "src" / "master_sdo.c"


def find_host_compiler():
    override = os.environ.get("HOST_CC")
    if override:
        return shutil.which(override)
    for candidate in ("cc", "gcc", "clang"):
        compiler = shutil.which(candidate)
        if compiler:
            return compiler
    return None


def function_definition(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if not match:
        raise AssertionError(f"function {name} not found")

    name_line = source.rfind("\n", 0, match.start()) + 1
    previous_end = name_line - 1
    previous_start = source.rfind("\n", 0, previous_end) + 1
    previous = source[previous_start:previous_end].strip()
    if previous in ("static void", "void", "static rt_err_t", "rt_err_t", "static rt_atomic_t"):
        start = previous_start
    else:
        start = name_line

    brace = source.find("{", match.start())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"function {name} has no closing brace")


class RuntimeRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_host_compiler()
        if not cls.compiler:
            raise unittest.SkipTest("host C compiler not found; set HOST_CC to a native compiler executable")

    def compile_and_run(self, source, name):
        with tempfile.TemporaryDirectory(prefix=f"lely-{name}-") as temp_dir:
            temp = Path(temp_dir)
            c_file = temp / f"{name}.c"
            binary = temp / name
            c_file.write_text(source, encoding="utf-8")
            subprocess.run(
                [
                    self.compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(c_file),
                    "-o",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
            completed = subprocess.run(
                [str(binary)],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                capture_output=True,
            )
            if completed.returncode != 0:
                self.fail(
                    f"{name} exited with {completed.returncode}: "
                    f"{completed.stderr.strip()}"
                )
            return completed.stdout

    def test_nested_boot_completion_preserves_final_sdo_gate_and_snapshot(self):
        runtime_source = RUNTIME_C.read_text(encoding="utf-8")
        sdo_source = MASTER_SDO_C.read_text(encoding="utf-8")
        definitions = "\n\n".join(
            (
                function_definition(runtime_source, "lely_rtt_remote_state_pack"),
                function_definition(sdo_source, "lely_rtt_master_sdo_set_suspended"),
                function_definition(sdo_source, "lely_rtt_master_sdo_before_boot"),
                function_definition(sdo_source, "lely_rtt_master_sdo_on_nmt_state"),
                function_definition(sdo_source, "lely_rtt_master_sdo_on_boot_complete"),
            )
        )
        state_ind = function_definition(runtime_source, "lely_rtt_master_state_ind")
        boot_ind = function_definition(runtime_source, "lely_rtt_master_boot_ind")

        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>

            #define PKG_LELY_USING_MASTER_SDO 1
            #define LELY_NO_CO_NMT_BOOT 0
            #define CO_NUM_NODES 127u
            #define CO_NMT_ST_BOOTUP 0x00u
            #define CO_NMT_ST_STOP 0x04u
            #define CO_NMT_ST_START 0x05u
            #define CO_NMT_ST_PREOP 0x7fu
            #define CO_NMT_ST_TOGGLE 0x80u
            #define RT_TRUE 1
            #define RT_FALSE 0
            #define RT_NULL NULL
            #define LELY_RTT_REMOTE_STATE_LAST_SHIFT 8u
            #define LELY_RTT_REMOTE_STATE_TIMEOUT 0x00010000u
            #define LELY_RTT_REMOTE_STATE_CURRENT_MASK 0x000000ffu
            #define LELY_RTT_REMOTE_BOOT_VALID 0x00010000u
            #define LELY_RTT_REMOTE_BOOT_ERROR_SHIFT 8u

            typedef int rt_bool_t;
            typedef int32_t rt_atomic_t;
            typedef uint8_t rt_uint8_t;
            typedef uint16_t rt_uint16_t;
            typedef uint32_t rt_uint32_t;
            typedef uint8_t co_unsigned8_t;
            typedef void co_csdo_t;
            typedef struct fake_nmt co_nmt_t;

            struct lely_rtt_runtime {
                co_nmt_t *master_nmt;
                co_csdo_t *sdo_clients[CO_NUM_NODES + 1u];
                rt_bool_t sdo_suspended[CO_NUM_NODES + 1u];
                rt_bool_t sdo_reset_pending[CO_NUM_NODES + 1u];
                rt_atomic_t local_nmt_state;
                rt_atomic_t remote_nmt_state[CO_NUM_NODES + 1u];
                rt_atomic_t remote_boot_result[CO_NUM_NODES + 1u];
            };

            struct fake_nmt {
                rt_uint8_t local_id;
                int booting;
                int nested_complete;
                rt_uint8_t nested_state;
                char nested_error;
                struct lely_rtt_runtime *runtime;
            };

            static rt_atomic_t rt_atomic_load(const rt_atomic_t *value)
            {
                return *value;
            }

            static void rt_atomic_store(rt_atomic_t *target, rt_atomic_t value)
            {
                *target = value;
            }

            static rt_uint8_t co_nmt_get_id(const co_nmt_t *nmt)
            {
                return nmt->local_id;
            }

            static int co_nmt_is_booting(const co_nmt_t *nmt, co_unsigned8_t id)
            {
                (void)id;
                return nmt->booting;
            }

            static void lely_rtt_master_sdo_cancel_node(
                    struct lely_rtt_runtime *runtime, rt_uint8_t node_id)
            {
                (void)runtime;
                (void)node_id;
            }

            static void co_csdo_destroy(co_csdo_t *sdo)
            {
                (void)sdo;
            }

            static void lely_rtt_master_boot_ind(co_nmt_t *nmt,
                    co_unsigned8_t id, co_unsigned8_t st, char es, void *data);
            """
        )
        harness += "\n" + definitions + "\n"
        harness += textwrap.dedent(
            r"""
            static void co_nmt_on_st(co_nmt_t *nmt, co_unsigned8_t id,
                    co_unsigned8_t st)
            {
                if (st == CO_NMT_ST_BOOTUP && nmt->nested_complete) {
                    nmt->booting = 0;
                    lely_rtt_master_boot_ind(nmt, id, nmt->nested_state,
                            nmt->nested_error, nmt->runtime);
                }
            }
            """
        )
        harness += "\n" + state_ind + "\n\n" + boot_ind + "\n"
        harness += textwrap.dedent(
            r"""
            static void check(int condition, const char *message)
            {
                if (!condition) {
                    fprintf(stderr, "FAIL: %s\n", message);
                    exit(1);
                }
            }

            static void reset_fixture(struct lely_rtt_runtime *runtime,
                    struct fake_nmt *nmt)
            {
                memset(runtime, 0, sizeof(*runtime));
                memset(nmt, 0, sizeof(*nmt));
                nmt->local_id = 1u;
                nmt->runtime = runtime;
                runtime->master_nmt = nmt;
            }

            int main(void)
            {
                struct lely_rtt_runtime runtime;
                struct fake_nmt nmt;
                rt_uint32_t boot_result;

                reset_fixture(&runtime, &nmt);
                nmt.nested_complete = 1;
                nmt.nested_state = CO_NMT_ST_STOP;
                lely_rtt_master_state_ind(&nmt, 2u, CO_NMT_ST_BOOTUP, &runtime);
                boot_result = (rt_uint32_t)rt_atomic_load(&runtime.remote_boot_result[2]);
                check(runtime.sdo_suspended[2] == RT_TRUE,
                        "failed nested boot must keep SDO suspended");
                check(runtime.sdo_reset_pending[2] == RT_FALSE,
                        "completed nested boot must clear reset pending");
                check(((rt_uint32_t)rt_atomic_load(&runtime.remote_nmt_state[2])
                                & LELY_RTT_REMOTE_STATE_CURRENT_MASK) == CO_NMT_ST_STOP,
                        "failed nested boot final state must remain published");
                check((boot_result & LELY_RTT_REMOTE_BOOT_VALID) != 0,
                        "nested boot result must remain valid");

                reset_fixture(&runtime, &nmt);
                nmt.nested_complete = 1;
                nmt.nested_state = CO_NMT_ST_PREOP;
                lely_rtt_master_state_ind(&nmt, 2u, CO_NMT_ST_BOOTUP, &runtime);
                check(runtime.sdo_suspended[2] == RT_FALSE,
                        "successful nested boot must reopen SDO");
                check(((rt_uint32_t)rt_atomic_load(&runtime.remote_nmt_state[2])
                                & LELY_RTT_REMOTE_STATE_CURRENT_MASK) == CO_NMT_ST_PREOP,
                        "successful nested boot final state must remain published");

                reset_fixture(&runtime, &nmt);
                nmt.booting = 1;
                lely_rtt_master_state_ind(&nmt, 2u, CO_NMT_ST_BOOTUP, &runtime);
                check(runtime.sdo_suspended[2] == RT_TRUE,
                        "in-progress asynchronous boot must suspend SDO");
                check(runtime.sdo_reset_pending[2] == RT_TRUE,
                        "in-progress asynchronous boot must keep reset pending");
                check(((rt_uint32_t)rt_atomic_load(&runtime.remote_nmt_state[2])
                                & LELY_RTT_REMOTE_STATE_CURRENT_MASK) == CO_NMT_ST_BOOTUP,
                        "asynchronous boot must publish outer BOOTUP");
                check(((rt_uint32_t)rt_atomic_load(&runtime.remote_boot_result[2])
                                & LELY_RTT_REMOTE_BOOT_VALID) == 0,
                        "asynchronous boot must not publish completion early");

                puts("PASS nested-boot-behavior");
                return 0;
            }
            """
        )
        output = self.compile_and_run(harness, "runtime_nested_boot")
        self.assertIn("PASS nested-boot-behavior", output)

    def test_master_init_sync_service_is_checked_after_reset(self):
        runtime_source = RUNTIME_C.read_text(encoding="utf-8")
        master_init = function_definition(runtime_source, "lely_rtt_master_init")
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>

            #define PKG_LELY_USING_MASTER_SYNC_PDO 1
            #define LELY_NO_CO_NMT_BOOT 1
            #define RT_EOK 0
            #define RT_ERROR 1
            #define CO_NMT_CS_RESET_NODE 0x81u
            #define CO_NMT_ST_PREOP 0x7fu
            #define CO_NMT_ST_TOGGLE 0x80u
            #define LELY_RTT_LOG_E(...) ((void)0)
            #define LELY_RTT_LOG_I(...) ((void)0)

            typedef int rt_err_t;
            typedef int32_t rt_atomic_t;
            typedef uint8_t rt_uint8_t;
            typedef struct fake_dev co_dev_t;
            typedef struct fake_nmt co_nmt_t;

            struct co_sdev { int unused; };
            struct fake_dev { int unused; };
            struct fake_nmt {
                int sync_bound;
                void *sync_service;
                rt_uint8_t id;
                rt_uint8_t state;
            };
            struct lely_rtt_runtime {
                const struct co_sdev *master_sdev;
                co_dev_t *master_dev;
                co_nmt_t *master_nmt;
                void *can_net;
                rt_atomic_t local_node_id;
                rt_atomic_t local_nmt_state;
            };

            static struct fake_dev fake_dev;
            static struct fake_nmt fake_nmt;
            static int fake_sync;
            static int create_sync_on_reset;
            static int reset_fails;
            static int bind_calls;
            static int reset_calls;
            static int get_sync_calls;
            static int order_violation;
            static int step;
            static int bind_step;
            static int reset_step;
            static int get_sync_step;

            static void rt_atomic_store(rt_atomic_t *target, rt_atomic_t value)
            {
                *target = value;
            }

            static void lely_rtt_master_snapshots_reset(
                    struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
            }

            static co_dev_t *co_dev_create_from_sdev(const struct co_sdev *sdev)
            {
                return sdev ? &fake_dev : NULL;
            }

            static void *io_can_net_get_net(void *can_net)
            {
                return can_net;
            }

            static co_nmt_t *co_nmt_create(void *net, co_dev_t *dev)
            {
                (void)net;
                (void)dev;
                return &fake_nmt;
            }

            static void lely_rtt_master_state_ind(void) {}
            static void lely_rtt_master_hb_ind(void) {}

            static void co_nmt_set_st_ind(co_nmt_t *nmt, void (*ind)(void), void *data)
            {
                (void)nmt;
                (void)ind;
                (void)data;
            }

            static void co_nmt_set_hb_ind(co_nmt_t *nmt, void (*ind)(void), void *data)
            {
                (void)nmt;
                (void)ind;
                (void)data;
            }

            static rt_err_t lely_rtt_master_sync_bind(struct lely_rtt_runtime *runtime)
            {
                bind_calls++;
                bind_step = ++step;
                runtime->master_nmt->sync_bound = 1;
                return RT_EOK;
            }

            static int co_nmt_cs_ind(co_nmt_t *nmt, rt_uint8_t cs)
            {
                (void)cs;
                reset_calls++;
                reset_step = ++step;
                if (!nmt->sync_bound)
                    order_violation = 1;
                if (reset_fails)
                    return -1;
                if (create_sync_on_reset)
                    nmt->sync_service = &fake_sync;
                return 0;
            }

            static int co_nmt_is_master(const co_nmt_t *nmt)
            {
                (void)nmt;
                return 1;
            }

            static void *co_nmt_get_sync(const co_nmt_t *nmt)
            {
                get_sync_calls++;
                get_sync_step = ++step;
                if (!reset_calls)
                    order_violation = 1;
                return nmt->sync_service;
            }

            static rt_uint8_t co_nmt_get_id(const co_nmt_t *nmt)
            {
                return nmt->id;
            }

            static rt_uint8_t co_nmt_get_st(const co_nmt_t *nmt)
            {
                return nmt->state;
            }
            """
        )
        harness += "\n" + master_init + "\n"
        harness += textwrap.dedent(
            r"""
            static void check(int condition, const char *message)
            {
                if (!condition) {
                    fprintf(stderr, "FAIL: %s\n", message);
                    exit(1);
                }
            }

            static void reset_fixture(struct lely_rtt_runtime *runtime,
                    struct co_sdev *sdev)
            {
                memset(runtime, 0, sizeof(*runtime));
                memset(&fake_nmt, 0, sizeof(fake_nmt));
                fake_nmt.id = 1u;
                fake_nmt.state = CO_NMT_ST_PREOP;
                runtime->master_sdev = sdev;
                runtime->can_net = &fake_sync;
                create_sync_on_reset = 0;
                reset_fails = 0;
                bind_calls = 0;
                reset_calls = 0;
                get_sync_calls = 0;
                order_violation = 0;
                step = 0;
                bind_step = 0;
                reset_step = 0;
                get_sync_step = 0;
            }

            int main(void)
            {
                struct lely_rtt_runtime runtime;
                struct co_sdev sdev;

                reset_fixture(&runtime, &sdev);
                create_sync_on_reset = 1;
                check(lely_rtt_master_init(&runtime) == RT_EOK,
                        "SYNC created during reset must allow startup");
                check(bind_calls == 1 && reset_calls == 1 && get_sync_calls == 1,
                        "normal startup must bind, reset, then verify SYNC");
                check(bind_step < reset_step && reset_step < get_sync_step,
                        "SYNC readiness must be checked after reset");
                check(order_violation == 0,
                        "callback ownership must exist before reset");

                reset_fixture(&runtime, &sdev);
                create_sync_on_reset = 0;
                check(lely_rtt_master_init(&runtime) == -RT_ERROR,
                        "missing post-reset SYNC service must fail closed");
                check(get_sync_calls == 1 && reset_calls == 1,
                        "missing service must be detected after reset");

                reset_fixture(&runtime, &sdev);
                reset_fails = 1;
                check(lely_rtt_master_init(&runtime) == -RT_ERROR,
                        "reset failure must fail startup");
                check(get_sync_calls == 0,
                        "SYNC service must not be checked after reset itself failed");

                puts("PASS master-init-sync-order");
                return 0;
            }
            """
        )
        output = self.compile_and_run(harness, "runtime_master_init_sync")
        self.assertIn("PASS master-init-sync-order", output)

    def test_owner_orders_rx_before_timeouts_and_timer_failures_stop_runtime(self):
        runtime_source = RUNTIME_C.read_text(encoding="utf-8")
        timer_source = TIMER_C.read_text(encoding="utf-8")
        owner_entry = function_definition(runtime_source, "lely_rtt_owner_entry")
        timer_sync = function_definition(timer_source, "lely_rtt_timer_sync_can_net")
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>

            #define PKG_LELY_USING_MASTER_COMMAND 1
            #define RT_EOK 0
            #define RT_ERROR 1
            #define RT_ETIMEOUT 110
            #define RT_TRUE 1
            #define RT_FALSE 0
            #define RT_EVENT_FLAG_OR 0x01u
            #define RT_EVENT_FLAG_CLEAR 0x02u
            #define LELY_RTT_EVENT_STOP (1u << 0)
            #define LELY_RTT_EVENT_RX_READY (1u << 1)
            #define LELY_RTT_EVENT_CAN_STATUS (1u << 2)
            #define LELY_RTT_EVENT_TIMER_DUE (1u << 3)
            #define LELY_RTT_EVENT_COMMAND (1u << 4)
            #define LELY_RTT_EVENT_READY (1u << 5)
            #define LELY_RTT_EVENT_EXIT (1u << 6)
            #define LELY_RTT_EVENT_OWNER_MASK (LELY_RTT_EVENT_STOP | LELY_RTT_EVENT_RX_READY \
                    | LELY_RTT_EVENT_CAN_STATUS | LELY_RTT_EVENT_TIMER_DUE | LELY_RTT_EVENT_COMMAND)
            #define LELY_RTT_COMMAND_SAFETY_POLL_MS 100u
            #define LELY_RTT_LOG_E(...) ((void)0)
            #define LELY_RTT_LOG_I(...) ((void)0)

            typedef int rt_err_t;
            typedef int32_t rt_int32_t;
            typedef uint32_t rt_uint32_t;
            struct rt_event { int unused; };
            struct lely_rtt_runtime_config { int use_status_indication; };
            struct lely_rtt_runtime {
                rt_err_t init_result;
                rt_err_t runtime_error;
                int running;
                struct rt_event event;
                struct lely_rtt_runtime_config config;
                void *can_net;
                int event_initialized;
            };

            static char trace_log[128];
            static size_t trace_len;
            static int event_recv_calls;
            static int rx_work_queued;
            static int response_processed;
            static int timeout_overtook_response;
            static int set_time_seen;
            static int command_used_stale_time;
            static int lock_fails;
            static int set_time_fails;
            static int unlock_fails;
            static int stop_events;
            static int lock_calls;
            static int set_time_calls;
            static int unlock_calls;

            static void trace(char marker)
            {
                trace_log[trace_len++] = marker;
                trace_log[trace_len] = '\0';
            }

            static rt_int32_t lely_rtt_timeout_ticks(unsigned int milliseconds)
            {
                (void)milliseconds;
                return 1;
            }

            static rt_err_t lely_rtt_owner_init(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('I');
                return RT_EOK;
            }

            static void lely_rtt_owner_cleanup(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('X');
            }

            static rt_err_t rt_event_send(struct rt_event *event, rt_uint32_t set)
            {
                (void)event;
                if (set & LELY_RTT_EVENT_STOP)
                    stop_events++;
                return RT_EOK;
            }

            static rt_err_t rt_event_recv(struct rt_event *event, rt_uint32_t set,
                    rt_uint32_t option, rt_int32_t timeout, rt_uint32_t *received)
            {
                (void)event;
                (void)set;
                (void)option;
                (void)timeout;
                event_recv_calls++;
                if (event_recv_calls == 1)
                    *received = LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_COMMAND;
                else
                    *received = LELY_RTT_EVENT_STOP;
                return RT_EOK;
            }

            static void lely_rtt_latch_error(struct lely_rtt_runtime *runtime, rt_err_t err)
            {
                if (runtime->runtime_error == RT_EOK)
                    runtime->runtime_error = err;
            }

            static void lely_rtt_master_command_admission_open(
                    struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('O');
            }

            static void lely_rtt_can_drain_rx(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('R');
                rx_work_queued = 1;
            }

            static void lely_rtt_can_process_status(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('S');
            }

            static void lely_rtt_timer_advance(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('A');
            }

            static void lely_rtt_drain_loop(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('D');
                if (rx_work_queued && !response_processed) {
                    response_processed = 1;
                    trace('P');
                }
            }

            static int io_can_net_lock(void *can_net)
            {
                (void)can_net;
                lock_calls++;
                trace('L');
                return lock_fails ? -1 : 0;
            }

            static int io_can_net_set_time(void *can_net)
            {
                (void)can_net;
                set_time_calls++;
                trace('T');
                if (!response_processed)
                    timeout_overtook_response = 1;
                set_time_seen = 1;
                return set_time_fails ? -1 : 0;
            }

            static int io_can_net_unlock(void *can_net)
            {
                (void)can_net;
                unlock_calls++;
                trace('U');
                return unlock_fails ? -1 : 0;
            }

            static void lely_rtt_master_command_dispatch(struct lely_rtt_runtime *runtime)
            {
                (void)runtime;
                trace('C');
                if (!set_time_seen)
                    command_used_stale_time = 1;
            }
            """
        )
        harness += "\n" + timer_sync + "\n\n" + owner_entry + "\n"
        harness += textwrap.dedent(
            r"""
            static void check(int condition, const char *message)
            {
                if (!condition) {
                    fprintf(stderr, "FAIL: %s trace=%s\n", message, trace_log);
                    exit(1);
                }
            }

            static void reset_observers(void)
            {
                memset(trace_log, 0, sizeof(trace_log));
                trace_len = 0;
                event_recv_calls = 0;
                rx_work_queued = 0;
                response_processed = 0;
                timeout_overtook_response = 0;
                set_time_seen = 0;
                command_used_stale_time = 0;
                lock_fails = 0;
                set_time_fails = 0;
                unlock_fails = 0;
                stop_events = 0;
                lock_calls = 0;
                set_time_calls = 0;
                unlock_calls = 0;
            }

            static void reset_timer_runtime(struct lely_rtt_runtime *runtime)
            {
                memset(runtime, 0, sizeof(*runtime));
                runtime->can_net = runtime;
                runtime->event_initialized = 1;
                runtime->runtime_error = RT_EOK;
            }

            int main(void)
            {
                struct lely_rtt_runtime runtime;
                const char *r;
                const char *d;
                const char *t;
                const char *c;

                reset_observers();
                reset_timer_runtime(&runtime);
                runtime.config.use_status_indication = 1;
                lely_rtt_owner_entry(&runtime);
                r = strchr(trace_log, 'R');
                d = strchr(trace_log, 'D');
                t = strchr(trace_log, 'T');
                c = strchr(trace_log, 'C');
                check(r && d && t && c && r < d && d < t && t < c,
                        "owner must process queued RX before CAN time and commands");
                check(timeout_overtook_response == 0,
                        "CAN timeout processing must not overtake queued response work");
                check(command_used_stale_time == 0,
                        "command dispatch must observe refreshed CAN protocol time");
                check(runtime.runtime_error == RT_EOK,
                        "normal owner iteration must not latch runtime error");

                reset_observers();
                reset_timer_runtime(&runtime);
                lock_fails = 1;
                lely_rtt_timer_sync_can_net(&runtime);
                check(runtime.runtime_error == -RT_ERROR && stop_events == 1,
                        "lock failure must latch error and request STOP");
                check(lock_calls == 1 && set_time_calls == 0 && unlock_calls == 0,
                        "lock failure must not access protected CAN time path");

                reset_observers();
                reset_timer_runtime(&runtime);
                response_processed = 1;
                set_time_fails = 1;
                lely_rtt_timer_sync_can_net(&runtime);
                check(runtime.runtime_error == -RT_ERROR && stop_events == 1,
                        "set-time failure must latch error and request STOP");
                check(lock_calls == 1 && set_time_calls == 1 && unlock_calls == 1,
                        "set-time failure must still unlock CAN network");

                reset_observers();
                reset_timer_runtime(&runtime);
                response_processed = 1;
                unlock_fails = 1;
                lely_rtt_timer_sync_can_net(&runtime);
                check(runtime.runtime_error == -RT_ERROR && stop_events == 1,
                        "unlock failure must latch error and request STOP");

                reset_observers();
                reset_timer_runtime(&runtime);
                response_processed = 1;
                runtime.runtime_error = -77;
                set_time_fails = 1;
                lely_rtt_timer_sync_can_net(&runtime);
                check(runtime.runtime_error == -77 && stop_events == 1,
                        "time sync must preserve the first latched runtime error");

                puts("PASS owner-time-order-and-failures");
                return 0;
            }
            """
        )
        output = self.compile_and_run(harness, "runtime_owner_time")
        self.assertIn("PASS owner-time-order-and-failures", output)


if __name__ == "__main__":
    unittest.main()
