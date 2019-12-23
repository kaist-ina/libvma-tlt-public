#include "tlt.h"
#include <stdlib.h>
#include <execinfo.h>
#include <string.h>


#define CURRENT_ROUND_MARK_DIRTY(s) ((s)->tlt.dirty_map |= 1)
#if LWIP_TCP_TLT
static void tlt_segment_verify (struct tlt_transmit_segment *pSegment, uint16_t *pLen) { 
    LWIP_UNUSED_ARG(pSegment);
    LWIP_UNUSED_ARG(pLen);
// #if DEBUG_TLT
    TLT_ASSERT(pLen);
    TLT_ASSERT(pSegment);
    TLT_ASSERT(*pLen < MAX_UIMP_QUEUE_ELEM);
    TLT_ASSERT(!(*pLen) || pSegment[0].len);
    uint16_t debug_cnt = 0;
    if(*pLen == 0) {
        TLT_ASSERT(pSegment[0].len == 0);
        TLT_ASSERT(pSegment[0].seq == 0);
    }
    
    // DEBUG_TLT_TLT("[Q] ===========");
    for(struct tlt_transmit_segment *ps = pSegment; ps != NULL; ps = ps->next) { 
        if(ps->len) {
            // DEBUG_TLT_TLT("[Q] Segment %u - %u (%p) r=%d", ps->seq, ps->seq + ps->len, ps, ps->round);
            debug_cnt++;
        } else {
            TLT_ASSERT(!ps->next);
        }
        if(ps->next) {
            TLT_ASSERT(ps->next->len);
        }
        // if(ps->seq > 64010 || ps->seq + ps->len > 64010) {
        //     printf("ERROR: ILLEGAL PS->SEQ!! ps->seq=%u. Abort.\n", ps->seq);
        //     char *p = NULL;
        //     *p = '3';
        //     abort();
        // }
    }
    
    // DEBUG_TLT_TLT("[Q] ===========");
    for(int i = 0; i< MAX_UIMP_QUEUE_ELEM; i++) 
    {
        if(!pSegment[i].len) {
            TLT_ASSERT(!pSegment[i].next);
            TLT_ASSERT(!pSegment[i].seq);
        }
        if(pSegment[i].seq) {
            TLT_ASSERT(pSegment[i].len);
        }
        
        if(pSegment[i].next) {
            TLT_ASSERT(pSegment[i].seq);
            TLT_ASSERT(pSegment[i].len);
        }
    }
    TLT_ASSERT(*pLen == debug_cnt);
    TLT_ASSERT(debug_cnt < MAX_UIMP_QUEUE_ELEM);
    
// #endif
}
static void tlt_segment_rearrange_last_block (struct tlt_transmit_segment *pSegment, uint16_t *pLen, int verify) {
    // need to arrange last block
    if (!pSegment || !pSegment->next)
        return;

    struct tlt_transmit_segment *last = pSegment, *prev_last = NULL;
    while(last && last->next) {
        prev_last = last;
        last = last->next;
    }

    TLT_ASSERT(prev_last && last && *pLen >= 2);

    struct tlt_transmit_segment *cur;
    int merged = 0;
    for(cur = pSegment; cur != NULL; cur = cur->next) {
        if (cur->round != last->round) 
            continue;
        if (merged)
            break;
        if (last->seq + last->len == cur->seq) {
            cur->len = last->len + cur->len;
            cur->seq = last->seq;
            merged = 1;
        } else if (cur->seq + cur->len == last->seq) {
            cur->len = cur->len + last->len;
            merged = 1;
        }
    }
    if (merged) {
        last->len = 0;
        last->seq = 0;
        last->round = 0;
        last->next = NULL;
        prev_last->next = NULL;
        (*pLen)--;
        tlt_segment_rearrange_last_block(pSegment, pLen, 0);
    }
    
    if (verify) {
        tlt_segment_verify(pSegment, pLen);
    }

}
static void tlt_segment_merge (struct tlt_transmit_segment *pSegment, uint16_t *pLen) {

    tlt_segment_verify(pSegment, pLen);
    if(pSegment->len && pSegment->next) {
        TLT_ASSERT(*pLen);
        struct tlt_transmit_segment *ps, *pps;
        for(pps = pSegment, ps = pSegment->next; ps != NULL; pps = ps, ps = ps->next) {
            TLT_ASSERT(ps->len);
            TLT_ASSERT(pps);
            if (pps->len + pps->seq == ps->seq && pps->round == ps->round) {
                pps->len += ps->len;
                pps->next = ps->next;
                ps->seq = 0;
                ps->len = 0;
                ps->next = NULL;
                ps = pps;
                *pLen = *pLen - 1;
            }

            if(pps!= ps && pps->seq == ps->seq && pps->len == ps->len) {
                uint32_t rnd = pps->round >= ps->round ? pps->round : ps->round;
                pps->next = ps->next;
                pps->round = rnd;
                ps->next = NULL;
                ps->seq = 0;
                ps->len = 0;
                ps->round = 0;
                *pLen = *pLen - 1;
                ps = pps;
            }
        }
    }

    // printf("Merged TLT Segment\n");
    tlt_segment_verify(pSegment, pLen);
}

