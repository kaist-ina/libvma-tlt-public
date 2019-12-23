#ifndef CUSTOM_H
#define CUSTOM_H


#define TIMER_US_GRAN 0

#if TIMER_US_GRAN
#define TCP_INITIAL_RTO 100000 //us
#define TCP_MIN_RTO 200 //us
#define TCP_INITIAL_RTT_EST 200 //us
#else
#define TCP_INITIAL_RTO 100 //ms
#define TCP_MIN_RTO 4 //ms
#define TCP_INITIAL_RTT_EST 4 //ms
#endif

#endif