
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include "../src/ndpi_flow_info.h"

/*
 * procfs buffer 256Kb
 */

#define FLOW_READ_COUNT 256*1024

struct dump_data {
	struct dump_data *next;
	size_t		 len;
	uint8_t		 data[];
};

#define MAX_PROTO_NAMES 320
char *proto_name[MAX_PROTO_NAMES];

struct dump_data *head = NULL, *tail = NULL;

void ndpi_get_proto_names(void) {
char lbuf[256],mark[64],name[64];
unsigned int id;
FILE *fd = fopen("/proc/net/xt_ndpi/proto","r");

if(!fd) return;

do {
	if(!fgets(lbuf,sizeof(lbuf)-1,fd)) break;
	if(lbuf[0] != '#') break;
	while(fgets(lbuf,sizeof(lbuf)-1,fd)) {
		if(sscanf(lbuf,"%x %s %s",&id,mark,name) == 3) {
			if(id >= MAX_PROTO_NAMES) break;
			proto_name[id] = strdup(name);
//			fprintf(stderr,"%d %s\n",id,name);
		}
	}
} while(0);
fclose(fd);
}

static const char *ndpi_proto_name(unsigned int id) {
	return (id < MAX_PROTO_NAMES && proto_name[id]) ? proto_name[id] : "Bad";
}

static int decode_flow(int fd,struct dump_data *dump) {
struct flow_data_common *c;
struct flow_data_v4 *v4;
struct flow_data_v6 *v6;
char *data,buff[512],
     a1[64],a2[64],a3[32],a4[32],
     p1[8],p2[8],p3[8],p4[8],
     pn[64];
int offs,l,rl;

	data = (char*)&dump->data[0];
	offs = 0;

	while(offs < dump->len-4) {
		c = (struct flow_data_common *)&data[offs];
		switch(c->rec_type) {
		case 0:
			return -1;
		case 1:
			if(offs+8 > dump->len) return -1;
			l = snprintf(buff,sizeof(buff)-1,"START %u\n",c->time_start);
			write(fd,buff,l);
			offs += 8;
			break;
		case 3:
			if(offs+sizeof(struct flow_data_common) > dump->len) return -1;
			l = snprintf(buff,sizeof(buff)-1,"LOST TRAFFIC %u %u %" PRIu64 " %" PRIu64 "\n",
				c->p[0],c->p[1],c->b[0],c->b[1]);
			write(fd,buff,l);
			offs += sizeof(struct flow_data_common);
			break;
		case 2:
			rl = sizeof(struct flow_data_common) + 
				( c->family ? sizeof(struct flow_data_v6) :
				  	      sizeof(struct flow_data_v4));
			if(offs+rl+c->cert_len+c->host_len > dump->len) return -1;
			if(c->family) {
				v6 = (struct flow_data_v6 *)&data[offs+sizeof(struct flow_data_common)];
				inet_ntop(AF_INET6,&v6->ip_s,a1,sizeof(a1)-1);
				inet_ntop(AF_INET6,&v6->ip_d,a2,sizeof(a2)-1);
				a3[0] = '\0';
				a4[0] = '\0';
				snprintf(p1,sizeof(p1)-1,"%d",htons(v6->sport));
				snprintf(p2,sizeof(p2)-1,"%d",htons(v6->dport));
				p3[0] = '\0';
				p4[0] = '\0';
			} else {
				v4 = (struct flow_data_v4 *)&data[offs+sizeof(struct flow_data_common)];
				if(c->nat_flags & 0x5) { // snat || userid
					inet_ntop(AF_INET,&v4->ip_snat,a1,sizeof(a1)-1);
					snprintf(p1,sizeof(p1)-1,"%d",htons(v4->sport_nat));
					inet_ntop(AF_INET,&v4->ip_s,a3,sizeof(a3)-1);
					snprintf(p3,sizeof(p3)-1,"%d",htons(v4->sport));
					
				} else {
					inet_ntop(AF_INET,&v4->ip_s,a1,sizeof(a1)-1);
					snprintf(p1,sizeof(p1)-1,"%d",htons(v4->sport));
					a3[0] = '\0';
					p3[0] = '\0';
				}
				if(c->nat_flags & 2) { // dnat
					inet_ntop(AF_INET,&v4->ip_dnat,a2,sizeof(a2)-1);
					snprintf(p2,sizeof(p2)-1,"%d",htons(v4->dport_nat));
					inet_ntop(AF_INET,&v4->ip_d,a4,sizeof(a4)-1);
					snprintf(p4,sizeof(p4)-1,"%d",htons(v4->dport));
				} else {
					inet_ntop(AF_INET,&v4->ip_d,a2,sizeof(a2)-1);
					snprintf(p2,sizeof(p2)-1,"%d",htons(v4->dport));
					a4[0] = '\0';
					p4[0] = '\0';
				}
			}

			l = snprintf(buff,sizeof(buff)-1,"%u %u %c %d %s %s %s %s %u %u %" PRIu64 " %" PRIu64 ,
				c->time_end,c->time_start,
				c->family ? '6':'4', c->proto, a1, p1, a2, p2,
				c->p[0],c->p[1],c->b[0],c->b[1]);

			pn[0] = '\0';
			if(c->proto_app) {
				if(c->proto_master) {
					snprintf(pn,sizeof(pn)-1,"%s,%s",
							ndpi_proto_name(c->proto_master),
							ndpi_proto_name(c->proto_app));
				} else
					strncpy(pn,ndpi_proto_name(c->proto_app),sizeof(pn)-1);
			} else if(c->proto_master)
					strncpy(pn,ndpi_proto_name(c->proto_master),sizeof(pn)-1);

			if(c->ifidx != c->ofidx)
				l += snprintf(&buff[l],sizeof(buff)-1-l," I=%d,%d",c->ifidx,c->ofidx);
			  else
				l += snprintf(&buff[l],sizeof(buff)-1-l," I=%d",c->ifidx);

			if(pn[0])
				l += snprintf(&buff[l],sizeof(buff)-1-l," P=%s",pn);

			if(c->nat_flags) {
				if(c->nat_flags & 5)
					l += snprintf(&buff[l],sizeof(buff)-1-l," %s=%s,%s",
							c->nat_flags & 1 ? "SN":"UI", a3,p3);
				if(c->nat_flags & 2)
					l += snprintf(&buff[l],sizeof(buff)-1-l," DN=%s,%s",a4,p4);
			}

			if(c->cert_len)
				l += snprintf(&buff[l],sizeof(buff)-1-l,
						" C=%.*s",c->cert_len,&data[offs+rl]);

			if(c->host_len)
				l += snprintf(&buff[l],sizeof(buff)-1-l,
						" H=%.*s",c->host_len,&data[offs+rl+c->cert_len]);

			buff[l++] = '\n';
			buff[l] = '\0';
			write(fd,buff,l);
			offs += rl + c->cert_len + c->host_len;
			break;
		}
	}
	return offs != dump->len ? -1:0;
}