static void tlt_segment_add (struct tlt_transmit_segment *pSegment, uint16_t *pLen, uint32_t seq_, uint32_t len_, uint32_t round) {
    
    TLT_ASSERT(pSegment);
    TLT_ASSERT(pLen);
    TLT_ASSERT(len_);
    
    // printf("Adding TLT Segment %u - %u\n", seq_, seq_+len_);
    tlt_segment_verify(pSegment, pLen);

    
    uint16_t i = 0;
    for(; i < *pLen && i < MAX_UIMP_QUEUE_ELEM; i++) {
        if(!pSegment[i].len) {
            break;
        }
    }
    TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
    if (i == 0) {
        // the first elem
        pSegment[i].next = NULL;
        pSegment[i].seq = seq_;
        pSegment[i].len = len_;
        pSegment[i].round = round;
        *pLen = *pLen + 1;
    } else {
        // remove duplicates
        struct tlt_transmit_segment *ps = pSegment;
        uint32_t seq = seq_;
        uint32_t len = len_;
        while(ps) {
            TLT_ASSERT(len);

            if(TCP_SEQ_LEQ(ps->seq, seq) && TCP_SEQ_LEQ(seq+len, ps->seq+ps->len)) {
                // overlap completely, target is included in existing block
                if (ps->round != round) {
                    // should not merge if round is different.
                    uint32_t a_seq = ps->seq;
                    uint32_t a_len = seq-a_seq;
                    uint32_t b_seq = seq;
                    uint32_t b_len = seq+len-b_seq;
                    uint32_t c_seq = seq+len;
                    uint32_t c_len = ps->seq+ps->len-c_seq;
                    if(a_len > 0) {
                        if(c_len > 0) {
                            TLT_ASSERT(a_len && b_len && c_len);
                            // split in three pieces : ps->seq ~ seq, seq~seq+len, seq+len ~ ps->seq+ps->len
                            pSegment[i].seq = b_seq;
                            pSegment[i].len = b_len;
                            pSegment[i].round = round;
                            struct tlt_transmit_segment *c_nxt = ps->next;
                            ps->next = &(pSegment[i]);
                            struct tlt_transmit_segment *b_ptr = &(pSegment[i]);
                            *pLen = *pLen + 1;
                            TLT_ASSERT(pSegment[i].len);
                            TLT_ASSERT(ps->len);
                            // find next available block
                            for(; i < *pLen; i++) {
                                if(!pSegment[i].len) {
                                    break;
                                }
                            }
                            TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                            pSegment[i].seq = b_seq;
                            pSegment[i].len = b_len;
                            pSegment[i].round = ps->round;
                            pSegment[i].next = c_nxt;
                            b_ptr->next = &(pSegment[i]);
                            *pLen = *pLen + 1;
                            TLT_ASSERT(pSegment[i].len);
                            TLT_ASSERT(ps->len);
                            // find next available block
                            for(; i < *pLen; i++) {
                                if(!pSegment[i].len) {
                                    break;
                                }
                            }
                            TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                            
                        } else {
                            TLT_ASSERT(a_len && b_len);
                            // split in two pieces, a and b only
                            pSegment[i].seq = b_seq;
                            pSegment[i].len = b_len;
                            pSegment[i].round = round;
                            struct tlt_transmit_segment *c_nxt = ps->next;
                            ps->next = &(pSegment[i]);
                            pSegment[i].next = c_nxt;
                            TLT_ASSERT(pSegment[i].len);
                            TLT_ASSERT(ps->len);
                            *pLen = *pLen + 1;

                            // find next available block
                            for(; i < *pLen; i++) {
                                if(!pSegment[i].len) {
                                    break;
                                }
                            }
                            TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                        }
                        
                    } else {
                        if(c_len) {
                            TLT_ASSERT(b_len && c_len);
                            // split in two pieces: b and c only
                            pSegment[i].seq = c_seq;
                            pSegment[i].len = c_len;
                            pSegment[i].round = ps->round;
                            pSegment[i].next = ps->next;
                            ps->next = &(pSegment[i]);
                            *pLen = *pLen + 1;

                            // find next available block
                            for(; i < *pLen; i++) {
                                if(!pSegment[i].len) {
                                    break;
                                }
                            }
                            TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                        } else {
                            // no need to split
                            ps->round = round;
                        }
                    }
                } else {
                    len = 0;
                    break;
                }
                tlt_segment_verify(pSegment, pLen);
                            
            } else if(TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(ps->seq+ps->len, seq+len)) {
                // overlap completely, existing block is included in target
                //insert block here (seq ~ ps->seq)
                if(ps->seq > seq) {
                    pSegment[i].seq = seq;
                    pSegment[i].len = ps->seq - seq;
                    pSegment[i].round = round;
                    ps->round = round;
                    struct tlt_transmit_segment *tmp = ps->next;
                    ps->next = &(pSegment[i]);
                    pSegment[i].next = tmp;
                    TLT_ASSERT(pSegment[i].len);
                    TLT_ASSERT(ps->len);
                    *pLen = *pLen + 1;

                    // find next available block
                    for(; i < *pLen; i++) {
                        if(!pSegment[i].len) {
                            break;
                        }
                    }
                    TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                }
                
                tlt_segment_verify(pSegment, pLen);
                len = seq+len - (ps->seq + ps->len);
                seq = ps->seq+ps->len;
                if(!len) break;
            
            } else if (TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(seq + len, ps->seq)) {
                // does not overlap, target < existing block
                // do nothing here
            } else if (TCP_SEQ_LEQ(ps->seq + ps->len, seq) && TCP_SEQ_LEQ(ps->seq + ps->len, seq + len)) {
                // does not overlap, existing block < target
                // do nothing here
            } else if (TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(seq + len, ps->seq + ps->len)) {
                // overlap only left
                if(ps->round != round) {
                    //detach right
                    pSegment[i].seq = seq + len;
                    pSegment[i].len = ps->seq + ps->len - (seq + len);
                    pSegment[i].round = ps->round;
                    pSegment[i].next = ps->next;
                    ps->next = &(pSegment[i]);
                    ps->len = seq + len - ps->seq;
                    ps->round = round;
                    TLT_ASSERT(pSegment[i].len);
                    TLT_ASSERT(ps->len);
                    *pLen = *pLen + 1;
                
                    // find next available block
                    for(; i < *pLen; i++) {
                        if(!pSegment[i].len) {
                            break;
                        }
                    }
                    TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                    
                    tlt_segment_verify(pSegment, pLen);
                }
                len = ps->seq - seq;
            } else if (TCP_SEQ_LEQ(ps->seq, seq) && TCP_SEQ_LEQ(ps->seq + ps->len, seq + len)) {
                // overlap only right
                if(ps->round != round) {
                    //detach right
                    pSegment[i].seq = seq;
                    pSegment[i].len = ps->seq + ps->len - seq;
                    pSegment[i].round = round;
                    pSegment[i].next = ps->next;
                    ps->next = &(pSegment[i]);
                    ps->len = seq - ps->seq;
                    TLT_ASSERT(pSegment[i].len);
                    TLT_ASSERT(ps->len);
                    *pLen = *pLen + 1;

                    
                    // find next available block
                    for(; i < *pLen; i++) {
                        if(!pSegment[i].len) {
                            break;
                        }
                    }
                    TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                    tlt_segment_verify(pSegment, pLen);
                }
                len = seq + len - (ps->seq + ps->len);
                seq = ps->seq + ps->len;
            }
            ps = ps->next;
        }
        TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
        tlt_segment_verify(pSegment, pLen);


        if(len) {
            struct tlt_transmit_segment *pps;
            TLT_ASSERT(pSegment->len);
            if(pSegment->next) {
                for(pps = pSegment, ps = pSegment->next; ps != NULL; pps = ps, ps = ps->next) {
                    TLT_ASSERT(ps->len);
                    TLT_ASSERT(pps);
                    if (TCP_SEQ_LEQ(seq + len, ps->seq)) {
                        pps->next = &(pSegment[i]);
                        pSegment[i].next = ps;
                        pSegment[i].len = len;
                        pSegment[i].seq = seq;
                        pSegment[i].round = round;
                        *pLen = *pLen + 1;
                        len = 0;
                        break;
                    }
                }  
            } else {
                pSegment[i].next = NULL;
                pSegment[i].len = len;
                pSegment[i].seq = seq;
                pSegment[i].round = round;
                pSegment->next = &(pSegment[i]);
                *pLen = *pLen + 1;
                len = 0;
            }
            
            if (len) {
                TLT_ASSERT(pps);
                pps->next = &(pSegment[i]);
                pSegment[i].seq = seq;
                pSegment[i].len = len;
                pSegment[i].round = round;
                pSegment[i].next = NULL;
                *pLen = *pLen + 1;
                len = 0;
            }
        }
        TLT_ASSERT(!len);
    }
    // debug
    // DEBUG_TLT_TLT("Added TLT Segment %u - %u", seq_, seq_+len_);
    tlt_segment_verify(pSegment, pLen);

    tlt_segment_merge(pSegment, pLen);
    tlt_segment_rearrange_last_block(pSegment, pLen, 1);
}

