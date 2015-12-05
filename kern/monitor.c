// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display the backtrace of a function", mon_backtrace},
	{ "showmappings", "Display the page mapping info", showmappings},
	{ "setperm", "Explicitly set, clear, or change the permissions.", setperm},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	unsigned int *ebp = (unsigned int*) read_ebp();
	int i;
	cprintf("Stack backtrace:\n");
	while (ebp != 0) {
		unsigned int eip = *(ebp + 1);
		cprintf("  %rebp %08x  %reip %08x  %rargs", 0x0c, ebp, 0x0a, eip, 0x09);
		for (i = 0; i < 5; i++) {
			cprintf(" %08x", *(ebp + i + 2));
		}
		cprintf("%r\n", 0x07);
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		cprintf("         %s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
		ebp = (unsigned int*)*ebp;
	}
	return 0;
}

uint32_t xtoi(char *s) {
	uint32_t result = 0;
	int i;
	for (i = 2; s[i] != '\0'; i++) {
		if (s[i] >= '0' && s[i] <= '9') result = result * 16 + s[i] - '0';
		else if (s[i] >= 'a' && s[i] <= 'f') result = result * 16 + s[i] - 'a' + 10;
		else if (s[i] >= 'A' && s[i] <= 'F') result = result * 16 + s[i] - 'A' + 10;
		else panic("xtoi: invalid string %s!", s);
	}
	return result;
}

void print_pte_info(pte_t *ppte) {
	cprintf("Phys memory: 0x%08x, PTE_P: %x, PTE_W: %x, PTE_U: %x\n", PTE_ADDR(*ppte), *ppte & PTE_P, *ppte & PTE_W, *ppte & PTE_U);
}

int
showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("Usage: showmappings 0xbegin 0xend\nshow page mappings from begin to end.\n");
		return 0;
	}
	uint32_t va = xtoi(argv[1]), vend = xtoi(argv[2]);
	cprintf("begin: %x, end: %x\n", va, vend);
	for (; va <= vend; va += PGSIZE) {
		pte_t *ppte = pgdir_walk(kern_pgdir, (void *)va, 1);
		if (!ppte) panic("showmappings: creating page error!");
		if (*ppte & PTE_P) {
			cprintf("page of 0x%x: ", va);
			print_pte_info(ppte);
		} else cprintf("page not exist: %x\n", va);
	}
	return 0;
}

int setperm(int argc, char **argv, struct Trapframe *tf) {
	if (argc == 1) {
		cprintf("Usage: setperm 0xaddr [(clear | set) [P | W | U] | change 0x<perm> ]\n");
		return 0;
	}
	uint32_t addr = xtoi(argv[1]);
	pte_t *ppte = pgdir_walk(kern_pgdir, (void *)addr, 1);
	uint32_t perm = 0;
	if (argv[2][1] == 'h') { //for change
		perm = xtoi(argv[3]);
		*ppte = (*ppte & 0xfff8) | perm;
	}
	else {
		if (argv[3][0] == 'P') perm = PTE_P;
		if (argv[3][0] == 'W') perm = PTE_W;
		if (argv[3][0] == 'U') perm = PTE_U;
		if (argv[2][1] == 'l') *ppte = *ppte & ~perm; // for clear
		else if (argv[2][1] == 'e') *ppte = *ppte | perm; // for set
		else {
			cprintf("Parameters error!\nUsage: setperm 0xaddr [(clear | set) [P | W | U] | change <perm> ]\n");
			return 0;
		}
	}
	cprintf("setperm success.\npage of 0x%x: ", addr);
	print_pte_info(ppte);
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");
	if (tf != NULL)
		print_trapframe(tf);
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
