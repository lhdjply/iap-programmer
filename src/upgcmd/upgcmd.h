/*
 * ArtInChip Upgrade Command Line Tool - C Reimplementation
 * Reverse-engineered from original upgcmd binary (C++14, G++ 6.4.0)
 * Original source path: /home/artmem.com/mintao.duan/workspace/aicupg/upgcmd/main.cpp
 */

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#ifndef UPGCMD_H
#define UPGCMD_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>
#include <libusb-1.0/libusb.h>

#define UPGCMD_VERSION      "V1.4.2"
#define UPGCMD_PROG_NAME    "upgcmd"

#define AIC_USB_VID_BOOTROM    0x33c3
#define AIC_USB_PID_BOOTROM    0x6677
#define AIC_USB_VID_UBOOT      0x33c3
#define AIC_USB_PID_UBOOT      0x0001
#define AIC_USB_VID_GENERIC    0x33c3

#define AIC_MAX_TRANSFER_SIZE   (64 * 1024)
#define AIC_MALLOC_CHUNK_SIZE   (1 * 1024 * 1024)
#define AIC_CMD_HEADER_SIZE     16
#define AIC_CMD_MAGIC           0x41494355

enum aic_cmd_type
{
  AIC_CMD_GET_HWINFO        = 0x00,
  AIC_CMD_GET_TRACEINFO     = 0x01,
  AIC_CMD_WRITE             = 0x02,
  AIC_CMD_READ              = 0x03,
  AIC_CMD_EXEC              = 0x04,
  AIC_CMD_RUN_SHELL_STR     = 0x05,
  AIC_CMD_JTAG_UNLOCK       = 0x06,
  AIC_CMD_CONTINUE_BOOT     = 0x07,
  AIC_CMD_GET_MEM_BUF       = 0x08,
  AIC_CMD_FREE_MEM_BUF      = 0x09,
  AIC_CMD_SET_UPG_CFG       = 0x0A,
  AIC_CMD_SET_UPG_END       = 0x0B,
  AIC_CMD_SET_FWC_META      = 0x10,
  AIC_CMD_GET_BLOCK_SIZE    = 0x11,
  AIC_CMD_SEND_FWC_DATA_START = 0x12,
  AIC_CMD_SEND_FWC_DATA_UPDATE = 0x12,
  AIC_CMD_SEND_FWC_DATA_FINAL = 0x12,
  AIC_CMD_GET_FWC_CRC       = 0x13,
  AIC_CMD_GET_FWC_BURN_RESULT = 0x14,
  AIC_CMD_GET_FWC_RUN_RESULT  = 0x15,
  AIC_CMD_READ_FWC_DATA     = 0x16,
  AIC_CMD_READ_FWC_DATA_FINAL = 0x16,
  AIC_CMD_GET_PARTITION_TABLE = 0x17,
  AIC_CMD_READ_FWC_DATA_START = 0x18,
  AIC_CMD_SET_UART_ARGS     = 0x19,
  AIC_CMD_GET_STORAGE_MEDIA = 0x19,
  AIC_CMD_PING              = 0x19,
  AIC_CMD_APP_PROTO_INIT    = 0x00,
  AIC_CMD_APP_PROTO_EXIT    = 0x00,
};

enum log_level
{
  LOG_EMERG   = 0,
  LOG_ALERT   = 1,
  LOG_CRIT    = 2,
  LOG_ERR     = 3,
  LOG_WARN    = 4,
  LOG_NOTICE  = 5,
  LOG_INFO    = 6,
  LOG_DEBUG   = 7,
};

enum boot_stage { BOOT_ROM_STAGE = 0, UBOOT_STAGE = 1 };
enum media_type { MEDIA_SPI_NOR = 0, MEDIA_SPI_NAND = 1, MEDIA_MMC = 2,
                  MEDIA_SPL = 3, MEDIA_UBI = 4, MEDIA_SPL_NAND = 5
                };

typedef struct
{
  char name[32];
  uint32_t offset;
  uint32_t size;
} partition_t;

typedef struct
{
  char name[16];
  enum media_type type;
  int partition_count;
  partition_t * partitions;
} media_t;

