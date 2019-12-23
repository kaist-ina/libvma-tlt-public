#include "dctcp.h"
#include <string.h>

#if LWIP_DCTCP
void dctcp_init(struct tcp_pcb *pcb) {
    memset(&(pcb->dctcp), 0, sizeof (struct dctcp_param));
    pcb->dctcp.enabled = 1;
    pcb->dctcp.WindowEnd = pcb->lastack; /* Initialize to SND.UNA = pcb->lastack = iss */
    pcb->dctcp.Alpha = 1;
}

void dctcp_send(struct tcp_pcb *pcb, struct tcp_seg *seg) {
    if(!seg || !pcb) return;

    /* Mark ECT */
    dctcp_send_empty(pcb, seg->tcphdr);
    seg->tos &= ~TOS_ECT_CE;
    if(TCPH_FLAGS(seg->tcphdr) & TCP_SYN) {

    } else {
        if(seg->len) {
#if EMULATE_RANDOM_CE_MARK
            int rand_ce = !(rand() % 8);
            if (rand_ce) {
                printf("Emulating Random CE: Mark=%c\n", rand_ce ? 'Y' : 'N');
                seg->tos |= TOS_ECT_CE; 
                pcb->dctcp.dbg_sent_ce++;
            } else {
                seg->tos |= TOS_ECT_1;  
                pcb->dctcp.dbg_sent_non_ce++;
            }
#else
            seg->tos |= TOS_ECT_1;   
#endif
        }
    }

}

void dctcp_send_empty(struct tcp_pcb *pcb, struct tcp_hdr *tcph) {
    if (!pcb || !tcph) return;

    TCPH_UNSET_FLAG(tcph, TCP_ECE);
    TCPH_UNSET_FLAG(tcph, TCP_CWR);
    

    if(TCPH_FLAGS(tcph) & TCP_SYN) {
        pcb->dctcp.enabled = 1;
        /* ECN-setup SYN packet per RFC 3168 */
        if(!(TCPH_FLAGS(tcph) & TCP_ACK))
            TCPH_SET_FLAG(tcph, TCP_CWR);
        TCPH_SET_FLAG(tcph, TCP_ECE);
    } else {
        if (pcb->dctcp.pending_cwr) {
            TCPH_SET_FLAG(tcph, TCP_CWR);
            pcb->dctcp.pending_cwr = 0;
        }
        if (pcb->dctcp.ce > 0) {
            TCPH_SET_FLAG(tcph, TCP_ECE);
            pcb->dctcp.ce--;
        }
    }
    DEBUG_DCTCP("<< OUT     Flag %2x: %s%s%s%s%s%s\n", TCPH_FLAGS(tcph),
        TCPH_FLAGS(tcph) & TCP_SYN ? "SYN " : "", TCPH_FLAGS(tcph) & TCP_FIN ? "FIN " : "", TCPH_FLAGS(tcph) & TCP_PSH ? "PSH " : "", TCPH_FLAGS(tcph) & TCP_ACK ? "ACK " : "", TCPH_FLAGS(tcph) & TCP_ECE ? "ECN " : "", TCPH_FLAGS(tcph) & TCP_CWR ? "CWR" : "");
}

