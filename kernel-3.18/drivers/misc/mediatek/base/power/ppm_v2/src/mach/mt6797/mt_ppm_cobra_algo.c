/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/cpumask.h>

#include "mt_ppm_platform.h"
#include "mt_ppm_internal.h"


struct ppm_cobra_data cobra_tbl;

static int prev_max_cpufreq_idx[NR_PPM_CLUSTERS] = {0, 0, 0};
static int Core_limit[NR_PPM_CLUSTERS] = {CORE_NUM_LL, CORE_NUM_L, CORE_NUM_B};
#if PPM_COBRA_NEED_OPP_MAPPING
static int freq_idx_mapping_tbl_fy[COBRA_OPP_NUM] = {0, 3, 5, 7, 9, 10, 12, 14};
static int freq_idx_mapping_tbl_fy_r[DVFS_OPP_NUM] = {0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 7, 7};
static int freq_idx_mapping_tbl_fy_big[COBRA_OPP_NUM] = {0, 4, 6, 8, 10, 12, 14, 15};
static int freq_idx_mapping_tbl_fy_big_r[DVFS_OPP_NUM] = {0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7};
static int freq_idx_mapping_tbl_sb[COBRA_OPP_NUM] = {0, 3, 6, 8, 9, 11, 12, 14};
static int freq_idx_mapping_tbl_sb_r[DVFS_OPP_NUM] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 5, 6, 7, 7, 7};
static int freq_idx_mapping_tbl_sb_big[COBRA_OPP_NUM] = {0, 3, 6, 8, 9, 10, 12, 15};
static int freq_idx_mapping_tbl_sb_big_r[DVFS_OPP_NUM] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 6, 6, 7, 7, 7};

int *freq_idx_mapping_tbl;
int *freq_idx_mapping_tbl_r;
int *freq_idx_mapping_tbl_big;
int *freq_idx_mapping_tbl_big_r;
#endif

#define ACT_CORE(cluster)		(active_core[PPM_CLUSTER_##cluster])
#define CORE_LIMIT(cluster)		(core_limit_tmp[PPM_CLUSTER_##cluster])
#define PREV_FREQ_LIMIT(cluster)	(prev_max_cpufreq_idx[PPM_CLUSTER_##cluster])


static int get_delta_pwr_LxLL(unsigned int L_core, unsigned int LL_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	unsigned int idx_L = get_cluster_max_cpu_core(PPM_CLUSTER_LL);
	unsigned int idx_LL = 0;
	unsigned int cur_opp, prev_opp, cur_pwr, prev_pwr;
	int delta_pwr;
#endif

	if (L_core > get_cluster_max_cpu_core(PPM_CLUSTER_L)
		|| LL_core > get_cluster_max_cpu_core(PPM_CLUSTER_LL)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_L)) {
		ppm_err("%s: Invalid input: L_core=%d, LL_core=%d, opp=%d\n",
			__func__, L_core, LL_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (L_core == 0 && LL_core == 0)
		return 0;

#if PPM_COBRA_NEED_OPP_MAPPING
	cur_opp = freq_idx_mapping_tbl[opp];
	prev_opp = freq_idx_mapping_tbl[opp+1];
#else
	cur_opp = opp;
	prev_opp = opp + 1;
#endif
	cur_pwr = (L_core) ? cobra_tbl.basic_pwr_tbl[idx_L+L_core-1][cur_opp].power_idx : 0; /* L */
	cur_pwr += (LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].power_idx : 0; /* L+LL */

	if (opp == COBRA_OPP_NUM - 1) {
		prev_pwr = (L_core) ? ((L_core > 1) ? (cobra_tbl.basic_pwr_tbl[idx_L+L_core-2][cur_opp].power_idx +
			((LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].power_idx : 0))
			: ((LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].power_idx : 0))
			: ((LL_core > 1) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-2][cur_opp].power_idx : 0);
	} else {
		prev_pwr = (L_core) ? cobra_tbl.basic_pwr_tbl[idx_L+L_core-1][prev_opp].power_idx : 0;
		prev_pwr += (LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][prev_opp].power_idx : 0;
	}

	delta_pwr = cur_pwr - prev_pwr;

	return delta_pwr;
#else
	return cobra_tbl.delta_tbl_LxLL[L_core][LL_core][opp].delta_pwr;
#endif
}