typedef struct
{
  uint8_t data[40];
  /* offset 40 (0x28) */
  uint8_t boot_stage;
  /* offset 41 (0x29): bit0=secure, bit1=encrypt, bit2=anti_rollback */
  uint8_t flags;
  /* offset 42 (0x2a) */
  uint8_t boot_device1;
  /* offset 43 (0x2b) */
  uint8_t boot_device2;
  uint8_t reserved[64];
} device_hwinfo_t;

typedef struct
{
  libusb_device * dev;
  libusb_device_handle * handle;
  uint8_t bus_number;
  uint8_t device_address;
  uint8_t bDeviceClass;
  uint8_t port_numbers[8];
  int port_count;
  int ep_out;
  int ep_in;
  int interface_num;
  int claimed;
  int link_mode;
  int trans_pkt_size;
} upg_device_t;

typedef int (*cmd_handler_t)(upg_device_t * dev, int argc, char ** argv);

typedef struct
{
  const char * name;
  cmd_handler_t handler;
  const char * description;
} cmd_entry_t;

typedef struct
{
  char magic[8];             /* 0x00 "AIC.FW\0\0" */
  char platform[64];         /* 0x08 chip/target */
  char product[64];          /* 0x48 image name */
  char version[64];          /* 0x88 version string */
  char media_type[68];       /* 0xC8 e.g. "spi-nand" */
  char media_id[64];         /* 0x10C e.g. "P=2K,B=128K" */
  uint32_t meta_offset;      /* 0x14C */
  uint32_t meta_size;        /* 0x150 */
  uint32_t file_offset;      /* 0x154 */
  uint32_t file_size;        /* 0x158 */
  uint32_t ex_flag;          /* 0x15C */
  uint32_t ex_offset;        /* 0x160 */
  uint32_t ex_size;          /* 0x164 */
} image_hdr_t;

#define FWC_META_ENTRY_SIZE  512

typedef struct
{
  char magic[8];             /* 0x00 "META\0\0\0\0" */
  char name[64];             /* 0x08 */
  char partition[64];        /* 0x48 */
  uint32_t offset;           /* 0x88 */
  uint32_t size;             /* 0x8C */
  uint32_t crc;              /* 0x90 */
  uint32_t ram;              /* 0x94 */
  char attributes[360];      /* 0x98 (padded to 512 bytes) */
} fwc_meta_t;

typedef struct
{
  int verbose;
  int show_progress;
  char * dev_spec;
  char * image_file;
  char * extract_file;
  int uart_mode;
  int baudrate;
  int debug_level;
  FILE * log_file;
  char * prog_name;
} config_t;

extern config_t g_config;

/* logging */
void aicupg_log_msg(int level, const char * src, int line, const char * fmt, ...) __attribute__((format(printf, 4, 5)));
void aicupg_log_hook_register(const char * prog_name);
void hexdump(const void * data, size_t len);

/* USB */
int upg_usb_dev_get_list(upg_device_t *** dev_list, int * count);
void upg_usb_dev_free_list(upg_device_t ** dev_list, int count);
int upg_usb_dev_open(upg_device_t * dev);
void upg_usb_dev_close(upg_device_t * dev);
int upg_usb_get_port_numbers(upg_device_t * dev, uint8_t * nums, int * cnt);
const char * upg_usb_dev_get_last_error(void);

/* transport */
int aicupg_trans_init(upg_device_t * dev);
void aicupg_trans_exit(upg_device_t * dev);
int aicupg_trans_send_pkt(upg_device_t * dev, const void * data, size_t len);
int aicupg_trans_recv_pkt(upg_device_t * dev, void * buf, size_t * len, int timeout_ms);
int aicupg_set_trans_pkt_size(int size);
int aicupg_get_trans_pkt_size(void);
int aicupg_ping_device(upg_device_t * dev);

