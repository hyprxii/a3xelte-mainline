// SPDX-License-Identifier: GPL-2.0
/*
 * Exynos-snapshot (ESS) "last_kmsg" console for Samsung Exynos7580 bring-up.
 *
 * Writes the kernel log as plain text into the ESS log_kernel ring
 * (ESS base + 8MB, 2MB long) and keeps the "last logbuf" pointer word
 * (ESS base + 0x200) updated, so the downstream Samsung kernel / TWRP
 * shows it as /proc/last_kmsg after a reset.
 *
 * Uses the earlycon fixmap slot, so it works right after setup_arch()
 * (cmdline: "essconsole") and needs no ioremap/vmalloc.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/io.h>
#include <asm/fixmap.h>
#include <asm/barrier.h>

#define ESS_BASE	0x46000000UL
#define ESS_LOG		(ESS_BASE + 0x800000UL)
#define ESS_LOG_SZ	0x200000UL
#define ESS_PTR		(ESS_BASE + 0x200UL)

static unsigned long ess_off;
static bool ess_ready;

static void __iomem *ess_map(unsigned long pa)
{
	set_fixmap_io(FIX_EARLYCON_MEM_BASE, pa & PAGE_MASK);
	return (void __iomem *)(__fix_to_virt(FIX_EARLYCON_MEM_BASE) +
				(pa & ~PAGE_MASK));
}

static void ess_unmap(void)
{
	clear_fixmap(FIX_EARLYCON_MEM_BASE);
}

static void ess_set_ptr(void)
{
	void __iomem *p = ess_map(ESS_PTR);

	writel_relaxed((u32)(ESS_LOG + ess_off), p);
	dsb(sy);
	ess_unmap();
}

static void ess_clear_ring(void)
{
	unsigned long off;

	for (off = 0; off < ESS_LOG_SZ; off += PAGE_SIZE) {
		void __iomem *p = ess_map(ESS_LOG + off);
		unsigned long i;

		for (i = 0; i < PAGE_SIZE; i += 8)
			writeq_relaxed(0, p + i);
		ess_unmap();
	}
	dsb(sy);
}

static void ess_write(struct console *con, const char *s, unsigned int n)
{
	if (!ess_ready) {
		void __iomem *pp = ess_map(ESS_PTR);
		u32 cur = readl_relaxed(pp);

		ess_unmap();
		ess_ready = true;
		/* continue after the bootloader / stub text if the pointer is sane */
		if (cur >= ESS_LOG && cur < ESS_LOG + ESS_LOG_SZ) {
			ess_off = cur - ESS_LOG;
		} else {
			ess_off = 0;
			ess_clear_ring();
		}
	}

	while (n) {
		unsigned long pa = ESS_LOG + ess_off;
		unsigned long chunk = min3((unsigned long)n,
					   PAGE_SIZE - (pa & ~PAGE_MASK),
					   ESS_LOG_SZ - ess_off);
		void __iomem *p = ess_map(pa);
		unsigned long i;

		for (i = 0; i < chunk; i++)
			writeb_relaxed(s[i], p + i);
		ess_unmap();

		ess_off = (ess_off + chunk) % ESS_LOG_SZ;
		s += chunk;
		n -= chunk;
	}
	dsb(sy);
	ess_set_ptr();
}

static struct console ess_console = {
	.name	= "ess",
	.write	= ess_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED,
	.index	= -1,
};

static int __init ess_console_setup(char *str)
{
	register_console(&ess_console);
	return 0;
}
early_param("essconsole", ess_console_setup);
