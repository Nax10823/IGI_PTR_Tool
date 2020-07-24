/*
 * Copyright (c) 2006
 * Ningning Hu and the Carnegie Mellon University.
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.  
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "setsignal.h"
#include "common.h"

#define PhaseSleep	(0)
#define ABS(a)     (((a) < 0) ? -(a) : (a))

extern char *optarg;	
extern int optind, opterr, optopt;

char version[] = "2.1";

int delay_num = 0;
int packet_size = PacketSize;
int phase_num = 3;
int probe_num = ProbeNum;
FILE * trace_fp = NULL;
int verbose = 0;
int debug = 0;

double b_bw = 0, competing_bw, PTR_bw, a_bw, c_bw[MaxRepeat];
int probing_phase_count = 0;
double probing_start_time = 0, probing_end_time = 0;

uint16 dst_port = START_PORT, src_port = 0, probing_port;
uint32 dst_ip, src_ip;
char dst_hostname[MAXHOSTNAMELEN], src_hostname[MAXHOSTNAMELEN];
char dst_ip_str[16], src_ip_str[16];
char dst[MAXHOSTNAMELEN], src[MAXHOSTNAMELEN];
int control_sock, probing_sock;
struct sockaddr_in probing_server, probing_server2 ;

RETSIGTYPE (*oldhandler)(int);

double src_gap_sum = 0; // entry value
double avg_src_gap; // entry value
struct pkt_rcd_t dst_gap[MaxProbeNum];
double dst_gap_sum;
double avg_dst_gap;
int dst_gap_count, src_gap_count;
int total_count;
double send_times[MaxProbeNum];
double tlt_src_gap, tlt_dst_gap;

/* keep the data from dst machine, 128 is for other control string */
char msg_buf[sizeof(struct pkt_rcd_t) * MaxProbeNum + 128];
int  msg_len;

/* trace item: record the item used to dump out into trace file */
struct trace_item {
	int probe_num, packet_size, delay_num;

     	double send_times[MaxProbeNum];
	struct pkt_rcd_t rcv_record[MaxProbeNum];
	int record_count;

	double avg_src_gap;
	double avg_dst_gap;

	double b_bw, c_bw, a_bw, ptr;

	struct trace_item * next;
};
struct trace_item * trace_list = NULL;
struct trace_item * trace_tail = NULL;
struct trace_item * cur_trace = NULL;


/* usage message */
void Usage()
{
printf("IGI/PTR-%s client usage:\n\n", version);
printf("\tptr-client [-n probe_num] [-s packet_size] [-p dst_port]\n");
printf("\t           [-k repeat_num] [-f trace_file] [-vdh] dst_address\n\n");
printf("\t-n      set the number of probing packets in each train [60]\n");
printf("\t-s      set the length of the probing packets in byte [500B]\n");
printf("\t-p      indicate the dst machine's listening port [10241]\n");
printf("\t        This is optional, it can itself search for the port\n");
printf("\t        that the igi_server is using.\n");
printf("\t-k      the number of train probed for each source gap [3]\n");
printf("\t-f      dump packet-level trace into trace_file\n");
printf("\t-v      verbose mode.\n");
printf("\t-d      debug mode.\n");
printf("\t-h      print this message.\n");
printf("dst_address     can be either an IP address or a hostname\n\n");
exit(1);
}

/* combine the sec & usec of record into a real number */
double get_rcd_time(struct pkt_rcd_t record)
{
	return (record.sec + (double)record.u_sec / 1000000);
}


