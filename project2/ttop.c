#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <utmpx.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#define BUFFER_SIZE (128)
#define MAX_USER_NAME (32)
#define INIT_LIST_SIZE (1024)
#define MILLIS (1000)
const unsigned long NANOS = 1000000000;
int page_size_in_KiB;
unsigned int print_start_index;

void print_system_infos();
void print_current_time();
void print_running_time();
void print_user_count();
void print_load_average();
void print_mem_infos();

void init_task_list();
void init_simple_task_list();

void increase_print_start_index();
void decrease_print_start_index();
void update_time();
void update_cpu_time();
void update_simple_task_status();
void update_task_status(int max_count);
void print_process_infos();
void print_process_info(int index);

unsigned long prev_cpu_idle;
unsigned long prev_cpu_nonidle;
unsigned long prev_cpu_time;
unsigned long cur_cpu_idle;
unsigned long cur_cpu_nonidle;
unsigned long cur_cpu_time;
unsigned long prev_time;
unsigned long cur_time;

int main(void) {
	page_size_in_KiB = getpagesize() / 1024;
	init_task_list();
	init_simple_task_list();

	print_system_infos();
	printf("\n");
	print_process_infos();

	return 0;
}

void print_system_infos() {
	printf("top - ");
	print_current_time();
	printf(" up ");
	print_running_time();
	printf(", ");
	print_user_count();
	printf(" user, ");
	printf("load average: ");
	print_load_average();


	printf("\n");

	printf("\n");

	print_mem_infos();
	printf("\n");

	return;
}

void print_current_time() {
	time_t raw_time;
	struct tm *time_info;
	char *time_text;
	char current_time_text[8];

	time(&raw_time);
	time_info = localtime(&raw_time);
	time_text = asctime(time_info);
	strncpy(current_time_text, time_text + 11, sizeof(current_time_text) / sizeof(current_time_text[0]));
	printf("%s", current_time_text);

	return;
}

void print_running_time() {
	// ~ min
	// ~:~~
	FILE *fp;
	const char* fname = "/proc/uptime";
	const char* mode = "r";
	float fuptime;
	int hour = 0;
	int minute = 0;

	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}
	fscanf(fp, "%f", &fuptime);
	fclose(fp);

	hour = fuptime / (60 * 60);
	minute = (fuptime - (hour *60 * 60)) / 60;

	if (hour == 0) {
		printf("%d min", minute);
	} else {
		printf("%d:%02d", hour, minute);
	}

	return;
}

void print_user_count() {
	struct utmpx *utmpxp;
	int logged_in_user_count = 0;

	setutxent();
	while ((utmpxp = getutxent()) != NULL) {
		if (utmpxp->ut_type == USER_PROCESS) {
			++logged_in_user_count;
		}
	}
	endutxent();

	printf("%d", logged_in_user_count);

	return;
}

void print_load_average() {
	FILE *fp;
	const char *fname = "/proc/loadavg";
	const char* mode = "r";
	float _1_min_avg, _5_min_avg, _15_min_avg;

	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}

	fscanf(fp, "%f%f%f", &_1_min_avg, &_5_min_avg, &_15_min_avg);
	fclose(fp);

	printf("%.2f, %.2f, %.2f", _1_min_avg, _5_min_avg, _15_min_avg);

	return;
}

