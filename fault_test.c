/*
 * cxllib fault test
 * Andrew Donnellan, OzLabs, IBM Corporation, 2016, GPL v2+
 * See http://unix.stackexchange.com/questions/188170/generate-major-page-faults
 * and http://stackoverflow.com/questions/23302763/measure-page-faults-from-a-c-program
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <misc/cxl.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>

// TODO: Hugepages?
// TODO: Writing
// TODO: The "cxllib page fault count" number is pretty meaningless.

static long perf_event_open(struct perf_event_attr *hw_event,
			    pid_t pid,
			    int cpu,
			    int group_fd,
			    unsigned long flags) {
	int ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
			  group_fd, flags);
	return ret;
}

int main(int argc, char * argv[]) {
	struct cxllib_ioctl_test_handle_fault *work;
	int adapter_fd;
	int file_fd;
	struct stat file_stats;
	char *filename;
	char *file_map;
	int result;
	int rc;
	int i;
	uint64_t page_fault_count;
	int page_faults_fd;
	struct perf_event_attr pe_attr_page_faults;

	if (argc != 2) {
		printf("usage: %s big_file\n", argv[0]);
		return 1;
	}

	// Set up perf
	memset(&pe_attr_page_faults, 0, sizeof(pe_attr_page_faults));
	pe_attr_page_faults.size = sizeof(pe_attr_page_faults);
	pe_attr_page_faults.type =  PERF_TYPE_SOFTWARE;
	pe_attr_page_faults.config = PERF_COUNT_SW_PAGE_FAULTS;
	pe_attr_page_faults.disabled = 1;
	pe_attr_page_faults.exclude_kernel = 1;
	page_faults_fd = perf_event_open(&pe_attr_page_faults, 0, -1, -1, 0);
	if (page_faults_fd == -1) {
		printf("perf_event_open failed for page faults: %s\n", strerror(errno));
		return -1;
	}

	// Set up mmap
	filename = argv[1];
	file_fd = open(filename, O_RDONLY);
	fstat(file_fd, &file_stats);
	posix_fadvise(file_fd, 0, file_stats.st_size, POSIX_FADV_DONTNEED);
	file_map = (char *) mmap(NULL, file_stats.st_size, PROT_READ,
				 MAP_SHARED, file_fd, 0);
	if (file_map == MAP_FAILED) {
		perror("Map failed!");
		return 1;
	}

	// Start counting
	ioctl(page_faults_fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(page_faults_fd, PERF_EVENT_IOC_ENABLE, 0);

	// Trigger page faults through cxllib
	adapter_fd = open("/dev/cxl/afu0.0m", O_NONBLOCK);
	work = calloc(1, sizeof(struct cxllib_ioctl_test_handle_fault));
	work->addr = (__u64)file_map;
	work->size = file_stats.st_size;
	work->flags = 0;
	rc = ioctl(adapter_fd, CXLLIB_IOCTL_TEST_HANDLE_FAULT, work);
	printf("ioctl returned: %d\n", rc);
	close(adapter_fd);

	// Stop counting
	ioctl(page_faults_fd, PERF_EVENT_IOC_DISABLE, 0);
	read(page_faults_fd, &page_fault_count, sizeof(page_fault_count));
	printf("cxllib page fault count: %" PRIu64 "\n", page_fault_count);

	// Start counting again
	ioctl(page_faults_fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(page_faults_fd, PERF_EVENT_IOC_ENABLE, 0);

	// Now try to access everything...
	for (i = 0; i < file_stats.st_size; i++) {
		result += file_map[i];
	}

	printf("meaningless result: %d\n", result);

	// Stop counting
	ioctl(page_faults_fd, PERF_EVENT_IOC_DISABLE, 0);
	read(page_faults_fd, &page_fault_count, sizeof(page_fault_count));
	printf("non-cxllib page fault count: %" PRIu64 "\n", page_fault_count);

	// Cleanup
	munmap(file_map, file_stats.st_size);
	close(file_fd);
}
