#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include "list.h"

#define VERSION "0.2.0"
#define SUB_MAX_BUF 1024

typedef uint64_t msec_t;
#define PRImsec PRIu64

struct srt_sub {
	msec_t start;
	msec_t end;
	char *position;
	char text[SUB_MAX_BUF];
	struct list_head list;
};

void init_srt_sub(struct srt_sub *sub)
{
	assert(sub != NULL);
	sub->start = 0;
	sub->end = 0;
	sub->position = NULL;
	sub->text[0] = '\0';
	sub->list.next = NULL;
	sub->list.prev = NULL;
}

void free_srt_sub(struct srt_sub *sub)
{
	assert(sub != NULL);
	if (sub->position)
		free(sub->position);
	free(sub);
}

void free_srt_sub_list(struct list_head *srt_head)
{
	struct list_head *pos, *tmp;
	struct srt_sub *sub;

	assert(srt_head != NULL);

	list_for_each_safe(pos, tmp, srt_head) {
		sub = list_entry(pos, struct srt_sub, list);
		free_srt_sub(sub);
	}
}

void *xmalloc(size_t size)
{
	void *m = malloc(size);

	if (m == NULL)
		abort();

	return m;
}

/* converts hh:mm:ss[,.]mss to milliseconds */
int timestr_to_msec(const char *time, msec_t *msecs)
{
	char *tmp;
	msec_t h, m, s, ms;
	int res;

	assert(msecs != NULL || time != NULL);

	tmp = strchr(time, '.');
	if (tmp != NULL)
		*tmp = ',';

	res = sscanf(time, "%" PRImsec ":%" PRImsec ":%" PRImsec ",%" PRImsec, &h, &m, &s, &ms);
	if (res != 4 || m >= 60 || s >= 60 || ms >= 1000) {
		fprintf(stderr, "Parsing error: Can not convert `%s' to milliseconds\n", time);
		return -1;
	}

	*msecs = h * 60 * 60 * 1000;
	*msecs += m * 60 * 1000;
	*msecs += s * 1000;
	*msecs += ms;

	return 0;
}

/* converts milliseconds to hh:mm:ss,mss */
char *msec_to_timestr(msec_t msecs, char *timestr, size_t size)
{
	msec_t h, m, s, ms;

	assert(timestr != NULL || size == 0);

	h = msecs / (60 * 60 * 1000);
	msecs %= 60 * 60 * 1000;
	m = msecs / (60 * 1000);
	msecs %= 60 * 1000;
	s = msecs / 1000;
	ms = msecs % 1000;

	snprintf(timestr, size, "%02" PRImsec ":%02" PRImsec ":%02" PRImsec ",%03" PRImsec,
		 h, m, s, ms);

	return timestr;
}

char *strip_eol(char *str)
{
	size_t i;

	assert(str != NULL);

	for (i = 0; str[i] != '\0'; i++) {
		if (str[i] == '\n') {
			str[i] = '\0';
			if (i > 0 && str[i - 1] == '\r')
				str[i - 1] = '\0';
			return str;
		}
	}

	return str;
}

/* read SubRip (srt) file */
int read_srt(FILE *fin, struct list_head *srt_head)
{
	int state = 0;
	char *s, buf[SUB_MAX_BUF];
	struct srt_sub *sub = NULL;

	assert(fin != NULL || srt_head != NULL);

	while (1) {
		s = fgets(buf, sizeof(buf), fin);
		if (s == NULL)
			break;

		strip_eol(buf);

		if (state == 0) {
			/* drop empty lines */
			if (buf[0] == '\0')
				continue;
			/* drop subtitle number */
			state = 1;
		} else if (state == 1) {
			char start_time[20], end_time[20], position[50];
			int res;

			sub = xmalloc(sizeof(*sub));
			init_srt_sub(sub);

			/* parse start, end, and position */
			res = sscanf(buf, "%19s --> %19s%49[^\n]", start_time, end_time, position);
			if (res < 2) {
				fprintf(stderr, "Parsing error: Wrong file format\n");
				goto out_err;
			}

			if (res == 3)
				sub->position = strdup(position);

			res = timestr_to_msec(start_time, &sub->start);
			if (res == -1)
				goto out_err;

			res = timestr_to_msec(end_time, &sub->end);
			if (res == -1)
				goto out_err;

			state = 2;
		} else if (state == 2) {
			/* empty line indicates the end of the subtitle,
			 * so append it to the list */
			if (buf[0] == '\0') {
				list_add_tail(&sub->list, srt_head);
				sub = NULL;
				state = 0;
				continue;
			}
			/* save subtitle text */
			strncat(sub->text, buf, sizeof(sub->text) - strlen(sub->text) - 1);
			strncat(sub->text, "\r\n", sizeof(sub->text) - strlen(sub->text) - 1);
		}
	}

	if (ferror(fin)) {
		fprintf(stderr, "read: File error\n");
		goto out_err;
	}

	return 0;

out_err:
	if (sub != NULL)
		free_srt_sub(sub);
	free_srt_sub_list(srt_head);
	return -1;
}

/* write SubRip (srt) file */
void write_srt(FILE *fout, struct list_head *srt_head)
{
	struct list_head *pos;
	struct srt_sub *sub;
	unsigned int id = 1;
	char tm[20];

	assert(fout != NULL || srt_head != NULL);

	list_for_each(pos, srt_head) {
		sub = list_entry(pos, struct srt_sub, list);
		fprintf(fout, "%u\r\n", id++);
		fprintf(fout, "%s", msec_to_timestr(sub->start, tm, sizeof(tm)));
		fprintf(fout, " --> ");
		fprintf(fout, "%s", msec_to_timestr(sub->end, tm, sizeof(tm)));
		if (sub->position)
			fprintf(fout, "%s", sub->position);
		fprintf(fout, "\r\n%s\r\n", sub->text);
	}
}