void print_mem_infos() {
	FILE *fp;
	const char *fname = "/proc/meminfo";
	const char *mode = "r";
	int mem_total, mem_free, mem_used, mem_cached, mem_buffers, mem_SReclaimable, mem_available, mem_cache;
	int swap_total, swap_free, swap_used;
	char tmp[BUFFER_SIZE];
	int i;
	
	// used -> total - free - buffers - cache
	// buffers -> Buffers in /proc/meminfo
	// cache -> Cached and SReclaimable in /proc/meminfo

	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}

	fscanf(fp, "%s%d%s", tmp, &mem_total, tmp);
	fscanf(fp, "%s%d%s", tmp, &mem_free, tmp);
	fscanf(fp, "%s%d%s", tmp, &mem_available, tmp);
	fscanf(fp, "%s%d%s", tmp, &mem_buffers, tmp);
	fscanf(fp, "%s%d%s", tmp, &mem_cached, tmp);
	for (i = 0; i < 9; ++i)
		fscanf(fp, "%s%s%s", tmp, tmp, tmp);
	fscanf(fp, "%s%d%s", tmp, &swap_total, tmp);
	fscanf(fp, "%s%d%s", tmp, &swap_free, tmp);
	for (i = 0; i < 7; ++i)
		fscanf(fp, "%s%s%s", tmp, tmp, tmp);
	fscanf(fp, "%s%d%s", tmp, &mem_SReclaimable, tmp);

	fclose(fp);

	mem_cache = mem_cached + mem_SReclaimable;
	mem_used = mem_total - mem_free - mem_buffers - mem_cache;
	swap_used = swap_total - swap_free;
	printf("KiB Mem : %d total, %d free, %d used, %d buff/cache\n", mem_total, mem_free, mem_used, mem_buffers + mem_cache);
	printf("KiB Swap: %d total, %d free, %d used, %d avail Mem\n", swap_total, swap_free, swap_used, mem_available);

}
///////////////////////////////////////////////////////

typedef struct _task_info {
	pid_t pid;
	char user_name[MAX_USER_NAME + 1];
	long pr;
	long ni;
	unsigned long virt;
	unsigned long res;
	unsigned long shr;
	char s[2];
	float cpu;
	float mem;
	unsigned long time;
	char command[BUFFER_SIZE];
} Task_info;

struct _task_list {
	Task_info **list;
	int len;
	int size;
	int is_sorted_by_pid;
	int is_sorted_by_cpu_and_pid;
} Task_list;

typedef struct _simple_task_info {
	pid_t pid;
	float cpu;
	unsigned long prev_cpu_time;
	unsigned long cur_cpu_time;
	int is_updated;
} Simple_task_info;

struct _simple_task_list {
	Simple_task_info **list;
	int len;
	int size;
	int is_sorted_by_pid;
	int is_sorted_by_cpu_and_pid;
} Simple_task_list;

// task info
void free_task_list() {
	if (Task_list.list != NULL) {
		int i;
		for (i = 0; i < Task_list.len; ++i) {
			if (Task_list.list[i] != NULL) {
				free(Task_list.list[i]);
			}
		}
		free(Task_list.list);

		Task_list.list = NULL;
		Task_list.len = 0;
		Task_list.size = 0;
	}

	return;
}

void init_task_list() {
	free_task_list();
	Task_list.list = (Task_info **)malloc(INIT_LIST_SIZE * sizeof(Task_info));
	if (Task_list.list == NULL) {
		fprintf(stderr, "malloc error in init_task_list\n");
		exit(1);
	}
	Task_list.len = 0;
	Task_list.size = INIT_LIST_SIZE;
	Task_list.is_sorted_by_pid = 0;
	Task_list.is_sorted_by_cpu_and_pid = 0;

	return;
}

void append_to_task_list(Task_info* new_info) {
	if (Task_list.len == Task_list.size) {
		Task_list.size *= 2;
		Task_list.list = (Task_info **)realloc(Task_list.list, Task_list.size * sizeof(Task_info *));
		if (Task_list.list == NULL) {
			fprintf(stderr, "realloc error in append_to_task_list\n");
			exit(1);
		}
	}
	Task_list.list[(Task_list.len)++] = new_info;
	Task_list.is_sorted_by_pid = 0;
	Task_list.is_sorted_by_cpu_and_pid = 0;

	return;
}

