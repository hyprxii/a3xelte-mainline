// SPDX-License-Identifier: GPL-2.0
/*
 * Exynos7580 (a3xelte) bring-up: power up the DWC USB2 PHY for device mode
 * before the dwc2 driver probes. Sequence ported from the downstream
 * phy-samsung-dwc-usb2.c / phy-samsung-usb2-cal.c (device mode path).
 * S-Boot leaves the PHY in reset/SIDDQ, USB PLL running, link clocks gated on.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/bits.h>

#define PMU_USBDEV_PHY_CTRL	0x10460738UL	/* bit0: PHY enable (isolation off) */
#define SYSREG_USB20PHY_CFG	0x13721020UL	/* bit0: 1 = host link */
#define CMU_FSYS_MUX_SEL1	0x13730204UL	/* bit8/12/16: phyclk user muxes */
#define CMU_FSYS_MUX_STAT1	0x13730404UL
#define PHY_BASE		0x135c0000UL

#define PHYUTMI			0x08
#define  PHYUTMI_OTGDISABLE	BIT(6)
#define  PHYUTMI_DRVVBUS	BIT(4)
#define  PHYUTMI_DPPULLDOWN	BIT(3)
#define  PHYUTMI_DMPULLDOWN	BIT(2)
#define  PHYUTMI_FORCESUSPEND	BIT(1)
#define  PHYUTMI_FORCESLEEP	BIT(0)
#define PHYCLKRST		0x10
#define  PHYCLKRST_COMMONONN	BIT(0)
#define PHYPARAM0		0x1c
#define RESUME			0x34
#define  RESUME_BYPASSSEL	BIT(4)
#define  RESUME_BYPASSDMEN	BIT(3)
#define  RESUME_BYPASSDPEN	BIT(2)
#define STDPHYCTRL		0x54
#define  STDPHYCTRL_SWRST_ALL	BIT(31)
#define  STDPHYCTRL_SIDDQ	BIT(6)
#define  STDPHYCTRL_SWRST_PHY	BIT(0)
#define OTGLINKCTL		0x8c
#define  OTGLINK_AVALID		BIT(14)
#define  OTGLINK_BVALID		BIT(13)
#define  OTGLINK_IDDIG		BIT(12)
#define  OTGLINK_VBUSDETECT	BIT(11)
#define  OTGLINK_LINK_PRST	BIT(4)
#define  OTGLINK_SW_RESET	BIT(3)

static void rmw(void __iomem *p, u32 clr, u32 set)
{
	writel((readl(p) & ~clr) | set, p);
}

static int __init a3x_usb_phy_init(void)
{
	void __iomem *phy = ioremap(PHY_BASE, 0x100);
	void __iomem *pmu = ioremap(PMU_USBDEV_PHY_CTRL, 4);
	void __iomem *sys = ioremap(SYSREG_USB20PHY_CFG, 4);
	void __iomem *mux = ioremap(CMU_FSYS_MUX_SEL1, 4);
	void __iomem *mst = ioremap(CMU_FSYS_MUX_STAT1, 4);
	u32 v;

	if (!phy || !pmu || !sys || !mux || !mst) {
		pr_err("a3x-usb: ioremap failed\n");
		return -ENOMEM;
	}

	pr_info("a3x-usb: before: PMU=%08x STDPHYCTRL=%08x PHYUTMI=%08x MUX=%08x\n",
		readl(pmu), readl(phy + STDPHYCTRL), readl(phy + PHYUTMI),
		readl(mux));

	/* 1. release PHY isolation */
	rmw(pmu, 0, BIT(0));

	/* 2. enable: out of SIDDQ, clear bypass cell, release sleep/suspend */
	rmw(phy + STDPHYCTRL, STDPHYCTRL_SIDDQ, 0);
	udelay(500);
	rmw(phy + RESUME, RESUME_BYPASSSEL | RESUME_BYPASSDMEN | RESUME_BYPASSDPEN, 0);
	rmw(phy + PHYUTMI, PHYUTMI_FORCESLEEP | PHYUTMI_FORCESUSPEND |
	    PHYUTMI_DMPULLDOWN | PHYUTMI_DPPULLDOWN | PHYUTMI_OTGDISABLE,
	    PHYUTMI_DRVVBUS);
	rmw(phy + PHYCLKRST, PHYCLKRST_COMMONONN, 0);	/* FSEL stays 50MHz (USB PLL) */

	/* 3. tune (device mode) */
	v = readl(phy + PHYPARAM0);
	v &= ~((0xfU << 22) | (0x3U << 15) | (0x7U << 6));
	v |= (0xeU << 22) | (0x2U << 15) | (0x6U << 6);
	writel(v, phy + PHYPARAM0);

	/* 4. reset PHY + OTG link */
	rmw(phy + STDPHYCTRL, 0, STDPHYCTRL_SWRST_PHY | STDPHYCTRL_SWRST_ALL);
	rmw(phy + OTGLINKCTL, 0, OTGLINK_SW_RESET | OTGLINK_LINK_PRST);
	udelay(10);
	rmw(phy + STDPHYCTRL, STDPHYCTRL_SWRST_PHY | STDPHYCTRL_SWRST_ALL, 0);
	rmw(phy + OTGLINKCTL, OTGLINK_SW_RESET | OTGLINK_LINK_PRST, 0);
	udelay(50);

	/* 5. device link select, tune again */
	rmw(sys, BIT(0), 0);
	v = readl(phy + PHYPARAM0);
	v &= ~((0xfU << 22) | (0x3U << 15) | (0x7U << 6));
	v |= (0xeU << 22) | (0x2U << 15) | (0x6U << 6);
	writel(v, phy + PHYPARAM0);

	/* 6. B-session valid / VBUS detect / ID floating (no external sensing) */
	rmw(phy + OTGLINKCTL, 0, OTGLINK_BVALID | OTGLINK_VBUSDETECT | OTGLINK_IDDIG);

	/* 7. switch link clocks to the PHY-provided ones (user muxes) */
	rmw(mux, 0, BIT(8) | BIT(12) | BIT(16));
	udelay(100);

	pr_info("a3x-usb: after:  PMU=%08x STDPHYCTRL=%08x PHYUTMI=%08x PHYCLKRST=%08x OTGLINK=%08x MUX=%08x STAT=%08x\n",
		readl(pmu), readl(phy + STDPHYCTRL), readl(phy + PHYUTMI),
		readl(phy + PHYCLKRST), readl(phy + OTGLINKCTL), readl(mux),
		readl(mst));

	iounmap(phy); iounmap(pmu); iounmap(sys); iounmap(mux); iounmap(mst);
	return 0;
}
arch_initcall(a3x_usb_phy_init);
