// Compile-only coverage for RG_GUI_NO_STRING_IDS.

#define RG_SPRINTF_NO_ASM 1
#define RG_GUI_NO_STRING_IDS 1
#define RGINLINE static inline
#include "../src/rg_gui.h"

#ifndef rg_gui_id_str
#error RG_GUI_NO_STRING_IDS must reject rg_gui_id_str at compile time
#endif
#ifndef rg_gui_push_id_str
#error RG_GUI_NO_STRING_IDS must reject rg_gui_push_id_str at compile time
#endif

static RgGuiId rg_gui_no_string_ids_smoke(const void* pointer)
{
	return rg_gui_id_combine(rg_gui_id_ptr(pointer), rg_gui_id_u64(7u));
}

#if defined(RG_GUI_TEST_EXPECT_STRING_ID_FAILURE)
static RgGuiId rg_gui_disabled_string_id_must_not_compile(void)
{
	return rg_gui_id_str("disabled");
}
#endif

int main(void)
{
	int marker = 0;
	(void)rg_gui_no_string_ids_smoke(&marker);
	return 0;
}
