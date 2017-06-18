/*
 * driver/irq-gic400.c
 *
 * Copyright(c) 2007-2017 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <interrupt/interrupt.h>
#include <arm64.h>

enum {
	DIST_CTRL 			= 0x0000,
	DIST_CTR 			= 0x0004,
	DIST_ENABLE_SET 	= 0x0100,
	DIST_ENABLE_CLEAR 	= 0x0180,
	DIST_PENDING_SET 	= 0x0200,
	DIST_PENDING_CLEAR	= 0x0280,
	DIST_ACTIVE_BIT		= 0x0300,
	DIST_PRI			= 0x0400,
	DIST_TARGET			= 0x0800,
	DIST_CONFIG			= 0x0c00,
	DIST_SOFTINT		= 0x0f00,

	CPU_CTRL 			= 0x1000,
	CPU_PRIMASK 		= 0x1004,
	CPU_BINPOINT 		= 0x1008,
	CPU_INTACK 			= 0x100c,
	CPU_EOI 			= 0x1010,
	CPU_RUNNINGPRI 		= 0x1014,
	CPU_HIGHPRI 		= 0x1018,
};

struct irq_gic400_pdata_t
{
	virtual_addr_t virt;
	int base;
	int nirq;
};

static void irq_gic400_enable(struct irqchip_t * chip, int offset)
{
	struct irq_gic400_pdata_t * pdat = (struct irq_gic400_pdata_t *)chip->priv;
	int irq = chip->base + offset;
	write32(pdat->virt + DIST_ENABLE_SET + (irq / 32) * 4, 1 << (irq % 32));
}

static void irq_gic400_disable(struct irqchip_t * chip, int offset)
{
	struct irq_gic400_pdata_t * pdat = (struct irq_gic400_pdata_t *)chip->priv;
	int irq = chip->base + offset;
	write32(pdat->virt + DIST_ENABLE_CLEAR + (irq / 32) * 4, 1 << (irq % 32));
}

static void irq_gic400_settype(struct irqchip_t * chip, int offset, enum irq_type_t type)
{
}

static void irq_gic400_dispatch(struct irqchip_t * chip)
{
	struct irq_gic400_pdata_t * pdat = (struct irq_gic400_pdata_t *)chip->priv;
	int irq = read32(pdat->virt + CPU_INTACK) & 0x3ff;
	int offset = irq - chip->base;

	if((offset >= 0) && (offset < chip->nirq))
	{
		(chip->handler[offset].func)(chip->handler[offset].data);
		write32(pdat->virt + CPU_EOI, irq);
	}
}

static void gic400_dist_init(virtual_addr_t virt)
{
	u32_t gic_irqs;
	u32_t cpumask;
	int i;

	write32(virt + DIST_CTRL, 0x0);

	/*
	 * Find out how many interrupts are supported.
	 * The GIC only supports up to 1020 interrupt sources.
	 */
	gic_irqs = read32(virt + DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if(gic_irqs > 1020)
		gic_irqs = 1020;

	/*
	 * Set all global interrupts to this CPU only.
	 */
	cpumask = 1 << arm64_smp_processor_id();
	cpumask |= cpumask << 8;
	cpumask |= cpumask << 16;
	for(i = 32; i < gic_irqs; i += 4)
		write32(virt + DIST_TARGET + i * 4 / 4, cpumask);

	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for(i = 32; i < gic_irqs; i += 16)
		write32(virt + DIST_CONFIG + i * 4 / 16, 0);

	/*
	 * Set priority on all global interrupts.
	 */
	for(i = 32; i < gic_irqs; i += 4)
		write32(virt + DIST_PRI + i * 4 / 4, 0xa0a0a0a0);

	/*
	 * Disable all interrupts, leave the SGI and PPI alone
	 * as these enables are banked registers.
	 */
	for(i = 32; i < gic_irqs; i += 32)
		write32(virt + DIST_ENABLE_CLEAR + i * 4 / 32, 0xffffffff);

	write32(virt + DIST_CTRL, 0x1);
}

static void gic400_cpu_init(virtual_addr_t virt)
{
	int i;

	/*
	 * Deal with the banked SGI and PPI interrupts - enable all
	 * SGI interrupts, ensure all PPI interrupts are disabled.
	 */
	write32(virt + DIST_ENABLE_CLEAR, 0xffff0000);
	write32(virt + DIST_ENABLE_SET, 0x0000ffff);

	/*
	 * Set priority on SGI and PPI interrupts
	 */
	for(i = 0; i < 32; i += 4)
		write32(virt + DIST_PRI + i * 4 / 4, 0xa0a0a0a0);

	write32(virt + CPU_PRIMASK, 0xf0);
	write32(virt + CPU_CTRL, 0x1);
}

static struct device_t * irq_gic400_probe(struct driver_t * drv, struct dtnode_t * n)
{
	struct irq_gic400_pdata_t * pdat;
	struct irqchip_t * chip;
	struct device_t * dev;
	virtual_addr_t virt = phys_to_virt(dt_read_address(n));
	int base = dt_read_int(n, "interrupt-base", -1);
	int nirq = dt_read_int(n, "interrupt-count", -1);

	if((base < 0) || (nirq <= 0))
		return NULL;

	pdat = malloc(sizeof(struct irq_gic400_pdata_t));
	if(!pdat)
		return NULL;

	chip = malloc(sizeof(struct irqchip_t));
	if(!chip)
	{
		free(pdat);
		return NULL;
	}

	pdat->virt = virt;
	pdat->base = base;
	pdat->nirq = nirq;

	chip->name = alloc_device_name(dt_read_name(n), -1);
	chip->base = pdat->base;
	chip->nirq = pdat->nirq;
	chip->handler = malloc(sizeof(struct irq_handler_t) * pdat->nirq);
	chip->enable = irq_gic400_enable;
	chip->disable = irq_gic400_disable;
	chip->settype = irq_gic400_settype;
	chip->dispatch = irq_gic400_dispatch;
	chip->priv = pdat;

	gic400_dist_init(pdat->virt);
	gic400_cpu_init(pdat->virt);
	arm64_interrupt_enable();

	if(!register_irqchip(&dev, chip))
	{
		free_device_name(chip->name);
		free(chip->handler);
		free(chip->priv);
		free(chip);
		return NULL;
	}
	dev->driver = drv;

	return dev;
}

static void irq_gic400_remove(struct device_t * dev)
{
	struct irqchip_t * chip = (struct irqchip_t *)dev->priv;

	if(chip && unregister_irqchip(chip))
	{
		free_device_name(chip->name);
		free(chip->handler);
		free(chip->priv);
		free(chip);
	}
}

static void irq_gic400_suspend(struct device_t * dev)
{
}

static void irq_gic400_resume(struct device_t * dev)
{
}

static struct driver_t irq_gic400 = {
	.name		= "irq-gic400",
	.probe		= irq_gic400_probe,
	.remove		= irq_gic400_remove,
	.suspend	= irq_gic400_suspend,
	.resume		= irq_gic400_resume,
};

static __init void irq_gic400_driver_init(void)
{
	register_driver(&irq_gic400);
}

static __exit void irq_gic400_driver_exit(void)
{
	unregister_driver(&irq_gic400);
}

driver_initcall(irq_gic400_driver_init);
driver_exitcall(irq_gic400_driver_exit);