static int get_delta_perf_LxLL(unsigned int L_core, unsigned int LL_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	unsigned int idx_L = get_cluster_max_cpu_core(PPM_CLUSTER_LL);
	unsigned int idx_LL = 0;
	unsigned int cur_opp, prev_opp, cur_perf, prev_perf;
	int delta_perf;
#endif

	if (L_core > get_cluster_max_cpu_core(PPM_CLUSTER_L)
		|| LL_core > get_cluster_max_cpu_core(PPM_CLUSTER_LL)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_L)) {
		ppm_err("%s: Invalid input: L_core=%d, LL_core=%d, opp=%d\n",
			__func__, L_core, LL_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (L_core == 0 && LL_core == 0)
		return 0;

#if PPM_COBRA_NEED_OPP_MAPPING
	cur_opp = freq_idx_mapping_tbl[opp];
	prev_opp = freq_idx_mapping_tbl[opp+1];
#else
	cur_opp = opp;
	prev_opp = opp + 1;
#endif
	cur_perf = (L_core) ? cobra_tbl.basic_pwr_tbl[idx_L+L_core-1][cur_opp].perf_idx : 0; /* L */
	cur_perf += (LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].perf_idx : 0; /* L+LL */

	if (opp == COBRA_OPP_NUM - 1) {
		prev_perf = (L_core) ? ((L_core > 1) ? (cobra_tbl.basic_pwr_tbl[idx_L+L_core-2][cur_opp].perf_idx +
			((LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].perf_idx : 0))
			: ((LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][cur_opp].perf_idx : 0))
			: ((LL_core > 1) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-2][cur_opp].perf_idx : 0);
	} else {
		prev_perf = (L_core) ? cobra_tbl.basic_pwr_tbl[idx_L+L_core-1][prev_opp].perf_idx : 0;
		prev_perf += (LL_core) ? cobra_tbl.basic_pwr_tbl[idx_LL+LL_core-1][prev_opp].perf_idx : 0;
	}

	delta_perf = cur_perf - prev_perf;

	return delta_perf;
#else
	return cobra_tbl.delta_tbl_LxLL[L_core][LL_core][opp].delta_perf;
#endif
}

static int get_delta_eff_LxLL(unsigned int L_core, unsigned int LL_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	int delta_pwr, delta_perf, delta_eff;
#endif

	if (L_core > get_cluster_max_cpu_core(PPM_CLUSTER_L)
		|| LL_core > get_cluster_max_cpu_core(PPM_CLUSTER_LL)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_L)) {
		ppm_err("%s: Invalid input: L_core=%d, LL_core=%d, opp=%d\n",
			__func__, L_core, LL_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (L_core == 0 && LL_core == 0)
		return 0;

	delta_pwr = get_delta_pwr_LxLL(L_core, LL_core, opp);
	delta_perf = get_delta_perf_LxLL(L_core, LL_core, opp);

	if (opp == COBRA_OPP_NUM - 1)
		/* x10 to make it hard to turn off cores */
		delta_eff = (delta_perf * 1000) / delta_pwr;
	else
		delta_eff = (delta_perf * 100) / delta_pwr;

	return delta_eff;
#else
	return cobra_tbl.delta_tbl_LxLL[L_core][LL_core][opp].delta_eff;
#endif
}

static int get_delta_pwr_B(unsigned int B_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	unsigned int idx = get_cluster_max_cpu_core(PPM_CLUSTER_LL) + get_cluster_max_cpu_core(PPM_CLUSTER_L);
	unsigned int cur_opp, prev_opp;
	int delta_pwr;
#endif

	if (B_core > get_cluster_max_cpu_core(PPM_CLUSTER_B)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_B)) {
		ppm_err("%s: Invalid input: B_core=%d, opp=%d\n", __func__, B_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (B_core == 0)
		return 0;

#if PPM_COBRA_NEED_OPP_MAPPING
	cur_opp = freq_idx_mapping_tbl_big[opp];
	prev_opp = freq_idx_mapping_tbl_big[opp+1];
#else
	cur_opp = opp;
	prev_opp = opp + 1;
#endif

	if (opp == COBRA_OPP_NUM - 1) {
		delta_pwr = (B_core == 1)
			? cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].power_idx
			: (cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].power_idx -
			cobra_tbl.basic_pwr_tbl[idx+B_core-2][cur_opp].power_idx);
	} else {
		delta_pwr = cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].power_idx -
			cobra_tbl.basic_pwr_tbl[idx+B_core-1][prev_opp].power_idx;
	}

	return delta_pwr;
#else
	return cobra_tbl.delta_tbl_B[B_core][opp].delta_pwr;
#endif
}

static int get_delta_perf_B(unsigned int B_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	unsigned int idx = get_cluster_max_cpu_core(PPM_CLUSTER_LL) + get_cluster_max_cpu_core(PPM_CLUSTER_L);
	unsigned int cur_opp, prev_opp;
	int delta_perf;
#endif

	if (B_core > get_cluster_max_cpu_core(PPM_CLUSTER_B)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_B)) {
		ppm_err("%s: Invalid input: B_core=%d, opp=%d\n", __func__, B_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (B_core == 0)
		return 0;

#if PPM_COBRA_NEED_OPP_MAPPING
	cur_opp = freq_idx_mapping_tbl_big[opp];
	prev_opp = freq_idx_mapping_tbl_big[opp+1];
#else
	cur_opp = opp;
	prev_opp = opp + 1;
#endif

	if (opp == COBRA_OPP_NUM - 1) {
		delta_perf = (B_core == 1)
			? cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].perf_idx
			: (cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].perf_idx -
			cobra_tbl.basic_pwr_tbl[idx+B_core-2][cur_opp].perf_idx);
	} else {
		delta_perf = cobra_tbl.basic_pwr_tbl[idx+B_core-1][cur_opp].perf_idx -
			cobra_tbl.basic_pwr_tbl[idx+B_core-1][prev_opp].perf_idx;
	}

	return delta_perf;
#else
	return cobra_tbl.delta_tbl_B[B_core][opp].delta_perf;
#endif
}

