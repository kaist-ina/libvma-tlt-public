/* 
 * Author: Hwijoon Lim <hwijoon.lim@kaist.ac.kr>
 */
#ifndef __LWIP_DCTCP_H__
#define __LWIP_DCTCP_H__
#include <assert.h>
#include <stdint.h>
#include "opt.h"

#define TOS_ECT_NONE 0
#define TOS_ECT_1 1
#define TOS_ECT_2 2
#define TOS_ECT_CE 3

#define DCTCP_SCF 65536
#define DCTCP_SHF 4

/* Debug features */
#define DCTCP_DEBUG_PRINT 0 
#define EMULATE_RANDOM_CE_MARK 0
#define DCTCP_ALPHA_DEBUG 0

#if DCTCP_DEBUG_PRINT
#define DEBUG_DCTCP(msg, ...) printf("%s:%d:" msg "\n", __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define DEBUG_DCTCP(msg, ...)
#endif

struct dctcp_param{
    int enabled;
    int ce;             /* use as counter to send ECE */
    int pending_ece; /* should handle ece this time: reduce cwnd */
    int pending_cwr; /* should send cwr this time */
    u32_t next_ece; /* if snd_nxt >= next_ece, react to ECE */

    u32_t WindowEnd;
    u32_t BytesAcked;
    u32_t BytesMarked;
    u32_t Alpha;
#if EMULATE_RANDOM_CE_MARK
    u32_t dbg_sent_ce;
    u32_t dbg_sent_non_ce;
    u32_t dbg_rcvd_ece;
#endif
#if DCTCP_ALPHA_DEBUG
    double acc_alpha;
#endif
};

#if LWIP_DCTCP
#include "tcp_impl.h"
struct tcp_hdr;

void dctcp_init(struct tcp_pcb *pcb);
void dctcp_send(struct tcp_pcb *pcb, struct tcp_seg *seg);
void dctcp_recv(struct tcp_pcb *pcb, int ecn, struct tcp_hdr *tcph);
int dctcp_reduce_cwnd (struct tcp_pcb *pcb);
void dctcp_send_empty(struct tcp_pcb *pcb, struct tcp_hdr *tcph);
void dctcp_reduce_cwnd_rto (struct tcp_pcb *pcb);
#endif
#endif