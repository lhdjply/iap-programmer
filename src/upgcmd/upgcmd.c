/*
 * ArtInChip Upgrade Command Line Tool - C Reimplementation
 * Reverse-engineered from original upgcmd binary
 * Original: C++14, G++ 6.4.0, main.cpp (1349+ lines)
 */

#include "upgcmd.h"

/* ---- Global state ---- */
config_t g_config =
{
  .verbose = 0,
  .show_progress = 0,
  .dev_spec = NULL,
  .image_file = NULL,
  .extract_file = NULL,
  .uart_mode = 0,
  .baudrate = 115200,
  .debug_level = LOG_INFO,
  .log_file = NULL,
};

static int g_chip_version = 0;
static int g_trans_pkt_size = 0x10000;  /* 64KB default */
static char g_last_error[256] = {0};
static uint8_t g_cmd_counter = 0;
static uint8_t __fwc_meta_sent = 0;

static upgcmd_output_cb _upg_output_cb = NULL;
static void * _upg_output_ud = NULL;
static upgcmd_progress_cb _upg_progress_cb = NULL;
static void * _upg_progress_ud = NULL;

void upgcmd_set_output_callback(upgcmd_output_cb cb, void * userdata)
{
  _upg_output_cb = cb;
  _upg_output_ud = userdata;
}

void upgcmd_set_progress_callback(upgcmd_progress_cb cb, void * userdata)
{
  _upg_progress_cb = cb;
  _upg_progress_ud = userdata;
}

void aicupg_output(const char * fmt, ...)
{
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if(_upg_output_cb)
    _upg_output_cb(buf, _upg_output_ud);
  else
    fputs(buf, stdout);
}

/* ---- Logging ---- */
static const char * level_strings[] =
{
  "[EMERG ]", "[ALERT ]", "[CRIT  ]", "[ERROR ]",
  "[WARN  ]", "[NOTICE]", "[INFO  ]", "[DEBUG ]",
};

static void log_msg_v(int level, const char * src, int line, const char * fmt, va_list ap)
{
  char buf[4096];
  struct timeval tv;
  struct tm * tm_info;
  int off;

  gettimeofday(&tv, NULL);
  time_t now = tv.tv_sec;
  tm_info = localtime(&now);

  off = snprintf(buf, sizeof(buf), "%s ", level_strings[level]);
  if(src && src[0])
  {
    off += strftime(buf + off, sizeof(buf) - off, "%Y-%m-%d %H:%M:%S", tm_info);
    off += snprintf(buf + off, sizeof(buf) - off, " [%s]: ", src);
  }
  if(line > 0)
    off += snprintf(buf + off, sizeof(buf) - off, "line %d: ", line);

  vsnprintf(buf + off, sizeof(buf) - off, fmt, ap);

  aicupg_output("%s", buf);

  if(g_config.log_file)
  {
    fprintf(g_config.log_file, "%s\n", buf);
    fflush(g_config.log_file);
  }
}

void aicupg_log_msg(int level, const char * src, int line, const char * fmt, ...)
{
  if(level > g_config.debug_level) return;
  va_list ap;
  va_start(ap, fmt);
  log_msg_v(level, src, line, fmt, ap);
  va_end(ap);
}

void aicupg_log_hook_register(const char * prog_name)
{
  (void)prog_name;
  g_config.debug_level = get_env_debug_level();
}

void hexdump(const void * data, size_t len)
{
  const uint8_t * p = (const uint8_t *)data;
  for(size_t i = 0; i < len; i += 16)
  {
    aicupg_output("%08lx: ", (unsigned long)i);
    for(size_t j = 0; j < 16; j++)
    {
      if(i + j < len)
        aicupg_output("%02x ", p[i + j]);
      else
        aicupg_output("   ");
    }
    aicupg_output(" ");
    for(size_t j = 0; j < 16 && i + j < len; j++)
    {
      char c = p[i + j];
      aicupg_output("%c", (c >= 32 && c < 127) ? c : '.');
    }
    aicupg_output("\n");
  }
}

/* ---- Environment helpers ---- */
int get_env_debug_level(void)
{
  char * env = getenv("UPGCMD_DEBUG");
  if(!env) env = getenv("AICUPG_DEBUG");
  if(!env) return LOG_INFO;
  int lvl = atoi(env);
  if(lvl < LOG_EMERG) lvl = LOG_EMERG;
  if(lvl > LOG_DEBUG) lvl = LOG_DEBUG;
  return lvl;
}

char * env_get(const char * key)
{
  return getenv(key);
}

/* ---- USB device enumeration ---- */
static int __is_aic_dev(libusb_device * dev)
{
  struct libusb_device_descriptor desc;
  if(libusb_get_device_descriptor(dev, &desc) < 0)
    return 0;
  return (desc.idVendor == AIC_USB_VID_GENERIC &&
          (desc.idProduct == AIC_USB_PID_BOOTROM ||
           desc.idProduct == AIC_USB_PID_UBOOT));
}

static int find_endpoint(libusb_device * dev, int * ep_out, int * ep_in, int * iface_num)
{
  struct libusb_device_descriptor desc;
  if(libusb_get_device_descriptor(dev, &desc) < 0)
    return -1;

  /* Try all configurations */
  for(uint8_t cfg_idx = 0; cfg_idx < desc.bNumConfigurations; cfg_idx++)
  {
    struct libusb_config_descriptor * config = NULL;
    if(libusb_get_config_descriptor(dev, cfg_idx, &config) < 0)
      continue;

    for(int i = 0; i < config->bNumInterfaces; i++)
    {
      const struct libusb_interface * iface = &config->interface[i];
      for(int j = 0; j < iface->num_altsetting; j++)
      {
        const struct libusb_interface_descriptor * altsetting = &iface->altsetting[j];
        int found_out = 0, found_in = 0;
        int tmp_ep_out = -1, tmp_ep_in = -1;

        for(int k = 0; k < altsetting->bNumEndpoints; k++)
        {
          const struct libusb_endpoint_descriptor * ep = &altsetting->endpoint[k];
          if((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)
          {
            if(ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)
            {
              tmp_ep_in = ep->bEndpointAddress;
              found_in = 1;
            }
            else
            {
              tmp_ep_out = ep->bEndpointAddress;
              found_out = 1;
            }
          }
        }

        if(found_out && found_in)
        {
          *ep_out = tmp_ep_out;
          *ep_in = tmp_ep_in;
          *iface_num = altsetting->bInterfaceNumber;
          libusb_free_config_descriptor(config);
          return 0;
        }
      }
    }
    libusb_free_config_descriptor(config);
  }
  return -1;
}

static int claim_interface(upg_device_t * dev)
{
  if(dev->claimed) return 0;

  int ret = libusb_claim_interface(dev->handle, dev->interface_num);
  if(ret == 0)
  {
    dev->claimed = 1;
    return 0;
  }

  /* detach kernel driver and retry */
  if(ret == LIBUSB_ERROR_BUSY)
  {
    if(libusb_kernel_driver_active(dev->handle, dev->interface_num) == 1)
    {
      libusb_detach_kernel_driver(dev->handle, dev->interface_num);
    }
    ret = libusb_claim_interface(dev->handle, dev->interface_num);
    if(ret == 0)
    {
      dev->claimed = 1;
      return 0;
    }
  }

  snprintf(g_last_error, sizeof(g_last_error),
           "claim_interface failed: %s", libusb_error_name(ret));
  return -1;
}

int upg_usb_get_port_numbers(upg_device_t * dev, uint8_t * nums, int * cnt)
{
  if(!dev || !dev->dev) return -1;
  int n = libusb_get_port_numbers(dev->dev, nums, 8);
  if(n <= 0) return -1;
  if(cnt) *cnt = n;
  return 0;
}

int upg_usb_dev_get_list(upg_device_t *** dev_list, int * count)
{
  libusb_device ** list = NULL;
  upg_device_t ** result = NULL;
  int total = 0;
  int capacity = 16;

  ssize_t ndev = libusb_get_device_list(NULL, &list);
  if(ndev < 0)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "libusb_get_device_list failed: %s", libusb_error_name((int)ndev));
    *dev_list = NULL;
    *count = 0;
    return -1;
  }

  result = (upg_device_t **)calloc(capacity, sizeof(upg_device_t *));
  if(!result)
  {
    libusb_free_device_list(list, 1);
    return -1;
  }

  for(ssize_t i = 0; i < ndev; i++)
  {
    if(!__is_aic_dev(list[i])) continue;

    if(total >= capacity)
    {
      capacity *= 2;
      result = (upg_device_t **)realloc(result, capacity * sizeof(upg_device_t *));
    }

    upg_device_t * dev = (upg_device_t *)calloc(1, sizeof(upg_device_t));
    dev->dev = list[i];
    libusb_ref_device(list[i]);
    dev->bus_number = (uint8_t)libusb_get_bus_number(list[i]);
    dev->device_address = (uint8_t)libusb_get_device_address(list[i]);

    struct libusb_device_descriptor desc;
    if(libusb_get_device_descriptor(list[i], &desc) == 0)
      dev->bDeviceClass = desc.bDeviceClass;

    dev->ep_out = -1;
    dev->ep_in = -1;

    find_endpoint(list[i], &dev->ep_out, &dev->ep_in, &dev->interface_num);

    uint8_t ports[8];
    int n = libusb_get_port_numbers(list[i], ports, 8);
    if(n > 0)
    {
      dev->port_count = n;
      memcpy(dev->port_numbers, ports, n);
    }

    result[total++] = dev;
  }

  libusb_free_device_list(list, 0);
  *dev_list = result;
  *count = total;
  return 0;
}

void upg_usb_dev_free_list(upg_device_t ** dev_list, int count)
{
  if(!dev_list) return;
  for(int i = 0; i < count; i++)
  {
    if(dev_list[i])
    {
      if(dev_list[i]->dev)
        libusb_unref_device(dev_list[i]->dev);
      free(dev_list[i]);
    }
  }
  free(dev_list);
}