static int get_delta_eff_B(unsigned int B_core, unsigned int opp)
{
#if PPM_COBRA_RUNTIME_CALC_DELTA
	int delta_pwr, delta_perf, delta_eff;
#endif

	if (B_core > get_cluster_max_cpu_core(PPM_CLUSTER_B)
		|| opp > get_cluster_min_cpufreq_idx(PPM_CLUSTER_B)) {
		ppm_err("%s: Invalid input: B_core=%d, opp=%d\n", __func__, B_core, opp);
		BUG();
	}

#if PPM_COBRA_RUNTIME_CALC_DELTA
	if (B_core == 0)
		return 0;

	delta_pwr = get_delta_pwr_B(B_core, opp);
	delta_perf = get_delta_perf_B(B_core, opp);

	if (opp == COBRA_OPP_NUM - 1)
		/* x10 to make it hard to turn off cores */
		delta_eff = (delta_perf * 1000) / delta_pwr;
	else
		delta_eff = (delta_perf * 100) / delta_pwr;

	return delta_eff;
#else
	return cobra_tbl.delta_tbl_B[B_core][opp].delta_eff;
#endif
}

void ppm_cobra_update_core_limit(unsigned int cluster, int limit)
{
	if (cluster >= NR_PPM_CLUSTERS) {
		ppm_err("%s: Invalid cluster id = %d\n", __func__, cluster);
		BUG();
	}

	if (limit < 0 || limit > get_cluster_max_cpu_core(cluster)) {
		ppm_err("%s: Invalid core limit for cluster%d = %d\n", __func__, cluster, limit);
		BUG();
	}

	Core_limit[cluster] = limit;
}

void ppm_cobra_update_freq_limit(unsigned int cluster, int limit)
{
	if (cluster >= NR_PPM_CLUSTERS) {
		ppm_err("%s: Invalid cluster id = %d\n", __func__, cluster);
		BUG();
	}

	if (limit < 0 || limit > get_cluster_min_cpufreq_idx(cluster)) {
		ppm_err("%s: Invalid freq limit for cluster%d = %d\n", __func__, cluster, limit);
		BUG();
	}

	prev_max_cpufreq_idx[cluster] = limit;
}

