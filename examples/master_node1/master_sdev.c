#include <lely/co/sdev.h>

#define CO_SDEV_STRING(s)	NULL

const struct co_sdev master_sdev = {
	.id = 0x7f,
	.name = NULL,
	.vendor_name = NULL,
	.vendor_id = 0x00000000,
	.product_name = NULL,
	.product_code = 0x00000000,
	.revision = 0x00000000,
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
	.rate = 1000,
	.lss = 1,
	.dummy = 0x0f7d00fe,
	.nobj = 39,
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
		.name = CO_SDEV_STRING("Pre-defined error field"),
#endif
		.idx = 0x1003,
		.code = CO_OBJECT_ARRAY,
		.nsub = 9,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x08 },
#endif
			.val = { .u8 = 0x08 },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Pre-defined error field1"),
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
			.name = CO_SDEV_STRING("Pre-defined error field2"),
#endif
			.subidx = 0x02,
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
			.name = CO_SDEV_STRING("Pre-defined error field3"),
#endif
			.subidx = 0x03,
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
			.name = CO_SDEV_STRING("Pre-defined error field4"),
#endif
			.subidx = 0x04,
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
			.name = CO_SDEV_STRING("Pre-defined error field5"),
#endif
			.subidx = 0x05,
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
			.name = CO_SDEV_STRING("Pre-defined error field6"),
#endif
			.subidx = 0x06,
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
			.name = CO_SDEV_STRING("Pre-defined error field7"),
#endif
			.subidx = 0x07,
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
			.name = CO_SDEV_STRING("Pre-defined error field8"),
#endif
			.subidx = 0x08,
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
		.name = CO_SDEV_STRING("COB-ID SYNC message"),
#endif
		.idx = 0x1005,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID SYNC message"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x40000080lu },
