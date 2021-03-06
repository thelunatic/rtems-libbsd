#include <machine/rtems-bsd-kernel-space.h>

#include <rtems/bsd/local/opt_dpaa.h>

/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qman_priv.h"
#ifdef __rtems__
#undef dev_crit
#undef dev_info
#define	dev_crit(dev, fmt, ...) printf(fmt, ##__VA_ARGS__)
#define	dev_info dev_crit
#endif /* __rtems__ */

#ifndef __rtems__
struct qman_portal *qman_dma_portal;
EXPORT_SYMBOL(qman_dma_portal);
#endif /* __rtems__ */

/* Enable portal interupts (as opposed to polling mode) */
#define CONFIG_FSL_DPA_PIRQ_SLOW  1
#define CONFIG_FSL_DPA_PIRQ_FAST  1

#ifndef __rtems__
static struct cpumask portal_cpus;
/* protect qman global registers and global data shared among portals */
static DEFINE_SPINLOCK(qman_lock);
#endif /* __rtems__ */

static void portal_set_cpu(struct qm_portal_config *pcfg, int cpu)
{
#ifdef CONFIG_FSL_PAMU
	struct device *dev = pcfg->dev;
	int window_count = 1;
	struct iommu_domain_geometry geom_attr;
	struct pamu_stash_attribute stash_attr;
	int ret;

	pcfg->iommu_domain = iommu_domain_alloc(&platform_bus_type);
	if (!pcfg->iommu_domain) {
		dev_err(dev, "%s(): iommu_domain_alloc() failed", __func__);
		goto no_iommu;
	}
	geom_attr.aperture_start = 0;
	geom_attr.aperture_end =
		((dma_addr_t)1 << min(8 * sizeof(dma_addr_t), (size_t)36)) - 1;
	geom_attr.force_aperture = true;
	ret = iommu_domain_set_attr(pcfg->iommu_domain, DOMAIN_ATTR_GEOMETRY,
				    &geom_attr);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_domain_set_attr() = %d", __func__,
			ret);
		goto out_domain_free;
	}
	ret = iommu_domain_set_attr(pcfg->iommu_domain, DOMAIN_ATTR_WINDOWS,
				    &window_count);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_domain_set_attr() = %d", __func__,
			ret);
		goto out_domain_free;
	}
	stash_attr.cpu = cpu;
	stash_attr.cache = PAMU_ATTR_CACHE_L1;
	ret = iommu_domain_set_attr(pcfg->iommu_domain,
				    DOMAIN_ATTR_FSL_PAMU_STASH,
				    &stash_attr);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_domain_set_attr() = %d",
			__func__, ret);
		goto out_domain_free;
	}
	ret = iommu_domain_window_enable(pcfg->iommu_domain, 0, 0, 1ULL << 36,
					 IOMMU_READ | IOMMU_WRITE);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_domain_window_enable() = %d",
			__func__, ret);
		goto out_domain_free;
	}
	ret = iommu_attach_device(pcfg->iommu_domain, dev);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_device_attach() = %d", __func__,
			ret);
		goto out_domain_free;
	}
	ret = iommu_domain_set_attr(pcfg->iommu_domain,
				    DOMAIN_ATTR_FSL_PAMU_ENABLE,
				    &window_count);
	if (ret < 0) {
		dev_err(dev, "%s(): iommu_domain_set_attr() = %d", __func__,
			ret);
		goto out_detach_device;
	}

no_iommu:
#endif
	qman_set_sdest(pcfg->channel, cpu);

	return;

#ifdef CONFIG_FSL_PAMU
out_detach_device:
	iommu_detach_device(pcfg->iommu_domain, NULL);
out_domain_free:
	iommu_domain_free(pcfg->iommu_domain);
	pcfg->iommu_domain = NULL;
#endif
}

static struct qman_portal *init_pcfg(struct qm_portal_config *pcfg)
{
	struct qman_portal *p;
	u32 irq_sources = 0;

	/* We need the same LIODN offset for all portals */
	qman_liodn_fixup(pcfg->channel);

#ifndef __rtems__
	pcfg->iommu_domain = NULL;
#endif /* __rtems__ */
	portal_set_cpu(pcfg, pcfg->cpu);
	p = qman_create_affine_portal(pcfg, NULL);
	if (!p) {
		dev_crit(pcfg->dev, "%s: Portal failure on cpu %d\n",
			 __func__, pcfg->cpu);
		return NULL;
	}

