#ifndef _BLKTAP_H_
#define _BLKTAP_H_

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <xen/blkif.h>
#include <xen/grant_table.h>

extern int blktap_debug_level;
extern int blktap_ring_major;
extern int blktap_device_major;

#define BTPRINTK(level, tag, force, _f, _a...)				\
	do {								\
		if (blktap_debug_level > level &&			\
		    (force || printk_ratelimit()))			\
			printk(tag "%s: " _f, __func__, ##_a);		\
	} while (0)

#define BTDBG(_f, _a...)             BTPRINTK(8, KERN_DEBUG, 1, _f, ##_a)
#define BTINFO(_f, _a...)            BTPRINTK(0, KERN_INFO, 0, _f, ##_a)
#define BTWARN(_f, _a...)            BTPRINTK(0, KERN_WARNING, 0, _f, ##_a)
#define BTERR(_f, _a...)             BTPRINTK(0, KERN_ERR, 0, _f, ##_a)

#define MAX_BLKTAP_DEVICE            1024

#define BLKTAP_CONTROL               1
#define BLKTAP_DEVICE                4
#define BLKTAP_DEVICE_CLOSED         5
#define BLKTAP_SHUTDOWN_REQUESTED    8

/* blktap IOCTLs: */
#define BLKTAP2_IOCTL_KICK_FE        1
#define BLKTAP2_IOCTL_ALLOC_TAP      200
#define BLKTAP2_IOCTL_FREE_TAP       201
#define BLKTAP2_IOCTL_CREATE_DEVICE  202
#define BLKTAP2_IOCTL_REMOVE_DEVICE  207

#define BLKTAP2_MAX_MESSAGE_LEN      256

#define BLKTAP2_RING_MESSAGE_CLOSE   3

#define BLKTAP_REQUEST_FREE          0
#define BLKTAP_REQUEST_PENDING       1

/*
 * The maximum number of requests that can be outstanding at any time
 * is determined by
 *
 *   [mmap_alloc * MAX_PENDING_REQS * BLKIF_MAX_SEGMENTS_PER_REQUEST] 
 *
 * where mmap_alloc < MAX_DYNAMIC_MEM.
 *
 * TODO:
 * mmap_alloc is initialised to 2 and should be adjustable on the fly via
 * sysfs.
 */
#define BLK_RING_SIZE		__RING_SIZE((struct blkif_sring *)0, PAGE_SIZE)
#define MAX_DYNAMIC_MEM		BLK_RING_SIZE
#define MAX_PENDING_REQS	BLK_RING_SIZE
#define MMAP_PAGES (MAX_PENDING_REQS * BLKIF_MAX_SEGMENTS_PER_REQUEST)
#define MMAP_VADDR(_start, _req, _seg)					\
        (_start +                                                       \
         ((_req) * BLKIF_MAX_SEGMENTS_PER_REQUEST * PAGE_SIZE) +        \
         ((_seg) * PAGE_SIZE))

struct grant_handle_pair {
	grant_handle_t                 kernel;
	grant_handle_t                 user;
};
#define INVALID_GRANT_HANDLE           0xFFFF

struct blktap_handle {
	unsigned int                   ring;
	unsigned int                   device;
	unsigned int                   minor;
};

struct blktap_params {
	char                           name[BLKTAP2_MAX_MESSAGE_LEN];
	unsigned long long             capacity;
	unsigned long                  sector_size;
};

struct blktap_device {
	spinlock_t                     lock;
	struct gendisk                *gd;
};

struct blktap_ring {
	struct task_struct            *task;

	struct vm_area_struct         *vma;
	struct blkif_front_ring             ring;
	struct vm_foreign_map          foreign_map;
	unsigned long                  ring_vstart;
	unsigned long                  user_vstart;

	wait_queue_head_t              poll_wait;

	dev_t                          devno;
	struct device                 *dev;
};

struct blktap_statistics {
	unsigned long                  st_print;
	int                            st_rd_req;
	int                            st_wr_req;
	int                            st_oo_req;
	int                            st_rd_sect;
	int                            st_wr_sect;
	s64                            st_rd_cnt;
	s64                            st_rd_sum_usecs;
	s64                            st_rd_max_usecs;
	s64                            st_wr_cnt;
	s64                            st_wr_sum_usecs;
	s64                            st_wr_max_usecs;	
};

struct blktap_request {
	struct request                *rq;
	uint16_t                       usr_idx;

	uint8_t                        status;
	atomic_t                       pendcnt;
	uint8_t                        nr_pages;
	unsigned short                 operation;

	struct timeval                 time;
	struct grant_handle_pair       handles[BLKIF_MAX_SEGMENTS_PER_REQUEST];
	struct list_head               free_list;
};

struct blktap {
	int                            minor;
	unsigned long                  dev_inuse;

	struct blktap_ring             ring;
	struct blktap_device           device;

	int                            pending_cnt;
	struct blktap_request         *pending_requests[MAX_PENDING_REQS];
	struct scatterlist             sg[BLKIF_MAX_SEGMENTS_PER_REQUEST];

	wait_queue_head_t              remove_wait;
	struct work_struct             remove_work;
	char                           name[BLKTAP2_MAX_MESSAGE_LEN];

	struct blktap_statistics       stats;
};

extern struct mutex blktap_lock;
extern struct blktap **blktaps;
extern int blktap_max_minor;

int blktap_control_destroy_tap(struct blktap *);
size_t blktap_control_debug(struct blktap *, char *, size_t);

int blktap_ring_init(void);
void blktap_ring_exit(void);
size_t blktap_ring_debug(struct blktap *, char *, size_t);
int blktap_ring_create(struct blktap *);
int blktap_ring_destroy(struct blktap *);
void blktap_ring_kick_user(struct blktap *);
void blktap_ring_kick_all(void);

int blktap_sysfs_init(void);
void blktap_sysfs_exit(void);
int blktap_sysfs_create(struct blktap *);
void blktap_sysfs_destroy(struct blktap *);

int blktap_device_init(void);
void blktap_device_exit(void);
size_t blktap_device_debug(struct blktap *, char *, size_t);
int blktap_device_create(struct blktap *, struct blktap_params *);
int blktap_device_destroy(struct blktap *);
void blktap_device_destroy_sync(struct blktap *);
int blktap_device_run_queue(struct blktap *);
void blktap_device_end_request(struct blktap *, struct blktap_request *, int);

int blktap_request_pool_init(void);
void blktap_request_pool_free(void);
int blktap_request_pool_grow(void);
int blktap_request_pool_shrink(void);
struct blktap_request *blktap_request_allocate(struct blktap *);
void blktap_request_free(struct blktap *, struct blktap_request *);
struct page *request_to_page(struct blktap_request *, int);

static inline unsigned long
request_to_kaddr(struct blktap_request *req, int seg)
{
	unsigned long pfn = page_to_pfn(request_to_page(req, seg));
	return (unsigned long)pfn_to_kaddr(pfn);
}

#endif