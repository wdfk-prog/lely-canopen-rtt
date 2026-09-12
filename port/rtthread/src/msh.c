/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-11     wdfk-prog         reduce MSH translation unit to command aggregation
 */

/**
 * @file msh.c
 * @brief RT-Thread MSH command aggregation for the default CANopen Master runtime.
 *
 * Feature-specific parsing and execution live in port/rtthread/src/msh/.
 * This file owns only the top-level help surface, command routing and export.
 *
 * @author wdfk-prog
 */

#include "msh/msh_internal.h"

#include <finsh.h>

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

void
lely_rtt_msh_help(void)
{
    rt_kprintf("co status\n");
    rt_kprintf("co node <node-id>\n");
    rt_kprintf("co boot <node-id>\n");
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    rt_kprintf("co nmt start|stop|preop|reset-node|reset-comm <node-id|all>\n");
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    rt_kprintf("co cfg <node-id> <timeout-ms>\n");
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
    rt_kprintf("co od status\n");
    rt_kprintf("co od read <index> <subindex> <type>\n");
    rt_kprintf("co od write <index> <subindex> <type> <value>\n");
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
    rt_kprintf("co tpdo event <pdo-number>\n");
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
    rt_kprintf("co sync status\n");
    rt_kprintf("co sync period <microseconds>\n");
    rt_kprintf("co pdo trans rx|tx <pdo-number> [0..240|254|255]\n");
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
    rt_kprintf("co emcy [node-id]\n");
    rt_kprintf("co emcy push <eec> <error-register> [msef-10hex]\n");
    rt_kprintf("co emcy pop|clear\n");
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
    rt_kprintf("co time status\n");
    rt_kprintf("co time mode off|consumer|producer|both\n");
    rt_kprintf("co time send <unix-sec> <nanoseconds>\n");
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
#if defined(PKG_LELY_USING_MASTER_SDO)
    rt_kprintf("co sdo read <node> <index> <subindex> <type> <timeout-ms>\n");
    rt_kprintf("co sdo write <node> <index> <subindex> <type> <value> <timeout-ms>\n");
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
    rt_kprintf("  type: bool|u8|u16|u32|i8|i16|i32\n");
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */
}


static int
co(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "help"))) {
        lely_rtt_msh_help();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "status")) {
        lely_rtt_msh_status();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "node")) {
        lely_rtt_msh_node(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "boot")) {
        lely_rtt_msh_boot(argv[2]);
        return 0;
    }
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    if (argc == 4 && !strcmp(argv[1], "nmt")) {
        lely_rtt_msh_nmt(argv[2], argv[3]);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    if (argc == 4 && !strcmp(argv[1], "cfg")) {
        lely_rtt_msh_cfg(argv[2], argv[3]);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
    if (argc >= 2 && !strcmp(argv[1], "od")) {
        lely_rtt_msh_od(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
    if (argc >= 2 && !strcmp(argv[1], "tpdo")) {
        lely_rtt_msh_tpdo(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
    if (argc >= 2 && !strcmp(argv[1], "sync")) {
        lely_rtt_msh_sync(argc, argv);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "pdo")) {
        lely_rtt_msh_pdo(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
    if (argc >= 2 && !strcmp(argv[1], "emcy")) {
        lely_rtt_msh_emcy(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
    if (argc >= 2 && !strcmp(argv[1], "time")) {
        lely_rtt_msh_time(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
#if defined(PKG_LELY_USING_MASTER_SDO)
    if (argc >= 2 && !strcmp(argv[1], "sdo")) {
        lely_rtt_msh_sdo(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    rt_kprintf("co: invalid command\n");
    lely_rtt_msh_help();
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(co, CANopen Master controller and diagnostics);


#endif /* defined(PKG_LELY_USING_MSH) */