	/* Determine what should be interrupt-vs-poll driven */
#ifdef CONFIG_FSL_DPA_PIRQ_SLOW
	irq_sources |= QM_PIRQ_EQCI | QM_PIRQ_EQRI | QM_PIRQ_MRI |
		       QM_PIRQ_CSCI;
#endif
#ifdef CONFIG_FSL_DPA_PIRQ_FAST
	irq_sources |= QM_PIRQ_DQRI;
#endif
	qman_p_irqsource_add(p, irq_sources);

#ifndef __rtems__
	spin_lock(&qman_lock);
	if (cpumask_equal(&portal_cpus, cpu_possible_mask)) {
		/* all assigned portals are initialized now */
		qman_init_cgr_all();
	}

	if (!qman_dma_portal)
		qman_dma_portal = p;

	spin_unlock(&qman_lock);
#endif /* __rtems__ */

	dev_info(pcfg->dev, "Portal initialised, cpu %d\n", pcfg->cpu);

	return p;
}

static void qman_portal_update_sdest(const struct qm_portal_config *pcfg,
							unsigned int cpu)
{
#ifdef CONFIG_FSL_PAMU /* TODO */
	struct pamu_stash_attribute stash_attr;
	int ret;

	if (pcfg->iommu_domain) {
		stash_attr.cpu = cpu;
		stash_attr.cache = PAMU_ATTR_CACHE_L1;
		ret = iommu_domain_set_attr(pcfg->iommu_domain,
				DOMAIN_ATTR_FSL_PAMU_STASH, &stash_attr);
		if (ret < 0) {
			dev_err(pcfg->dev,
				"Failed to update pamu stash setting\n");
			return;
		}
	}
#endif
	qman_set_sdest(pcfg->channel, cpu);
}

#ifndef __rtems__
static int qman_offline_cpu(unsigned int cpu)
{
	struct qman_portal *p;
	const struct qm_portal_config *pcfg;

	p = affine_portals[cpu];
	if (p) {
		pcfg = qman_get_qm_portal_config(p);
		if (pcfg) {
			irq_set_affinity(pcfg->irq, cpumask_of(0));
			qman_portal_update_sdest(pcfg, 0);
		}
	}
	return 0;
}

static int qman_online_cpu(unsigned int cpu)
{
	struct qman_portal *p;
	const struct qm_portal_config *pcfg;

	p = affine_portals[cpu];
	if (p) {
		pcfg = qman_get_qm_portal_config(p);
		if (pcfg) {
			irq_set_affinity(pcfg->irq, cpumask_of(cpu));
			qman_portal_update_sdest(pcfg, cpu);
		}
	}
	return 0;
}

