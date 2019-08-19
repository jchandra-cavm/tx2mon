// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell International Ltd.
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define NODE0_MC_BASE_ADDR	0x80008000U
#define NODE1_MC_BASE_ADDR	0x80009000U
#define MC_MAP_SIZE		1024
#define MC_REGION_SIZE		512
#define MAX_NODES		2

struct tx2_node_data {
	struct bin_attribute	bin_attr;
	void			*mc_region;
};

struct tx2mon_data {
	struct platform_device	*pdev;
	int			num_nodes;
	int			num_cores;
	int			num_threads;
	struct tx2_node_data	node_data[MAX_NODES];
};
static struct tx2mon_data *tx2mon_data;

static ssize_t socinfo_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	return sprintf(buf, "%d %d %d\n",
		tx2mon_data->num_nodes, tx2mon_data->num_cores,
		tx2mon_data->num_threads);
}
static DEVICE_ATTR_RO(socinfo);

static void get_tx2_info(struct tx2mon_data *tx2d)
{
	struct arm_smccc_res r;

	arm_smccc_smc(0xC200FF00, 0xB001, 0, 0, 0, 0, 0, 0, &r);
	if (r.a0 == SMCCC_RET_SUCCESS)  {
		tx2mon_data->num_nodes = (r.a1 >> 16) & 0xF;
		tx2mon_data->num_cores = (r.a1 >> 8) & 0xFF;
		tx2mon_data->num_threads = r.a1 & 0xF;
		pr_info("%d %d %d\n", tx2mon_data->num_nodes,
			tx2mon_data->num_cores, tx2mon_data->num_threads);
	} else {
		tx2mon_data->num_nodes = MAX_NODES;
		tx2mon_data->num_cores = 32;
		tx2mon_data->num_threads = 4;
		pr_info("failed\n");
	}
}

static ssize_t tx2mon_read(struct file *file, struct kobject *kobj,
		       struct bin_attribute *attr, char *buf,
		       loff_t off, size_t count)
{
	struct tx2_node_data *nd = attr->private;

	return memory_read_from_buffer(buf, count, &off, nd->mc_region,
				       MC_REGION_SIZE);
}

static int setup_tx2_node(struct tx2mon_data *tx2d, int node)
{
	const phys_addr_t mc_bases[] = {NODE0_MC_BASE_ADDR, NODE1_MC_BASE_ADDR};
	struct tx2_node_data *nd;

	nd = &tx2d->node_data[node];
	nd->mc_region = memremap(mc_bases[node], MC_MAP_SIZE, MEMREMAP_WB);
	if (nd->mc_region == NULL)
		return -ENODEV;

	sysfs_bin_attr_init(&nd->bin_attr);
	nd->bin_attr.attr.name = devm_kasprintf(&tx2d->pdev->dev, GFP_KERNEL,
						"node%d_raw", node);
	nd->bin_attr.attr.mode = 0600;
	nd->bin_attr.size = MC_REGION_SIZE;
	nd->bin_attr.read = tx2mon_read;
	nd->bin_attr.private = nd;
	return sysfs_create_bin_file(&tx2d->pdev->dev.kobj, &nd->bin_attr);
}

static void tx2mon_cleanup(struct tx2mon_data *tx2d)
{
	/* TODO - does it work on partial init */
	struct tx2_node_data *nd;
	int i;

	for (i = 0; i < tx2d->num_nodes; i++) {
		nd = &tx2mon_data->node_data[i];
		sysfs_remove_bin_file(&tx2d->pdev->dev.kobj, &nd->bin_attr);
		memunmap(nd->mc_region);
	}
	sysfs_remove_file(&tx2d->pdev->dev.kobj, &dev_attr_socinfo.attr);
	platform_device_unregister(tx2d->pdev);
}

static int __init socmon_init(void)
{
	struct platform_device_info pdevinfo;
	struct platform_device *pdev;
	int err, i;

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = NULL;
	pdevinfo.name = "tx2mon";
	pdevinfo.id = -1;
	pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	tx2mon_data = devm_kzalloc(&pdev->dev, sizeof(*tx2mon_data),
				   GFP_KERNEL);
	if (tx2mon_data == NULL) {
		return -ENOMEM;
	}

	get_tx2_info(tx2mon_data);
	tx2mon_data->pdev = pdev;
	err = sysfs_create_file(&tx2mon_data->pdev->dev.kobj,
				&dev_attr_socinfo.attr);
	if (err)
		goto failout;
	for (i = 0; i < tx2mon_data->num_nodes; i++) {
		err = setup_tx2_node(tx2mon_data, i);
		if (err)
			goto failout;
	}
	return 0;

failout:
	tx2mon_cleanup(tx2mon_data);
	return -ENOMEM;
}

static void __exit socmon_exit(void)
{
	tx2mon_cleanup(tx2mon_data);
}

module_init(socmon_init);
module_exit(socmon_exit);
MODULE_LICENSE("GPL");
