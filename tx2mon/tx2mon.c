// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Marvell International Ltd.
 */

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <term.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mc_oper_region.h"

/* Handle older linux headers */
#ifndef SMCCC_RET_SUCCESS
#define SMCCC_RET_SUCCESS	0
#endif

#define VERSION_MAJOR_NUM	1
#define VERSION_MINOR_NUM	4
#define CORES_PER_ROW		4

#define PATH_T99MON_DEV		"/sys/bus/platform/devices/tx2mon"
#define PATH_T99MON_NODE0	PATH_T99MON_DEV "/node0_raw"
#define PATH_T99MON_NODE1	PATH_T99MON_DEV "/node1_raw"
#define PATH_T99MON_SOCINFO	PATH_T99MON_DEV "/socinfo"

static struct termios *ts_saved;
static int interactive = 1;
static int display_extra = 0;
static char *out_filename;
static struct timeval delay = { .tv_sec = 1 };

static struct term_seq {
	char *cl;
	char *nl;
} term_seq;

struct node_data {
	int	fd;
	int	cores;
	int	node;
	struct	mc_oper_region buf;
};

struct tx2mon {
	int		nodes;
	int		samples;
	FILE 		*fileout;
	volatile int	stop;
	struct	node_data node[2];
};
static struct tx2mon *tx2mon;

static void cleanup(void);
static int fail(char *str);
static int fail_err(char *str, int err);

static inline double cpu_temp(struct node_data *d, int c)
{
	return to_c(d->buf.tmon_cpu[c]);
}

static inline unsigned int cpu_freq(struct node_data *d, int c)
{
	return d->buf.freq_cpu[c];
}

static inline double to_v(int mv)
{
	return mv/1000.0;
}

static inline double to_w(int mw)
{
	return mw/1000.0;
}

static void clearscreen(void)
{
	printf("%s", term_seq.cl);
}

static void term_init_save(void)
{
	static struct termios nts;
	char buf[1024];

	if (!isatty(1)) {
		term_seq.cl = "";
		term_seq.nl = "\n";
		return;
	}
	ts_saved = malloc(sizeof(*ts_saved));
	if (tcgetattr(0, ts_saved) < 0)
		goto fail;

	nts = *ts_saved;
	nts.c_lflag &= ~(ICANON | ECHO);
	nts.c_cc[VMIN] = 1;
	nts.c_cc[VTIME] = 0;
	if (tcsetattr (0, TCSANOW, &nts) < 0)
		goto fail;
	tgetent(buf, getenv("TERM"));
	term_seq.cl = tgetstr("cl", NULL);
	term_seq.nl = "\r\n";
	return;
fail:
	if (ts_saved) {
		free(ts_saved);
		ts_saved = NULL;
	}
	fail_err("Setting up terminal failed", errno);
}

static void term_restore(void)
{
	if (ts_saved) {
		tcsetattr(0, TCSANOW, ts_saved);
		free(ts_saved);
	}
}