int upg_usb_dev_open(upg_device_t * dev)
{
  if(!dev || !dev->dev) return -1;

  int ret = libusb_open(dev->dev, &dev->handle);
  if(ret != 0)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "Open upg device failed: %s", libusb_error_name(ret));
    return -1;
  }

  /* Detach kernel driver on all interfaces of the device */
  struct libusb_device_descriptor desc;
  if(libusb_get_device_descriptor(dev->dev, &desc) == 0)
  {
    for(uint8_t cfg_idx = 0; cfg_idx < desc.bNumConfigurations; cfg_idx++)
    {
      struct libusb_config_descriptor * config = NULL;
      if(libusb_get_config_descriptor(dev->dev, cfg_idx, &config) == 0)
      {
        /* Detach kernel drivers from all interfaces */
        for(int i = 0; i < config->bNumInterfaces; i++)
        {
          for(int j = 0; j < config->interface[i].num_altsetting; j++)
          {
            int iface_num = config->interface[i].altsetting[j].bInterfaceNumber;
            if(libusb_kernel_driver_active(dev->handle, iface_num) == 1)
            {
              libusb_detach_kernel_driver(dev->handle, iface_num);
            }
          }
        }
        libusb_free_config_descriptor(config);
      }
    }
  }

  /* Set configuration */
  int cfg = -1;
  if(libusb_get_configuration(dev->handle, &cfg) == 0)
  {
    if(cfg != 1)
    {
      ret = libusb_set_configuration(dev->handle, 1);
      if(ret != 0 && ret != LIBUSB_ERROR_BUSY)
      {
        /* Some devices may not support set_configuration, try anyway */
      }
    }
  }
  else
  {
    libusb_set_configuration(dev->handle, 1);
  }

  if(claim_interface(dev) < 0)
  {
    libusb_close(dev->handle);
    dev->handle = NULL;
    return -1;
  }

  dev->trans_pkt_size = g_trans_pkt_size;
  return 0;
}

void upg_usb_dev_close(upg_device_t * dev)
{
  if(!dev) return;
  if(dev->claimed && dev->handle)
  {
    libusb_release_interface(dev->handle, dev->interface_num);
    dev->claimed = 0;
  }
  if(dev->handle)
  {
    libusb_close(dev->handle);
    dev->handle = NULL;
  }
}

const char * upg_usb_dev_get_last_error(void)
{
  return g_last_error;
}

/*
 * Transport layer - "USBC" framing over USB bulk transfer
 *
 * Transport frame (31 bytes total):
 *   [0:4]   magic    = 0x43425355 ("USBC")
 *   [4:8]   pkt_number (uint32_t)
 *   [8:12]  data_len   (uint32_t)
 *   [12:13] reserved
 *   [13:14] reserved
 *   [14]    version = 0x01
 *   [15]    type    = 0x01
 *   [16:31] reserved (zero-filled)
 *
 * App protocol header (16 bytes) is placed inside the data portion:
 *   [0:4]   magic    = 0x43475055 ("UPGC")
 *   [4]     version  = 0x01
 *   [5]     pkt_type = 0x01 (request) / 0x02 (response)
 *   [6]     cmd      = command byte
 *   [7]     flags    = 0x00
 *   [8:12]  data_len (uint32_t)
 *   [12:16] checksum = magic + (flags<<24|cmd<<16|pkt_type<<8|version) + data_len
 *
 * The transport layer sends one frame, then sends data if data_len > 0.
 */

/* ---- Transport layer ---- */
int aicupg_trans_init(upg_device_t * dev)
{
  (void)dev;
  g_cmd_counter = 0;
  return 0;
}

void aicupg_trans_exit(upg_device_t * dev)
{
  (void)dev;
}

int aicupg_set_trans_pkt_size(int size)
{
  g_trans_pkt_size = size;
  return 0;
}
int aicupg_get_trans_pkt_size(void)
{
  return g_trans_pkt_size;
}

/*
 * Build app protocol header (16 bytes).
 * magic=0x43475055 ("UPGC"), version=1, pkt_type=1, flags=0
 */
static void gen_app_header(uint8_t hdr[16], uint8_t cmd, uint32_t data_len)
{
  uint32_t magic = 0x43475055;
  uint8_t version = 0x01;
  uint8_t pkt_type = 0x01;
  uint8_t flags = 0x00;

  hdr[0] = (uint8_t)(magic);
  hdr[1] = (uint8_t)(magic >> 8);
  hdr[2] = (uint8_t)(magic >> 16);
  hdr[3] = (uint8_t)(magic >> 24);
  hdr[4] = version;
  hdr[5] = pkt_type;
  hdr[6] = cmd;
  hdr[7] = flags;
  hdr[8] = (uint8_t)(data_len);
  hdr[9] = (uint8_t)(data_len >> 8);
  hdr[10] = (uint8_t)(data_len >> 16);
  hdr[11] = (uint8_t)(data_len >> 24);

  /* checksum */
  uint32_t cksum = magic + (flags << 24) + (cmd << 16) + (pkt_type << 8) + version + data_len;
  hdr[12] = (uint8_t)(cksum);
  hdr[13] = (uint8_t)(cksum >> 8);
  hdr[14] = (uint8_t)(cksum >> 16);
  hdr[15] = (uint8_t)(cksum >> 24);
}

/*
 * Build transport frame (31 bytes).
 * magic=0x43425355 ("USBC"), version=1, type=1 (send) or 2 (recv)
 * For recv: byte[12]=0x80, byte[15]=0x02
 * For send: byte[12]=0x00, byte[15]=0x01
 */
static void gen_trans_frame(uint8_t frame[31], uint32_t pkt_number, uint32_t data_len, int is_recv)
{
  uint32_t magic = 0x43425355;
  memset(frame, 0, 31);
  frame[0] = (uint8_t)(magic);
  frame[1] = (uint8_t)(magic >> 8);
  frame[2] = (uint8_t)(magic >> 16);
  frame[3] = (uint8_t)(magic >> 24);
  frame[4] = (uint8_t)(pkt_number);
  frame[5] = (uint8_t)(pkt_number >> 8);
  frame[6] = (uint8_t)(pkt_number >> 16);
  frame[7] = (uint8_t)(pkt_number >> 24);
  frame[8] = (uint8_t)(data_len);
  frame[9] = (uint8_t)(data_len >> 8);
  frame[10] = (uint8_t)(data_len >> 16);
  frame[11] = (uint8_t)(data_len >> 24);
  if(is_recv)
  {
    frame[12] = 0x80;
    frame[14] = 0x01;
    frame[15] = 0x02;
  }
  else
  {
    frame[14] = 0x01;
    frame[15] = 0x01;
  }
}

/* Send: transport frame + app_header + optional data in a single bulk transfer */
int aicupg_trans_send_pkt(upg_device_t * dev, const void * app_data, size_t app_len)
{
  if(!dev || !dev->handle || dev->ep_out < 0) return 0;

  /* Guard: data must fit within one transfer packet */
  if((uint32_t)app_len > (uint32_t)aicupg_get_trans_pkt_size())
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "data size %lu exceeds pkt_size %d", (unsigned long)app_len, aicupg_get_trans_pkt_size());
    return 0;
  }

  uint32_t pkt_number = g_cmd_counter++;
  uint8_t frame[31];
  gen_trans_frame(frame, pkt_number, (uint32_t)app_len, 0);

  /* Step 1: send transport frame (31 bytes) */
  int transferred = 0;
  int ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_out,
                                 frame, 31, &transferred, 10000);
  if(ret != 0 || transferred != 31)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "send frame failed: %s (%d/%d)", libusb_error_name(ret), transferred, 31);
    return 0;
  }

  /* Step 2: send app data */
  if(app_data && app_len > 0)
  {
    ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_out,
                               (unsigned char *)app_data, (int)app_len,
                               &transferred, 10000);
    if(ret != 0 || transferred != (int)app_len)
    {
      snprintf(g_last_error, sizeof(g_last_error),
               "send data failed: %s (%d/%lu)", libusb_error_name(ret), transferred, (unsigned long)app_len);
      return 0;
    }
  }

  /* Step 3: read 13-byte ACK from device (magic "USBS" = 0x53425355) */
  if(dev->ep_in >= 0)
  {
    uint8_t ack[13];
    ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_in,
                               ack, 13, &transferred, 10000);
    if(ret != 0 || transferred != 13)
    {
      snprintf(g_last_error, sizeof(g_last_error),
               "send ack failed: %s (%d/%d)", libusb_error_name(ret), transferred, 13);
      return 0;
    }

    /* Verify ack magic = "USBS" */
    uint32_t ack_magic = ack[0] | ((uint32_t)ack[1] << 8) |
                         ((uint32_t)ack[2] << 16) | ((uint32_t)ack[3] << 24);
    if(ack_magic != 0x53425355)
    {
      snprintf(g_last_error, sizeof(g_last_error),
               "bad ack magic: 0x%08x", ack_magic);
      return 0;
    }

    uint32_t ack_pkt = ack[4] | ((uint32_t)ack[5] << 8) |
                       ((uint32_t)ack[6] << 16) | ((uint32_t)ack[7] << 24);
    if(ack_pkt != pkt_number)
    {
      snprintf(g_last_error, sizeof(g_last_error),
               "ack pkt mismatch: %u != %u", ack_pkt, pkt_number);
      return 0;
    }
  }

  return (int)app_len;
}