/* commands */
int aicupg_cmd_get_hwinfo(upg_device_t * dev, device_hwinfo_t * info);
int aicupg_cmd_get_traceinfo(upg_device_t * dev, char * buf, size_t sz);
int aicupg_cmd_write(upg_device_t * dev, uint32_t addr, const void * data, size_t len);
int aicupg_cmd_read(upg_device_t * dev, uint32_t addr, void * buf, size_t len);
int aicupg_cmd_exec(upg_device_t * dev, uint32_t addr);
int aicupg_cmd_continue_boot(upg_device_t * dev);
int aicupg_cmd_get_mem_buf(upg_device_t * dev, void ** buf, size_t * size);
int aicupg_cmd_free_mem_buf(upg_device_t * dev, void * buf);
int aicupg_cmd_set_upg_cfg(upg_device_t * dev, uint32_t total, uint32_t blk);
int aicupg_cmd_set_upg_end(upg_device_t * dev);
int aicupg_cmd_set_fwc_meta(upg_device_t * dev, const fwc_meta_t * meta);
int aicupg_cmd_set_uart_args(upg_device_t * dev, int baudrate);
int aicupg_cmd_get_block_size(upg_device_t * dev, uint32_t * block_size);
int aicupg_cmd_send_fwc_data_start(upg_device_t * dev, uint32_t total_size);
int aicupg_cmd_send_fwc_data_update(upg_device_t * dev, const void * data, size_t len);
int aicupg_cmd_send_fwc_data_final(upg_device_t * dev);
int aicupg_cmd_get_fwc_crc(upg_device_t * dev, uint32_t * crc);
int aicupg_cmd_get_fwc_burn_result(upg_device_t * dev, int * result);
int aicupg_cmd_get_fwc_run_result(upg_device_t * dev, int * result);
int aicupg_cmd_read_fwc_data_start(upg_device_t * dev, const char * name);
int aicupg_cmd_read_fwc_data(upg_device_t * dev, void * buf, size_t * len);
int aicupg_cmd_read_fwc_data_final(upg_device_t * dev);
int aicupg_cmd_get_partition_table(upg_device_t * dev, const char * media,
                                   partition_t ** parts, int * count);
int aicupg_cmd_get_storage_media(upg_device_t * dev, media_t ** media, int * count);
int aicupg_cmd_jtag_unlock(upg_device_t * dev, const void * data, size_t len);
int aicupg_cmd_run_shell_str(upg_device_t * dev, const char * cmd);
int aicupg_app_proto_init(upg_device_t * dev);
void aicupg_app_proto_exit(upg_device_t * dev);
void aicupg_set_chip_version(uint32_t version);
uint32_t aicupg_get_chip_version(void);

/* image */
int image_hdr_read(const char * filename, image_hdr_t * hdr);
int image_meta_read(const char * filename, fwc_meta_t * meta, int * count);
int image_partition_read(const char * filename, partition_t * parts, int * count);
int image_do_upgrade(upg_device_t * dev, const char * image_file);
int image_do_ramboot(upg_device_t * dev, const char * fwc_name,
                     uint32_t ram_addr, const char * file);
int image_extract(const char * image_file);
int image_display_info(const char * image_file);
int check_in_uboot_stage(upg_device_t * dev);

/* storage */
int get_storage_media(upg_device_t * dev, media_t ** media, int * count);
void aicupg_partition_free(partition_t * partitions);
partition_t * aicupg_new_partition(const char * name, uint32_t offset, uint32_t size);

/* progress */
void show_progress_init(void);
void show_progress_print(int current, int total);

/* library */
void libaicupg_init(void);
void libaicupg_exit(void);
void libaicupg_set_verbose(int verbose);
int libaicupg_get_verbose(void);
int libaicupg_get_link_mode(upg_device_t * dev);
void libaicimg_set_verbose(int verbose);
void libaicimg_set_progress(int enable);
int libaicimg_set_max_baudrate(int baudrate);

/* env */
int get_env_debug_level(void);
char * env_get(const char * key);

/* callbacks for library integration */
typedef void (*upgcmd_output_cb)(const char * msg, void * userdata);
typedef void (*upgcmd_progress_cb)(int current, int total, void * userdata);

void upgcmd_set_output_callback(upgcmd_output_cb cb, void * userdata);
void upgcmd_set_progress_callback(upgcmd_progress_cb cb, void * userdata);
void aicupg_output(const char * fmt, ...) __attribute__((format(printf, 1, 2)));

/* main entry point exposed for library use */
int upgcmd(int argc, char ** argv);

#endif
