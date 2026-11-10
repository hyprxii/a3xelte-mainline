// SPDX-License-Identifier: GPL-2.0
/*
 * Exynos7580 (a3xelte) bring-up: S-Boot leaves DECON in HW-trigger (TE)
 * mode with the trigger masked after the boot logo, so the command-mode
 * panel is no longer refreshed.  Unmask it (value used by the edk2-exynos
 * port: TRIGCON = 0x2058) so the bootloader framebuffer is scanned out again.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/kernel.h>

#define A3X_DECON_BASE		0x14830000UL
#define A3X_DECON_TRIGCON	0x06b0
#define A3X_DECON_TRIGCON_VAL	0x2058

static int __init a3x_decon_trig_init(void)
{
	void __iomem *b = ioremap(A3X_DECON_BASE, 0x1000);
	u32 old;

	if (!b)
		return -ENOMEM;
	old = readl(b + A3X_DECON_TRIGCON);
	writel(A3X_DECON_TRIGCON_VAL, b + A3X_DECON_TRIGCON);
	pr_info("a3x-decon: TRIGCON %08x -> %08x\n", old,
		readl(b + A3X_DECON_TRIGCON));
	iounmap(b);
	return 0;
}
early_initcall(a3x_decon_trig_init);

/* Dump the eMMC (dwmmc0) controller state exactly as S-Boot left it, before
 * the mainline driver resets it. Non-zero words only, a few per line. */
#define A3X_MMC0_BASE	0x13540000UL

static int __init a3x_mmc_dump_init(void)
{
	void __iomem *b = ioremap(A3X_MMC0_BASE, 0x300);
	char line[160];
	int off, n = 0;

	if (!b)
		return -ENOMEM;
	pr_info("a3x-mmc0: --- S-Boot state @0x%lx ---\n", A3X_MMC0_BASE);
	for (off = 0; off < 0x300; off += 4) {
		u32 v = readl(b + off);

		if (!v)
			continue;
		n += scnprintf(line + n, sizeof(line) - n, "%x=%08x ", off, v);
		if (n > 110) {
			pr_info("a3x-mmc0: %s\n", line);
			n = 0;
		}
	}
	if (n)
		pr_info("a3x-mmc0: %s\n", line);
	iounmap(b);
	return 0;
}
early_initcall(a3x_mmc_dump_init);