/* Receive: transport frame + optional app data */
int aicupg_trans_recv_pkt(upg_device_t * dev, void * buf, size_t * len, int timeout_ms)
{
  if(!dev || !dev->handle || dev->ep_in < 0 || dev->ep_out < 0) return -1;

  size_t expected = len ? *len : 0;
  uint32_t pkt_number = g_cmd_counter++;
  int transferred = 0;
  int tmo = timeout_ms > 0 ? timeout_ms : 10000;

  /* Step 1: send transport frame on EP OUT to request data */
  uint8_t req_frame[31];
  gen_trans_frame(req_frame, pkt_number, (uint32_t)expected, 1);

  int ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_out,
                                 req_frame, 31, &transferred, tmo);
  if(ret != 0 || transferred != 31)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "recv: send req failed: %s (%d/31)", libusb_error_name(ret), transferred);
    return 0;
  }

  /* Step 2: receive data from EP IN */
  if(expected > 0 && buf)
  {
    ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_in,
                               (unsigned char *)buf, (int)expected,
                               &transferred, tmo);
    if(ret != 0)
    {
      snprintf(g_last_error, sizeof(g_last_error),
               "recv data failed: %s", libusb_error_name(ret));
      return 0;
    }
    *len = (size_t)transferred;
  }
  else if(len)
  {
    *len = 0;
  }

  /* Step 3: read 13-byte ACK/footer ("USBS") */
  uint8_t ack[13];
  ret = libusb_bulk_transfer(dev->handle, (unsigned char)dev->ep_in,
                             ack, 13, &transferred, tmo);
  if(ret != 0 || transferred != 13)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "recv ack failed: %s", libusb_error_name(ret));
    return 0;
  }

  uint32_t ack_magic = ack[0] | ((uint32_t)ack[1] << 8) |
                       ((uint32_t)ack[2] << 16) | ((uint32_t)ack[3] << 24);
  if(ack_magic != 0x53425355)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "recv bad ack magic: 0x%08x", ack_magic);
    return 0;
  }

  return (int)expected;
}

int aicupg_ping_device(upg_device_t * dev)
{
  /* Send a PING to wake up / initialize device USB stack */
  uint8_t app_hdr[16];
  gen_app_header(app_hdr, 0x19, 0);
  aicupg_trans_send_pkt(dev, app_hdr, 16);

  /* Try to get hwinfo to determine boot stage and set pkt_size */
  device_hwinfo_t info = {0};
  if(aicupg_cmd_get_hwinfo(dev, &info) == 0)
  {
    if(info.boot_stage == UBOOT_STAGE)
      aicupg_set_trans_pkt_size(0x10000);
    else
      aicupg_set_trans_pkt_size(0x800);
    return (info.boot_stage == UBOOT_STAGE) ? 1 : 0;
  }

  /* Default: U-Boot mode, 64KB packets */
  aicupg_set_trans_pkt_size(0x10000);
  return 1;
}

/* ---- Protocol helpers ---- */
static int send_command(upg_device_t * dev, uint16_t cmd, const void * data,
                        size_t data_len, void * resp, size_t * resp_len, int timeout_ms)
{
  int tmo = timeout_ms > 0 ? timeout_ms : 5000;

  /* Step 1: build app header + optional payload, send via transport layer */
  size_t app_size = 16 + data_len;
  uint8_t * app_buf = (uint8_t *)malloc(app_size);
  if(!app_buf) return -1;

  gen_app_header(app_buf, (uint8_t)cmd, (uint32_t)data_len);
  if(data && data_len > 0)
    memcpy(app_buf + 16, data, data_len);

  int ret = aicupg_trans_send_pkt(dev, app_buf, app_size);
  free(app_buf);
  if(ret != (int)app_size) return -1;

  /* Step 2: receive response app header (16 bytes) */
  uint8_t resp_hdr[16];
  size_t hdr_len = sizeof(resp_hdr);
  ret = aicupg_trans_recv_pkt(dev, resp_hdr, &hdr_len, tmo);
  if(ret != (int)hdr_len) return -1;

  /* Step 3: validate response header magic "UPGR" (Response) or "UPGC" (Command) */
  uint32_t resp_magic = resp_hdr[0] | ((uint32_t)resp_hdr[1] << 8) |
                        ((uint32_t)resp_hdr[2] << 16) | ((uint32_t)resp_hdr[3] << 24);
  if(resp_magic != 0x43475055 && resp_magic != 0x52475055)
  {
    snprintf(g_last_error, sizeof(g_last_error),
             "bad response magic: 0x%08x", resp_magic);
    return -1;
  }

  /* Step 4: read expected data length from response header */
  uint32_t resp_data_len = resp_hdr[8] | ((uint32_t)resp_hdr[9] << 8) |
                           ((uint32_t)resp_hdr[10] << 16) | ((uint32_t)resp_hdr[11] << 24);

  /* Step 5: receive response data payload */
  if(resp && resp_len && *resp_len > 0 && resp_data_len > 0)
  {
    size_t rd_len = (resp_data_len < *resp_len) ? resp_data_len : *resp_len;
    ret = aicupg_trans_recv_pkt(dev, resp, &rd_len, tmo);
    if(ret != (int)rd_len) return -1;
    *resp_len = rd_len;
  }
  else if(resp_len)
  {
    *resp_len = 0;
  }

  return 0;
}

static int send_simple_command(upg_device_t * dev, uint16_t cmd)
{
  return send_command(dev, cmd, NULL, 0, NULL, NULL, 2000);
}

/* ---- AIC Upgrade Commands ---- */

int aicupg_cmd_get_hwinfo(upg_device_t * dev, device_hwinfo_t * info)
{
  uint8_t resp[256];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_HWINFO, NULL, 0, resp, &resp_len, 3000);
  if(ret < 0) return ret;

  if(resp_len >= sizeof(device_hwinfo_t))
    memcpy(info, resp, sizeof(device_hwinfo_t));
  return 0;
}

int aicupg_cmd_get_traceinfo(upg_device_t * dev, char * buf, size_t buf_size)
{
  size_t resp_len = buf_size;
  return send_command(dev, AIC_CMD_GET_TRACEINFO, NULL, 0, buf, &resp_len, 3000);
}

int aicupg_cmd_write(upg_device_t * dev, uint32_t addr, const void * data, size_t len)
{
  uint8_t req[8 + AIC_MAX_TRANSFER_SIZE];
  size_t total = 0;

  while(total < len)
  {
    size_t chunk = len - total;
    if(chunk > AIC_MAX_TRANSFER_SIZE - 8)
      chunk = AIC_MAX_TRANSFER_SIZE - 8;

    memcpy(req, &addr, 4);
    uint32_t chunk32 = (uint32_t)chunk;
    memcpy(req + 4, &chunk32, 4);
    memcpy(req + 8, (const uint8_t *)data + total, chunk);

    size_t resp_len = 0;
    int ret = send_command(dev, AIC_CMD_WRITE, req, 8 + chunk, NULL, &resp_len, 10000);
    if(ret < 0) return ret;

    addr += (uint32_t)chunk;
    total += chunk;
  }

  return 0;
}

int aicupg_cmd_read(upg_device_t * dev, uint32_t addr, void * buf, size_t len)
{
  uint8_t req[8];
  uint8_t * dst = (uint8_t *)buf;
  size_t total = 0;

  while(total < len)
  {
    size_t chunk = len - total;
    if(chunk > AIC_MAX_TRANSFER_SIZE)
      chunk = AIC_MAX_TRANSFER_SIZE;

    memcpy(req, &addr, 4);
    uint32_t chunk32 = (uint32_t)chunk;
    memcpy(req + 4, &chunk32, 4);

    size_t resp_len = chunk;
    int ret = send_command(dev, AIC_CMD_READ, req, 8, dst + total, &resp_len, 10000);
    if(ret < 0) return ret;

    addr += (uint32_t)chunk;
    total += resp_len;
  }

  return 0;
}

int aicupg_cmd_exec(upg_device_t * dev, uint32_t addr)
{
  (void)addr;
  return send_simple_command(dev, AIC_CMD_EXEC);
}

int aicupg_cmd_continue_boot(upg_device_t * dev)
{
  return send_simple_command(dev, AIC_CMD_CONTINUE_BOOT);
}

int aicupg_cmd_get_mem_buf(upg_device_t * dev, void ** buf, size_t * size)
{
  uint8_t resp[16];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_MEM_BUF, NULL, 0, resp, &resp_len, 3000);
  if(ret < 0) return ret;
  if(resp_len >= 8)
  {
    uint64_t addr;
    uint32_t sz;
    memcpy(&addr, resp, 8);
    memcpy(&sz, resp + 8, 4);
    *buf = (void *)(uintptr_t)addr;
    *size = sz;
  }
  return 0;
}

int aicupg_cmd_free_mem_buf(upg_device_t * dev, void * buf)
{
  uint64_t addr = (uint64_t)(uintptr_t)buf;
  size_t resp_len = 0;
  return send_command(dev, AIC_CMD_FREE_MEM_BUF, &addr, 8, NULL, &resp_len, 3000);
}

int aicupg_cmd_set_upg_cfg(upg_device_t * dev, uint32_t total_size, uint32_t block_size)
{
  uint32_t cfg[2] = { total_size, block_size };
  size_t resp_len = 0;
  return send_command(dev, AIC_CMD_SET_UPG_CFG, cfg, 8, NULL, &resp_len, 3000);
}

int aicupg_cmd_set_upg_end(upg_device_t * dev)
{
  uint8_t app_hdr[16];
  uint32_t cfg_size = 0x20;

  gen_app_header(app_hdr, AIC_CMD_SET_UPG_END, 4 + cfg_size);
  int ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret != 16) return -1;

  ret = aicupg_trans_send_pkt(dev, &cfg_size, 4);
  if(ret != 4) return -1;

  uint8_t cfg_data[32];
  memset(cfg_data, 0, sizeof(cfg_data));
  ret = aicupg_trans_send_pkt(dev, cfg_data, sizeof(cfg_data));
  if(ret != (int)sizeof(cfg_data)) return -1;

  return 0;
}

