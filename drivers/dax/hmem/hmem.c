// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/memregion.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include "../bus.h"

static bool region_idle;
module_param_named(region_idle, region_idle, bool, 0644);

static int dax_hmem_probe(struct platform_device *pdev)
{
	unsigned long flags = IORESOURCE_DAX_KMEM;
	struct device *dev = &pdev->dev;
	struct dax_region *dax_region;
	struct memregion_info *mri;
	struct dev_dax_data data;

	/*
	 * @region_idle == true indicates that an administrative agent
	 * wants to manipulate the range partitioning before the devices
	 * are created, so do not send them to the dax_kmem driver by
	 * default.
	 */
	if (region_idle)
		flags = 0;

	mri = dev->platform_data;
	dax_region = alloc_dax_region(dev, pdev->id, &mri->range,
				      mri->target_node, PMD_SIZE, flags);
	if (!dax_region)
		return -ENOMEM;

	data = (struct dev_dax_data) {
		.dax_region = dax_region,
		.id = -1,
		.size = region_idle ? 0 : range_len(&mri->range),
		.memmap_on_memory = false,
	};

	return PTR_ERR_OR_ZERO(devm_create_dev_dax(&data));
}

static int dax_hmem_remove(struct platform_device *pdev)
{
	/* devm handles teardown */
	return 0;
}

static struct platform_driver dax_hmem_driver = {
	.probe = dax_hmem_probe,
	.remove = dax_hmem_remove,
	.driver = {
		.name = "hmem",
	},
};

module_platform_driver(dax_hmem_driver);

MODULE_ALIAS("platform:hmem*");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