#endif
			.val = { .u32 = 0x40000080lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Communication cycle period"),
#endif
		.idx = 0x1006,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Communication cycle period"),
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
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Synchronous window length"),
#endif
		.idx = 0x1007,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Synchronous window length"),
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
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("COB-ID time stamp object"),
#endif
		.idx = 0x1012,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID time stamp object"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000100lu },
#endif
			.val = { .u32 = 0x00000100lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("High resolution time stamp"),
#endif
		.idx = 0x1013,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("High resolution time stamp"),
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
		.name = CO_SDEV_STRING("COB-ID EMCY"),
#endif
		.idx = 0x1014,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID EMCY"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x000000fflu },
#endif
			.val = { .u32 = 0x000000fflu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
				| CO_OBJ_FLAGS_DEF_NODEID
				| CO_OBJ_FLAGS_VAL_NODEID
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Inhibit time EMCY"),
#endif
		.idx = 0x1015,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Inhibit time EMCY"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Consumer heartbeat time"),
#endif
		.idx = 0x1016,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Consumer heartbeat time1"),
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
			.val = { .u32 = 0x00010bb8lu },
			.access = CO_ACCESS_RW,
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
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Identity Object"),
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
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
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
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
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
			.def = { .u32 = CO_UNSIGNED32_MIN },
#endif
			.val = { .u32 = CO_UNSIGNED32_MIN },
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Synchronous counter overflow value"),
#endif
		.idx = 0x1019,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Synchronous counter overflow value"),
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
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Emergency consumer object"),
#endif
		.idx = 0x1028,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Emergency consumer object1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000081lu },
#endif
			.val = { .u32 = 0x00000081lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Error behavior object"),
#endif
		.idx = 0x1029,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Error behavior object1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("NMT inhibit time"),
#endif
		.idx = 0x102a,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NMT inhibit time"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("RPDO communication parameter"),
#endif
		.idx = 0x1400,
		.code = CO_OBJECT_RECORD,
		.nsub = 6,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("highest sub-index supported"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x05 },
#endif
			.val = { .u8 = 0x05 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID used by RPDO"),
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
			.name = CO_SDEV_STRING("transmission type"),
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
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("inhibit time"),
#endif
			.subidx = 0x03,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("compatibility entry"),
#endif
			.subidx = 0x04,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("event-timer"),
#endif
			.subidx = 0x05,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("RPDO mapping parameter"),
#endif
		.idx = 0x1600,
		.code = CO_OBJECT_RECORD,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("RPDO mapping parameter1"),
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
			.val = { .u32 = 0x20000120lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("TPDO communication parameter"),
#endif
		.idx = 0x1800,
		.code = CO_OBJECT_RECORD,
		.nsub = 6,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("highest sub-index supported"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = 0x06 },
#endif
			.val = { .u8 = 0x06 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("COB-ID used by TPDO"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000201lu },
#endif
			.val = { .u32 = 0x00000201lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("transmission type"),
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
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("inhibit time"),
#endif
			.subidx = 0x03,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("event timer"),
#endif
			.subidx = 0x05,
			.type = CO_DEFTYPE_UNSIGNED16,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u16 = CO_UNSIGNED16_MIN },
			.max = { .u16 = CO_UNSIGNED16_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u16 = CO_UNSIGNED16_MIN },
#endif
			.val = { .u16 = CO_UNSIGNED16_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("SYNC start value"),
#endif
			.subidx = 0x06,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("TPDO mapping parameter"),
#endif
		.idx = 0x1a00,
		.code = CO_OBJECT_RECORD,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("TPDO mapping parameter1"),
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
			.val = { .u32 = 0x22000120lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Configuration request"),
#endif
		.idx = 0x1f25,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Configuration request1"),
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
			.access = CO_ACCESS_WO,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Expected software identification"),
#endif
		.idx = 0x1f55,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Expected software identification1"),
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
			.def = { .u32 = 0x00000009lu },
#endif
			.val = { .u32 = 0x00000009lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("NMT slave assignment"),
#endif
		.idx = 0x1f81,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NMT slave assignment1"),
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
			.val = { .u32 = 0x00000005lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Request NMT"),
#endif
		.idx = 0x1f82,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Request NMT1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Device type identification"),
#endif
		.idx = 0x1f84,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Device type identification1"),
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
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Vendor identification"),
#endif
		.idx = 0x1f85,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Vendor identification1"),
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
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Product code"),
#endif
		.idx = 0x1f86,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Product code1"),
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
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Revision_number"),
#endif
		.idx = 0x1f87,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Revision_number1"),
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
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Serial number"),
#endif
		.idx = 0x1f88,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Serial number1"),
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
			.val = { .u32 = 0x00000001lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Boot time"),
#endif
		.idx = 0x1f89,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Boot time"),
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
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Restore configuration"),
#endif
		.idx = 0x1f8a,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Restore configuration1"),
#endif
			.subidx = 0x01,
			.type = CO_DEFTYPE_UNSIGNED8,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u8 = CO_UNSIGNED8_MIN },
			.max = { .u8 = CO_UNSIGNED8_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u8 = CO_UNSIGNED8_MIN },
#endif
			.val = { .u8 = CO_UNSIGNED8_MIN },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Mapped application objects for RPDO 1"),
#endif
		.idx = 0x2000,
		.code = CO_OBJECT_RECORD,
		.nsub = 2,
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
			.def = { .u8 = 0x01 },
#endif
			.val = { .u8 = 0x01 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("node1: TPDO test value"),
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
			.access = CO_ACCESS_RWW,
			.pdo_mapping = 1,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Mapped application objects for TPDO 1"),
#endif
		.idx = 0x2200,
		.code = CO_OBJECT_RECORD,
		.nsub = 2,
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
			.def = { .u8 = 0x01 },
#endif
			.val = { .u8 = 0x01 },
			.access = CO_ACCESS_CONST,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("node1: SDO RPDO test value"),
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
			.access = CO_ACCESS_RWR,
			.pdo_mapping = 1,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Remote TPDO number and node-ID"),
#endif
		.idx = 0x5800,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Remote TPDO number and node-ID"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000101lu },
#endif
			.val = { .u32 = 0x00000101lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Remote TPDO mapping parameter"),
#endif
		.idx = 0x5a00,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Remote TPDO mapping parameter1"),
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
			.val = { .u32 = 0x20010020lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Remote RPDO number and node-ID"),
#endif
		.idx = 0x5c00,
		.code = CO_OBJECT_VAR,
		.nsub = 1,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Remote RPDO number and node-ID"),
#endif
			.subidx = 0x00,
			.type = CO_DEFTYPE_UNSIGNED32,
#if !LELY_NO_CO_OBJ_LIMITS
			.min = { .u32 = CO_UNSIGNED32_MIN },
			.max = { .u32 = CO_UNSIGNED32_MAX },
#endif
#if !LELY_NO_CO_OBJ_DEFAULT
			.def = { .u32 = 0x00000101lu },
#endif
			.val = { .u32 = 0x00000101lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}, {
#if !LELY_NO_CO_OBJ_NAME
		.name = CO_SDEV_STRING("Remote RPDO mapping parameter"),
#endif
		.idx = 0x5e00,
		.code = CO_OBJECT_ARRAY,
		.nsub = 2,
		.subs = (const struct co_ssub[]){{
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("NrOfObjects"),
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
			.access = CO_ACCESS_RO,
			.pdo_mapping = 0,
			.flags = 0
		}, {
#if !LELY_NO_CO_OBJ_NAME
			.name = CO_SDEV_STRING("Remote RPDO mapping parameter1"),
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
			.val = { .u32 = 0x20000020lu },
			.access = CO_ACCESS_RW,
			.pdo_mapping = 0,
			.flags = 0
		}}
	}}
};