int aicupg_cmd_set_fwc_meta(upg_device_t * dev, const fwc_meta_t * meta)
{
  int ret;

  if(!dev || !meta) return -3;

  uint8_t app_hdr[16];
  gen_app_header(app_hdr, AIC_CMD_SET_FWC_META, sizeof(fwc_meta_t));

  ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret != 16)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "set fwc meta: send header failed");
    return -1;
  }

  ret = aicupg_trans_send_pkt(dev, meta, sizeof(fwc_meta_t));
  if(ret != (int)sizeof(fwc_meta_t))
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "set fwc meta: send data failed");
    return -1;
  }

  /* Receive response */
  uint8_t resp[16];
  size_t resp_len = sizeof(resp);
  ret = aicupg_trans_recv_pkt(dev, resp, &resp_len, 3000);
  if(ret != 16)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "set fwc meta: recv response failed");
    return -1;
  }

  /* Check response status (byte 7 = flags) */
  if(resp[7] != 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "set fwc meta: device rejected");
    return -1;
  }

  __fwc_meta_sent = 1;
  return 0;
}

int aicupg_cmd_set_uart_args(upg_device_t * dev, int baudrate)
{
  uint32_t baud = (uint32_t)baudrate;
  size_t resp_len = 0;
  return send_command(dev, AIC_CMD_SET_UART_ARGS, &baud, 4, NULL, &resp_len, 3000);
}

int aicupg_cmd_get_block_size(upg_device_t * dev, uint32_t * block_size)
{
  int ret;

  uint8_t app_hdr[16];
  gen_app_header(app_hdr, AIC_CMD_GET_BLOCK_SIZE, 0);
  ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret != 16) return -1;

  uint8_t resp_hdr[16];
  size_t len = sizeof(resp_hdr);
  ret = aicupg_trans_recv_pkt(dev, resp_hdr, &len, 5000);
  if(ret != 16) return -1;

  uint32_t data_len = resp_hdr[8] | ((uint32_t)resp_hdr[9] << 8) |
                      ((uint32_t)resp_hdr[10] << 16) | ((uint32_t)resp_hdr[11] << 24);
  if(data_len != 4) return -1;

  uint32_t value = 0;
  size_t vlen = sizeof(value);
  ret = aicupg_trans_recv_pkt(dev, &value, &vlen, 5000);
  if(ret != 4) return -1;

  if(block_size) *block_size = value;
  return 0;
}

int aicupg_cmd_send_fwc_data_start(upg_device_t * dev, uint32_t total_size)
{
  int ret;

  if(!dev) return -3;

  /* Special handling for chip version 6 */
  if(aicupg_get_chip_version() == 6)
  {
    uint32_t val = 0xffffffff;
    aicupg_cmd_write(dev, 0x1001000c, &val, 4);
  }

  if(!__fwc_meta_sent)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "fwc meta not sent");
    return -4;
  }

  uint8_t app_hdr[16];
  gen_app_header(app_hdr, AIC_CMD_SEND_FWC_DATA_START, total_size);

  ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret != 16)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "send fwc data start failed");
    return -1;
  }

  return 0;
}

int aicupg_cmd_send_fwc_data_update(upg_device_t * dev, const void * data, size_t len)
{
  if(!dev || !data) return -3;
  if(!__fwc_meta_sent) return -4;

  const uint8_t * ptr = (const uint8_t *)data;
  uint32_t pkt_size = (uint32_t)aicupg_get_trans_pkt_size();
  if(pkt_size == 0) pkt_size = 2048;

  size_t num_full = len / pkt_size;
  size_t remaining = len % pkt_size;

  for(size_t i = 0; i < num_full; i++)
  {
    int ret = aicupg_trans_send_pkt(dev, ptr, pkt_size);
    if(ret != (int)pkt_size)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                     "cmd write failed, ret=%d, chunk=%lu/%lu, err=%s",
                     ret, (unsigned long)i, (unsigned long)pkt_size, g_last_error);
      return -1;
    }
    ptr += pkt_size;
  }

  if(remaining > 0)
  {
    int ret = aicupg_trans_send_pkt(dev, ptr, remaining);
    if(ret != (int)remaining)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                     "cmd write failed, ret=%d, remainder=%lu, err=%s",
                     ret, (unsigned long)remaining, g_last_error);
      return -1;
    }
  }

  return (int)len;
}

int aicupg_cmd_send_fwc_data_final(upg_device_t * dev)
{
  int ret;

  if(!dev) return -3;
  if(!__fwc_meta_sent)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "fwc meta not sent");
    return -4;
  }

  uint8_t resp[16];
  size_t len = sizeof(resp);
  ret = aicupg_trans_recv_pkt(dev, resp, &len, 10000);
  if(ret != 16)
  {
    aicupg_log_msg(LOG_DEBUG, __func__, __LINE__,
                   "send fwc data final: recv failed, ret=%d", ret);
    return -1;
  }

  if(resp[7] != 0)
  {
    aicupg_log_msg(LOG_DEBUG, __func__, __LINE__,
                   "send fwc data final: device rejected");
    return -1;
  }

  return 0;
}

int aicupg_cmd_get_fwc_crc(upg_device_t * dev, uint32_t * crc)
{
  int ret;

  if(!dev || !crc) return -3;
  if(!__fwc_meta_sent)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "fwc meta not sent");
    return -4;
  }

  uint8_t app_hdr[16];
  gen_app_header(app_hdr, 0x13, 0);
  ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret != 16) return -1;

  uint8_t resp_hdr[16];
  size_t len = sizeof(resp_hdr);
  ret = aicupg_trans_recv_pkt(dev, resp_hdr, &len, 10000);
  if(ret != 16) return -1;

  uint32_t resp_data_len = resp_hdr[8] | ((uint32_t)resp_hdr[9] << 8) |
                           ((uint32_t)resp_hdr[10] << 16) | ((uint32_t)resp_hdr[11] << 24);
  if(resp_data_len != 4) return -1;

  uint8_t crc_data[4];
  size_t crc_len = sizeof(crc_data);
  ret = aicupg_trans_recv_pkt(dev, crc_data, &crc_len, 10000);
  if(ret != 4) return -1;

  memcpy(crc, crc_data, 4);
  return 0;
}

int aicupg_cmd_get_fwc_burn_result(upg_device_t * dev, int * result)
{
  uint8_t resp[8];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_FWC_BURN_RESULT, NULL, 0, resp, &resp_len, 5000);
  if(ret < 0) return ret;
  if(resp_len >= 4) memcpy(result, resp, 4);
  return 0;
}

int aicupg_cmd_get_fwc_run_result(upg_device_t * dev, int * result)
{
  uint8_t resp[8];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_FWC_RUN_RESULT, NULL, 0, resp, &resp_len, 5000);
  if(ret < 0) return ret;
  if(resp_len >= 4) memcpy(result, resp, 4);
  return 0;
}

int aicupg_cmd_read_fwc_data_start(upg_device_t * dev, const char * fwc_name)
{
  uint8_t req[32];
  memset(req, 0, sizeof(req));
  snprintf((char *)req, 32, "%s", fwc_name);
  size_t resp_len = 0;
  return send_command(dev, AIC_CMD_READ_FWC_DATA_START, req, 32, NULL, &resp_len, 3000);
}

int aicupg_cmd_read_fwc_data(upg_device_t * dev, void * buf, size_t * len)
{
  return send_command(dev, AIC_CMD_READ_FWC_DATA, NULL, 0, buf, len, 5000);
}

int aicupg_cmd_read_fwc_data_final(upg_device_t * dev)
{
  return send_simple_command(dev, AIC_CMD_READ_FWC_DATA_FINAL);
}

int aicupg_cmd_get_partition_table(upg_device_t * dev, const char * media_name,
                                   partition_t ** partitions, int * count)
{
  uint8_t req[32];
  memset(req, 0, sizeof(req));
  strncpy((char *)req, media_name, 31);

  uint8_t resp[4096];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_PARTITION_TABLE, req, 32, resp, &resp_len, 5000);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "Get media partition table failed.");
    return ret;
  }

  int part_count = (int)(resp_len / sizeof(partition_t));
  *partitions = (partition_t *)malloc(part_count * sizeof(partition_t));
  memcpy(*partitions, resp, part_count * sizeof(partition_t));
  *count = part_count;
  return 0;
}

int aicupg_cmd_get_storage_media(upg_device_t * dev, media_t ** media, int * count)
{
  uint8_t resp[4096];
  size_t resp_len = sizeof(resp);
  int ret = send_command(dev, AIC_CMD_GET_STORAGE_MEDIA, NULL, 0, resp, &resp_len, 5000);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "Get storage media list failed.");
    return ret;
  }

  *count = 0;
  *media = NULL;

  /* Parse media list from response */
  size_t off = 0;
  int med_count = 0;
  media_t * med_list = NULL;

  while(off + 16 <= resp_len)
  {
    med_list = (media_t *)realloc(med_list, (med_count + 1) * sizeof(media_t));
    memset(&med_list[med_count], 0, sizeof(media_t));
    memcpy(med_list[med_count].name, resp + off, 15);
    med_list[med_count].partitions = NULL;
    med_list[med_count].partition_count = 0;
    med_count++;
    off += 16;
  }

  *media = med_list;
  *count = med_count;
  return 0;
}

int aicupg_cmd_jtag_unlock(upg_device_t * dev, const void * data, size_t len)
{
  size_t resp_len = 8;
  uint8_t resp[8];
  return send_command(dev, AIC_CMD_JTAG_UNLOCK, data, len, resp, &resp_len, 10000);
}