static int qman_portal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct qm_portal_config *pcfg;
	struct resource *addr_phys[2];
	void __iomem *va;
	int irq, cpu, err;
	u32 val;

	pcfg = devm_kmalloc(dev, sizeof(*pcfg), GFP_KERNEL);
	if (!pcfg)
		return -ENOMEM;

	pcfg->dev = dev;

	addr_phys[0] = platform_get_resource(pdev, IORESOURCE_MEM,
					     DPAA_PORTAL_CE);
	if (!addr_phys[0]) {
		dev_err(dev, "Can't get %s property 'reg::CE'\n",
			node->full_name);
		return -ENXIO;
	}

	addr_phys[1] = platform_get_resource(pdev, IORESOURCE_MEM,
					     DPAA_PORTAL_CI);
	if (!addr_phys[1]) {
		dev_err(dev, "Can't get %s property 'reg::CI'\n",
			node->full_name);
		return -ENXIO;
	}

	err = of_property_read_u32(node, "cell-index", &val);
	if (err) {
		dev_err(dev, "Can't get %s property 'cell-index'\n",
			node->full_name);
		return err;
	}
	pcfg->channel = val;
	pcfg->cpu = -1;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Can't get %s IRQ\n", node->full_name);
		return -ENXIO;
	}
	pcfg->irq = irq;

	va = ioremap_prot(addr_phys[0]->start, resource_size(addr_phys[0]), 0);
	if (!va) {
		dev_err(dev, "ioremap::CE failed\n");
		goto err_ioremap1;
	}

	pcfg->addr_virt[DPAA_PORTAL_CE] = va;

	va = ioremap_prot(addr_phys[1]->start, resource_size(addr_phys[1]),
			  _PAGE_GUARDED | _PAGE_NO_CACHE);
	if (!va) {
		dev_err(dev, "ioremap::CI failed\n");
		goto err_ioremap2;
	}

	pcfg->addr_virt[DPAA_PORTAL_CI] = va;

	pcfg->pools = qm_get_pools_sdqcr();

	spin_lock(&qman_lock);
	cpu = cpumask_next_zero(-1, &portal_cpus);
	if (cpu >= nr_cpu_ids) {
		/* unassigned portal, skip init */
		spin_unlock(&qman_lock);
		return 0;
	}

	cpumask_set_cpu(cpu, &portal_cpus);
	spin_unlock(&qman_lock);
	pcfg->cpu = cpu;

	if (dma_set_mask(dev, DMA_BIT_MASK(40))) {
		dev_err(dev, "dma_set_mask() failed\n");
		goto err_portal_init;
	}

	if (!init_pcfg(pcfg)) {
		dev_err(dev, "portal init failed\n");
		goto err_portal_init;
	}

	/* clear irq affinity if assigned cpu is offline */
	if (!cpu_online(cpu))
		qman_offline_cpu(cpu);

	return 0;

err_portal_init:
	iounmap(pcfg->addr_virt[DPAA_PORTAL_CI]);
err_ioremap2:
	iounmap(pcfg->addr_virt[DPAA_PORTAL_CE]);
err_ioremap1:
	return -ENXIO;
}

static const struct of_device_id qman_portal_ids[] = {
	{
		.compatible = "fsl,qman-portal",
	},
	{}
};
MODULE_DEVICE_TABLE(of, qman_portal_ids);

static struct platform_driver qman_portal_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = qman_portal_ids,
	},
	.probe = qman_portal_probe,
};

static int __init qman_portal_driver_register(struct platform_driver *drv)
{
	int ret;

	ret = platform_driver_register(drv);
	if (ret < 0)
		return ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"soc/qman_portal:online",
					qman_online_cpu, qman_offline_cpu);
	if (ret < 0) {
		pr_err("qman: failed to register hotplug callbacks.\n");
		platform_driver_unregister(drv);
		return ret;
	}
	return 0;
}

module_driver(qman_portal_driver,
	      qman_portal_driver_register, platform_driver_unregister);
#else /* __rtems__ */
#include <bsp/fdt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define MAX_QMAN_PORTALS 50

static struct qm_portal_config qman_configs[MAX_QMAN_PORTALS];

static LIST_HEAD(qman_free_portals);

struct qman_portal *
qman_get_dedicated_portal(int cpu)
{
	struct qm_portal_config *pcfg;
	struct qman_portal *p;
	u32 irq_sources;

	if (list_empty(&qman_free_portals))
		return (NULL);

	pcfg = list_first_entry(&qman_free_portals, struct qm_portal_config,
	   node);
	pcfg->cpu = cpu;
	p = qman_create_dedicated_portal(pcfg, NULL);
	if (p == NULL)
		return (NULL);

	list_del_init(&pcfg->node);

	irq_sources = QM_PIRQ_EQCI | QM_PIRQ_EQRI | QM_PIRQ_MRI | QM_PIRQ_CSCI
	    | QM_PIRQ_DQRI;
	qman_p_irqsource_add(p, irq_sources);
	return (p);
}

static bool
is_dequeue_enabled(const struct device_node *dn)
{
	const char *dequeue;
	int len;

	dequeue = of_get_property(dn, "libbsd,dequeue", &len);
	return (len <= 0 || strcmp(dequeue, "disabled") != 0);
}

