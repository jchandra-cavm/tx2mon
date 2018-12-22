/*
 * Copyright (c) 2018 Marvell International Ltd.
 */
#ifndef _MC_OPER_REGION_H
#define _MC_OPER_REGION_H

#define MAX_CPUS_PER_SOC	32

/* for cmd_status below */
#define STATUS_READY		(1U << 1)

/* MC val to celsius */
#define to_c(val)	((446.18 + 7.92) - ((val) * 0.5582))

/* MC operating region layout */
struct mc_oper_region
{
    uint32_t	cmd_status;
    uint32_t	counter;
    uint32_t	resv0;
    uint32_t	temp_abs_max;
    uint32_t	temp_soft_thresh;
    uint32_t	temp_hard_thresh;
    uint32_t	resv1;
    uint32_t	resv2;
    uint32_t	freq_cpu[MAX_CPUS_PER_SOC];
    int32_t	resv3[MAX_CPUS_PER_SOC];
    uint16_t	tmon_cpu[MAX_CPUS_PER_SOC];
    uint32_t	tmon_soc_avg;
    uint32_t	freq_mem_net;
    uint32_t	freq_socs;
    uint32_t	freq_socn;
    uint32_t	freq_max;
    uint32_t	freq_min;
    uint32_t	pwr_core;
    uint32_t	pwr_sram;
    uint32_t	pwr_mem;
    uint32_t	pwr_soc;
    uint32_t	v_core;
    uint32_t	v_sram;
    uint32_t	v_mem;
    uint32_t	v_soc;
    uint32_t	resv4;
    uint32_t	resv5;
    uint32_t	resv6;
    uint32_t	resv7;
    uint32_t	resv8;
};

#endif