int aicupg_cmd_run_shell_str(upg_device_t * dev, const char * cmd)
{
  size_t cmd_len = strlen(cmd) + 1;

  /* Check size limit */
  if(cmd_len > (size_t)aicupg_get_trans_pkt_size())
    return -1;

  /* Step 1: send app header, data_len = cmd_len + 4 */
  uint8_t app_hdr[16];
  gen_app_header(app_hdr, 0x05, (uint32_t)(cmd_len + 4));

  int ret = aicupg_trans_send_pkt(dev, app_hdr, 16);
  if(ret < 0) return -1;

  /* Step 2: send 4-byte length */
  uint32_t len32 = (uint32_t)cmd_len;
  ret = aicupg_trans_send_pkt(dev, &len32, 4);
  if(ret < 0) return -1;

  /* Step 3: send command string */
  ret = aicupg_trans_send_pkt(dev, cmd, cmd_len);
  if(ret < 0) return -1;

  /* Step 4: receive response */
  uint8_t resp[16];
  size_t rlen = sizeof(resp);
  ret = aicupg_trans_recv_pkt(dev, resp, &rlen, 5000);
  if(ret < 0 || rlen != 16) return -1;

  /* Check response status */
  if(resp[7] != 0) return -1;

  return 0;
}

int aicupg_app_proto_init(upg_device_t * dev)
{
  uint8_t resp[16];
  size_t resp_len = sizeof(resp);
  return send_command(dev, AIC_CMD_APP_PROTO_INIT, NULL, 0, resp, &resp_len, 3000);
}

void aicupg_app_proto_exit(upg_device_t * dev)
{
  send_simple_command(dev, AIC_CMD_APP_PROTO_EXIT);
}

void aicupg_set_chip_version(uint32_t version)
{
  g_chip_version = (int)version;
}
uint32_t aicupg_get_chip_version(void)
{
  return (uint32_t)g_chip_version;
}

/* ---- Image handling ---- */
int image_hdr_read(const char * filename, image_hdr_t * hdr)
{
  FILE * fp = fopen(filename, "rb");
  if(!fp)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "Open file %s failed", filename);
    return -1;
  }
  size_t n = fread(hdr, 1, sizeof(image_hdr_t), fp);
  fclose(fp);
  if(n < 0x14C) return -1;
  if(memcmp(hdr->magic, "AIC.FW", 6) != 0) return -1;
  return 0;
}

int image_meta_read(const char * filename, fwc_meta_t * meta, int * count)
{
  image_hdr_t hdr = {0};
  if(image_hdr_read(filename, &hdr) < 0) return -1;

  int meta_count = (int)(hdr.meta_size / FWC_META_ENTRY_SIZE);
  if(meta_count <= 0 || meta_count > 64) return -1;

  FILE * fp = fopen(filename, "rb");
  if(!fp) return -1;

  for(int i = 0; i < meta_count; i++)
  {
    fseek(fp, (long)(hdr.meta_offset + i * FWC_META_ENTRY_SIZE), SEEK_SET);
    if(fread(&meta[i], 1, sizeof(fwc_meta_t), fp) != sizeof(fwc_meta_t))
    {
      fclose(fp);
      return -1;
    }
  }
  fclose(fp);

  *count = meta_count;
  return 0;
}

int image_partition_read(const char * filename, partition_t * partitions, int * count)
{
  image_hdr_t hdr = {0};
  if(image_hdr_read(filename, &hdr) < 0) return -1;

  FILE * fp = fopen(filename, "rb");
  if(!fp) return -1;

  /* Partition table follows the fwc_meta table */
  long part_offset = (long)(hdr.meta_offset + hdr.meta_size);
  fseek(fp, part_offset, SEEK_SET);

  partition_t part_buf[64];
  size_t n = fread(part_buf, sizeof(partition_t), 64, fp);
  fclose(fp);

  int part_count = (int)(n / sizeof(partition_t));
  if(partitions && part_count > 0)
    memcpy(partitions, part_buf, n);
  if(count) *count = part_count;
  return 0;
}

int __do_fwc_upgrade(upg_device_t * dev, FILE * fp,
                     const fwc_meta_t * meta, uint32_t * total_sent)
{
  int ret;
  uint32_t block_size;
  uint32_t buf_size;
  uint32_t crc;

  ret = aicupg_cmd_set_fwc_meta(dev, meta);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "set fwc meta failed");
    return -1;
  }

  /* Get actual block size from device */
  if(aicupg_cmd_get_block_size(dev, &block_size) < 0 || block_size == 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "get block size failed");
    return -1;
  }

  /* Calculate buffer size: align to block_size, max 1MB */
  if(block_size > 0x100000)
    buf_size = block_size;
  else
    buf_size = 0x100000 - (0x100000 % block_size);

  uint8_t * buf = (uint8_t *)malloc(buf_size);
  if(!buf)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "malloc chunk buffer failed.");
    return -1;
  }

  aicupg_output("\nUpgrade fwc: %s, size %u ...\n", meta->name, meta->size);

  /* Check if this is the updater component - refuse in U-Boot stage */
  if(strstr(meta->attributes, "updater"))
  {
    /* In uboot stage we cannot upgrade the updater itself */
    if(check_in_uboot_stage(dev) == 1)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                     "Cannot upgrade updater in U-Boot stage");
      free(buf);
      return -1;
    }
  }

  if(aicupg_cmd_send_fwc_data_start(dev, meta->size) < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "send fwc data start failed");
    free(buf);
    return -1;
  }

  fseek(fp, (long)meta->offset, SEEK_SET);

  uint32_t num_full = meta->size / buf_size;
  uint32_t remainder = meta->size % buf_size;


  /* Send full packets */
  for(uint32_t i = 0; i < num_full; i++)
  {
    size_t n = fread(buf, 1, buf_size, fp);
    if(n != buf_size)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__, "Read file failed");
      free(buf);
      return -1;
    }

    ret = aicupg_cmd_send_fwc_data_update(dev, buf, buf_size);
    if(ret != (int)buf_size)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                     "cmd write failed, return %d", ret);
      free(buf);
      return -1;
    }

    *total_sent += buf_size;

    if(buf_size >= 0x100000)
      aicupg_output("Send the %u block, size %uMB\n", i, buf_size >> 20);
    else
      aicupg_output("Send the %u block, size %uKB\n", i, buf_size >> 10);
  }

  /* Send remainder (padded to block_size alignment) */
  if(remainder > 0)
  {
    size_t n = fread(buf, 1, remainder, fp);
    if(n != remainder)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__, "Read file failed");
      free(buf);
      return -1;
    }

    /* Pad remaining buffer to block_size */
    uint32_t pad = block_size - (remainder % block_size);
    if(pad != block_size)
    {
      memset(buf + remainder, 0, pad);
      remainder += pad;
    }

    ret = aicupg_cmd_send_fwc_data_update(dev, buf, remainder);
    if(ret != (int)remainder)
    {
      aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                     "cmd write failed, return %d", ret);
      free(buf);
      return -1;
    }

    *total_sent += remainder;
    aicupg_output("Send the rest %u\n", remainder);
  }

  free(buf);

  int is_updater = (strstr(meta->name, "updater") != NULL);

  ret = aicupg_cmd_send_fwc_data_final(dev);
  if(ret < 0)
  {
    /* Updater components may trigger a device reboot */
    if(is_updater)
    {
      return -2;  /* Signal stage switch to caller */
    }
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "send fwc data final failed");
    return -1;
  }

  ret = aicupg_cmd_get_fwc_crc(dev, &crc);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "get fwc crc failed");
    return -1;
  }

  /* Verify CRC against meta */
  if(crc != meta->crc)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "CRC mismatch: device=0x%x, meta=0x%x", crc, meta->crc);
    return -1;
  }

  /* Get burn and run results */
  int burn_result = 0, run_result = 0;
  aicupg_cmd_get_fwc_burn_result(dev, &burn_result);
  aicupg_cmd_get_fwc_run_result(dev, &run_result);

  return 0;
}

int image_do_upgrade_inner(upg_device_t * dev, const char * image_file)
{
  int reconnected = 0;

  /* Initialize device: send ping to wake up USB stack */
  aicupg_ping_device(dev);

  image_hdr_t hdr = {0};
  if(image_hdr_read(image_file, &hdr) < 0)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__, "Read image header failed");
    return -1;
  }

  int comp_count = (int)(hdr.meta_size / FWC_META_ENTRY_SIZE);
  if(comp_count <= 0 || comp_count > 64) return -1;

  fwc_meta_t * meta = (fwc_meta_t *)malloc((size_t)comp_count * sizeof(fwc_meta_t));
  if(!meta) return -1;

  if(image_meta_read(image_file, meta, &comp_count) < 0)
  {
    free(meta);
    return -1;
  }

  /* Get file size */
  FILE * fp = fopen(image_file, "rb");
  if(!fp)
  {
    free(meta);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fclose(fp);

  aicupg_output("\nThe Image file: %s, size %ld\n", image_file, file_size);

  fp = fopen(image_file, "rb");
  if(!fp)
  {
    free(meta);
    return -1;
  }

  struct timeval tv_start, tv_end;
  gettimeofday(&tv_start, NULL);

  uint32_t total_sent = 0;

  for(int i = 0; i < comp_count; i++)
  {
    int ret = __do_fwc_upgrade(dev, fp, &meta[i], &total_sent);
    if(ret == -2)
    {
      /* Device rebooted after flashing updater - wait for reconnect */
      aicupg_output("Device stage switch, waiting for reconnect...\n");
      upg_usb_dev_close(dev);
      sleep(1);

      /* Wait for device to reappear */
      upg_device_t * new_dev = NULL;
      for(int retry = 0; retry < 10; retry++)
      {
        upg_device_t ** dev_list = NULL;
        int dev_count = 0;
        upg_usb_dev_get_list(&dev_list, &dev_count);
        if(dev_count > 0)
        {
          new_dev = dev_list[0];
          /* Keep the list but free others */
          for(int j = 1; j < dev_count; j++)
          {
            upg_usb_dev_close(dev_list[j]);
            free(dev_list[j]);
          }
          free(dev_list);
          break;
        }
        if(dev_list) free(dev_list);
        sleep(1);
      }

      if(!new_dev)
      {
        aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                       "Device did not reconnect after stage switch");
        fclose(fp);
        free(meta);
        return -1;
      }

      if(upg_usb_dev_open(new_dev) < 0)
      {
        aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                       "Failed to open reconnected device");
        free(new_dev);
        fclose(fp);
        free(meta);
        return -1;
      }

      aicupg_trans_init(new_dev);
      dev = new_dev;
      reconnected = 1;
      aicupg_output("Bus:Port %u:", new_dev->bus_number);
      for(int j = 0; j < new_dev->port_count; j++)
        aicupg_output("%s%u", j == 0 ? "" : "-", new_dev->port_numbers[j]);
      aicupg_output(" connected\n");
      continue;
    }
    if(ret < 0)
    {
      fclose(fp);
      free(meta);
      return -1;
    }
  }

  aicupg_cmd_set_upg_end(dev);
  fclose(fp);

  gettimeofday(&tv_end, NULL);
  double elapsed = (double)(tv_end.tv_sec - tv_start.tv_sec) +
                   (double)(tv_end.tv_usec - tv_start.tv_usec) / 1000000.0;
  double speed = elapsed > 0 ? (double)total_sent / elapsed / 1048576.0 : 0;

  aicupg_output("\nBurn %s successfully!\n", image_file);
  aicupg_output("Used time: %.1f sec, Speed: %.2f MB/s\n", elapsed, speed);

  if(reconnected)
  {
    aicupg_trans_exit(dev);
    upg_usb_dev_close(dev);
    libusb_unref_device(dev->dev);
    free(dev);
  }

  free(meta);
  return 0;
}

