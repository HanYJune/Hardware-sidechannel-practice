#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xf8d7ac5e, "proc_create" },
	{ 0xd272d446, "__SCT__preempt_schedule" },
	{ 0x5d3be6f8, "pcpu_hot" },
	{ 0xd272d446, "__fentry__" },
	{ 0x5a844b26, "__x86_indirect_thunk_rax" },
	{ 0xe8213e80, "_printk" },
	{ 0x1d177ede, "noop_llseek" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0xb9e81daf, "proc_remove" },
	{ 0xdb76e3db, "nonseekable_open" },
	{ 0x70eca2ca, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xf8d7ac5e,
	0xd272d446,
	0x5d3be6f8,
	0xd272d446,
	0x5a844b26,
	0xe8213e80,
	0x1d177ede,
	0xd272d446,
	0xb9e81daf,
	0xdb76e3db,
	0x70eca2ca,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"proc_create\0"
	"__SCT__preempt_schedule\0"
	"pcpu_hot\0"
	"__fentry__\0"
	"__x86_indirect_thunk_rax\0"
	"_printk\0"
	"noop_llseek\0"
	"__x86_return_thunk\0"
	"proc_remove\0"
	"nonseekable_open\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "5332C328D4DA74DAE1DFB1E");