static void tlt_segment_remove_round (struct tlt_transmit_segment *pSegment, uint16_t *pLen, uint32_t seq, uint32_t len, uint32_t round) {
    // DEBUG_TLT_TLT("Removing TLT Segment %u - %u", seq, seq+len);
    tlt_segment_verify(pSegment, pLen);
    int check_round = (round != 0xFFFFFFFF);

    if(pSegment->len) {
        TLT_ASSERT(*pLen);
        struct tlt_transmit_segment *ps, *pps;
        int preserve_loc = 0;
        for(pps = NULL, ps = pSegment; ps != NULL; pps = (preserve_loc ? NULL : ps), ps = (preserve_loc ? ps : ps->next)) {
            if(!preserve_loc) // might removed last element
                TLT_ASSERT(ps->len);
            else if (!ps->len) break;
            preserve_loc = 0;
            if(check_round && round > ps->round)
                continue;
            if(TCP_SEQ_LEQ(ps->seq, seq) && TCP_SEQ_LEQ(seq+len, ps->seq+ps->len)) {
                // overlap completely, target is included in existing block

                if(ps->seq == seq && seq + len == ps->seq + ps->len) {
                    if(!pps) {
                        TLT_ASSERT(ps == pSegment);
                        if(ps->next) {
                            // move next segment here if exists
                            ps->len = ps->next->len;
                            ps->seq = ps->next->seq;
                            ps->round = ps->next->round;
                            ps->next->len = 0;
                            ps->next->seq = 0;
                            struct tlt_transmit_segment *tns = ps->next->next;
                            ps->next->next = NULL;
                            ps->next = tns;                   
                            *pLen = *pLen - 1;
                            preserve_loc = 1;
                        } else {
                            ps->len = 0;
                            ps->seq = 0;
                            ps->round = 0;
                            ps->next = NULL;
                            ps = pSegment;               
                            *pLen = *pLen - 1;
                            preserve_loc = 1;
                        }
                    } else {
                        pps->next = ps->next;
                        ps->len = 0;
                        ps->seq = 0;
                        ps->next = NULL;
                        ps = pps;                   
                        *pLen = *pLen - 1;
                    }
                    
                    // if(ps->seq > 64010 || ps->seq + ps->len > 64010) {
                    //     printf("ERROR: ILLEGAL PS->SEQ!! ps->seq=%u. Abort.\n", ps->seq);
                    //     char *p = NULL;
                    //     *p = '3';
                    //     abort();
                    // }
                } else if (ps->seq == seq) {
                    uint32_t pslen = ps->len - len; //ps->seq + ps->len - (seq + len);
                    uint32_t psseq = seq + len; // ps->seq + ps->len - len;
                    // if(psseq > 64010 || psseq + pslen > 64010) {
                    //     printf("ERROR: ILLEGAL PS->SEQ!! ps->seq=%u. Abort.\n", ps->seq);
                    //     printf("ps:%u-%u, cur:%u-%u, res:%u-%u\n", ps->seq, ps->seq+ps->len, seq, seq+len, psseq, psseq+pslen);
                    //     char *p = NULL;
                    //     *p = '3';
                    //     abort();
                    // }
                    
                    ps->len = pslen;
                    ps->seq = psseq;
                } else if (ps->seq + ps->len == seq + len) {
                    ps->len = seq - ps->seq;
                } else {
                    uint16_t i = 0;
                    // find next available block
                    for(; i < *pLen; i++) {
                        if(!pSegment[i].len) {
                            break;
                        }
                    }
                    TLT_ASSERT(i < MAX_UIMP_QUEUE_ELEM);
                    pSegment[i].seq = seq + len;
                    pSegment[i].len = ps->len + ps->seq - (seq+len);
                    pSegment[i].next = ps->next;
                    ps->len = seq - ps->seq;
                    ps->next = &(pSegment[i]);
                    *pLen = *pLen + 1;
                }
                len = 0;
                break;
                
            } else if(TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(ps->seq+ps->len, seq+len)) {
                // overlap completely, existing block is included in target
                //insert block here (seq ~ ps->seq)
                if(!pps) {
                    TLT_ASSERT(ps == pSegment);
                    if(ps->next) {
                        // move next segment here if exists
                        ps->len = ps->next->len;
                        ps->seq = ps->next->seq;
                        ps->round = ps->next->round;
                        ps->next->len = 0;
                        ps->next->seq = 0;
                        struct tlt_transmit_segment *tns = ps->next->next;
                        ps->next->next = NULL;
                        ps->next = tns;      
                        ps = pSegment;               
                        *pLen = *pLen - 1;
                        preserve_loc = 1;
                    } else {
                        ps->len = 0;
                        ps->seq = 0;
                        ps->round = 0;
                        ps->next = NULL;
                        ps = pSegment;                   
                        *pLen = *pLen - 1;
                        preserve_loc = 1;
                    }
                } else {
                    pps->next = ps->next;
                    ps->len = 0;
                    ps->seq = 0;
                    ps->next = NULL;
                    ps = pps;
                    *pLen = *pLen - 1;
                }
                
                // if(ps->seq > 64010 || ps->seq + ps->len > 64010) {
                //     printf("ERROR: ILLEGAL PS->SEQ!! ps->seq=%u. Abort.\n", ps->seq);
                //     char *p = NULL;
                //     *p = '3';
                //     abort();
                // }
            
            } else if (TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(seq + len, ps->seq)) {
                // does not overlap, target < existing block
                // do nothing here
            } else if (TCP_SEQ_LEQ(ps->seq + ps->len, seq) && TCP_SEQ_LEQ(ps->seq + ps->len, seq + len)) {
                // does not overlap, existing block < target
                // do nothing here
            } else if (TCP_SEQ_LEQ(seq, ps->seq) && TCP_SEQ_LEQ(seq + len, ps->seq + ps->len)) {
                // overlap only left
                ps->len = ps->seq + ps->len - (seq + len);
                ps->seq = seq + len;

                
                // if(ps->seq > 64010 || ps->seq + ps->len > 64010) {
                //     printf("ERROR: ILLEGAL PS->SEQ!! ps->seq=%u. Abort.\n", ps->seq);
                //     char *p = NULL;
                //     *p = '3';
                //     abort();
                // }

            } else if (TCP_SEQ_LEQ(ps->seq, seq) && TCP_SEQ_LEQ(ps->seq + ps->len, seq + len)) {
                // overlap only right
                ps->len = seq - ps->seq;
            }
        }
    }
    // printf("Removed TLT Segment %u - %u\n", seq, seq+len);
    tlt_segment_verify(pSegment, pLen);
}