int image_do_upgrade(upg_device_t * dev, const char * image_file)
{
  return image_do_upgrade_inner(dev, image_file);
}

int image_do_ramboot(upg_device_t * dev, const char * fwc_name,
                     uint32_t ram_addr, const char * file)
{
  (void)fwc_name;
  FILE * fp = fopen(file, "rb");
  if(!fp)
  {
    aicupg_log_msg(LOG_ERR, __func__, __LINE__,
                   "Open file %s failed", file);
    return -1;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint8_t * data = (uint8_t *)malloc((size_t)file_size);
  if(!data)
  {
    fclose(fp);
    return -1;
  }

  fread(data, 1, (size_t)file_size, fp);
  fclose(fp);

  aicupg_cmd_write(dev, ram_addr, data, (size_t)file_size);
  aicupg_cmd_exec(dev, ram_addr);

  free(data);
  return 0;
}

static void __dump_image_hdr(const image_hdr_t * hdr)
{
  aicupg_output("Image header:\n");
  aicupg_output("\tmagic      : %.8s\n", hdr->magic);
  aicupg_output("\tplatform   : %.64s\n", hdr->platform);
  aicupg_output("\tproduct    : %.64s\n", hdr->product);
  aicupg_output("\tversion    : %.64s\n", hdr->version);
  aicupg_output("\tmedia type : %.68s\n", hdr->media_type);
  aicupg_output("\tmedia id   : %.64s\n", hdr->media_id);
  aicupg_output("\tmeta offset: 0x%X\n", hdr->meta_offset);
  aicupg_output("\tmeta size  : 0x%X\n", hdr->meta_size);
  aicupg_output("\tfile offset: 0x%X\n", hdr->file_offset);
  aicupg_output("\tfile size  : 0x%X\n", hdr->file_size);
  aicupg_output("\tex flag    : 0x%X\n", hdr->ex_flag);
  aicupg_output("\tex offset  : 0x%X\n", hdr->ex_offset);
  aicupg_output("\tex size    : 0x%X\n", hdr->ex_size);
}

static void __dump_meta_info(const fwc_meta_t * meta, int count)
{
  for(int i = 0; i < count; i++)
  {
    aicupg_output("FWC Info %d:\n", i);
    aicupg_output("\tmagic      : %.8s\n", meta[i].magic);
    aicupg_output("\tname       : %.64s\n", meta[i].name);
    aicupg_output("\tpartition  : %.64s\n", meta[i].partition);
    aicupg_output("\toffset     : 0x%X\n", meta[i].offset);
    aicupg_output("\tsize       : 0x%X\n", meta[i].size);
    aicupg_output("\tCRC        : 0x%X\n", meta[i].crc);
    aicupg_output("\tRAM        : 0x%X\n", meta[i].ram);
    aicupg_output("\tAttributes : %.104s\n", meta[i].attributes);
  }
}

int image_display_info(const char * image_file)
{
  image_hdr_t hdr = {0};
  if(image_hdr_read(image_file, &hdr) < 0)
  {
    aicupg_output("Cannot parse image file: %s\n", image_file);
    return -1;
  }

  aicupg_output("Image file: %s\n", image_file);
  __dump_image_hdr(&hdr);

  int comp_count = (int)(hdr.meta_size / FWC_META_ENTRY_SIZE);
  if(comp_count > 0 && comp_count <= 64)
  {
    fwc_meta_t meta[64];
    if(image_meta_read(image_file, meta, &comp_count) == 0)
      __dump_meta_info(meta, comp_count);
  }

  return 0;
}

int image_extract(const char * image_file)
{
  aicupg_output("Extracting from image file: %s\n", image_file);

  image_hdr_t hdr = {0};
  if(image_hdr_read(image_file, &hdr) < 0)
  {
    aicupg_output("Cannot open image file: %s\n", image_file);
    return -1;
  }

  int comp_count = (int)(hdr.meta_size / FWC_META_ENTRY_SIZE);
  if(comp_count <= 0 || comp_count > 64)
  {
    aicupg_output("Invalid component count: %d\n", comp_count);
    return -1;
  }

  fwc_meta_t meta[64];
  if(image_meta_read(image_file, meta, &comp_count) < 0)
  {
    aicupg_output("Cannot read component metadata\n");
    return -1;
  }

  FILE * fp = fopen(image_file, "rb");
  if(!fp) return -1;

  for(int i = 0; i < comp_count; i++)
  {
    char * out_name = NULL;
    asprintf(&out_name, "%s_%s.bin", image_file, meta[i].name);
    if(!out_name) continue;

    FILE * out = fopen(out_name, "wb");
    if(!out)
    {
      free(out_name);
      continue;
    }

    uint8_t * data = (uint8_t *)malloc(meta[i].size);
    if(data)
    {
      fseek(fp, (long)meta[i].offset, SEEK_SET);
      fread(data, 1, meta[i].size, fp);
      fwrite(data, 1, meta[i].size, out);
      free(data);
    }
    fclose(out);

    aicupg_output("  Extracted: %s (0x%x bytes)\n", out_name, meta[i].size);
    free(out_name);
  }

  fclose(fp);
  return 0;
}

int check_in_uboot_stage(upg_device_t * dev)
{
  device_hwinfo_t info;
  memset(&info, 0, sizeof(info));
  if(aicupg_cmd_get_hwinfo(dev, &info) < 0) return -1;
  return (info.boot_stage == UBOOT_STAGE) ? 1 : 0;
}

/* ---- Storage / partition helpers ---- */
int get_storage_media(upg_device_t * dev, media_t ** media, int * count)
{
  return aicupg_cmd_get_storage_media(dev, media, count);
}

void aicupg_partition_free(partition_t * partitions)
{
  free(partitions);
}

partition_t * aicupg_new_partition(const char * name, uint32_t offset, uint32_t size)
{
  partition_t * p = (partition_t *)calloc(1, sizeof(partition_t));
  if(p)
  {
    strncpy(p->name, name, 31);
    p->offset = offset;
    p->size = size;
  }
  return p;
}

/* ---- Progress display ---- */
void show_progress_init(void)
{
  if(_upg_progress_cb)
    _upg_progress_cb(0, 0, _upg_progress_ud);
  else if(g_config.show_progress)
    aicupg_output("Progress: ");
}

void show_progress_print(int current, int total)
{
  if(_upg_progress_cb)
  {
    _upg_progress_cb(current, total, _upg_progress_ud);
    return;
  }
  if(!g_config.show_progress) return;
  if(total <= 0) return;
  int pct = current * 100 / total;
  aicupg_output("\rProgress: %d%% (%d/%d)", pct, current, total);
}

/* ---- Library interface ---- */
void libaicupg_init(void)
{
  libusb_init(NULL);
}

void libaicupg_exit(void)
{
  libusb_exit(NULL);
}

void libaicupg_set_verbose(int verbose)
{
  g_config.verbose = verbose;
}
int libaicupg_get_verbose(void)
{
  return g_config.verbose;
}
int libaicupg_get_link_mode(upg_device_t * dev)
{
  return dev ? dev->link_mode : -1;
}
void libaicimg_set_verbose(int verbose)
{
  g_config.verbose = verbose;
}
void libaicimg_set_progress(int enable)
{
  g_config.show_progress = enable;
}
int libaicimg_set_max_baudrate(int baudrate)
{
  g_config.baudrate = baudrate;
  return 0;
}

/* ---- Command implementations ---- */

static int __cmd_write(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 3)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", 0, "Invalid arguments.");
    return -1;
  }

  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  const char * filename = argv[2];
  uint32_t skip = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 0) : 0;
  uint32_t len = (argc > 4) ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;

  FILE * fp = fopen(filename, "rb");
  if(!fp)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Open file %s failed", filename);
    return -1;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  if(len == 0) len = (uint32_t)file_size - skip;
  if(skip + len > (uint32_t)file_size)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__, "Skip too much.");
    fclose(fp);
    return -1;
  }

  uint8_t * data = (uint8_t *)malloc(len);
  fseek(fp, (long)skip, SEEK_SET);
  fread(data, 1, len, fp);
  fclose(fp);

  int ret = aicupg_cmd_write(dev, addr, data, len);
  free(data);
  return ret;
}