Task_info *make_new_task_info(pid_t pid) {
	FILE *fp;
	char fname[MAXNAMLEN + 1];
	char tmp[BUFFER_SIZE];
	const char *mode = "r";
	unsigned long stime, utime;
	float mem_total;
	int i;
	struct passwd *result;
	uid_t uid;

	//printf("1111\n");
	Task_info *new_info = (Task_info *)malloc(sizeof(Task_info));
	if (new_info == NULL) {
		fprintf(stderr, "malloc error in make_new_task_info\n");
		exit(1);
	}
	new_info->pid = pid;

	//printf("2222\n");
	/////
	sprintf(fname, "/proc/%d/stat", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}

	for(i = 0; i < 2; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%s", new_info->s);
	for(i = 0; i < 10; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%lu%lu", &stime, &utime);
	new_info->time = stime + utime;
	for(i = 0; i < 2; ++i)
		fscanf(fp, "%s", tmp);
	fscanf(fp, "%ld%ld", &(new_info->pr), &(new_info->ni));
	fclose(fp);
	
	//printf("3333\n");
	/////
	sprintf(fname, "/proc/%d/loginuid", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}
	fscanf(fp, "%d", &uid);
	fclose(fp);

	if ((result = getpwuid(uid)) == NULL) {
		strcpy(new_info->user_name, "root");
	} else {
		strcpy(new_info->user_name, result->pw_name);
	}

	//printf("4444\n");
	/////
	sprintf(fname, "/proc/%d/status", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}
	fscanf(fp, "%s%s", tmp, new_info->command);

	fclose(fp);

	//printf("5555\n");
	/////
	sprintf(fname, "/proc/%d/statm", pid);
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}
	fscanf(fp, "%lu%lu%lu", &(new_info->virt), &(new_info->res), &(new_info->shr));
	fclose(fp);
	new_info->virt *= page_size_in_KiB;
	new_info->res *= page_size_in_KiB;
	new_info->shr *= page_size_in_KiB;

	//printf("6666\n");
	/////
	sprintf(fname, "/proc/meminfo");
	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}
	fscanf(fp, "%s%f", tmp, &mem_total);
	fclose(fp);

	new_info->mem = (new_info->res - new_info->shr) / mem_total * 100.0;

	//PID - directory name - ok
	//S - stat - ok
	//PR - stat - ok
	//NI - stat - ok
	//TIME+ - stat - stime + utime - ok
	//%CPU - (usageSystemMode + usageUserMode) / tick_count - ok
	//USER - getpwnam(/proc/$pid/loginuid) - ok
	//COMMAND - status - ok
	//VIRT - statm (VmSize - 1) * pagesize(getpagesize()) / 1024(KiB)) - ok
	//RES - statm (VmRSS - 2) * pagesize(getpagesize()) / 1024(KiB)) - ok
	//SHR - statm (shared * pagesize(getpagesize()) / 1024(KiB)) - ok
	//%MEM - (RES - SHR) / mem_total(/proc/meminfo) * 100 - ok
	
//	char user_name[MAX_USER_NAME + 1];
//	int pr;
//	int ni;
//	int virt;
//	int res;
//	int shr;
//	int s;
//	float cpu;
//	float mem;
//	int time;
//	char command[BUFFER_SIZE];

	return new_info;
}

int compare_by_pid(const void *a, const void *b) {
	return (*(Task_info **)a)->pid - (*(Task_info **)b)->pid;
}

int compare_by_cpu_and_pid(const void *_a, const void *_b) {
	int a_cpu, b_cpu;
	Task_info **a = (Task_info **)_a;
	Task_info **b = (Task_info **)_b;
	a_cpu = (int)((*a)->cpu * 100);
	b_cpu = (int)((*b)->cpu * 100);

	if (a_cpu == b_cpu) {
		return (*a)->pid - (*b)->pid;
	} else {
		return a_cpu - b_cpu;
	}
}

void sort_list_by_pid() {
	Task_list.is_sorted_by_pid = 1;
	Task_list.is_sorted_by_cpu_and_pid = 0;

	qsort(Task_list.list, Task_list.len, sizeof(Task_info *), compare_by_pid);
}

void sort_list_by_cpu_and_pid() {
	Task_list.is_sorted_by_pid = 0;
	Task_list.is_sorted_by_cpu_and_pid = 1;

	qsort(Task_list.list, Task_list.len, sizeof(Task_info *), compare_by_cpu_and_pid);
}

Task_info **find_element(pid_t pid) {
	Task_info tmp_info;
	Task_info *key;
	if (!Task_list.is_sorted_by_pid) {
		sort_list_by_pid();
	}

	tmp_info.pid = pid;
	key = &tmp_info;
	return bsearch(&key, Task_list.list, Task_list.len, sizeof(Task_info *), compare_by_pid);
}

