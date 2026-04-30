	.set noreorder

#include "pspstub.s"

	STUB_START "psplink",0x40090000,0x000C0005
	STUB_FUNC  0x001,get_kernel_delay_thread_function
	STUB_END