void ppm_cobra_update_limit(enum ppm_power_state new_state, void *user_req)
{
	struct ppm_policy_req *req;
	int power_budget;
	int opp[NR_PPM_CLUSTERS];
	int active_core[NR_PPM_CLUSTERS];
#if PPM_COBRA_USE_CORE_LIMIT
	int core_limit_tmp[NR_PPM_CLUSTERS];
#endif
	int i;
	struct cpumask cluster_cpu, online_cpu;
	int delta_power;
	int LxLL;
	/* Get power index of current OPP */
	int curr_power = 0;
	struct ppm_cluster_status cl_status[NR_PPM_CLUSTERS];
	int LxLLisLimited = 0;

	/* skip if DVFS is not ready (we cannot get current freq...) */
	if (!ppm_main_info.client_info[PPM_CLIENT_DVFS].limit_cb)
		return;

	if (!user_req)
		return;

	req = (struct ppm_policy_req *)user_req;
	power_budget = req->power_budget;

	if (power_budget >= ppm_get_max_pwr_idx())
		return;

	ppm_dbg(COBRA_ALGO, "[PREV]Core_Limit=%d%d%d, policy_limit=%d%d%d, state=%s\n",
			Core_limit[PPM_CLUSTER_LL],
			Core_limit[PPM_CLUSTER_L],
			Core_limit[PPM_CLUSTER_B],
			req->limit[PPM_CLUSTER_LL].max_cpu_core,
			req->limit[PPM_CLUSTER_L].max_cpu_core,
			req->limit[PPM_CLUSTER_B].max_cpu_core,
			ppm_get_power_state_name(new_state));

	for_each_ppm_clusters(i) {
		arch_get_cluster_cpus(&cluster_cpu, i);
		cpumask_and(&online_cpu, &cluster_cpu, cpu_online_mask);

		cl_status[i].core_num = cpumask_weight(&online_cpu);
		cl_status[i].volt = 0;	/* don't care */
		if (!cl_status[i].core_num)
			cl_status[i].freq_idx = -1;
		else
			cl_status[i].freq_idx = ppm_main_freq_to_idx(i,
					mt_cpufreq_get_cur_phy_freq_no_lock(i), CPUFREQ_RELATION_L);

		ppm_ver("[%d] core = %d, freq_idx = %d\n",
			i, cl_status[i].core_num, cl_status[i].freq_idx);
	}

#if PPM_COBRA_USE_CORE_LIMIT
	for_each_ppm_clusters(i) {
		if (cl_status[i].core_num > Core_limit[i])
			cl_status[i].core_num = Core_limit[i];
		if (req->limit[i].max_cpu_core < Core_limit[i])
			Core_limit[i] = req->limit[i].max_cpu_core;
	}
#endif
	if (cl_status[PPM_CLUSTER_LL].core_num == 0 && cl_status[PPM_CLUSTER_L].core_num == 0) {
		if (Core_limit[PPM_CLUSTER_LL] > 0) {
			cl_status[PPM_CLUSTER_LL].core_num = 1;
			cl_status[PPM_CLUSTER_LL].freq_idx = get_cluster_max_cpufreq_idx(PPM_CLUSTER_LL);
		} else {
			cl_status[PPM_CLUSTER_L].core_num = 1;
			cl_status[PPM_CLUSTER_L].freq_idx = get_cluster_max_cpufreq_idx(PPM_CLUSTER_L);
		}
	}

	/* use L cluster frequency */
	if (cl_status[PPM_CLUSTER_L].core_num > 0)
		cl_status[PPM_CLUSTER_LL].freq_idx = cl_status[PPM_CLUSTER_L].freq_idx;

	/* check LxLL is limited or not */
	if (cl_status[PPM_CLUSTER_LL].freq_idx <= PREV_FREQ_LIMIT(LL)
		|| cl_status[PPM_CLUSTER_L].freq_idx <= PREV_FREQ_LIMIT(L))
		LxLLisLimited = 1;

	curr_power = ppm_find_pwr_idx(cl_status);
	if (curr_power < 0)
		curr_power = mt_ppm_thermal_get_max_power();
	delta_power = power_budget - curr_power;

	for_each_ppm_clusters(i) {
#if PPM_COBRA_NEED_OPP_MAPPING
		int *tbl = (i == PPM_CLUSTER_B) ? freq_idx_mapping_tbl_big_r : freq_idx_mapping_tbl_r;

		/* convert current OPP(frequency only) from 16 to 8 */
		opp[i] = (cl_status[i].freq_idx >= 0)
			? tbl[cl_status[i].freq_idx] : -1;
#else
		opp[i] = cl_status[i].freq_idx;
#endif

		/* Get Active Core number of each cluster */
		active_core[i] = (cl_status[i].core_num >= 0) ? cl_status[i].core_num : 0;

#if PPM_COBRA_USE_CORE_LIMIT
		core_limit_tmp[i] = Core_limit[i];
		req->limit[i].max_cpu_core = core_limit_tmp[i];
#endif
	}

	/* Which Cluster in L and LL is active (1: L is on, 0: LL is on, 2: LL/L is off) */
	LxLL = (ACT_CORE(L) > 0) ? 1 : ((ACT_CORE(LL) > 0) ? 0 : 2);

	ppm_dbg(COBRA_ALGO,
		"[IN](bgt/delta/cur)=(%d/%d/%d), (opp/act/c_lmt/f_lmt)=(%d,%d,%d/%d%d%d/%d%d%d/%d,%d,%d)\n",
				power_budget, delta_power, curr_power,
				opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B],
				ACT_CORE(LL), ACT_CORE(L), ACT_CORE(B),
				CORE_LIMIT(LL), CORE_LIMIT(L), CORE_LIMIT(B),
				PREV_FREQ_LIMIT(LL), PREV_FREQ_LIMIT(L), PREV_FREQ_LIMIT(B));

	/* increase ferquency limit */
	if (delta_power >= 0) {
		while (1) {
			int ChoosenCl = -1, MaxEff = 0, ChoosenPwr = 0;
			int target_delta_pwr, target_delta_eff;

			/* give remaining power to big if LxLL opp is 0 */
			if (opp[LxLL] == 0 && ACT_CORE(B) == 0) {
				target_delta_pwr = get_delta_pwr_B(1, COBRA_OPP_NUM-1);
				if (delta_power >= target_delta_pwr) {
					ACT_CORE(B) = 1;
					delta_power -= target_delta_pwr;
					opp[PPM_CLUSTER_B] = COBRA_OPP_NUM - 1;
				}
			}

			/* Big Cluster */
			if (ACT_CORE(B) > 0 && opp[PPM_CLUSTER_B] > 0) {
				target_delta_pwr = get_delta_pwr_B(ACT_CORE(B), opp[PPM_CLUSTER_B]-1);
				if (delta_power >= target_delta_pwr) {
					MaxEff = get_delta_eff_B(ACT_CORE(B), opp[PPM_CLUSTER_B]-1);
					ChoosenCl = PPM_CLUSTER_B;
					ChoosenPwr = target_delta_pwr;
				}
			}

			/* LxLL Cluster */
			if (LxLL < 2 && opp[LxLL] > 0
				&& (LxLLisLimited || opp[PPM_CLUSTER_B] == 0 || ACT_CORE(B) == 0)) {
				target_delta_pwr = get_delta_pwr_LxLL(ACT_CORE(L), ACT_CORE(LL), opp[LxLL]-1);
				target_delta_eff = get_delta_eff_LxLL(ACT_CORE(L), ACT_CORE(LL), opp[LxLL]-1);
				if (delta_power >= target_delta_pwr && MaxEff <= target_delta_eff) {
					MaxEff = target_delta_eff;
					ChoosenCl = 1;
					ChoosenPwr = target_delta_pwr;
				}
			}

			if (ChoosenCl != -1)
				goto prepare_next_round;

			/* exceed power budget or all active core is highest freq. */
#if PPM_COBRA_USE_CORE_LIMIT
			/* no enough budget */
			if (opp[LxLL] != 0)
				goto end;

			/* PPM state L_ONLY --> LL core remain turned off */
			if (new_state != PPM_POWER_STATE_L_ONLY) {
				while (CORE_LIMIT(LL) < get_cluster_max_cpu_core(PPM_CLUSTER_LL)) {
					target_delta_pwr = get_delta_pwr_LxLL(ACT_CORE(L),
							CORE_LIMIT(LL)+1, COBRA_OPP_NUM-1);
					if (delta_power < target_delta_pwr)
						break;

					delta_power -= target_delta_pwr;
					req->limit[PPM_CLUSTER_LL].max_cpu_core = ++CORE_LIMIT(LL);
				}
			}
			/* PPM state LL_ONLY --> L core remain turned off */
			if (new_state != PPM_POWER_STATE_LL_ONLY) {
				while (CORE_LIMIT(L) < get_cluster_max_cpu_core(PPM_CLUSTER_L)) {
					target_delta_pwr = get_delta_pwr_LxLL(CORE_LIMIT(L)+1,
							ACT_CORE(LL), COBRA_OPP_NUM-1);
					if (delta_power < target_delta_pwr)
						break;

					delta_power -= target_delta_pwr;
					req->limit[PPM_CLUSTER_L].max_cpu_core = ++CORE_LIMIT(L);
				}
			}
			/* PPM state L_ONLY or LL_ONLY--> B core remain turned off */
			if (new_state != PPM_POWER_STATE_LL_ONLY && new_state != PPM_POWER_STATE_L_ONLY) {
				while (CORE_LIMIT(B) < get_cluster_max_cpu_core(PPM_CLUSTER_B)) {
					target_delta_pwr = get_delta_pwr_B(CORE_LIMIT(B)+1, COBRA_OPP_NUM-1);
					if (delta_power < target_delta_pwr)
						break;

					delta_power -= target_delta_pwr;
					req->limit[PPM_CLUSTER_B].max_cpu_core = ++CORE_LIMIT(B);
				}
			}
end:
#endif
			ppm_dbg(COBRA_ALGO, "[+]ChoosenCl=-1! delta=%d, (opp/c_lmt)=(%d,%d,%d/%d%d%d)\n",
				delta_power, opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B],
				CORE_LIMIT(LL), CORE_LIMIT(L), CORE_LIMIT(B));

			break;

prepare_next_round:
			/* if choose Cluster LxLL, change opp of active cluster only */
			if (ChoosenCl == 1) {
				if (ACT_CORE(L) > 0)
					opp[PPM_CLUSTER_L] -= 1;
				if (ACT_CORE(LL) > 0)
					opp[PPM_CLUSTER_LL] -= 1;
			} else
				opp[ChoosenCl] -= 1;

			delta_power -= ChoosenPwr;

			ppm_dbg(COBRA_ALGO, "[+](delta/MaxEff/Cl/Pwr)=(%d,%d,%d,%d), opp=%d,%d,%d\n",
					delta_power, MaxEff, ChoosenCl, ChoosenPwr,
					opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B]);
		}
	} else {
		while (delta_power < 0) {
			int ChoosenCl = -1;
			int MinEff = 10000;	/* should be bigger than max value of efficiency_* array */
			int ChoosenPwr = 0;
			int target_delta_eff;

			/* B */
			if (ACT_CORE(B) > 0 && opp[PPM_CLUSTER_B] < PPM_COBRA_MAX_FREQ_IDX) {
				MinEff = get_delta_eff_B(ACT_CORE(B), opp[PPM_CLUSTER_B]);
				ChoosenCl = PPM_CLUSTER_B;
				ChoosenPwr = get_delta_pwr_B(ACT_CORE(B), opp[PPM_CLUSTER_B]);
			}

			/* LxLL */
			if (LxLL < 2 && opp[LxLL] < PPM_COBRA_MAX_FREQ_IDX) {
				target_delta_eff = get_delta_eff_LxLL(ACT_CORE(L), ACT_CORE(LL), opp[LxLL]);
				if (MinEff > target_delta_eff) {
					MinEff = target_delta_eff;
					ChoosenCl = 1;
					ChoosenPwr = get_delta_pwr_LxLL(ACT_CORE(L), ACT_CORE(LL), opp[LxLL]);
				}
			}

			if (ChoosenCl == -1) {
				ppm_err("No lower OPP!(bgt/delta/cur)=(%d/%d/%d),(opp/act)=(%d,%d,%d/%d%d%d)\n",
					power_budget, delta_power, curr_power,
					opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B],
					ACT_CORE(LL), ACT_CORE(L), ACT_CORE(B));
				break;
			}

			/* if choose Cluster LxLL, change opp of active cluster only */
			if (ChoosenCl == 1) {
				if (ACT_CORE(L) > 0)
					opp[PPM_CLUSTER_L] += 1;
				if (ACT_CORE(LL) > 0)
					opp[PPM_CLUSTER_LL] += 1;
			} else
				opp[ChoosenCl] += 1;

			/* Turned off core */
#if PPM_COBRA_USE_CORE_LIMIT
			if (opp[PPM_CLUSTER_B] == PPM_COBRA_MAX_FREQ_IDX && ACT_CORE(B) > 0) {
				req->limit[PPM_CLUSTER_B].max_cpu_core = --ACT_CORE(B);
				opp[PPM_CLUSTER_B] = PPM_COBRA_MAX_FREQ_IDX - 1;
			} else if (opp[LxLL] == PPM_COBRA_MAX_FREQ_IDX) {
				if (ACT_CORE(L) > 1 || (ACT_CORE(LL) > 0 && ACT_CORE(L) > 0))
					req->limit[PPM_CLUSTER_L].max_cpu_core = --ACT_CORE(L);
				else if (ACT_CORE(LL) > 1)
					req->limit[PPM_CLUSTER_LL].max_cpu_core = --ACT_CORE(LL);
				if (ACT_CORE(L) > 0)
					opp[PPM_CLUSTER_L] = PPM_COBRA_MAX_FREQ_IDX - 1;
				else
					LxLL = 0;
				if (ACT_CORE(LL) > 0)
					opp[PPM_CLUSTER_LL] = PPM_COBRA_MAX_FREQ_IDX - 1;
			}
#endif

			delta_power += ChoosenPwr;
			curr_power -= ChoosenPwr;

			ppm_dbg(COBRA_ALGO, "[-](delta/MinEff/Cl/Pwr)=(%d,%d,%d,%d), (opp/act)=(%d,%d,%d/%d%d%d)\n",
					delta_power, MinEff, ChoosenCl, ChoosenPwr,
					opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B],
					ACT_CORE(LL), ACT_CORE(L), ACT_CORE(B));
		}
	}

	/* Set frequency limit */
	/* For non share buck */
