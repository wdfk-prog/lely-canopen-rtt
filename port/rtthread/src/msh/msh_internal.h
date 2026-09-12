/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_internal.h
 * @brief Private interfaces shared by the RT-Thread CANopen MSH modules.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_MSH_INTERNAL_H_
#define LELY_RTT_MSH_INTERNAL_H_

#include <lely/rtthread/runtime.h>

#include <rtthread.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
struct lely_rtt_msh_scalar_type {
    const char *name;
    rt_uint8_t size;
    rt_bool_t is_signed;
    rt_bool_t is_boolean;
};
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */

rt_bool_t lely_rtt_msh_parse_u32(const char *text, rt_uint32_t max_value,
        rt_uint32_t *value);
rt_bool_t lely_rtt_msh_parse_node(const char *text, rt_uint8_t *node_id);
lely_rtt_runtime_t *lely_rtt_msh_runtime(void);
void lely_rtt_msh_help(void);

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
const struct lely_rtt_msh_scalar_type *lely_rtt_msh_find_scalar_type(
        const char *name);
rt_bool_t lely_rtt_msh_encode_scalar(
        const struct lely_rtt_msh_scalar_type *type, const char *text,
        rt_uint8_t data[4]);
void lely_rtt_msh_print_scalar(const struct lely_rtt_msh_scalar_type *type,
        const void *data, rt_size_t size);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */

void lely_rtt_msh_status(void);
void lely_rtt_msh_node(const char *node_text);
void lely_rtt_msh_boot(const char *node_text);
#if defined(PKG_LELY_USING_MASTER_COMMAND)
void lely_rtt_msh_nmt(const char *command_text, const char *target_text);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
void lely_rtt_msh_cfg(const char *node_text, const char *timeout_text);
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
void lely_rtt_msh_od(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
void lely_rtt_msh_tpdo(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
void lely_rtt_msh_sync(int argc, char **argv);
void lely_rtt_msh_pdo(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
void lely_rtt_msh_emcy(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
void lely_rtt_msh_time(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
#if defined(PKG_LELY_USING_MASTER_SDO)
void lely_rtt_msh_sdo(int argc, char **argv);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

#endif /* defined(PKG_LELY_USING_MSH) */

#endif /* LELY_RTT_MSH_INTERNAL_H_ */
