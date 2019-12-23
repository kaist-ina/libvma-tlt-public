/* 
 * Author: Hwijoon Lim <hwijoon.lim@kaist.ac.kr>
 */
#ifndef __LWIP_TLT_H__
#define __LWIP_TLT_H__
#include <assert.h>
#include <stdint.h>
/**
 * TLT Important Marking
 * Important : AF11(0x0A)
 * Important Echo : AF12(0x0C)
 * Important Force : AF21(0x12)
 * Important Echo Force : AF22(0x14)
 * Important Fast Retrans/CTRL : AF31(0x1A)
 * Unimportant : CS1(0x08)
 */
#define TLT_TOS_IMP     ((uint8_t)(0x0A<<2))
#define TLT_TOS_IMPE    ((uint8_t)(0x0C<<2))
#define TLT_TOS_IMPF    ((uint8_t)(0x12<<2))
#define TLT_TOS_IMPEF   ((uint8_t)(0x14<<2))
#define TLT_TOS_IMPFR   ((uint8_t)(0x1A<<2))
#define TLT_TOS_IMPC    ((uint8_t)(0x1A<<2))
#define TLT_TOS_UIMP    ((uint8_t)(0x08<<2))

#define DEBUG_TLT 0
#if DEBUG_TLT
#define DEBUG_TLT_TLT(msg, ...) printf("%s:%d:" msg "\n", __FILE__, __LINE__, ## __VA_ARGS__)

#define TLT_ASSERT(assertion) assert(assertion)
#define DEBUG_TLT_STACKTRACE(msg, ...) DEBUG_TLT_TLT(msg,## __VA_ARGS__); tlt_print_stacktrace()
#else
#define DEBUG_TLT_SACK(msg, ...)
#define DEBUG_TLT_TLT(msg, ...) 
#define TLT_ASSERT(assertion)
#define DEBUG_TLT_STACKTRACE(msg, ...)
#endif

#define MAX_UIMP_QUEUE_ELEM 256

typedef enum tlt_state {
    Idle = 0,
    Pending,
    PendingForce
} tlt_state_t;

typedef enum tlt_recv_behavior {
    PACKET_ACCEPT = 0,
    PACKET_DROP
} tlt_recv_behavior_t;

struct tlt_transmit_segment {
    uint32_t seq;
    uint32_t len;
    uint32_t round;
    struct tlt_transmit_segment *next;
};

struct tlt_param {
    tlt_state_t recv_state;
    tlt_state_t send_state;
    struct tlt_transmit_segment uimp_queue[MAX_UIMP_QUEUE_ELEM];
    uint16_t uimp_queue_len;
    // struct tcp_seg *uimp;
    uint32_t cur_round;
    uint8_t dirty_map;
};


#include "tcp_impl.h"
int tlt_uimpq_peek (struct tcp_pcb *pcb, uint32_t maxLen, uint32_t *pSeq, uint32_t *pLen, uint32_t round);
int tlt_uimpq_pop (struct tcp_pcb *pcb, uint32_t maxLen, uint32_t *pSeq, uint32_t *pLen, uint32_t round);
int tlt_is_lost (struct tcp_pcb *pcb, int check_dirty);
void tlt_init(struct tcp_pcb *pcb);
uint8_t tlt_out_empty_ack (struct tcp_pcb *pcb);
int tlt_in (struct tcp_pcb *pcb, struct tcp_seg *seg, uint8_t tos);
void tlt_out (struct tcp_pcb *pcb, struct tcp_seg *seg);

void tlt_print_stacktrace ();

#endif /* __LWIP_TLT_H__ */