static void tlt_segment_remove (struct tlt_transmit_segment *pSegment, uint16_t *pLen, uint32_t seq, uint32_t len) {
    tlt_segment_remove_round(pSegment, pLen, seq, len, 0xFFFFFFFF);
}

void tlt_init(struct tcp_pcb *pcb) {
    memset(&pcb->tlt, 0, sizeof(pcb->tlt));
    pcb->tlt.recv_state = Idle;
    pcb->tlt.send_state = Idle;
    
}

static void tlt_send_syn (struct tcp_pcb *pcb, struct tcp_seg *seg) {
    TLT_ASSERT(pcb && seg);

    seg->tos |= TLT_TOS_IMP;
    pcb->tlt.send_state = Pending;
}

static void tlt_send_ack (struct tcp_pcb *pcb, struct tcp_seg *seg) {
    TLT_ASSERT(pcb && seg);

    TLT_ASSERT(pcb->tlt.recv_state == Pending || pcb->tlt.recv_state == PendingForce || pcb->tlt.recv_state == Idle);
    
    if(pcb->tlt.recv_state == Pending) {
        // When the receiver has received important
        pcb->tlt.recv_state = Idle;
        seg->tos |= TLT_TOS_IMPE;
        return;
    }
    
    if(pcb->tlt.recv_state == PendingForce) {
        // When the receiver has received important force
        pcb->tlt.recv_state = Idle;
        seg->tos |= TLT_TOS_IMPEF;
        return;
    }

    /* When receiver has not received important/important force echo */
    seg->tos |= TLT_TOS_IMPC;
}