void print_list() {
	int i;

	printf("*** Task_info ***\n");
	for (i = 0; i < Task_list.len; ++i) {
		printf("pid: %d, cpu: %.1f\n", Task_list.list[i]->pid, Task_list.list[i]->cpu);
		printf("username: %s, pri: %ld, ni: %ld, virt: %lu, res: %lu, shr: %lu, s: %s, mem: %.1f, time: %lu, command: %s\n", Task_list.list[i]->user_name, Task_list.list[i]->pr, Task_list.list[i]->ni, Task_list.list[i]->virt, Task_list.list[i]->res, Task_list.list[i]->shr, Task_list.list[i]->s, Task_list.list[i]->mem, Task_list.list[i]->time, Task_list.list[i]->command);
	}
	printf("\n");

	return;
}

// simple task info
void free_simple_task_list() {
	if (Simple_task_list.list != NULL) {
		int i;
		for (i = 0; i < Simple_task_list.len; ++i) {
			if (Simple_task_list.list[i] != NULL) {
				free(Simple_task_list.list[i]);
			}
		}
		free(Simple_task_list.list);

		Simple_task_list.list = NULL;
		Simple_task_list.len = 0;
		Simple_task_list.size = 0;
	}

	return;
}

void init_simple_task_list() {
	free_simple_task_list();
	Simple_task_list.list = (Simple_task_info **)malloc(INIT_LIST_SIZE * sizeof(Simple_task_info *));
	if (Simple_task_list.list == NULL) {
		fprintf(stderr, "malloc error in init_simple_task_list\n");
		exit(1);
	}
	Simple_task_list.len = 0;
	Simple_task_list.size = INIT_LIST_SIZE;
	Simple_task_list.is_sorted_by_pid = 0;
	Simple_task_list.is_sorted_by_cpu_and_pid = 0;

	return;
}

void append_to_simple_task_list(Simple_task_info *new_info) {
	if (Simple_task_list.len == Simple_task_list.size) {
		Simple_task_list.size *= 2;
		Simple_task_list.list = (Simple_task_info **)realloc(Simple_task_list.list, Simple_task_list.size * sizeof(Simple_task_info *));
		if (Simple_task_list.list == NULL) {
			fprintf(stderr, "realloc error in append_to_simple_task_list\n");
			exit(1);
		}
	}
	Simple_task_list.list[(Simple_task_list.len)++] = new_info;
	Simple_task_list.is_sorted_by_pid = 0;
	Simple_task_list.is_sorted_by_cpu_and_pid = 0;

	return;
}

Simple_task_info *make_new_simple_task_info(pid_t pid) {
	Simple_task_info *new_info = (Simple_task_info *)malloc(sizeof(Simple_task_info));
	if (new_info == NULL) {
		fprintf(stderr, "malloc error in make_new_simple_task_info\n");
		exit(1);
	}
	new_info->pid = pid;
	new_info->is_updated = 1;

	return new_info;
}

int compare_simple_by_pid(const void *_a, const void *_b) {
	//printf("compare start\n");
	Simple_task_info **a = (Simple_task_info **)_a;
	Simple_task_info **b = (Simple_task_info **)_b;
	//printf("%d - %d\n", (*a)->pid, (*b)->pid);
	//printf("compare end\n");
	return (*a)->pid - (*b)->pid;
}

int compare_simple_by_cpu_and_pid(const void *_a, const void *_b) {
	int a_cpu, b_cpu;
	Simple_task_info **a = (Simple_task_info **)_a;
	Simple_task_info **b = (Simple_task_info **)_b;
	a_cpu = (int)((*a)->cpu * 100);
	b_cpu = (int)((*b)->cpu * 100);

	if (a_cpu == b_cpu) {
		return (*a)->pid - (*b)->pid;
	} else {
		return b_cpu - a_cpu;
	}
}

void sort_simple_list_by_pid() {
	Simple_task_list.is_sorted_by_pid = 1;
	Simple_task_list.is_sorted_by_cpu_and_pid = 0;

	qsort(Simple_task_list.list, Simple_task_list.len, sizeof(Simple_task_info *), compare_simple_by_pid);
}

void sort_simple_list_by_cpu_and_pid() {
	Simple_task_list.is_sorted_by_pid = 0;
	Simple_task_list.is_sorted_by_cpu_and_pid = 1;

	qsort(Simple_task_list.list, Simple_task_list.len, sizeof(Simple_task_info *), compare_simple_by_cpu_and_pid);
}