void help(void) {
	fprintf(stderr,"%s [-s] [-S output_biary_file] [-i input_binary_file]\n",
			"flow_dump");
	exit(1);
}

int main(int argc,char **argv) {
	int fd,e,n,q=0;
	size_t blk_size;
	long long int r;
	int file_read;
	struct dump_data *c;
	char *src_file= NULL;
	char *bin_file= NULL;
	int text_dump = 0;
	struct stat src_st;
	struct timeval tv1,tv2;
	long int delta;

	while((n=getopt(argc,argv,"qsS:i:")) != -1) {
	  switch(n) {
	      case 'q': q = 1; break;
	      case 's': text_dump = 1; break;
	      case 'S': bin_file  = strdup(optarg); break;
	      case 'i': src_file  = strdup(optarg); break;
	      default: help();
	  }
	}
	if(!text_dump && !bin_file) {
		fprintf(stderr,"-s or -S required!\n");
		exit(1);
	}
	ndpi_get_proto_names();
	if(!src_file) {
		fd = open("/proc/net/xt_ndpi/flows",O_RDWR);
		if(fd < 0) {
			perror("open /proc/net/xt_ndpi/flows");
			exit(1);
		}
		if(write(fd,"read_all_bin\n",13) != 13) {
			perror("Set mode read_all_bin failed");
			close(fd);
			exit(1);
			exit(1);
		}
		blk_size = FLOW_READ_COUNT;
		file_read = 0;
	} else {
		fd = open(src_file ,O_RDONLY);
		if(fd < 0) {
			perror("open lows");
			exit(1);
		}
		if(fstat(fd,&src_st) < 0) {
			perror("stat");
			exit(1);
		}
		if(!src_st.st_size) {
			exit(0);
		}
		blk_size = src_st.st_size;
		file_read = 1;
	}
	c = NULL;
	n = 0;
	e = 0;
	r = 0;
	gettimeofday(&tv1,NULL);
	while(1) {
		if(!c) 
			c = malloc(sizeof(struct dump_data)+blk_size);
		if(!c) {
			perror("malloc");
			break;
		}
		c->next = NULL;
		e = read(fd,&c->data[0],blk_size);
		if(e < 0) {
			if(errno == EINTR) continue;
			perror("read error");
			break;
		}
		if(e == 0) {
			free(c);
			break;
		}
		r += e;
		c->len = e;
		n++;
		if(!head) {
			head = tail = c;
			c = NULL;
		} else {
			tail->next = c;
			c = NULL;
		}
		if(file_read) break;
	}
	close(fd);
	gettimeofday(&tv2,NULL);
	tv2.tv_sec -= tv1.tv_sec;
	delta = tv2.tv_sec*1000000 + tv2.tv_usec - tv1.tv_usec;
	if(!delta) delta=1;

	if(!q)
		fprintf(stderr,"read %llu bytes %ld ms, speed %d MB/s \n",r,delta/1000,(int)(r/delta));

	if(bin_file) {
		struct stat st;
		if(stat(bin_file,&st) == 0 &&
			st.st_dev == src_st.st_dev &&
			st.st_ino == src_st.st_ino) {
			fprintf(stderr,"The input and output files are identical. Do not rewrited.\n");
		} else {
			fd = open(bin_file,O_CREAT|O_WRONLY,0644);
			if(fd < 0) {
				perror("create");
				exit(1);
			}
			for(c = head; c; c = c->next ) {
				e = write(fd,&c->data[0],c->len);
				if(e != c->len) {
					perror("write");
					exit(1);
				}
			}
			close(fd);
		}
	}
	if(text_dump)
		for(c = head; c; c = c->next ) {
			if(decode_flow(1,c) < 0) {
				fprintf(stderr,"Decode error.\n");
				exit(1);
			}
		}

	for(c = head; c; c = head ) {
		head = c->next;
		free(c);
	}

	exit(0);
}