static void tlt_send_payload (struct tcp_pcb *pcb, struct tcp_seg *seg) {
    TLT_ASSERT(pcb->tlt.send_state == Pending || pcb->tlt.send_state == Idle || pcb->tlt.send_state == PendingForce);

    if (pcb->tlt.send_state == Pending) {
        pcb->tlt.send_state = Idle;
        seg->tos |= TLT_TOS_IMP;
        return;
    }

    if (pcb->tlt.send_state == PendingForce) {
        pcb->tlt.send_state = Idle;
        seg->tos |= TLT_TOS_IMPF;
        return;
    }

    /* When the sender has not received imporatnt echo */
    seg->tos |= (uint8_t) TLT_TOS_UIMP;
    tlt_segment_add(pcb->tlt.uimp_queue, &(pcb->tlt.uimp_queue_len), 
        seg->seqno - pcb->iss, seg->len, pcb->tlt.cur_round);
        
    pcb->tlt.dirty_map |= 1;
}

int tlt_uimpq_peek (struct tcp_pcb *pcb, uint32_t maxLen, uint32_t *pSeq, uint32_t *pLen, uint32_t round) {
    LWIP_UNUSED_ARG(round);
    if(pcb->tlt.uimp_queue->len) {
        *pSeq = pcb->tlt.uimp_queue->seq;
        if(pcb->tlt.uimp_queue->len > maxLen)
            *pLen = maxLen;
        else
            *pLen = pcb->tlt.uimp_queue->len;

        return *pLen;
    }
    return 0;
}
int tlt_uimpq_pop (struct tcp_pcb *pcb, uint32_t maxLen, uint32_t *pSeq, uint32_t *pLen, uint32_t round) {
    if(pcb->tlt.uimp_queue->len) {
        *pSeq = pcb->tlt.uimp_queue->seq;
        if(pcb->tlt.uimp_queue->len > maxLen)
            *pLen = maxLen;
        else
            *pLen = pcb->tlt.uimp_queue->len;
        
        tlt_segment_remove_round(pcb->tlt.uimp_queue, &(pcb->tlt.uimp_queue_len), *pSeq, *pLen, round);
        return *pLen;
    }
    return 0;
}