#if 0
	if (opp[PPM_CLUSTER_LL] >= 0 && ACT_CORE(LL) > 0)
		req->limit[PPM_CLUSTER_LL].max_cpufreq_idx = freq_idx_mapping_tbl[opp[PPM_CLUSTER_LL]];
	if (opp[PPM_CLUSTER_L] >= 0 && ACT_CORE(L) > 0)
		req->limit[PPM_CLUSTER_L].max_cpufreq_idx = freq_idx_mapping_tbl[opp[PPM_CLUSTER_L]];
#endif

	/* Set all frequency limit of the cluster */
	/* Set OPP of Cluser n to opp[n] */
	for_each_ppm_clusters(i) {
		if (i == PPM_CLUSTER_B) {
			if (opp[i] >= 0 && ACT_CORE(B) > 0)
#if PPM_COBRA_NEED_OPP_MAPPING
				req->limit[i].max_cpufreq_idx = freq_idx_mapping_tbl_big[opp[i]];
#else
				req->limit[i].max_cpufreq_idx = opp[i];
#endif
			else
				req->limit[i].max_cpufreq_idx = get_cluster_min_cpufreq_idx(i);
		} else
#if PPM_COBRA_NEED_OPP_MAPPING
			req->limit[i].max_cpufreq_idx = freq_idx_mapping_tbl[opp[LxLL]];
#else
			req->limit[i].max_cpufreq_idx = opp[LxLL];
#endif
	}

	ppm_dbg(COBRA_ALGO, "[OUT]delta=%d, (opp/act/c_lmt/f_lmt)=(%d,%d,%d/%d%d%d/%d%d%d/%d,%d,%d)\n",
				delta_power,
				opp[PPM_CLUSTER_LL], opp[PPM_CLUSTER_L], opp[PPM_CLUSTER_B],
				ACT_CORE(LL), ACT_CORE(L), ACT_CORE(B),
				req->limit[PPM_CLUSTER_LL].max_cpu_core,
				req->limit[PPM_CLUSTER_L].max_cpu_core,
				req->limit[PPM_CLUSTER_B].max_cpu_core,
				req->limit[PPM_CLUSTER_LL].max_cpufreq_idx,
				req->limit[PPM_CLUSTER_L].max_cpufreq_idx,
				req->limit[PPM_CLUSTER_B].max_cpufreq_idx);

	/* error check */
	for_each_ppm_clusters(i) {
		if (req->limit[i].max_cpufreq_idx > req->limit[i].min_cpufreq_idx)
			req->limit[i].min_cpufreq_idx = req->limit[i].max_cpufreq_idx;
		if (req->limit[i].max_cpu_core < req->limit[i].min_cpu_core)
			req->limit[i].min_cpu_core = req->limit[i].max_cpu_core;
	}
}

