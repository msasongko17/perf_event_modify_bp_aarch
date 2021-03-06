#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <asm/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/kernel.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <strings.h>
#include <time.h>

#define CHECK(x) ({int err = (x); \
if (err) { \
fprintf(stderr, "%s: Failed with %d on line %d of file %s\n", strerror(errno), err, __LINE__, __FILE__); \
exit(-1); }\
err;})


static int pgsz;
static void * mmapBuffer = 0;
static int count=0;

static inline long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}
/*
static pid_t gettid() {
    return syscall(__NR_gettid);
}*/

static inline void enable_watchpoint(int fd) {
    CHECK(ioctl(fd, PERF_EVENT_IOC_ENABLE, 0));
}

static inline void disable_watchpoint(int fd) {
    CHECK(ioctl(fd, PERF_EVENT_IOC_DISABLE, 0));
}

static inline void * mmap_wp_buffer(int fd){
    void * buf = mmap(0, 2 * pgsz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
		perror("mmap");
		exit(-1);
    }
    return buf;
}

static inline void unmap_wp_buffer(void * buf){
    CHECK(munmap(buf, 2 * pgsz));
}

static inline void distroy_watchpoint(int fd){
    unmap_wp_buffer(mmapBuffer);
    mmapBuffer = 0;
    //fprintf(stderr, "fd: %d\n");
    //CHECK(close(fd));
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    CHECK(close(fd));
    //close(fd);
}

static void watchpoint_signal_handler(int signum, siginfo_t *info, void *context){
	//fprintf(stderr, "wp trap 1 is detected\n");
        count++;
	distroy_watchpoint(info->si_fd);
	return;
}

static inline void distroy_watchpoint2(int fd){
    unmap_wp_buffer(mmapBuffer);
    mmapBuffer = 0;
    //fprintf(stderr, "fd: %d\n");
    //CHECK(close(fd));
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    //CHECK(close(fd));
    //close(fd);
}

static void watchpoint_signal_handler2(int signum, siginfo_t *info, void *context){
        //fprintf(stderr, "wp trap 2 is detected\n");
        count++;
        distroy_watchpoint2(info->si_fd);
        return;
}

static void InitConfig(){
        
    // Setup the signal handler
    sigset_t block_mask;
    sigfillset(&block_mask);
    // Set a signal handler for SIGUSR1
    struct sigaction sa1 = {
        .sa_sigaction = watchpoint_signal_handler,
        .sa_mask = block_mask,
        .sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK
    };
    
    if(sigaction(SIGRTMIN + 3,  &sa1, 0) == -1) {
        fprintf(stderr, "Failed to set WHICH_SIG handler: %s\n", strerror(errno));
        exit(-1);
    }
    
   struct sigaction sa2 = {
        .sa_sigaction = watchpoint_signal_handler2,
        .sa_mask = block_mask,
        .sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK
    };

    if(sigaction(SIGRTMIN + 4,  &sa2, 0) == -1) {
        fprintf(stderr, "Failed to set WHICH_SIG handler: %s\n", strerror(errno));
        exit(-1);
    }        
 
    pgsz = sysconf(_SC_PAGESIZE);
    
}

static inline int create_watchpoint(uintptr_t address) {
    // Perf event settings
    struct perf_event_attr pe = {
        .type                   = PERF_TYPE_BREAKPOINT,
        .size                   = sizeof(struct perf_event_attr),
        .bp_type                = HW_BREAKPOINT_W | HW_BREAKPOINT_R,
        .bp_len                 = HW_BREAKPOINT_LEN_1,
	   .bp_addr = (uintptr_t) address,
        .sample_period          = 1,
        .sample_type            = (PERF_SAMPLE_IP),
        .exclude_user           = 0,
        .exclude_kernel         = 1,
        .exclude_hv             = 1,
        .disabled               = 0, /* enabled */
    };
        // fresh creation
        // Create the perf_event for this thread on all CPUs with no event group
        int perf_fd = perf_event_open(&pe, 0, -1, -1 /*group*/, 0);
        if (perf_fd == -1) {
            perror("perf_event_open");
		  exit (-1);
        }
        // Set the perf_event file to async mode
        CHECK(fcntl(perf_fd, F_SETFL, fcntl(perf_fd, F_GETFL, 0) | O_ASYNC));
        
        // Tell the file to send a signal when an event occurs
        CHECK(fcntl(perf_fd, F_SETSIG, SIGRTMIN + 3));
        
        // Deliver the signal to this thread
        struct f_owner_ex fown_ex;
        fown_ex.type = F_OWNER_TID;
        fown_ex.pid  = syscall(__NR_gettid); //gettid();
        int ret = fcntl(perf_fd, F_SETOWN_EX, &fown_ex);
        if (ret == -1){
            perror("fcntl");
		  exit (-1);
        }        
        // mmap the file 
        mmapBuffer = mmap_wp_buffer(perf_fd);
        return perf_fd;

}