Simple_task_info **find_simple_element(pid_t pid) {
	Simple_task_info tmp_info;
	Simple_task_info *key;
	//printf("-1111\n");
	if (!Simple_task_list.is_sorted_by_pid) {
		sort_simple_list_by_pid();
	}
	//printf("-2222\n");

	tmp_info.pid = pid;
	key = &tmp_info;
	//printf("-3333\n");
	return (Simple_task_info **)bsearch(&key, Simple_task_list.list, Simple_task_list.len, sizeof(Simple_task_info *), compare_simple_by_pid);
}

void print_simple_list() {
	int i;

	printf("*** Simple_task_info ***\n");
	for (i = 0; i < Simple_task_list.len; ++i) {
		//if (Simple_task_list.list[i]->cpu > 0)
		printf("pid: %d, cpu: %.1f, cputime: %lu\n", Simple_task_list.list[i]->pid, Simple_task_list.list[i]->cpu, Simple_task_list.list[i]->cur_cpu_time - Simple_task_list.list[i]->prev_cpu_time);
	}
	printf("\n");

	return;
}

//////////////////////////////////////////////////////////////

unsigned int print_start_index;
void increase_print_start_index() {
	if (print_start_index >= Simple_task_list.len - 1) {
		print_start_index = Simple_task_list.len - 1;
	} else {
		++print_start_index;
	}

	return;
}

void decrease_print_start_index() {
	if (print_start_index <= 0) {
		print_start_index = 0;
	} else {
		--print_start_index;
	}

	return;
}

void update_time() {
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		fprintf(stderr, "clock_gettime error\n");
		exit(1);
	}

	prev_time = cur_time;
	cur_time = (NANOS * ts.tv_sec + ts.tv_nsec) / (NANOS / MILLIS);
	printf("prev_time = %lu, current_time = %lu\n", prev_time, cur_time);
}

void update_cpu_time() {
	FILE *fp;
	const char *fname = "/proc/stat";
	const char *mode = "r";
	char tmp[BUFFER_SIZE];
	unsigned long user, nice, system, idle, lowait, irq, softirq, steal, guest, guest_nice;

	if ((fp = fopen(fname, mode)) == NULL) {
		fprintf(stderr, "fopen error for %s\n", fname);
		exit(1);
	}

	fscanf(fp, "%s%lu%lu%lu%lu%lu%lu%lu%lu%lu%lu", tmp, &user, &nice, &system, &idle, &lowait, &irq, &softirq, &steal, &guest, &guest_nice);
	fclose(fp);

	prev_cpu_nonidle = cur_cpu_nonidle;
	prev_cpu_idle = cur_cpu_idle;
	prev_cpu_time = cur_cpu_time;
	//cur_cpu_time = user + nice + system + idle + lowait + irq + softirq + steal + guest + guest_nice;

	cur_cpu_nonidle = user + nice + system + irq + softirq + steal;
	cur_cpu_idle = idle + lowait;
	cur_cpu_time = cur_cpu_nonidle + cur_cpu_idle;

	return;
}

void update_task_status(int max_count) {
	//PID - directory name
	//USER - getpwnam(/proc/$pid/loginuid)
	//PR - stat
	//NI - stat
	//VIRT - status (VmSize)
	//RES - status (VmRSS)
	//SHR - statm (shared * pagesize(getpagesize()) / 1024(KiB))
	//S - stat
	//%CPU - (usageSystemMode + usageUserMode) / tick_count
	//%MEM - (RES - SHR) / mem_total(/proc/meminfo) * 100
	//TIME+ - stime + utime
	//COMMAND - stat
	int i;
	int target_index;

	init_task_list();
	for(i = 0, target_index = print_start_index; i < max_count && print_start_index < Simple_task_list.len; ++i) {
		pid_t target_pid;

		target_index = print_start_index + i;
		target_pid = Simple_task_list.list[target_index]->pid;

		//printf("make new task info\n");
		Task_info *new_info = make_new_task_info(target_pid);
		new_info->cpu = Simple_task_list.list[target_index]->cpu;
		append_to_task_list(new_info);
		//printf("finish %d\n", target_index);
	}
	//print_list();
}