void ppm_cobra_init(void)
{
	int i, j;
#if !PPM_COBRA_RUNTIME_CALC_DELTA
	int k;
#endif
	struct ppm_pwr_idx_ref_tbl_data pwr_ref_tbl = ppm_get_pwr_idx_ref_tbl();
	struct ppm_power_state_data *state_info = ppm_get_power_state_info();

#if PPM_COBRA_NEED_OPP_MAPPING
	if (ppm_main_info.dvfs_tbl_type == DVFS_TABLE_TYPE_SB) {
		freq_idx_mapping_tbl = freq_idx_mapping_tbl_sb;
		freq_idx_mapping_tbl_r = freq_idx_mapping_tbl_sb_r;
		freq_idx_mapping_tbl_big = freq_idx_mapping_tbl_sb_big;
		freq_idx_mapping_tbl_big_r = freq_idx_mapping_tbl_sb_big_r;
	} else {
		freq_idx_mapping_tbl = freq_idx_mapping_tbl_fy;
		freq_idx_mapping_tbl_r = freq_idx_mapping_tbl_fy_r;
		freq_idx_mapping_tbl_big = freq_idx_mapping_tbl_fy_big;
		freq_idx_mapping_tbl_big_r = freq_idx_mapping_tbl_fy_big_r;
	}
#endif

	/* generate basic power table for EAS */
	ppm_info("basic power table:\n");
	for (i = 0; i < TOTAL_CORE_NUM; i++) {
		for (j = 0; j < DVFS_OPP_NUM; j++) {
			int *perf_ref_tbl = ppm_get_perf_idx_ref_tbl(i/4);
			unsigned char core = (i % 4) + 1;

			if (!perf_ref_tbl)
				BUG();

			cobra_tbl.basic_pwr_tbl[i][j].power_idx =
				pwr_ref_tbl.pwr_idx_ref_tbl[i/4].core_total_power[j] * core +
				pwr_ref_tbl.pwr_idx_ref_tbl[i/4].l2_power[j];
			cobra_tbl.basic_pwr_tbl[i][j].perf_idx =	perf_ref_tbl[j] * core;

			ppm_info("[%d][%d] = (%d, %d)\n", i, j,
				cobra_tbl.basic_pwr_tbl[i][j].power_idx,
				cobra_tbl.basic_pwr_tbl[i][j].perf_idx);
		}
	}

	/* decide min_pwr_idx and max_perf_idx for each state */
	for_each_ppm_power_state(i) {
		for_each_ppm_clusters(j) {
			int max_core = state_info[i].cluster_limit->state_limit[j].max_cpu_core;
			int min_core = state_info[i].cluster_limit->state_limit[j].min_cpu_core;
			int idx = 4 * j;

			if (max_core > get_cluster_max_cpu_core(j) || min_core < get_cluster_min_cpu_core(j))
				continue;

			state_info[i].min_pwr_idx += (min_core)
				? cobra_tbl.basic_pwr_tbl[idx+min_core-1][DVFS_OPP_NUM-1].power_idx : 0;
			state_info[i].max_perf_idx += (max_core)
				? cobra_tbl.basic_pwr_tbl[idx+max_core-1][0].perf_idx : 0;
		}
		ppm_info("%s: min_pwr_idx = %d, max_perf_idx = %d\n", state_info[i].name,
			state_info[i].min_pwr_idx, state_info[i].max_perf_idx);
	}

#if !PPM_COBRA_RUNTIME_CALC_DELTA
	/* generate delta power and delta perf table for B (non-shared buck)*/
	ppm_info("Big delta table:\n");
	for (i = 0; i <= get_cluster_max_cpu_core(PPM_CLUSTER_B); i++) {
		for (j = 0; j < COBRA_OPP_NUM; j++) {
			int idx_B = get_cluster_max_cpu_core(PPM_CLUSTER_LL) +
				get_cluster_max_cpu_core(PPM_CLUSTER_L);
#if PPM_COBRA_NEED_OPP_MAPPING
			int opp = freq_idx_mapping_tbl_big[j];
#else
			int opp = j;
#endif
			int prev_opp;

			if (i == 0) {
				cobra_tbl.delta_tbl_B[i][j].delta_pwr = 0;
				cobra_tbl.delta_tbl_B[i][j].delta_perf = 0;
				cobra_tbl.delta_tbl_B[i][j].delta_eff = 0;

				ppm_info("[%d][%d] = (0, 0, 0)\n", i, j);
				continue;
			}

			if (j == COBRA_OPP_NUM - 1) {
				cobra_tbl.delta_tbl_B[i][j].delta_pwr = (i == 1)
					? cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].power_idx
					: (cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].power_idx -
					cobra_tbl.basic_pwr_tbl[idx_B+i-2][opp].power_idx);
				cobra_tbl.delta_tbl_B[i][j].delta_perf = (i == 1)
					? cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].perf_idx
					: (cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].perf_idx -
					cobra_tbl.basic_pwr_tbl[idx_B+i-2][opp].perf_idx);
				/* x10 to make it hard to turn off cores */
				cobra_tbl.delta_tbl_B[i][j].delta_eff =
					(cobra_tbl.delta_tbl_B[i][j].delta_perf * 1000) /
					cobra_tbl.delta_tbl_B[i][j].delta_pwr;
			} else {
#if PPM_COBRA_NEED_OPP_MAPPING
				prev_opp = freq_idx_mapping_tbl_big[j+1];
#else
				prev_opp = j+1;
#endif
				cobra_tbl.delta_tbl_B[i][j].delta_pwr =
					cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].power_idx -
					cobra_tbl.basic_pwr_tbl[idx_B+i-1][prev_opp].power_idx;
				cobra_tbl.delta_tbl_B[i][j].delta_perf =
					cobra_tbl.basic_pwr_tbl[idx_B+i-1][opp].perf_idx -
					cobra_tbl.basic_pwr_tbl[idx_B+i-1][prev_opp].perf_idx;
				cobra_tbl.delta_tbl_B[i][j].delta_eff =
					(cobra_tbl.delta_tbl_B[i][j].delta_perf * 100) /
					cobra_tbl.delta_tbl_B[i][j].delta_pwr;
			}

			ppm_info("[%d][%d] = (%d, %d, %d)\n", i, j,
				cobra_tbl.delta_tbl_B[i][j].delta_pwr,
				cobra_tbl.delta_tbl_B[i][j].delta_perf,
				cobra_tbl.delta_tbl_B[i][j].delta_eff);
		}
	}

	/* generate delta power and delta perf table for LxLL */
	ppm_info("LxLL delta table:\n");
	for (i = 0; i <= get_cluster_max_cpu_core(PPM_CLUSTER_L); i++) {
		for (j = 0; j <= get_cluster_max_cpu_core(PPM_CLUSTER_LL); j++) {
			for (k = 0; k < COBRA_OPP_NUM; k++) {
				int idx_L = get_cluster_max_cpu_core(PPM_CLUSTER_LL);
				int idx_LL = 0;
#if PPM_COBRA_NEED_OPP_MAPPING
				int opp = freq_idx_mapping_tbl[k];
#else
				int opp = k;
#endif
				int cur_pwr, cur_perf, prev_pwr, prev_perf, prev_opp;

				if (i == 0 && j == 0) {
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr = 0;
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf = 0;
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_eff = 0;

					ppm_info("[%d][%d][%d] = (0, 0, 0)\n", i, j, k);
					continue;
				}

				cur_pwr = (i) ? cobra_tbl.basic_pwr_tbl[idx_L+i-1][opp].power_idx : 0; /* L */
				cur_pwr += (j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].power_idx : 0; /* L+LL */
				cur_perf = (i) ? cobra_tbl.basic_pwr_tbl[idx_L+i-1][opp].perf_idx : 0; /* L */
				cur_perf += (j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].perf_idx : 0; /* L+LL */

				if (k == COBRA_OPP_NUM - 1) {
					prev_pwr = (i) ?
						((i > 1) ? (cobra_tbl.basic_pwr_tbl[idx_L+i-2][opp].power_idx +
						((j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].power_idx : 0))
						: ((j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].power_idx : 0))
						: ((j > 1) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-2][opp].power_idx : 0);
					prev_perf = (i) ?
						((i > 1) ? (cobra_tbl.basic_pwr_tbl[idx_L+i-2][opp].perf_idx +
						((j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].perf_idx : 0))
						: ((j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][opp].perf_idx : 0))
						: ((j > 1) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-2][opp].perf_idx : 0);

					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr = cur_pwr - prev_pwr;
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf = cur_perf - prev_perf;
					/* x10 to make it hard to turn off cores */
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_eff =
						(cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf * 1000) /
						cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr;
				} else {
#if PPM_COBRA_NEED_OPP_MAPPING
					prev_opp = freq_idx_mapping_tbl[k+1];
#else
					prev_opp = k+1;
#endif
					prev_pwr = (i) ? cobra_tbl.basic_pwr_tbl[idx_L+i-1][prev_opp].power_idx : 0;
					prev_pwr += (j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][prev_opp].power_idx : 0;
					prev_perf = (i) ? cobra_tbl.basic_pwr_tbl[idx_L+i-1][prev_opp].perf_idx : 0;
					prev_perf += (j) ? cobra_tbl.basic_pwr_tbl[idx_LL+j-1][prev_opp].perf_idx : 0;

					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr = cur_pwr - prev_pwr;
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf = cur_perf - prev_perf;
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_eff =
						(cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf * 100) /
						cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr;
				}

				ppm_info("[%d][%d][%d] = (%d, %d, %d)\n", i, j, k,
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_pwr,
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_perf,
					cobra_tbl.delta_tbl_LxLL[i][j][k].delta_eff);
			}
		}
	}