static void
do_init_pcfg(struct device_node *dn, struct qm_portal_config *pcfg,
    int cpu_count)
{
	struct qman_portal *portal;
	struct resource res;
	int ret;
	u32 val;

	ret = of_address_to_resource(dn, 0, &res);
	if (ret != 0)
		panic("qman: no portal CE address");
	pcfg->addr_virt[0] = (__iomem void *)(uintptr_t)res.start;
#if QORIQ_CHIP_IS_T_VARIANT(QORIQ_CHIP_VARIANT) && \
    !defined(QORIQ_IS_HYPERVISOR_GUEST)
	BSD_ASSERT((uintptr_t)pcfg->addr_virt[0] >=
	    (uintptr_t)&qoriq_qman_portal[0][0]);
	BSD_ASSERT((uintptr_t)pcfg->addr_virt[0] <
	    (uintptr_t)&qoriq_qman_portal[1][0]);
#endif

	ret = of_address_to_resource(dn, 1, &res);
	if (ret != 0)
		panic("qman: no portal CI address");
	pcfg->addr_virt[1] = (__iomem void *)(uintptr_t)res.start;
#if QORIQ_CHIP_IS_T_VARIANT(QORIQ_CHIP_VARIANT) && \
    !defined(QORIQ_IS_HYPERVISOR_GUEST)
	BSD_ASSERT((uintptr_t)pcfg->addr_virt[1] >=
	    (uintptr_t)&qoriq_qman_portal[1][0]);
	BSD_ASSERT((uintptr_t)pcfg->addr_virt[1] <
	    (uintptr_t)&qoriq_qman_portal[2][0]);
#endif

	ret = of_property_read_u32(dn, "cell-index", &val);
	if (ret != 0)
		panic("qman: no cell-index");
	pcfg->channel = val;

	pcfg->irq = of_irq_to_resource(dn, 0, NULL);
	if (pcfg->irq == NO_IRQ)
		panic("qman: no portal interrupt");

	if (val < cpu_count) {
		pcfg->cpu = val;

		if (is_dequeue_enabled(dn)) {
			pcfg->pools = qm_get_pools_sdqcr();
		}

		portal = init_pcfg(pcfg);
		BSD_ASSERT(portal != NULL);

		qman_portal_update_sdest(pcfg, val);
	} else {
		pcfg->cpu = -1;
		list_add_tail(&pcfg->node, &qman_free_portals);
	}
}

void
qman_sysinit_portals(void)
{
	const char *fdt = bsp_fdt_get();
	struct device_node dn;
	const char *name;
	int cpu_count = (int)rtems_get_processor_count();
	int i;
	int node;

#if QORIQ_CHIP_IS_T_VARIANT(QORIQ_CHIP_VARIANT) && \
    !defined(QORIQ_IS_HYPERVISOR_GUEST)
	qoriq_clear_ce_portal(&qoriq_qman_portal[0][0],
	    sizeof(qoriq_qman_portal[0]));
	qoriq_clear_ci_portal(&qoriq_qman_portal[1][0],
	    sizeof(qoriq_qman_portal[1]));
#endif

	memset(&dn, 0, sizeof(dn));
	name = "fsl,qman-portal";
	node = -1;
	dn.full_name = name;
	i = 0;

	while (i < MAX_QMAN_PORTALS) {
		node = fdt_node_offset_by_compatible(fdt, node, name);
		if (node < 0)
			break;

		dn.offset = node;
		do_init_pcfg(&dn, &qman_configs[i], cpu_count);
		++i;
	}

	if (i < cpu_count)
		panic("qman: not enough affine portals");

	/*
	 * We try to use the "cell-index" for the affine portal processor
	 * index.  This is not always possible, so equip the remaining
	 * processors with portals from the free list.  Ignore the
	 * "libbsd,dequeue" property.
	 */
	for (i = 0; i < cpu_count; ++i) {
		struct qman_portal *p;

		p = affine_portals[i];
		if (p == NULL) {
			struct qm_portal_config *pcfg;
			struct qman_portal *portal;

			if (list_empty(&qman_free_portals))
				panic("qman: no free affine portal");

			pcfg = list_first_entry(&qman_free_portals,
			    struct qm_portal_config, node);
			list_del_init(&pcfg->node);

			pcfg->cpu = i;
			pcfg->pools = qm_get_pools_sdqcr();

			portal = init_pcfg(pcfg);
			BSD_ASSERT(portal != NULL);

			qman_portal_update_sdest(pcfg, i);

		}
	}

	/* all assigned portals are initialized now */
	qman_init_cgr_all();
}
#endif /* __rtems__ */