/* dump out all the trace packet time stamps */
void dump_trace()
{
	struct trace_item * p;
	int i, index;

	if (trace_fp == NULL) {
	    printf("-w not specified, no trace to dump\n");
	    return;
	}

	/* first dump out the summary data */
	p = trace_list;
	index = 0;
	fprintf(trace_fp, "\n%%probe_num packet_size delay_num avg_src_gap arv_dst_gap b_bw c_bw a_bw ptr\n");
	fprintf(trace_fp, "summary_data = [\n");
	while (p != NULL) {
	    index ++;
	    fprintf(trace_fp,"%2d %4d %5d %f %f %12.3f %12.3f %12.3f %12.3f\n", 
			    p->probe_num,
			    p->packet_size,
			    p->delay_num,
			    p->avg_src_gap,
			    p->avg_dst_gap,
			    p->b_bw,
			    p->c_bw,
			    p->a_bw,
			    p->ptr);
	    p = p->next;
	}
	fprintf(trace_fp, "];\n");
	fprintf(trace_fp, "phase_num = %d; \n\n", index);

	/* then the detail packet trace */
	p = trace_list;
	index = 0;
	while (p != NULL) {
	    index ++;

	    fprintf(trace_fp, "send_time_%d = [\n", index);
	    for (i=1; i<p->probe_num; i++) {
		/*fprintf(trace_fp, "%f %f %f ", 
			p->send_times[i-1], p->send_times[i], 
			p->send_times[i] - p->send_times[i-1]);*/
		fprintf(trace_fp, "%f ", 
			p->send_times[i] - p->send_times[i-1]);
	    } 
	    fprintf(trace_fp, "];\n");
	    fprintf(trace_fp, "send_array_size(%d) = %d; \n\n", 
			    index, p->probe_num - 1);


	    fprintf(trace_fp, "recv_time_%d = [\n", index);

	    for (i=0; i<p->record_count-1; i++) {
	    	/*fprintf(trace_fp, "%f %f %f %d ", 
			get_rcd_time(p->rcv_record[i]),
			get_rcd_time(p->rcv_record[i+1]),
			get_rcd_time(p->rcv_record[i+1]) - 
			get_rcd_time(p->rcv_record[i]),
			p->rcv_record[i+1].seq);*/
	    	fprintf(trace_fp, "%f %d ", 
			get_rcd_time(p->rcv_record[i+1]) - 
			get_rcd_time(p->rcv_record[i]),
			p->rcv_record[i+1].seq);
	    }
	    fprintf(trace_fp, "];\n");
	    fprintf(trace_fp, "recv_array_size(%d) = %d; \n\n", 
			    index, p->record_count - 1);

	    p = p->next;
	}
}


/* make a clean exit on interrupts */
RETSIGTYPE cleanup(int signo)
{
	if (trace_fp != NULL) {
	    dump_trace();
	    fclose(trace_fp);
	}

	close(probing_sock);
	close(control_sock);

	/* free trace list */
  	if (trace_list != NULL) {
	    struct trace_item * pre, * p;
	    p = trace_list;

	    while (p != NULL) {
		pre = p;
		p = p->next;
		free(pre);
	    }
  	}

  	exit(0);
}

void quit()
{
	fflush(stdout);
	fflush(stderr);
	cleanup(1);
	exit(-1);
}

/* get the current time */
double get_time()
{
        struct timeval tv;
        struct timezone tz;
        double cur_time;

        if (gettimeofday(&tv, &tz) < 0) {
            perror("get_time() fails, exit\n");
            quit();
        }

        cur_time = (double)tv.tv_sec + ((double)tv.tv_usec/(double)1000000.0);
        return cur_time;
}


/* find the delay number which can set the src_gap exactly as "gap" */
int get_delay_num(double gap) 
{
#define Scale 		(10)
	int lower, upper, mid;
	double s_time, e_time, tmp;
	int k;

	lower = 0;
	upper = 16;
	tmp = 133333;

	/* search for upper bound */
	s_time = e_time = 0;
	while (e_time - s_time < gap * Scale) {
	    s_time = get_time();
	    for (k=0; k<upper * Scale; k++) {
		tmp = tmp * 7;
		tmp = tmp / 13;
	    }
	    e_time = get_time();

	    upper *= 2;
	}

	/* binary search for delay_num */
	mid = (int)(upper + lower) / 2;
	while (upper - lower > 20) {
	    s_time = get_time();
	    for (k=0; k<mid * Scale; k++) {
		tmp = tmp * 7;
		tmp = tmp / 13;
	    }
	    e_time = get_time();

	    if (e_time - s_time > gap * Scale) 
		upper = mid;
	    else 
		lower = mid;

	    mid = (int)(upper + lower) / 2;
	}

	return mid;
}