static int __cmd_read(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 3)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", 0, "Invalid arguments.");
    return -1;
  }

  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t len = (uint32_t)strtoul(argv[2], NULL, 0);
  const char * filename = argv[3];

  uint8_t * data = (uint8_t *)malloc(len);
  if(!data) return -1;

  int ret = aicupg_cmd_read(dev, addr, data, len);
  if(ret == 0)
  {
    FILE * fp = fopen(filename, "wb");
    if(fp)
    {
      fwrite(data, 1, len, fp);
      fclose(fp);
    }
  }

  free(data);
  return ret;
}

static int __cmd_writel(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 2) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t value = (uint32_t)strtoul(argv[2], NULL, 0);
  return aicupg_cmd_write(dev, addr, &value, 4);
}

static int __cmd_readl(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t value = 0;
  int ret = aicupg_cmd_read(dev, addr, &value, 4);
  if(ret == 0)
    aicupg_output("0x%08x: 0x%08x\n", addr, value);
  return ret;
}

static int __cmd_exec(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  return aicupg_cmd_exec(dev, addr);
}

static int __cmd_continue_boot(upg_device_t * dev, int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  return aicupg_cmd_continue_boot(dev);
}

static int __cmd_get_trace(upg_device_t * dev, int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  char buf[4096];
  int ret = aicupg_cmd_get_traceinfo(dev, buf, sizeof(buf));
  if(ret == 0)
    aicupg_output("%s\n", buf);
  else
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Get trace data failed.");
  return ret;
}

static int __cmd_hexdump(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 2) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t len = (uint32_t)strtoul(argv[2], NULL, 0);

  uint8_t * data = (uint8_t *)malloc(len);
  if(!data) return -1;

  int ret = aicupg_cmd_read(dev, addr, data, len);
  if(ret == 0)
    hexdump(data, len);

  free(data);
  return ret;
}

static int __cmd_dump_partition(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 3) return -1;
  const char * media = argv[1];
  const char * partition_name = argv[2];
  const char * out_file = argv[3];

  partition_t * parts = NULL;
  int count = 0;
  int ret = aicupg_cmd_get_partition_table(dev, media, &parts, &count);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Dump partition failed.");
    return ret;
  }

  partition_t * target = NULL;
  for(int i = 0; i < count; i++)
  {
    if(strcmp(parts[i].name, partition_name) == 0)
    {
      target = &parts[i];
      break;
    }
  }

  if(!target)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Partition %s not found", partition_name);
    free(parts);
    return -1;
  }

  uint8_t * data = (uint8_t *)malloc(target->size);
  ret = aicupg_cmd_read(dev, target->offset, data, target->size);
  if(ret == 0)
  {
    FILE * fp = fopen(out_file, "wb");
    if(fp)
    {
      fwrite(data, 1, target->size, fp);
      fclose(fp);
    }
  }
  free(data);
  free(parts);
  return ret;
}

static int __cmd_partition_table(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  const char * media = argv[1];

  partition_t * parts = NULL;
  int count = 0;
  int ret = aicupg_cmd_get_partition_table(dev, media, &parts, &count);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Get media partition table failed.");
    return ret;
  }

  aicupg_output("The partition list in %s:\n\n", media);
  aicupg_output("%-14s %-10s %s\n", "Name", "Offset", "Size");
  aicupg_output("-------------- ---------- ----------\n");
  for(int i = 0; i < count; i++)
  {
    aicupg_output("%-14s 0x%08lx 0x%08lx (%lu KB)\n",
                  parts[i].name,
                  (unsigned long)parts[i].offset,
                  (unsigned long)parts[i].size,
                  (unsigned long)(parts[i].size / 1024));
  }

  free(parts);
  return 0;
}

static int __cmd_lsmedia(upg_device_t * dev, int argc, char ** argv)
{
  (void)argc;
  (void)argv;
  media_t * media = NULL;
  int count = 0;
  int ret = get_storage_media(dev, &media, &count);
  if(ret < 0)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Get storage media list failed.");
    return ret;
  }

  aicupg_output("\nThe available storage media:\n");
  for(int i = 0; i < count; i++)
    aicupg_output("  %s\n", media[i].name);

  free(media);
  return 0;
}

static int __cmd_fill_memory(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 3) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t len = (uint32_t)strtoul(argv[2], NULL, 0);
  uint8_t value = (uint8_t)strtoul(argv[3], NULL, 0);

  uint8_t * buf = (uint8_t *)malloc(AIC_MALLOC_CHUNK_SIZE);
  if(!buf)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "malloc chunk buffer failed.");
    return -1;
  }
  memset(buf, value, AIC_MALLOC_CHUNK_SIZE);

  uint32_t remaining = len;
  uint32_t cur_addr = addr;
  while(remaining > 0)
  {
    uint32_t chunk = remaining < AIC_MALLOC_CHUNK_SIZE ? remaining : AIC_MALLOC_CHUNK_SIZE;
    aicupg_cmd_write(dev, cur_addr, buf, chunk);
    cur_addr += chunk;
    remaining -= chunk;
  }

  free(buf);
  return 0;
}

static int __cmd_clear_memory(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 2) return -1;
  uint32_t addr = (uint32_t)strtoul(argv[1], NULL, 0);
  uint32_t len = (uint32_t)strtoul(argv[2], NULL, 0);

  uint8_t * buf = (uint8_t *)calloc(1, AIC_MALLOC_CHUNK_SIZE);
  if(!buf)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "malloc chunk buffer failed.");
    return -1;
  }

  uint32_t remaining = len;
  uint32_t cur_addr = addr;
  while(remaining > 0)
  {
    uint32_t chunk = remaining < AIC_MALLOC_CHUNK_SIZE ? remaining : AIC_MALLOC_CHUNK_SIZE;
    aicupg_cmd_write(dev, cur_addr, buf, chunk);
    cur_addr += chunk;
    remaining -= chunk;
  }

  free(buf);
  return 0;
}

static int __cmd_shcmd(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  char cmd_buf[256];
  cmd_buf[0] = '\0';
  for(int i = 1; i < argc; i++)
  {
    if(i > 1) strcat(cmd_buf, " ");
    strcat(cmd_buf, argv[i]);
  }
  return aicupg_cmd_run_shell_str(dev, cmd_buf);
}

static int __cmd_upgrade_image(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  return image_do_upgrade(dev, argv[1]);
}

static int __cmd_chipdata(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1) return -1;
  const char * filename = argv[1];
  FILE * fp = fopen(filename, "wb");
  if(!fp) return -1;

  /* Read chip unique data via firmware component read */
  int ret = aicupg_cmd_read_fwc_data_start(dev, "chipdata");
  if(ret < 0)
  {
    fclose(fp);
    return ret;
  }

  uint8_t buf[4096];
  size_t len = sizeof(buf);
  ret = aicupg_cmd_read_fwc_data(dev, buf, &len);
  if(ret == 0)
    fwrite(buf, 1, len, fp);

  aicupg_cmd_read_fwc_data_final(dev);
  fclose(fp);
  return ret;
}

static int __cmd_jtag_unlock(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 1)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", 0, "Invalid arguments.");
    return -1;
  }

  FILE * fp = fopen(argv[1], "rb");
  if(!fp)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Cannot open file %s", argv[1]);
    return -1;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if(file_size <= 0 || file_size > AIC_MALLOC_CHUNK_SIZE)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "Data length is 0.");
    fclose(fp);
    return -1;
  }

  uint8_t * data = (uint8_t *)malloc((size_t)file_size);
  if(!data)
  {
    aicupg_log_msg(LOG_ERR, "upgcmd", __LINE__,
                   "malloc chunk buffer failed.");
    fclose(fp);
    return -1;
  }

  size_t n = fread(data, 1, (size_t)file_size, fp);
  fclose(fp);

  int ret = aicupg_cmd_jtag_unlock(dev, data, n);
  if(ret < 0)
    aicupg_output("jtag unlock failed.\n");
  else
    aicupg_output("jtag unlock ok.\n");

  free(data);
  return ret;
}

static int __cmd_ramboot(upg_device_t * dev, int argc, char ** argv)
{
  if(argc < 3) return -1;
  const char * fwc_name = argv[1];
  uint32_t ram_addr = (uint32_t)strtoul(argv[2], NULL, 0);
  const char * file = argv[3];
  return image_do_ramboot(dev, fwc_name, ram_addr, file);
}

/* ---- Command dispatch table ---- */
static cmd_entry_t g_cmds[] =
{
  {"write",      __cmd_write,            "write <address> <file> [skip] [len]"},
  {"writel",     __cmd_writel,           "writel <address> <value>"},
  {"read",       __cmd_read,             "read <address> <length> <file>"},
  {"readl",      __cmd_readl,            "readl <address>"},
  {"exec",       __cmd_exec,             "exec <address>"},
  {"continue",   __cmd_continue_boot,    "continue"},
  {"trace",      __cmd_get_trace,        "trace"},
  {"hexdump",    __cmd_hexdump,          "hexdump <address> <length>"},
  {"dump",       __cmd_dump_partition,   "dump <media> <partition> <file>"},
  {"lspart",     __cmd_partition_table,  "lspart <media>"},
  {"lsmedia",    __cmd_lsmedia,          "lsmedia"},
  {"fill",       __cmd_fill_memory,      "fill <address> <length> <value>"},
  {"clear",      __cmd_clear_memory,     "clear <address> <length>"},
  {"shcmd",      __cmd_shcmd,            "shcmd \"u-boot shell string\""},
  {"image",      __cmd_upgrade_image,    "image <file>"},
  {"chipdata",   __cmd_chipdata,         "chipdata <file>"},
  {"jtagunlock", __cmd_jtag_unlock,      "jtagunlock <signed chipdata file>"},
  {"ramboot",    __cmd_ramboot,          "ramboot <fwc name> <ram addr> <file>"},
  {NULL, NULL, NULL},
};

