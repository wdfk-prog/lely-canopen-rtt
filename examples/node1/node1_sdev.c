#include <lely/co/sdev.h>

#define CO_SDEV_STRING(s)	s

const struct co_sdev node1_sdev = {
	.id = 0x01,
	.name = CO_SDEV_STRING("node1"),
	.vendor_name = CO_SDEV_STRING("Lely RT-Thread Example"),
	.vendor_id = 0x00000000,
	.product_name = CO_SDEV_STRING("Node1 smoke slave"),
	.product_code = 0x00000001,
	.revision = 0x00000001,
	.order_code = NULL,
	.baud = 0
		| CO_BAUD_1000
		| CO_BAUD_800
		| CO_BAUD_500
		| CO_BAUD_250
		| CO_BAUD_125
		| CO_BAUD_50
		| CO_BAUD_20
		| CO_BAUD_10,
	.rate = 0,
	.lss = 0,
	.dummy = 0x00000000,
	.nobj = 10,
	.objs = (const struct co_sobj[]){{
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Device type"),
#endif
		.idx = 0x1000,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Device type"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Error register"),
#endif
		.idx = 0x1001,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Error register"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Producer heartbeat time"),
#endif
		.idx = 0x1017,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Producer heartbeat time"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = 0x03e8u },
#endif
			.val = { .u16 = 0x03e8u },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
				| CO_OBJ_FLAGS_PARAMETER_VALUE
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Identity object"),
#endif
		.idx = 0x1018,
		.code = CO_OBJECT_RECORD,
		.nsub = 5,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Highest sub-index supported"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x04 },
#endif
			.val = { .u8 = 0x04 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Vendor-ID"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Product code"),
#endif
			.subidx = 0x02,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000001lu },
#endif
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Revision number"),
#endif
			.subidx = 0x03,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000001lu },
#endif
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Serial number"),
#endif
			.subidx = 0x04,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000001lu },
#endif
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Server SDO 1 parameter"),
#endif
		.idx = 0x1200,
		.code = CO_OBJECT_RECORD,
		.nsub = 3,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Highest sub-index supported"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x02 },
#endif
			.val = { .u8 = 0x02 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID client to server"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000601lu },
#endif
			.val = { .u32 = 0x00000601lu },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID server to client"),
#endif
			.subidx = 0x02,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000581lu },
#endif
			.val = { .u32 = 0x00000581lu },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Transmit PDO 1 communication parameter"),
#endif
		.idx = 0x1800,
		.code = CO_OBJECT_RECORD,
		.nsub = 3,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Highest sub-index supported"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x02 },
#endif
			.val = { .u8 = 0x02 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID used by TPDO 1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000181lu },
#endif
			.val = { .u32 = 0x00000181lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Transmission type"),
#endif
			.subidx = 0x02,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MAX },
#endif
			.val = { .u8 = CO_UNSIGNED8_MAX },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Transmit PDO 1 mapping parameter"),
#endif
		.idx = 0x1a00,
		.code = CO_OBJECT_RECORD,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Number of mapped application objects"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x01 },
#endif
			.val = { .u8 = 0x01 },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Mapped object 1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x20010020lu },
#endif
			.val = { .u32 = 0x20010020lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("NMT startup"),
#endif
		.idx = 0x1f80,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NMT startup"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000004lu },
#endif
			.val = { .u32 = 0x00000004lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("SDO RPDO test value"),
#endif
		.idx = 0x2000,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("SDO RPDO test value"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 1,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("TPDO test value"),
#endif
		.idx = 0x2001,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("TPDO test value"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 1,
			.flags = 0
		}}
	}}
};