#endif

	ppm_info("ET init done!\n");
}


void ppm_cobra_dump_tbl(struct seq_file *m)
{
	int i, j, k;

	seq_puts(m, "\n==========================================\n");
	seq_puts(m, "basic power table (pwr, perf)");
	seq_puts(m, "\n==========================================\n");
	for (i = 0; i < TOTAL_CORE_NUM; i++) {
		for (j = 0; j < DVFS_OPP_NUM; j++) {
			seq_printf(m, "[%d][%d] = (%d, %d)\n", i, j,
				cobra_tbl.basic_pwr_tbl[i][j].power_idx,
				cobra_tbl.basic_pwr_tbl[i][j].perf_idx);
		}
	}

	if (ppm_debug > 0) {
		seq_puts(m, "\n==================================================\n");
		seq_puts(m, "Big delta table (delta_pwr, delta_perf, delta_eff)");
		seq_puts(m, "\n==================================================\n");
		for (i = 0; i <= get_cluster_max_cpu_core(PPM_CLUSTER_B); i++) {
			for (j = 0; j < COBRA_OPP_NUM; j++) {
				seq_printf(m, "[%d][%d] = (%d, %d, %d)\n", i, j,
					get_delta_pwr_B(i, j),
					get_delta_perf_B(i, j),
					get_delta_eff_B(i, j));
			}
		}

		seq_puts(m, "\n==================================================\n");
		seq_puts(m, "LxLL delta table (delta_pwr, delta_perf, delta_eff)");
		seq_puts(m, "\n==================================================\n");
		for (i = 0; i <= get_cluster_max_cpu_core(PPM_CLUSTER_L); i++) {
			for (j = 0; j <= get_cluster_max_cpu_core(PPM_CLUSTER_LL); j++) {
				for (k = 0; k < COBRA_OPP_NUM; k++) {
					seq_printf(m, "[%d][%d][%d] = (%d, %d, %d)\n", i, j, k,
						get_delta_pwr_LxLL(i, j, k),
						get_delta_perf_LxLL(i, j, k),
						get_delta_eff_LxLL(i, j, k));
				}
			}
		}
	}
}