/* ---- Usage and help ---- */
static void usage(const char * prog)
{
  aicupg_output("\nArtInChip Upgrade Command Line Tool\n\n");
  aicupg_output("Usage: %s options command arguments...\n\n", prog);
  aicupg_output("  -h, --help                             Show this help.\n");
  aicupg_output("  -i, --imagefile <image file>           Parse a image file\n");
  aicupg_output("  -e, --extract <image file>             Extract firmware components from image file\n");
  aicupg_output("  -l, --list                             List all connected devices.\n");
  aicupg_output("  -d, --dev bus:portnum                  Use specific usb upgrade device\n");
  aicupg_output("  -p, --progress                         Print burn progress\n");
  aicupg_output("  -v, --verbose                          Show the verbose log\n");
  aicupg_output("\n");
  aicupg_output("  chipdata <file>                        Read IC's unique chip data to <file>\n");
  aicupg_output("  jtagunlock <signed chipdata file>      Unlock JTAG\n");
  aicupg_output("  continue                               Boot ROM continue boot.\n");
  aicupg_output("  write <address> <file> [skip] [len]    Download file to device memory.\n");
  aicupg_output("  read <address> <length> <file>         Read device memory to file.\n");
  aicupg_output("  writel <address> <value>               Write 32-bit value.\n");
  aicupg_output("  readl <address>                        Read 32-bit value.\n");
  aicupg_output("  exec <address>                         Execute firmware at address.\n");
  aicupg_output("  hexdump <address> <length>             Hex dump memory area.\n");
  aicupg_output("  dump <media> <partition> <file>        Dump storage partition.\n");
  aicupg_output("  lsmedia <file>                         Show storage media list.\n");
  aicupg_output("  lspart <media>                         Show media partition table.\n");
  aicupg_output("  fill <address> <length> <value>        Memory set with byte.\n");
  aicupg_output("  clear <address> <length>               Memory clear to zero.\n");
  aicupg_output("  shcmd \"u-boot shell string\"            Run U-Boot shell command.\n");
  aicupg_output("  image <file>                           Upgrade AIC image file.\n");
  aicupg_output("  ramboot <fwc name> <ram addr> <file>   Download fwc to ram and boot.\n");
  aicupg_output("\nExamples:\n");
  aicupg_output("  %s -l\n", prog);
  aicupg_output("  %s write 0x12345678 fw.bin\n", prog);
  aicupg_output("\n");
}

/* ---- List devices ---- */
static void list_devices(void)
{
  upg_device_t ** dev_list = NULL;
  int count = 0;

  libusb_init(NULL);

  if(upg_usb_dev_get_list(&dev_list, &count) < 0 || count == 0)
  {
    aicupg_output("No usbupg device is found.\n");
    libusb_exit(NULL);
    return;
  }

  aicupg_output("There are %d USB device in list:\n\n", count);
  for(int i = 0; i < count; i++)
  {
    upg_device_t * d = dev_list[i];

    /* Print Bus:Port line */
    aicupg_output("Bus:Port %u:", d->bus_number);
    for(int j = 0; j < d->port_count; j++)
      aicupg_output("%s%u", j == 0 ? "" : "-", d->port_numbers[j]);
    aicupg_output("\n");

    /* Try to get more info if device is openable */
    if(upg_usb_dev_open(d) == 0)
    {
      device_hwinfo_t info;
      memset(&info, 0, sizeof(info));
      if(aicupg_cmd_get_hwinfo(d, &info) == 0)
      {
        aicupg_output("\t- Device %d, Class %d\n", d->device_address, d->bDeviceClass);
        aicupg_output("\t- Boot stage: %s\n",
                      info.boot_stage == 0 ? "Boot ROM" : "U-Boot");
        aicupg_output("\t- Stage: %d\n", info.boot_stage & 0x01);
        aicupg_output("\t- Secure boot: %d\n", info.flags & 0x01);
        aicupg_output("\t- Encrypt boot: %d\n", (info.flags >> 1) & 0x01);
        aicupg_output("\t- Anti-rollback: %d\n", (info.flags >> 2) & 0x01);
        aicupg_output("\t- Boot device1: %d\n", info.boot_device1);
        aicupg_output("\t- Boot device2: %d\n", info.boot_device2);
      }
      upg_usb_dev_close(d);
    }
  }
  aicupg_output("\n");

  upg_usb_dev_free_list(dev_list, count);
  libusb_exit(NULL);
}

/* ---- Parse device spec "bus:portnum" ---- */
static int parse_dev_spec(const char * spec, upg_device_t ** dev_list, int count,
                          upg_device_t ** out)
{
  unsigned int bus, port;
  if(sscanf(spec, "%u:%u", &bus, &port) != 2)
  {
    aicupg_output("busnum:portnum format error.\n");
    return -1;
  }

  for(int i = 0; i < count; i++)
  {
    if(dev_list[i]->bus_number == bus &&
       dev_list[i]->port_numbers[0] == port)
    {
      *out = dev_list[i];
      return 0;
    }
  }
  return -1;
}

/* ---- Main dispatch function ---- */
int upgcmd(int argc, char ** argv)
{
  const char * short_opts = "hle:i:vpd:";
  struct option long_opts[] =
  {
    {"help",      no_argument,       NULL, 'h'},
    {"list",      no_argument,       NULL, 'l'},
    {"extract",   required_argument, NULL, 'e'},
    {"imagefile", required_argument, NULL, 'i'},
    {"verbose",   no_argument,       NULL, 'v'},
    {"progress",  no_argument,       NULL, 'p'},
    {"dev",       required_argument, NULL, 'd'},
    {NULL, 0, NULL, 0},
  };

  optind = 0;

  /* Parse options */
  int opt;
  while((opt = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
  {
    switch(opt)
    {
      case 'h':
        usage(g_config.prog_name ? g_config.prog_name : UPGCMD_PROG_NAME);
        return 0;
      case 'l':
        list_devices();
        return 0;
      case 'v':
        g_config.verbose = 1;
        g_config.debug_level = LOG_DEBUG;
        break;
      case 'p':
        g_config.show_progress = 1;
        break;
      case 'd':
        g_config.dev_spec = strdup(optarg);
        break;
      case 'e':
        g_config.extract_file = strdup(optarg);
        break;
      case 'i':
        g_config.image_file = strdup(optarg);
        break;
      default:
        usage(argv[0]);
        return 1;
    }
  }

  /* Handle standalone options that don't need a device */
  if(g_config.image_file)
  {
    return image_display_info(g_config.image_file);
  }
  if(g_config.extract_file)
  {
    return image_extract(g_config.extract_file);
  }

  /* Remaining arguments after options */
  int cmd_argc = argc - optind;
  char ** cmd_argv = argv + optind;

  if(cmd_argc < 1)
  {
    aicupg_output("Wrong options combination.\n");
    usage(argv[0]);
    return 1;
  }

  const char * cmd_name = cmd_argv[0];

  /* Handle "imageinfo", "extract", "list", "help" as commands */
  if(strcmp(cmd_name, "help") == 0)
  {
    usage(argv[0]);
    return 0;
  }
  if(strcmp(cmd_name, "list") == 0)
  {
    list_devices();
    return 0;
  }
  if(strcmp(cmd_name, "imageinfo") == 0 && cmd_argc > 1)
  {
    return image_display_info(cmd_argv[1]);
  }
  if(strcmp(cmd_name, "extract") == 0 && cmd_argc > 1)
  {
    return image_extract(cmd_argv[1]);
  }

  /* All other commands need a device */
#ifndef UPGCMD_AS_LIBRARY
  libusb_init(NULL);
#endif

  upg_device_t ** dev_list = NULL;
  int dev_count = 0;
  upg_device_t * target_dev = NULL;

  upg_usb_dev_get_list(&dev_list, &dev_count);

  if(dev_count == 0)
  {
    aicupg_output("No usbupg device is found.\n");
#ifndef UPGCMD_AS_LIBRARY
    libusb_exit(NULL);
#endif
    return 1;
  }

  if(g_config.dev_spec)
  {
    if(parse_dev_spec(g_config.dev_spec, dev_list, dev_count, &target_dev) < 0)
    {
      aicupg_output("No usbupg device is found.\n");
      upg_usb_dev_free_list(dev_list, dev_count);
#ifndef UPGCMD_AS_LIBRARY
      libusb_exit(NULL);
#endif
      return 1;
    }
  }
  else
  {
    target_dev = dev_list[0];
  }

  if(upg_usb_dev_open(target_dev) < 0)
  {
    aicupg_output("Open upg device failed: %s\n", upg_usb_dev_get_last_error());
    upg_usb_dev_free_list(dev_list, dev_count);
#ifndef UPGCMD_AS_LIBRARY
    libusb_exit(NULL);
#endif
    return 1;
  }

  aicupg_trans_init(target_dev);

  /* Dispatch command */
  int ret = -1;
  for(cmd_entry_t * c = g_cmds; c->name != NULL; c++)
  {
    if(strcmp(cmd_name, c->name) == 0)
    {
      ret = c->handler(target_dev, cmd_argc, cmd_argv);
      goto done;
    }
  }

  aicupg_output("Command '%s' not found!\n\n", cmd_name);
  usage(argv[0]);
  ret = 1;

done:
  aicupg_trans_exit(target_dev);
  upg_usb_dev_close(target_dev);
  upg_usb_dev_free_list(dev_list, dev_count);
#ifndef UPGCMD_AS_LIBRARY
  libusb_exit(NULL);
#endif

  return ret;
}

/* ---- Main entry ---- */
#ifndef UPGCMD_AS_LIBRARY
int main(int argc, char ** argv)
{
  aicupg_log_hook_register(UPGCMD_PROG_NAME);
  g_config.prog_name = argv[0];
  return upgcmd(argc, argv);
}
#endif