static int init_socinfo(void)
{
	char *path;
	char buf[32];
	int fd, ret;
	int nodes, cores, threads;

	path = realpath(PATH_T99MON_SOCINFO, NULL);
	if (path == NULL) {
		perror("socinfo open");
		return errno;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;
	ret = read(fd, buf, sizeof(buf));
	if (ret <= 0)
		return -EBADF;

	ret = sscanf(buf, "%d %d %d", &nodes, &cores, &threads);
	if (ret != 3)
		return -EBADF;
	close(fd);
	free(path);
	printf("Read nodes = %d cores = %d threads = %d from socinfo\n",
			nodes, cores, threads);
	tx2mon->nodes = nodes;
	tx2mon->node[0].node = 0;
	tx2mon->node[0].cores = cores;
	if (nodes > 1) {
		tx2mon->node[1].node = 1;
		tx2mon->node[1].cores = cores;
	}
	return 0;
}

void file_dump_node(struct node_data *d)
{
	int c;
	FILE *of = tx2mon->fileout;
	struct mc_oper_region *op = &d->buf;

	for (c = 0; c < d->cores; c++)
		fprintf(of, "%.2f,%4u,", cpu_temp(d, c), cpu_freq(d, c));
	fprintf(of, "%.2f,", to_c(op->tmon_soc_avg));
	fprintf(of, "%4u,", op->freq_mem_net);
	if (display_extra)
		fprintf(of, "%4u,%4u,", op->freq_socs, op->freq_socn);
	fprintf(of, "%.2f,%.2f,%.2f,%.2f,", to_v(op->v_core),
		to_v(op->v_sram), to_v(op->v_mem), to_v(op->v_soc));
	fprintf(of, "%.2f,%.2f,%.2f,%.2f", to_w(op->pwr_core),
		to_w(op->pwr_sram), to_w(op->pwr_mem), to_w(op->pwr_soc));
}

void screen_disp_node(struct node_data *d)
{
	struct mc_oper_region *op = &d->buf;
	struct term_seq *t = &term_seq;
	int i, c, n;

	printf("Node: %d  Snapshot: %u%s", d->node, op->counter, t->nl);
	printf("Freq (Min/Max): %u/%u MHz     Temp Thresh (Soft/Max): %6.2f/%6.2f C%s",
		op->freq_min, op->freq_max, to_c(op->temp_soft_thresh),
		to_c(op->temp_abs_max), t->nl);
	printf("%s", t->nl);
	n = d->cores < CORES_PER_ROW ? d->cores : CORES_PER_ROW;
	for (i = 0; i < n; i++)
		printf("|Core  Temp   Freq ");
	printf("|%s", t->nl);
	for (i = 0; i < n; i++)
		printf("+------------------");
	printf("+%s", t->nl);
	for (c = 0;  c < d->cores; ) {
		for (i = 0; i < CORES_PER_ROW && c < d->cores; i++, c++)
			printf("|%3d: %6.2f %5d ", c,
					cpu_temp(d, c), cpu_freq(d, c));
		printf("|%s", t->nl);
	}
	printf("%s", t->nl);
	printf("SOC Center Temp: %6.2f C%s", to_c(op->tmon_soc_avg), t->nl);
	printf("Voltage    Core: %6.2f V, SRAM: %5.2f V,  Mem: %5.2f V, SOC: %5.2f V%s",
		to_v(op->v_core), to_v(op->v_sram), to_v(op->v_mem),
		to_v(op->v_soc), t->nl);
	printf("Power      Core: %6.2f W, SRAM: %5.2f W,  Mem: %5.2f W, SOC: %5.2f W%s",
		to_w(op->pwr_core), to_w(op->pwr_sram), to_w(op->pwr_mem),
		to_w(op->pwr_soc), t->nl);
	printf("Frequency    Memnet: %4d MHz", op->freq_mem_net);
	if (display_extra)
		printf(", SOCS: %4d MHz, SOCN: %4d MHz", op->freq_socs, op->freq_socn);
	printf("%s%s", t->nl, t->nl);
}

void dump_node(struct node_data *d)
{
	if (interactive)
		screen_disp_node(d);
	else
		file_dump_node(d);
}

int read_node(struct node_data *d)
{
	int rv;
	struct mc_oper_region *op = &d->buf;

	rv = lseek(d->fd, 0, SEEK_SET);
	if (rv < 0)
	       return rv;	
	rv = read(d->fd, op, sizeof(*op));
	if (rv < sizeof(*op))
		return rv;
	if ((op->cmd_status & STATUS_READY) == 0)
		return 0;
	return 1;
}

static void cleanup(void)
{
	if (interactive)
		term_restore();
	else {
		free(out_filename);
		if (tx2mon->fileout)
			fclose(tx2mon->fileout);
	}

	close(tx2mon->node[0].fd);
	if (tx2mon->nodes > 1)
		close(tx2mon->node[1].fd);
	free(tx2mon);
}

static int fail(char *str)
{
	fprintf(stderr, "%s\n", str);
	cleanup();
	exit(1);
}

static int fail_err(char *str, int err)
{
	fprintf(stderr, "%s : %s\n", str, strerror(err));
	cleanup();
	exit(1);
}

static void handle_input(void)
{
	int c;

	c = getchar();
	if (c == EOF || c == 'q')
	       tx2mon->stop = 1;
}

static void display_loop(void)
{
	struct term_seq *t = &term_seq;
	fd_set rdfds;
	struct timeval tv;
	int nfds;
	int ret, ret0, ret1;

	FD_ZERO(&rdfds);
	nfds = interactive ? 1 : 0;
	while (1) {
		tv = delay;
		if (interactive)
			FD_SET(0, &rdfds);
		ret = select(nfds, &rdfds, NULL, NULL, &tv);
		if (interactive && ret > 0 && FD_ISSET(0, &rdfds))
			handle_input();
		if (tx2mon->stop)
			return;

		if (interactive)
			clearscreen();
		ret0 = ret1 = 1;
		ret0 = read_node(&tx2mon->node[0]);
		if (tx2mon->nodes > 1)
			ret1 = read_node(&tx2mon->node[1]);
		if (ret0 < 0 || ret1 < 0)
			fail("Unexpected read error!");
		if (ret0 > 0 && ret1 > 0) {
			tx2mon->samples++;
			dump_node(&tx2mon->node[0]);
			if (!interactive)
				fprintf(tx2mon->fileout, ",");
			if (tx2mon->nodes > 1)
				dump_node(&tx2mon->node[1]);
			if (!interactive)
				fprintf(tx2mon->fileout, "\n");
		}
		if (interactive)
			printf("%s['q' to quit, any other key for refresh.]%s",
				t->nl, t->nl);
	}
}

static void handler(int sig)
{
	tx2mon->stop = 1;
}

static void usage(const char *prog, int exit_code)
{
	fprintf(stderr, "Usage: %s [-hx] [-d delay] [-f csv_file]\n", prog);
	exit(exit_code);
}

static void setup_fileout(void)
{
	struct node_data *nd;
	FILE *of;
	int c, n;

	of = fopen(out_filename, "w+");
	if (of == NULL)
		fail_err("Cannot open csv file!", errno);
	tx2mon->fileout = of;

	printf("Saving to %s, use INTR key to stop.\n",
		out_filename);
	for (n = 0; n < tx2mon->nodes; n++) {
		nd = &tx2mon->node[n];
		if (n == 1)
			fprintf(of, ",");
		for (c = 0; c < nd->cores; c++)
			fprintf(of, "cpu_temp%dc%d,cpu_freq%dc%d,", n, c, n, c);
		fprintf(of, "tmon_soc_avg%d,", n);
		fprintf(of, "freq_mem_net%d,", n);
		if (display_extra)
			fprintf(of, "freq_socs%d,freq_socn%d,", n, n);
		fprintf(of, "v_core%d,v_sram%d,v_mem%d,v_soc%d,", n, n, n, n);
		fprintf(of, "pwr_core%d,pwr_sram%d,pwr_mem%d,pwr_soc%d", n, n, n, n);
	}
	fprintf(of, "\n");
}

int main(int argc, char *argv[])
{
	int opt, ret;
	int fd;
	double dval;

	tx2mon = malloc(sizeof(*tx2mon));
	while ((opt = getopt(argc, argv, "f:d:hx")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0], 0);
			break;
		case 'd':
			dval = atof(optarg);
			if (dval >= 0.0001 && dval <= 9999.0) {
				delay.tv_sec = dval;
				delay.tv_usec = 1000000 * (dval - delay.tv_sec);
			} else {
				fprintf(stderr, "Bad delay %f!- allowed range [0.0001..9999]\n", dval);
				usage(argv[0], 1);
			}

			break;
		case 'f':
			out_filename = strdup(optarg);
			interactive = 0;
			break;
		case 'x':
			display_extra = 1;
			break;
		default:
			usage(argv[0], 1);
			break;
		}
	}

	ret = init_socinfo();
	if (ret < 0)
		fail_err("Check if you loaded tx2mon module", ret);

	fd = open(PATH_T99MON_NODE0, O_RDONLY);
	if (fd < 0)
		fail_err("Reading node0 entry", errno);
	tx2mon->node[0].fd = fd;

	if (tx2mon->nodes > 1) {
		fd = open(PATH_T99MON_NODE1, O_RDONLY);
		if (fd < 0)
			fail_err("Reading node1 entry", errno);
		tx2mon->node[1].fd = fd;
	}

	signal(SIGTERM, handler);
	signal(SIGINT, handler);
	signal(SIGHUP, handler);

	if (interactive)
		term_init_save();
	else
		setup_fileout();
	display_loop();
	if (!interactive)
		printf("\n%d samples saved.\n", tx2mon->samples);
	cleanup();
	return 0;
}