void dump_bandwidth()
{
	printf("\nPTR: %7.3f Mpbs (suggested)\n", PTR_bw / 1000000);
	printf("IGI: %7.3f Mpbs\n", a_bw / 1000000);
	printf("Probing uses %.3f seconds, %d trains, ending at a gap value of %d us.\n", probing_end_time - probing_start_time,
		probing_phase_count,
	    	(int)(tlt_dst_gap * 1000000));

	if (trace_fp != NULL) {
	    fprintf(trace_fp, "%%Bottleneck Bandwidth: %7.3f Mbps\n", b_bw / 1000000);
	    fprintf(trace_fp, "%%Competing  Bandwidth: %7.3f Mpbs\n", competing_bw / 1000000);
	    fprintf(trace_fp, "%%Packet Transmit Rate: %7.3f Mpbs\n", PTR_bw / 1000000);
	    fprintf(trace_fp, "%%Available  Bandwidth: %7.3f Mpbs\n", a_bw / 1000000);
	}
}

int init_sockets(uint32 dst_ip)
{
	struct sockaddr_in client;

	probing_sock = socket(AF_INET, SOCK_DGRAM, 0);

	probing_server.sin_family = AF_INET;
	probing_server.sin_addr.s_addr = dst_ip;
	probing_server.sin_port = htons(probing_port);

	/* used for sending the tail "junk" packet */
	probing_server2.sin_family = AF_INET;
	probing_server2.sin_addr.s_addr = dst_ip;
	probing_server2.sin_port = htons(probing_port+1);

	client.sin_addr.s_addr = htonl(INADDR_ANY);
	client.sin_family = AF_INET;
	client.sin_port = htons(0);

        if (bind(probing_sock, (const struct sockaddr *) &client,
                 sizeof(client)) < 0) {
           fprintf(stderr, "server: Unable to bind local address.\n");
           quit();
        }

#ifndef LINUX
        if (fcntl(probing_sock, F_SETFL, O_NDELAY) < 0) {
#else
        if (fcntl(probing_sock, F_SETFL, FNDELAY) < 0) {
#endif
           printf("fcntl fail \n");
           quit();
        }

	return 1;
}

/* any parameter can be NULL */
int get_host_info(char *string, char *name, uint32_t *ip, char *ip_str)
{
	struct hostent *hp;
	struct in_addr in_a;
	uint32_t tmp_ip;

#ifdef SUN
	if (inet_addr(string) == INADDR_BROADCAST) {
#else
	/* string is a hostname */
	if (inet_addr(string) == INADDR_NONE) { 
#endif
	    /* not interested in IP */
	    if (!ip && !ip_str) {
		if (name) 
		    strcpy(name, string);
		return 0;
	    }

	    /* find hostname */
	    if ((hp = gethostbyname(string)) == NULL) {
	    	printf("ERROR: couldn't obtain address for %s\n", string);
	    	return -1;
	    } else {
	    	if (name) 
		    strncpy(name, hp->h_name, MAXHOSTNAMELEN-1);
		if (ip)
	    	    memcpy((void *)ip, (void *)hp->h_addr, hp->h_length);
		if (ip_str) {
		    in_a.s_addr = *ip;
		    strcpy(ip_str, inet_ntoa(in_a)); 
		}
	    }

	} else { /* string is an IP */

	    /* not interested in name */
	    if (ip_str) strcpy(ip_str, string);

	    tmp_ip = inet_addr(string);
	    if (ip) *ip = tmp_ip;

	    if (name) {
		if ((hp = gethostbyaddr((char *)&tmp_ip, sizeof(tmp_ip), AF_INET)) == NULL)
		    strncpy(name, string, MAXHOSTNAMELEN - 1);
		else
		    strncpy(name, hp->h_name, MAXHOSTNAMELEN - 1);
	    }
	}
	return 0;
}

/* the begining of the msg_buf should have the form $**$, return the string */
// TODO: simplify using index()
void get_item(int control_sock, char *str)
{
	int i, j, cur_len;

	/* check if two $ already exit */
	if (msg_len > 0) {
	    if (msg_buf[0] != '$') {
		printf("get unknown msg, it will mess up everything, exit\n");
		quit();
	    }
	    i=1;
	    while ((i<msg_len) && (msg_buf[i] != '$')) {
		i ++;
	    }
	    if (i<msg_len) {
		strncpy(str, msg_buf+1, i-1);
		str[i-1] = 0;
		goto out;
	    }
	}

	/* read until we get two $ */
        while (1) {
	    if (debug) {
	        printf("msg_len = %d  ", msg_len);
	    }
            cur_len = read(control_sock, msg_buf+msg_len, sizeof(msg_buf) - msg_len);
	    /* guard against control channel breaks */
	    if (cur_len <= 0) {
		printf("read failed in get_item()\n");
		quit();
	    }
					
	    msg_len += cur_len;
	    msg_buf[msg_len] = 0;

	    i = 1; 
	    while ((i<msg_len) && (msg_buf[i] != '$')) 
		i ++;
	    if (i<msg_len) {
		strncpy(str, msg_buf+1, i-1);
		str[i-1] = 0;
		break;
	    }
        }

out:
	/* get out the string and move the buf ahead, with i points
	 * to the 2nd $ */
	i++;
	for (j=i; j<msg_len; j++) 
	     msg_buf[j-i] = msg_buf[j];
	msg_len -= i;
}


void init_connection() 
{
	struct sockaddr_in server, src_addr;
	int src_size, result, msg_len;
	char cur_str[16], msg_buf[64];

	/* Setup signal handler for cleaning up */
	(void)setsignal(SIGTERM, cleanup);
	(void)setsignal(SIGINT, cleanup);
	if ((oldhandler = setsignal(SIGHUP, cleanup)) != SIG_DFL) 
	    (void)setsignal(SIGHUP, oldhandler);

  	/* Get hostname and IP address of target host */
  	if (get_host_info(dst, dst_hostname, &dst_ip, dst_ip_str) < 0) {
    	    quit();
  	}

        server.sin_addr.s_addr = dst_ip;
        server.sin_family = AF_INET;

	/* search for the right dst port to connect with */
        control_sock = socket(AF_INET, SOCK_STREAM, 0);
       	++dst_port;
        server.sin_port = htons(dst_port);
        result = connect(control_sock, (struct sockaddr *)&server, sizeof(server));
   	while ((result < 0) && (dst_port < END_PORT)) {
	    close(control_sock);
            control_sock = socket(AF_INET, SOCK_STREAM, 0);
       	    ++dst_port;
            server.sin_port = htons(dst_port);
            result = connect(control_sock, (struct sockaddr *)&server, sizeof(server));
	    if (verbose)
	        printf("dst_port = %d\n", dst_port);
   	};
   	if (result < 0) {
       	    perror("no dst port found, connection fails\n");
	    quit();
	}

	/* Get hostname and IP address of source host */

	src_size = sizeof(struct sockaddr);
	getsockname(control_sock, (struct sockaddr *)&src_addr, &src_size);
	strcpy(src, inet_ntoa(src_addr.sin_addr));

	if (get_host_info(src, src_hostname, &src_ip, src_ip_str) < 0) {
	    printf("get source address info fails, exit\n");
	    quit();
	}

	if (verbose) {
	    printf("src addr: %s\n", src_ip_str);
	    printf("dst addr: %s\n", dst_ip_str);
	}

        /* send START message */
	sprintf(msg_buf, "$START$%d$", probe_num);
	msg_len = strlen(msg_buf);
        write(control_sock, msg_buf, msg_len);

        /* wait for READY ack */
	get_item(control_sock, cur_str);
	if (verbose)
	    printf("we get str: %s\n", cur_str);
        if (strncmp(cur_str, "READY", 5) != 0) {
	    printf("get unknow str when waiting for READY from dst, exit\n");
	    quit();
        }

	get_item(control_sock, cur_str);
	if (verbose)
	    printf("probing_port = %s\n", cur_str);
	probing_port = atoi(cur_str);

	if (init_sockets(dst_ip) == 0) {
	    perror("init sockets failed");
	    quit();
	}
}


/* send out probe_num packets, each packet is packet_size bytes,   */
/* and the inital gap is set using delay_num                       */
void send_packets(int probe_num, int packet_size, int delay_num, double *sent_times)
{
	int i, k;
	double tmp = 133333.0003333;
	char send_buf[4096];

	/* send out probing packets */
	for (i=0; i<probe_num-1; i++) {
	    /* TODO: the middle send_times are not useful any more, since
	     * we don't use sanity-check */
	    sent_times[i] = get_time();
	    send_buf[0] = i;
	    sendto(probing_sock, send_buf, packet_size, 0, (struct sockaddr *)&(probing_server), sizeof(probing_server));

	    /* gap generation */
	    for (k=0; k<delay_num; k++) {
		tmp = tmp * 7;
		tmp = tmp / 13;
	    }
	}

	/* the last packets */
	send_buf[0] = i;
	sendto(probing_sock, send_buf, packet_size, 0, (struct sockaddr *)&(probing_server), sizeof(probing_server));

	sendto(probing_sock, send_buf, 40, 0, (struct sockaddr *)&(probing_server2), sizeof(probing_server2));
	sent_times[probe_num-1] = get_time();
}

/* get dst gap_sum and gap_count from the records */
double get_dst_sum(struct pkt_rcd_t *rcv_record, int count, int *gap_count)
{
        double gap_sum, time1, time2;
        int i;

        gap_sum = 0;
        *gap_count = 0;
        for (i=0; i<count-1; i++) {
            /* only consider those gaps composed by two in-order packets */
	    if (rcv_record[i+1].seq - rcv_record[i].seq == 1) {
		time1 = get_rcd_time(rcv_record[i+1]);
		time2 = get_rcd_time(rcv_record[i]);
                gap_sum += (time1 - time2);
                (*gap_count) ++;
            }
        }

        return gap_sum;
}

/* get dst gap trace data through control channel 	*/
/* return 0 means fails in reading dst's data           */
int get_dst_gaps(struct pkt_rcd_t * rcv_record)
{
	int i, cur_len, data_size, total_count, ptr;
	char num_str[32];

	get_item(control_sock, num_str);
	data_size = atoi(num_str); // this depends on stdlib.h
	total_count = data_size / sizeof(struct pkt_rcd_t);
	if (verbose)
	    printf("from dst: data_size = %d total_count = %d \n", data_size, total_count);

	/* get the data package */
	while (msg_len < data_size) {
            cur_len = read(control_sock, msg_buf+msg_len, sizeof(msg_buf) - msg_len);
	    if (cur_len <= 0) {
		printf("read failed in get_dst_gaps()\n");
		quit();
	    }
	    msg_len += cur_len;
	}

	/* split out the record */
	ptr = 0;
	for (i=0; i<total_count; i++) {
	    memcpy((void *)&rcv_record[i], (const void *)(msg_buf + ptr), sizeof(struct pkt_rcd_t));

	    rcv_record[i].sec = ntohl(rcv_record[i].sec);
	    rcv_record[i].u_sec = ntohl(rcv_record[i].u_sec);
	    rcv_record[i].seq = ntohl(rcv_record[i].seq);

	    if (debug)
	    	printf("%d %d %d \n", rcv_record[i].sec, rcv_record[i].u_sec, rcv_record[i].seq);
	    ptr += sizeof(struct pkt_rcd_t);
	}
	msg_len -= data_size;

	if (trace_fp != NULL) {
	    memcpy(cur_trace->rcv_record, rcv_record, data_size);
	    cur_trace->record_count = total_count;
	}

	return total_count;
}

/* here we use some a similar technique with nettimer, but the  */
/* implementation is much simpler 			        */
double get_bottleneck_bw(struct pkt_rcd_t *rcv_record, int count)
{
	double gaps[MaxProbeNum];
	int gap_count;

	struct bin_item {
	    int value;
	    int count;
	} bin[MaxProbeNum];
	int bin_count;

	double lower_time, upper_time;
	int max_index, max_count, max_value, cur_value, i, j;

	double gap_sum;
	int sum_count;

	// return 100000000;

	/* get valid dst gaps */
	gap_count = 0;
	for (i=0; i<count-1; i++) {
	    if (rcv_record[i+1].seq - rcv_record[i].seq == 1) {
	        gaps[gap_count] = get_rcd_time(rcv_record[i+1]) 
			- get_rcd_time(rcv_record[i]);
		gap_count ++;
	    }
	}

	/* use 25us bins to go through all the gaps */
	bin_count = 0;
	for (i=0; i<gap_count; i++) {
	    cur_value = (int)(gaps[i]/BinWidth);
	    j = 0;
	    while ((j<bin_count) && (cur_value != bin[j].value))  {
		j++;
	    }
	    if (j == bin_count) {
		bin[bin_count].value = cur_value;
		bin[bin_count].count = 1;
		bin_count ++;
	    }
	    else {
		bin[j].count ++;
	    }
	}

	/* find out the biggest bin */
	max_index = 0; max_count = bin[0].count;
	for (i=1; i<bin_count; i++) {
	    if (bin[i].count > max_count) {
		max_count = bin[i].count;
		max_index = i;
	    }
	}
	max_value = bin[max_index].value;
	lower_time = max_value * BinWidth;
	upper_time = (max_value + 1) * BinWidth;

	/* see whether the adjacent two bins also have some items */
	for (i=0; i<bin_count; i++) {
	    if (bin[i].value == max_value + 1) 
		upper_time += BinWidth;
	    if (bin[i].value == max_value - 1) 
		lower_time -= BinWidth;
	}

	if (debug) {
	    printf("get_bottleneck_bw: %f %f \n", lower_time, upper_time);
	}

	/* average them */
	gap_sum = 0;
	sum_count = 0;
	for (i=0; i<gap_count; i++) {
	    if ((gaps[i] >= lower_time) && (gaps[i] <= upper_time)) {
	      	gap_sum += gaps[i];
	    	sum_count ++;
	    }
	}

	return (packet_size * 8 / (gap_sum / sum_count));
}


/* calculate the competing traffic rate using IGO method */
double get_competing_bw(struct pkt_rcd_t *rcv_record, double avg_src_gap, int count, double b_bw)
{
	double b_gap, cur_gap, m_gap;
	double gap_sum, inc_gap_sum;
        int i;

	b_gap = packet_size * 8 / b_bw;
	m_gap = MAX(b_gap, avg_src_gap);

	gap_sum = inc_gap_sum = 0;
        for (i=0; i<count-1; i++) {
            /* only consider those gaps composed by two in-order packets */
            if (rcv_record[i+1].seq - rcv_record[i].seq == 1) {
		cur_gap = get_rcd_time(rcv_record[i+1]) 
			- get_rcd_time(rcv_record[i]);
		gap_sum += cur_gap;

		if (cur_gap > m_gap + 0.000005)
                    inc_gap_sum += (cur_gap - b_gap);
            }
        }

	if (debug)
	    printf("b_gap = %f %f %f \n", b_gap, inc_gap_sum, gap_sum);

	if (gap_sum == 0) {
	    printf("\nNo valid trace in the last phase probing\n");
	    printf("You may want to try again. Exit\n");
	    quit();
	}

        return (inc_gap_sum * b_bw / gap_sum);
}

/* return summary of the "valid" src gaps */
double get_src_sum(double * times)
{
	int i;
	double sum;

	sum = 0;
	for (i=0; i<probe_num-1; i++)
	    sum += (times[i+1] - times[i]);
	return sum;
}

void get_bandwidth()
{
	if (dst_gap_count == 0) 
	    competing_bw = PTR_bw = a_bw = 0;
	else {
	    if (b_bw < 1) 
	        b_bw = get_bottleneck_bw((struct pkt_rcd_t *)&dst_gap, total_count);
	    competing_bw = get_competing_bw((struct pkt_rcd_t *)&dst_gap, avg_src_gap, total_count, b_bw);
	}
}

/* one complete probing procedure, which includes:			*/
/* 1. send out packets with (probe_num, packet_size, delay_num)  	*/
/* 2. retrieve the dst gaps values from dst machine			*/ 
/* 3. filter out those bad trace data and compute the src gap & dst 	*/ 
/*    gap summary and average						*/ 
void one_phase_probing()
{
	if (verbose)
	    printf("\nprobe_num = %d packet_size = %d delay_num = %d \n", 
		    probe_num, packet_size, delay_num);

	/* probing */
  	send_packets(probe_num, packet_size, delay_num, send_times);
	if (trace_fp != NULL) {
	    /* create a new trace item */
	    cur_trace = (struct trace_item *)malloc(sizeof(struct trace_item));
	    cur_trace->next = NULL;
	    if (trace_list == NULL) {
		trace_list = cur_trace;
		trace_tail = cur_trace;
	    }
	    else {
	        trace_tail->next = cur_trace;
	        trace_tail = cur_trace;
	    }
	}
	/* get dst gap logs */
	total_count = get_dst_gaps((struct pkt_rcd_t *)&dst_gap);

	/* compute avg_src_gap */
	src_gap_count = probe_num - 1;
	if (debug) 
	    printf("src_gap_count = %d \n", src_gap_count);

	src_gap_sum = get_src_sum(send_times);
	if (src_gap_count != 0) 
	    avg_src_gap = src_gap_sum / src_gap_count;
	else 
	    avg_src_gap = 0;

	/* compute avg_dst_gap */
	dst_gap_sum = get_dst_sum((struct pkt_rcd_t *)&dst_gap, 
			    total_count, &dst_gap_count);
	if (debug) 
	    printf("%d dst_gap_count = %d \n", total_count, dst_gap_count);

	if (dst_gap_count != 0)
	    avg_dst_gap = dst_gap_sum / dst_gap_count;
	else
	    avg_dst_gap = 0;

	/* record total src gaps */
	tlt_src_gap += avg_src_gap;
	tlt_dst_gap += avg_dst_gap;

	if (verbose)
	    printf("gaps (us): %5.0f %5.0f | %5.0f %5.0f\n", 
	    	tlt_src_gap * 1000000, tlt_dst_gap * 1000000,
	    	avg_src_gap * 1000000, avg_dst_gap * 1000000);

	if (trace_fp != NULL) {
	    double tmp1, tmp2, tmp3;

	    cur_trace->probe_num = probe_num;
	    cur_trace->packet_size = packet_size;
	    cur_trace->delay_num = delay_num;

	    memcpy(cur_trace->send_times, send_times, 
			    sizeof(double) * probe_num);

	    cur_trace->avg_src_gap = avg_src_gap;
	    cur_trace->avg_dst_gap = avg_dst_gap;

	    /* we can't use get_bandwidth() here since we want to record
	     * the current data, which may be wrong value*/
	    if (dst_gap_count == 0)
		cur_trace->b_bw = 0;
	    else 
	        cur_trace->b_bw = get_bottleneck_bw((struct pkt_rcd_t *)&dst_gap, total_count);

	    tmp1 = PTR_bw;
	    tmp2 = competing_bw;
	    tmp3 = a_bw;
	    get_bandwidth();
	    cur_trace->ptr = PTR_bw;
	    cur_trace->c_bw = competing_bw;
	    cur_trace->a_bw = a_bw;
	    PTR_bw = tmp1;
	    competing_bw = tmp2;
	    a_bw = tmp3;
	}

	probing_phase_count ++;
}

void n_phase_probing(int n)
{
	int i, j, k;

	tlt_src_gap = 0;
	tlt_dst_gap = 0;

	for (i=0; i<n; i++) {
	    one_phase_probing();
	    get_bandwidth();

	    /* insert, so as to be able to pick the median easily */
	    j = 0;
	    while (j < i && c_bw[j] < competing_bw) 
		j ++;
	    for (k=i-1; k>=j; k--)
		c_bw[k+1] = c_bw[k];
	    c_bw[j] = competing_bw;
	}
	PTR_bw = packet_size * 8 * phase_num / tlt_dst_gap;
	a_bw = b_bw - c_bw[(int)(n/2)];

	if (verbose)
	    printf("------------------------------------------------------\n");
}

int gap_comp(double dst_gap, double src_gap)
{
	double delta = 0.05;

	if (dst_gap < src_gap / (1+delta)) {
	    if (verbose) 
	       	printf("smaller dst_gap, considered to be equal \n");
	    return 0;
	}

	if (dst_gap > src_gap + 0.000005 * phase_num) 
	    return 1;
	else 
	    return 0;
}

void fast_probing()
{
        int double_check = 0, first = 1, tmp_num;
        double interval, pre_gap = 0;
	double saved_ptr, saved_abw;

        probing_start_time = get_time();

        delay_num = 0;
        n_phase_probing(phase_num);
	b_bw = get_bottleneck_bw((struct pkt_rcd_t *)&dst_gap, total_count);

        delay_num = get_delay_num(avg_dst_gap);
        interval = (double) delay_num / 4;
        delay_num /= 2;

	while (1) {
	    if (gap_comp(tlt_dst_gap, tlt_src_gap) == 0) {
		if (double_check) {
		    tlt_dst_gap = pre_gap;
		    break;
		} else {
		    pre_gap = tlt_dst_gap;
		    double_check = 1;
		}
	    } else {
		if (double_check && verbose)
		    printf("DISCARD: gap = %d\n", (int)(tlt_dst_gap * 1000000));
		double_check = 0;
	    }

	    if (!first && tlt_dst_gap >= 1.5 * tlt_src_gap) {
		tmp_num = get_delay_num((tlt_src_gap + tlt_dst_gap) / (2 * total_count));
		if (tmp_num > delay_num) 
		    delay_num = tmp_num;
	    }
            delay_num = (int)(delay_num + interval);

	    /* TODO: need a better way to deal with them */
	    saved_ptr = PTR_bw;
	    saved_abw = a_bw;
            n_phase_probing(phase_num);
	    first = 0;	
	}
	PTR_bw = saved_ptr;
	a_bw = saved_abw;

	probing_end_time = get_time();
	dump_bandwidth();
}

int main(int argc, char *argv[]) 
{
	int opt;

        while ((opt = getopt(argc, argv, "k:l:n:s:p:f:dhv")) != EOF) {
            switch ((char)opt) {
            case 'k':
		phase_num = atoi(optarg);
		if (phase_num > MaxRepeat) {
		    phase_num = MaxRepeat;
		    printf("phase_num is too large, reset as %d\n", MaxRepeat);
		}
                break;
            case 'l':
		delay_num = atoi(optarg);
                break;
            case 'n':
		probe_num = atoi(optarg);
		if (probe_num > MaxProbeNum) {
		    probe_num = MaxProbeNum;
		    printf("probe_num is too large, reset as %d\n", MaxProbeNum);
		}
                break;
            case 's':
		packet_size = atoi(optarg);
                break;
            case 'p':
		dst_port = atoi(optarg);
                break;
            case 'f':
		if ((trace_fp = fopen(optarg, "w")) == NULL) {
		    printf("tracefile open fails, exits\n");
		    exit(1);
		}
                break;

            case 'd':
		debug = 1;
                break;
            case 'v':
		verbose = 1;
                break;
	    case 'h':
	    default:
		Usage();
            }
	}
	switch (argc - optind) {
        case 1:
	    strcpy(dst, argv[optind]);
	    break;
	default:
	    Usage();
	}

	init_connection();
	/* allow dst 2 seconds to start up the packet filter */
	sleep(2);

        /* probing */
	if (delay_num > 0) {
	    one_phase_probing();
	    get_bandwidth();
	    dump_bandwidth();
	} else 
            fast_probing();

	/* finishing */
  	cleanup(1);

	return (0);
}