#define CURRENT_ROUND_MARK_DIRTY(s) ((s)->tlt.dirty_map |= 1)

int tlt_is_lost (struct tcp_pcb *pcb, int check_dirty) {

    if(pcb->tlt.uimp_queue->len) {
        struct tlt_transmit_segment *ps;
        for(ps = pcb->tlt.uimp_queue; ps != NULL; ps = ps->next) {
            assert(ps->len);
            if(ps->round < pcb->tlt.cur_round - 2)
                return 1;
        }
    }

    
    // must also return true if tlt.cur_round - 2 has never been written
    if(check_dirty && !(pcb->tlt.dirty_map & (1<<2))) {
        return 1;
    }
    return 0;
}

int tlt_in (struct tcp_pcb *pcb, struct tcp_seg *seg, uint8_t tos) {
    
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(seg);
    LWIP_UNUSED_ARG(tos);
    
    if (!pcb || !seg)
        return PACKET_ACCEPT;

    if (TCPH_FLAGS(seg->tcphdr) & TCP_ACK) {
        tlt_segment_remove(pcb->tlt.uimp_queue, &(pcb->tlt.uimp_queue_len), 0, (seg->tcphdr->ackno) - pcb->iss);
    }

    tos = tos & ~(0x03);

    if(tos == TLT_TOS_IMP) {
        pcb->tlt.recv_state = Pending;
    } else if (tos == TLT_TOS_IMPE) {
        pcb->tlt.send_state = Pending;
        pcb->tlt.cur_round++;
        pcb->tlt.dirty_map = (pcb->tlt.dirty_map << 1);
    } else if (tos == TLT_TOS_IMPF) {
        pcb->tlt.recv_state = PendingForce;
    } else if (tos == TLT_TOS_IMPEF) {
        pcb->tlt.send_state = Pending;
        pcb->tlt.cur_round++;
        pcb->tlt.dirty_map = (pcb->tlt.dirty_map << 1);
        /* According to logic, drop packet by comparing acks and seq number */
        // if(TCP_SEQ_GT(pcb->unacked->seqno + TCP_TCPLEN(pcb->unacked), seg->tcphdr->ackno)) {
        //     return PACKET_DROP;
        // }
    }
    return PACKET_ACCEPT;
}

