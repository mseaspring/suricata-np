/* Copyright (C) 2011,2012 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Tom DeCanio <td@npulsetech.com>
 */

#ifndef __SOURCE_NETMAP_H__
#define __SOURCE_NETMAP_H__

#ifndef NETMAP_PACKET_FANOUT
/* Placeholder for possible future RSS support */
#define NETMAP_PACKET_FANOUT 1
#endif /* NETMAP_PACKET_FANOUT */

#define NETMAP_COPY_MODE_NONE  0
#define NETMAP_COPY_MODE_TAP   1
#define NETMAP_COPY_MODE_IPS   2

#define NETMAP_FILE_MAX_PKTS 256
#define NETMAP_IFACE_NAME_LENGTH 48

#if 1
typedef struct NetmapIfaceConfig_
{
    char iface[NETMAP_IFACE_NAME_LENGTH];
    /* number of threads */
    int threads;
    /* promisc mode */
    int promisc;
    /* misc use flags including ring mode */
    int flags;
    int copy_mode;
    ChecksumValidationMode checksum_mode;
    char *bpf_filter;
    char *out_iface;
    SC_ATOMIC_DECLARE(unsigned int, ref);
    void (*DerefFunc)(void *);
} NetmapIfaceConfig;
#else
typedef struct AFPIfaceConfig_
{
    char iface[AFP_IFACE_NAME_LENGTH];
    /* number of threads */
    int threads;
    /* socket buffer size */
    int buffer_size;
    /* ring size in number of packets */
    int ring_size;
    /* cluster param */
    int cluster_id;
    int cluster_type;
    /* promisc mode */
    int promisc;
    /* misc use flags including ring mode */
    int flags;
    int copy_mode;
    ChecksumValidationMode checksum_mode;
    char *bpf_filter;
    char *out_iface;
    SC_ATOMIC_DECLARE(unsigned int, ref);
    void (*DerefFunc)(void *);
} AFPIfaceConfig;
#endif

/**
 * \ingroup afppeers
 * @{
 */

typedef struct NetmapPeer_ {
    char iface[NETMAP_IFACE_NAME_LENGTH];
    //SC_ATOMIC_DECLARE(int, socket);
    //SC_ATOMIC_DECLARE(int, sock_usage);
    SC_ATOMIC_DECLARE(int, if_idx);
    SC_ATOMIC_DECLARE(uint8_t, state);
    //SCMutex sock_protect;
    int flags;
    int turn; /**< Field used to store initialisation order. */
    struct NetmapPeer_ *peer;
    TAILQ_ENTRY(NetmapPeer_) next;
} NetmapPeer;

/**
 * \brief per packet AF_PACKET vars
 *
 * This structure is used y the release data system and is cleaned
 * up by the AFPV_CLEANUP macro below.
 */
typedef struct NetmapPacketVars_
{
    void *relptr;
    int copy_mode;
    NetmapPeer *peer; /**< Sending peer for IPS/TAP mode */
    /** Pointer to ::AFPPeer used for capture. Field is used to be able
     * to do reference counting.
     */
    NetmapPeer *mpeer;
} NetmapPacketVars;

#define NEMAPV_CLEANUP(v) do {           \
    (v)->relptr = NULL;                \
    (v)->copy_mode = 0;                \
    (v)->peer = NULL;                  \
    (v)->mpeer = NULL;                 \
} while(0)

/**
 * @}
 */

void TmModuleReceiveNetmapRegister (void);
void TmModuleDecodeNetmapRegister (void);

TmEcode NetmapPeersListInit();
TmEcode NetmapPeersListCheck();
void NetmapPeersListClean();


#endif /* __SOURCE_NETMAP_H__ */