void dctcp_recv (struct tcp_pcb *pcb, int ecn, struct tcp_hdr *tcph) {
    /* Per RFC 8257, process CWR first and then CE */
    /* Not consider delayed ack here */
    if (!pcb || !tcph) return;

    DEBUG_DCTCP(">> IN CE=%d Flag %2x: %s%s%s%s%s%s\n", ecn & 0x03, TCPH_FLAGS(tcph),
        TCPH_FLAGS(tcph) & TCP_SYN ? "SYN " : "", TCPH_FLAGS(tcph) & TCP_FIN ? "FIN " : "", TCPH_FLAGS(tcph) & TCP_PSH ? "PSH " : "", TCPH_FLAGS(tcph) & TCP_ACK ? "ACK " : "", TCPH_FLAGS(tcph) & TCP_ECE ? "ECN " : "", TCPH_FLAGS(tcph) & TCP_CWR ? "CWR" : "");

    /* Per DCTCP paper, we should mark ECE if and only if CE = 1, which is different from TCP-ECN (mark ECE until CWR is received)
     * if (TCPH_FLAGS(tcph) & TCP_CWR) {
     *   pcb->dctcp.ce = 0;
     * } 
     * 
     * if((ecn & 0x03) == TOS_ECT_CE) {
     *   pcb->dctcp.ce = 1;
     * }
     * */

    if((ecn & 0x03) == TOS_ECT_CE) {
        pcb->dctcp.ce++; /* Use as counter to send ECE */
    }

    if ((TCPH_FLAGS(tcph) & TCP_ACK) && !(TCPH_FLAGS(tcph) & TCP_SYN)) {
        u32_t BytesAcked = tcph->ackno - pcb->lastack;
        pcb->dctcp.BytesAcked += BytesAcked;
        if (TCPH_FLAGS(tcph) & TCP_ECE) {
            pcb->dctcp.BytesMarked += BytesAcked;            
#if EMULATE_RANDOM_CE_MARK
            pcb->dctcp.dbg_rcvd_ece++;
#endif
        }
        if (TCP_SEQ_LEQ(tcph->ackno, pcb->dctcp.WindowEnd)) {
            /* stop processing */
        } else if (pcb->dctcp.BytesAcked > 0) {
            /* check pcb->dctcp.BytesAcked > 0 to prevent div/0 err */

#if DCTCP_ALPHA_DEBUG
            /* FP calculation */
            double cur_alpha = ((double)pcb->dctcp.BytesMarked) / pcb->dctcp.BytesAcked;
            pcb->dctcp.acc_alpha = (1. - 1./16.) * pcb->dctcp.acc_alpha + cur_alpha/16.;
#endif

            u32_t ScaledM = DCTCP_SCF * pcb->dctcp.BytesMarked;
            ScaledM = ScaledM / pcb->dctcp.BytesAcked;
            if ((pcb->dctcp.Alpha >> DCTCP_SHF) == 0)
                pcb->dctcp.Alpha = 0;

            pcb->dctcp.Alpha += (ScaledM >> DCTCP_SHF) - (pcb->dctcp.Alpha >> DCTCP_SHF);

            if (pcb->dctcp.Alpha > DCTCP_SCF)
                pcb->dctcp.Alpha = DCTCP_SCF;

            pcb->dctcp.WindowEnd = pcb->snd_nxt;
            pcb->dctcp.BytesAcked = pcb->dctcp.BytesMarked = 0;
            DEBUG_DCTCP("[%p] Sending Alpha=%lf\n", pcb, ((double)pcb->dctcp.Alpha)/DCTCP_SCF);
#if DCTCP_ALPHA_DEBUG
            printf("[%p] Alpha by FP calculation = %lf\n", pcb,  pcb->dctcp.acc_alpha );
#endif
#if EMULATE_RANDOM_CE_MARK
            printf("[%p] Sent non-CE = %d, Sent CE = %d, Received ECE =%d\n", pcb, pcb->dctcp.dbg_sent_non_ce, pcb->dctcp.dbg_sent_ce, pcb->dctcp.dbg_rcvd_ece);
            printf("[%p] Ideal alpha should be %d/%d = %lf\n", pcb, pcb->dctcp.dbg_sent_ce,  pcb->dctcp.dbg_sent_non_ce+ pcb->dctcp.dbg_sent_ce, ((double)pcb->dctcp.dbg_sent_ce) / (pcb->dctcp.dbg_sent_non_ce+ pcb->dctcp.dbg_sent_ce));
#endif
        }
    }

    if ((TCPH_FLAGS(tcph) & TCP_ECE) && !(TCPH_FLAGS(tcph) & TCP_SYN)) {
        /* should reduce cwnd no more than once per window */
        if (pcb->snd_nxt >= pcb->dctcp.next_ece) {
            pcb->dctcp.pending_ece = 1;
            pcb->dctcp.next_ece = pcb->snd_nxt + pcb->cwnd;
            /* somewhere inside tcp_in.c, must call dctcp_reduce_cwnd */
        }
    }
}

int dctcp_reduce_cwnd (struct tcp_pcb *pcb) {
    if (!pcb) return 0;
    if (!pcb->dctcp.pending_ece) return 0;

    pcb->dctcp.pending_ece = 0;

    /* According to RFC */
    /* pcb->cwnd = cwnd * (1 - pcb->dctcp.Alpha / 2); if unscaled */
    uint64_t cwnd = (uint64_t) pcb->cwnd;
    cwnd = cwnd * (uint64_t)(DCTCP_SCF - pcb->dctcp.Alpha / 2);
    cwnd = cwnd / DCTCP_SCF;

    DEBUG_DCTCP("[%p] Reducing CWND to %u -> %u (Alpha=%lf)\n", pcb, pcb->cwnd, (uint32_t) cwnd, ((double)pcb->dctcp.Alpha)/DCTCP_SCF);
    pcb->cwnd = cwnd;

    
    pcb->dctcp.pending_cwr = 1;
    return 1;
}

void dctcp_reduce_cwnd_rto (struct tcp_pcb *pcb) {
    if (!pcb) return;
    /* Already CWND has been reduced to half. just report CWR */
    pcb->dctcp.pending_cwr = 1;
    return;
}

#endif