uint8_t tlt_out_empty_ack (struct tcp_pcb *pcb) {
    TLT_ASSERT(pcb); 
    TLT_ASSERT(pcb->tlt.recv_state == Pending || pcb->tlt.recv_state == PendingForce || pcb->tlt.recv_state == Idle);

    if(pcb->tlt.recv_state == Pending) {
        /* When the receiver has received important */
        pcb->tlt.recv_state = Idle;
        return pcb->tos | TLT_TOS_IMPE;
    }
    
    if(pcb->tlt.recv_state == PendingForce) {
        /* When the receiver has received important force */
        pcb->tlt.recv_state = Idle;
        return pcb->tos | TLT_TOS_IMPEF;
    }

    /* When receiver has not received important/important force echo */
    return pcb->tos | TLT_TOS_IMPC;
}

void tlt_out (struct tcp_pcb *pcb, struct tcp_seg *seg) {
    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(seg);
     
    if(!pcb || !seg) return;

    if(seg->len) {
        tlt_send_payload(pcb, seg);
    } else {
        // data len
        if (TCPH_FLAGS(seg->tcphdr) & TCP_SYN) {
            DEBUG_TLT_TLT("SYN");
            tlt_send_syn(pcb, seg);
            return;
        } else if (TCPH_FLAGS(seg->tcphdr) & TCP_ACK) {
            DEBUG_TLT_TLT("ACK");
            tlt_send_ack(pcb, seg);
        } else if (TCPH_FLAGS(seg->tcphdr) & TCP_RST) {
            DEBUG_TLT_TLT("RST");
            return;
        }
    }
    // TLT_ASSERT(pcb->tlt.send_state == Idle);
}

void tlt_print_stacktrace () {
    void* frames[100]; 
    size_t frame_depth = backtrace(frames, 100); 
    char** frame_info = backtrace_symbols(frames, frame_depth); 
    size_t i; 
    for(i = 0; i < frame_depth; ++i) printf("%s\n", frame_info[i]); 
    free(frame_info);
}
#endif