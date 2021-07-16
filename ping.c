/*
 * ICMP PING 
 * version:
 * 0.1 created	2021 June
 *
 * https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

// ping packet structure

#define PING_PKT_S 64
#define IP_START 2
#define IP_SIZE 255

int debug = 0;
char ip_base[12];

char ip_ok[IP_SIZE];

struct ping_pkt
{
  // header
  struct icmphdr hdr;
  // time
  struct timespec t;
  // rest - no used
  char msg[PING_PKT_S - sizeof(struct icmphdr) - sizeof(struct timespec)];
};

// Calculating the Check Sum
unsigned short checksum(void *b, int len)
{    
    unsigned short *buf = b;
    unsigned int sum=0;
    unsigned short result;

    for ( sum = 0; len > 1; len -= 2 )
        sum += *buf++;
    if ( len == 1 ) sum += *(unsigned char*)buf;

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}


static void *ping_recv()
{
    int rc;
    fd_set read_set;
    char buf[2048];
	int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sockfd < 0) {
		printf ("%s: sockfd failed\n", __func__);
		return 0;
	}

	if (debug) printf ("%s: start with sockfd %d\n", __func__, sockfd);

    /*
     * wait for a reply with a timeout
     */
    for (;;) {
        struct timeval timeout = {3, 0}; //wait max 3 seconds for a reply
        memset(&read_set, 0, sizeof read_set);

        FD_SET(sockfd, &read_set);
	
        rc = select(sockfd + 1, &read_set, NULL, NULL, &timeout);
        if (rc == 0) {
            printf ("%s: select got no reply\n", __func__);
            break;
        } else if (rc < 0) {
            printf("%s: Select faile with %s\n", __func__, strerror(errno));
            continue;
        }
        rc = recvfrom(sockfd, buf, sizeof (buf), 0, NULL, 0);
	
        if (rc <= 0) {
            if (debug) printf("%s: recv error: %s\n", __func__, strerror(errno));
            return 0;
        }
	    {
	        struct ip *ip = (struct ip *)buf;
	        struct ping_pkt *pckt = (struct ping_pkt *)&buf[ip->ip_hl * 4];
	        struct timespec *t_start = (struct timespec *)&pckt->t;
	        struct timespec t_end;
	        char *rec_addr;
            int16_t seq = 0;
	      	/*
	        * get received address
	        */
	        rec_addr = inet_ntoa(ip->ip_src);
            if (debug) printf("%s: FROM %s ", __func__, rec_addr);
	
	        if (pckt->hdr.type == ICMP_ECHOREPLY) {
	        /*
	         * calculate time if we received orginal packet
	         */ 
	        clock_gettime(CLOCK_REALTIME, &t_end);
		    t_end.tv_sec -= t_start->tv_sec;
		    if (t_end.tv_nsec < t_start->tv_nsec) {
	            t_end.tv_sec--;
		        t_end.tv_nsec = (1000000000 -  t_start->tv_nsec) + t_end.tv_nsec;
	        } else {
	            t_end.tv_nsec -= t_start->tv_nsec;
	        }
	        if (debug) printf(" time laps: %ld ns ", t_end.tv_sec * 1000000000 + t_end.tv_nsec);
            seq = pckt->hdr.un.echo.sequence;	
	        if (debug) printf(" ICMP Reply rc=%d, id=0x%x, sequence =  0x%x - %s.%d\n", rc, pckt->hdr.un.echo.id, seq, ip_base, seq);

            if (! strncmp(ip_base, rec_addr, strlen(ip_base) - 1)) {
                ip_ok[seq] = 1;	
            }
	/*        s  = spec.tv_sec;
	    ms = round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds
	    if (ms > 999) {
	        s++;
	        ms = 0;
	    }
	*/
	/*
	 * debug pct
	 */
	//  for (i = 0; i < rc; i++) printf("%s: BYTE %d - D %u X %x C %c\n", __func__, i, buf[i], buf[i], buf[i]);  
	
	        } else {
	            if (debug) printf(" Got ICMP packet with type 0x%x ?!?\n", pckt->hdr.type);
	        } // if (pckt->hdr.type)
	    }
    } // for (;;)
  return 0;
}

int ping_send_work(int sockfd)
{
    int i;
    // 123.123.123.123 -  - 15 + NULL
    char ip[16];
    uint16_t   n = 1;
    struct in_addr dst;
    struct ping_pkt pckt;
    struct sockaddr_in addr;

    for (n = IP_START; n < IP_SIZE; n++) {
   
	  /*
	   * if this address has 1 - mean OK
	   */
        if (ip_ok[n]) { continue; }

        sprintf(ip,"%s.%d", ip_base, n);
        
		inet_aton(ip, &dst);
		addr.sin_family = AF_INET;
		addr.sin_port = htons (0);
		addr.sin_addr =dst;
	
	    /*
	     * clean memory
	     */
		memset(&pckt, 0, sizeof (pckt));
	
	    /*
	     * filling packet
	     */
		pckt.hdr.type = ICMP_ECHO;
		pckt.hdr.un.echo.id = 59;
		pckt.hdr.un.echo.sequence = n;
	    /*
	     * payload
	     */
		for ( i = 0; i < sizeof(pckt.msg)-1; i++ )
	        pckt.msg[i] = i + 50;
		          
	    pckt.msg[i] = 0;
	    /*
	     * put current time
	     */
	    clock_gettime(CLOCK_REALTIME, &pckt.t);
	
		pckt.hdr.checksum = checksum(&pckt, sizeof(pckt));
		
		if (sendto(sockfd, &pckt, sizeof(pckt), 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
	        printf ("%s: sendto error %d\n", __func__, sockfd);
		    return 1;
        }
	
	    if (debug) printf ("%s: sendto %s OK seq: %x id %x\n", __func__, ip, n, i);
	 //   sleep (1);
    } // for 
    return 0;
}



int ping_send()
{
	int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    int n;

	if (sockfd < 0) {
		printf ("%s: sockfd failed\n", __func__);
		return 1;
	}
	if (debug) printf ("%s: sockfd OK\n", __func__);

    if (debug) printf ("%s: start with sockfd %d\n", __func__, sockfd);
    for (n = 0; n < IP_SIZE; n++) {
        ip_ok[n] = 0;
    }
    ping_send_work(sockfd);
    printf("%s: second round\n", __func__);
    sleep (5);
    ping_send_work(sockfd);
    return 0;
}

int main(int argc, char *argv[])
{
    pthread_t precv;
    int n;

    if (argc < 2) {
        printf ("%s: %s A.B.C\n", __func__, argv[0]);
        return 1;
    }
    /*
     * more test
     */
    sprintf(ip_base,"%s", argv[1]);

    if (pthread_create( &precv, NULL, &ping_recv, (void*)NULL)) {
        printf ("%s: pthread failed with %s", __func__,strerror(errno));
        return 1;
    }
    ping_send();
    sleep (5);
    for (n = IP_START; n < IP_SIZE; n++) {
        if (ip_ok[n]) {
            printf ("%s.%d\n", ip_base, n);
        }
    }
 
	return 0;
}
	