void update_simple_task_status() {
	struct dirent *dentry;
	struct stat statbuf;
	char filename[MAXNAMLEN + 1];
	DIR *dirp;
	int i;

	for (i = 0; i < Simple_task_list.len; ++i) {
		Simple_task_list.list[i]->prev_cpu_time = Simple_task_list.list[i]->cur_cpu_time;
		Simple_task_list.list[i]->cur_cpu_time = 0;
		Simple_task_list.list[i]->is_updated = 0;
	}

	if ((dirp = opendir("/proc")) == NULL) {
		fprintf(stderr, "opendir error for /proc\n");
		exit(1);
	}

	//printf("1111\n");

	while ((dentry = readdir(dirp)) != NULL) {
		//printf("2222\n");
		pid_t pid;
		if (dentry->d_ino == 0)
			continue;

		memcpy(filename, dentry->d_name, MAXNAMLEN);
		if ((pid = atoi(filename))) {
			FILE *fp;
			unsigned long stime, utime;
			char proc_stat_filename[MAXNAMLEN + 1];
			char tmp[BUFFER_SIZE];
			int is_new_info = 0;
			int i;
			//printf("2.5222\n");
			Simple_task_info *task_info ;
			Simple_task_info **task_info_p = find_simple_element(pid);
			//printf("3333\n");
			if (task_info_p == NULL) {
				is_new_info = 1;
				task_info = make_new_simple_task_info(pid);
			} else {
				task_info = *task_info_p;
			}

			//printf("4444\n");
			sprintf(proc_stat_filename, "/proc/%d/stat", pid);
			if ((fp = fopen(proc_stat_filename, "r")) == NULL) {
				fprintf(stderr, "fopen error for %s\n", proc_stat_filename);
				exit(1);
			}

			for (i = 0; i < 13; ++i)
				fscanf(fp, "%s", tmp);
			fscanf(fp, "%lu%lu", &utime, &stime);
			fclose(fp);

			//printf("5555\n");
			task_info->cur_cpu_time = utime + stime;
			task_info->cpu = ((float)(task_info->cur_cpu_time - task_info->prev_cpu_time) / (sysconf(_SC_CLK_TCK) * (cur_time - prev_time))) * 100.0 * MILLIS;
			if (task_info->cpu > 100.0) {
				task_info->cpu = 100.0;
			}
			task_info->is_updated = 1;

			//printf("6666\n");
			if (is_new_info) {
				printf("add new simple info\n");
				append_to_simple_task_list(task_info);
			}
			//printf("7777\n");
		} else {
			continue;
		}
	}
	// 종료된 프로세스 Simple_task_list에서 제거하는 작업
	for (i = 0; i < Simple_task_list.len; ++i) {
		if (Simple_task_list.list[i]->is_updated == 0) { // 종료된 프로세스
			free(Simple_task_list.list[i]);
			if (i < --(Simple_task_list.len)) {
				Simple_task_list.list[i] = Simple_task_list.list[Simple_task_list.len];
			}
			Simple_task_list.list[Simple_task_list.len] = NULL;
			Simple_task_list.is_sorted_by_pid = 0;
			Simple_task_list.is_sorted_by_cpu_and_pid = 0;
		}
	}

	sort_simple_list_by_cpu_and_pid();
	//printf("8888\n");
	return;
}

void print_process_infos() {
	int i;
	char task_info_string[1024];

	while(1) {
		update_time();
		update_cpu_time();
		update_simple_task_status();
		update_task_status(20);

		sprintf(task_info_string, "%6s %s\t%3s %3s %7s %7s %7s %s %4s %4s %9s %s", "PID", "USER", "PR", "NI", "VIRT", "RES", "SHR", "S", "%CPU", "%MEM", "TIME+", "COMMAND");
		printf("%s\n", task_info_string);
		for (i = 0; i < Task_list.len; ++i) {
			print_process_info(i);
		}

		sleep(3);
	}

	return;
}

void print_process_info(int index) {
	Task_info *t;
	char task_info_string[1024];
	char pr_string[10];

	t = Task_list.list[index];
	if (t->pr == -100) {
		sprintf(pr_string, "rt");
	} else {
		sprintf(pr_string, "%ld", t->pr);
	}
	sprintf(task_info_string, "%6d %s\t%3s %3ld %7lu %7lu %7lu %s %4.1f %4.1f %9s %s", t->pid, t->user_name, pr_string, t->ni, t->virt, t->res, t->shr, t->s, t->cpu, t->mem, "test", t->command);

	printf("%s\n", task_info_string);

	return;
}