static inline int create_watchpoint2(uintptr_t address) {
    // Perf event settings
    struct perf_event_attr pe = {
        .type                   = PERF_TYPE_BREAKPOINT,
        .size                   = sizeof(struct perf_event_attr),
        .bp_type                = HW_BREAKPOINT_W | HW_BREAKPOINT_R,
        .bp_len                 = HW_BREAKPOINT_LEN_1,
           .bp_addr = (uintptr_t) address,
        .sample_period          = 1,
        .sample_type            = (PERF_SAMPLE_IP),
        .exclude_user           = 0,
        .exclude_kernel         = 1,
        .exclude_hv             = 1,
        .disabled               = 0, /* enabled */
    };
        // fresh creation
        // Create the perf_event for this thread on all CPUs with no event group
        int perf_fd = perf_event_open(&pe, 0, -1, -1 /*group*/, 0);
        if (perf_fd == -1) {
            perror("perf_event_open");
                  exit (-1);
        }
        // Set the perf_event file to async mode
        CHECK(fcntl(perf_fd, F_SETFL, fcntl(perf_fd, F_GETFL, 0) | O_ASYNC));

        // Tell the file to send a signal when an event occurs
        CHECK(fcntl(perf_fd, F_SETSIG, SIGRTMIN + 4));

	// Deliver the signal to this thread
        struct f_owner_ex fown_ex;
        fown_ex.type = F_OWNER_TID;
        fown_ex.pid  = syscall(__NR_gettid); //gettid();
        int ret = fcntl(perf_fd, F_SETOWN_EX, &fown_ex);
        if (ret == -1){
            perror("fcntl");
                  exit (-1);
        }
        // mmap the file 
        mmapBuffer = mmap_wp_buffer(perf_fd);
        return perf_fd;

}

#if 0
static inline void distroy_watchpoint(int fd){
    unmap_wp_buffer(mmapBuffer);
    mmapBuffer = 0;    
    CHECK(close(fd));
}
#endif

static inline bool modify_watchpoint(int fd, uintptr_t address) {
    // Perf event settings
    struct perf_event_attr pe = {
        .type                   = PERF_TYPE_BREAKPOINT,
        .size                   = sizeof(struct perf_event_attr),
        .bp_type                = HW_BREAKPOINT_W | HW_BREAKPOINT_R,
        .bp_len                 = HW_BREAKPOINT_LEN_1,
	   .bp_addr = (uintptr_t) address,
        .sample_period          = 1,
        .sample_type            = (PERF_SAMPLE_IP),
        .exclude_user           = 0,
        .exclude_kernel         = 1,
        .exclude_hv             = 1,
        .disabled               = 0, /* enabled */
    };
    CHECK(ioctl(fd, PERF_EVENT_IOC_MODIFY_ATTRIBUTES, (unsigned long) (&pe)));
}


static inline void
rmb(void) {
    asm volatile("lfence":::"memory");
}

static inline void ConsumeAllRingBufferData(void  *mbuf) {
    struct perf_event_mmap_page *hdr = (struct perf_event_mmap_page *)mbuf;
    void *data;
    unsigned long tail;
    size_t avail_sz, m, c;
    size_t pgmsk = pgsz - 1;
    /*
     * data points to beginning of buffer payload
     */
    data = ((void *)hdr) + pgsz;
    
    /*
     * position of tail within the buffer payload
     */
    tail = hdr->data_tail & pgmsk;
    
    /*
     * size of what is available
     *
     * data_head, data_tail never wrap around
     */
    avail_sz = hdr->data_head - hdr->data_tail;
    rmb();
#if 0
    if(avail_sz == 0 )
        EMSG("\n avail_sz = %d\n", avail_sz);
    else
        EMSG("\n EEavail_sz = %d\n", avail_sz);
#endif
    // reset tail to head
    hdr->data_tail = hdr->data_head;
}

#define N (1000000)
//#define N (100)
char dummy[N+1];
int main(){
        /* This test compares the timing of default breakpoint creation vs. 
           modifying an existing breakpoint. Each new breakpoint traps and the older one should not trap. */

        InitConfig();

	//fprintf(stderr, "1\n");
        clock_t t1  = clock();
	int sum = 0;
//#if 0
        for(int i = 1 ; i <= N; i++){
                int fd = create_watchpoint((uintptr_t) &dummy[i]);
		//fprintf(stderr, "fd: %d\n", fd);
		//sum += fd;
		//fd = fd;
	        dummy[i] = 1; // Traps
		dummy[i-1] = 1;
		//fprintf(stderr, "should be destroyed\n");
                //distroy_watchpoint(fd);
        }
//#endif
	//fprintf(stderr, "sum: %d\n", sum);
        clock_t t2  = clock();
        assert(count == N);
        count = 0;
//#if  0
        clock_t t3  = clock();
//#if  0
        int fd = create_watchpoint2((uintptr_t) &dummy[0]);
        for(int i = 1 ; i <= N; i++){
                modify_watchpoint(fd, (uintptr_t) &dummy[i]);
 	        dummy[i] = 1; // traps
		dummy[i-1] = 1;
		//fprintf(stderr, "should be destroyed\n");
        }
	//fprintf(stderr, "2\n");
        //distroy_watchpoint(fd);
//#endif
        clock_t t4  = clock();

        //assert(count == N);

        if( count == N)
		printf("\n Test passed\n");
	else
		printf("\n Test failed, count: %d, N: %d\n", count, N);

         printf("Without MODIFY_ATTRIBUTES elapsed: %f seconds\n", (double)(t2-t1) / CLOCKS_PER_SEC);
         printf("With MODIFY_ATTRIBUTES elapsed: %f seconds\n", (double)(t4-t3) / CLOCKS_PER_SEC);
         printf("Speedup: %fx\n", (double)(t2-t1)/(t4-t3));

        return 0;
}