/* synchronize subtitles by knowing the start time of the first and the last subtitle.
 * to achieve this we must use the linear equation: y = mx + b */
void sync_srt(struct list_head *srt_head, msec_t synced_first, msec_t synced_last)
{
	long double slope, yint;
	msec_t desynced_first, desynced_last;
	struct list_head *pos;
	struct srt_sub *sub;

	assert(srt_head != NULL);

	desynced_first = list_first_entry(srt_head, struct srt_sub, list)->start;
	desynced_last = list_last_entry(srt_head, struct srt_sub, list)->start;

	/* m = (y2 - y1) / (x2 - x1)
	 * m: slope
	 * y2: synced_last
	 * y1: synced_first
	 * x2: desynced_last
	 * x1: desynced_first */
	slope = (long double)(synced_last - synced_first)
		/ (long double)(desynced_last - desynced_first);

	/* b = y - mx
	 * b: yint
	 * y: synced_last
	 * m: slope
	 * x: desynced_last */
	yint = synced_last - slope * desynced_last;

	list_for_each(pos, srt_head) {
		sub = list_entry(pos, struct srt_sub, list);
		/* y = mx + b
		 * y: sub->start and sub->end
		 * m: slope
		 * x: sub->start and sub->end
		 * b: yint */
		sub->start = llroundl(slope * sub->start + yint);
		sub->end = llroundl(slope * sub->end + yint);
	}
}

void usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  subsync [options]\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -h, --help           Show this help\n");
	fprintf(stderr, "  -f, --first-sub      Time of the first subtitle\n");
	fprintf(stderr, "  -l, --last-sub       Time of the last subtitle\n");
	fprintf(stderr, "  -i, --input          Input file\n");
	fprintf(stderr, "  -o, --output         Output file");
	fprintf(stderr, " (if not specified, it overwrites the input file)\n");
	fprintf(stderr, "  -v, --version        Print version\n");
	fprintf(stderr, "\nExample:\n");
	fprintf(stderr, "  subsync -f 00:01:33,492 -l 01:39:23,561 -i file.srt\n");
}

#define FLAG_F (1 << 0)
#define FLAG_L (1 << 1)

int main(int argc, char *argv[])
{
	struct list_head subs_head;
	unsigned int flags = 0;
	msec_t first_ms, last_ms;
	char *input_path = NULL, *output_path = NULL;
	FILE *fin = stdin, *fout = stdout;
	int res;

	if (argc <= 1) {
		usage();
		return 1;
	}

	init_list_head(&subs_head);

	while (1) {
		int c, option_index;
		static struct option long_options[] = {
			{"help",	no_argument,		0, 'h'},
			{"first-sub",	required_argument,	0, 'f'},
			{"last-sub",	required_argument,	0, 'l'},
			{"input",	required_argument,	0, 'i'},
			{"output",	required_argument,	0, 'o'},
			{"version",	required_argument,	0, 'v'},
			{ 0,		0,			0,   0}
		};

		c = getopt_long(argc, argv, "f:l:i:o:hv", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'f':
			flags |= FLAG_F;
			res = timestr_to_msec(optarg, &first_ms);
			if (res == -1)
				return 1;
			break;
		case 'l':
			flags |= FLAG_L;
			res = timestr_to_msec(optarg, &last_ms);
			if (res == -1)
				return 1;
			break;
		case 'i':
			input_path = optarg;
			break;
		case 'o':
			output_path = optarg;
			break;
		case 'v':
			printf("%s\n", VERSION);
			return 0;
		default:
			return 1;
		}
	}

	if (optind < argc) {
		int i;
		fprintf(stderr, "Invalid argument%s:", argc - optind > 1 ? "s" : "");
		for (i = optind; i < argc; i++)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n");
		return 1;
	}

	if (input_path == NULL) {
		fprintf(stderr, "You must specify an input file with -i option.\n");
		return 1;
	}

	if (output_path == NULL)
		output_path = input_path;

	/* read srt file */
	if (strcmp(input_path, "-") != 0)
		fin = fopen(input_path, "r");

	if (fin == NULL) {
		fprintf(stderr, "open: %s: %s\n", input_path, strerror(errno));
		return 1;
	}

	res = read_srt(fin, &subs_head);
	fclose(fin);
	if (res == -1)
		return 1;

	/* if user didn't pass 'f' flag, then get the time of the first subtitle */
	if (!(flags & FLAG_F))
		first_ms = list_first_entry(&subs_head, struct srt_sub, list)->start;

	/* if user didn't pass 'l' flag, then get the time of the last subtitle */
	if (!(flags & FLAG_L))
		last_ms = list_last_entry(&subs_head, struct srt_sub, list)->start;

	/* sync subtitles */
	sync_srt(&subs_head, first_ms, last_ms);

	/* write subtitles */
	if (strcmp(output_path, "-") != 0)
		fout = fopen(output_path, "w");

	if (fout == NULL) {
		fprintf(stderr, "open: %s: %s\n", output_path, strerror(errno));
		free_srt_sub_list(&subs_head);
		return 1;
	}

	write_srt(fout, &subs_head);
	fclose(fout);

	free_srt_sub_list(&subs_head);
	return 0;
}
