/*
	Copyright (c) 2013, University of Lugano
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
    	* Redistributions of source code must retain the above copyright
		  notice, this list of conditions and the following disclaimer.
		* Redistributions in binary form must reproduce the above copyright
		  notice, this list of conditions and the following disclaimer in the
		  documentation and/or other materials provided with the distribution.
		* Neither the name of the copyright holders nor the
		  names of its contributors may be used to endorse or promote products
		  derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.	
*/


#include <paxos.h>
#include <evpaxos.h>
#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <sys/time.h>
#include <stdio.h>

#define MAX_VALUE_SIZE 64*1024


struct client_value
{
  struct timeval t;
  size_t size;
  char value[MAX_VALUE_SIZE];
};

struct stats
{
  int avg_latency;
  int delivered;
};

struct client
{
	int value_size;
	int outstanding;
	struct stats stats;
	struct event_base* base;
	struct bufferevent* bev;
	struct event* stats_ev;
	struct timeval stats_interval;
	struct timeval start_time;
	struct event* sig;
	struct evlearner* learner;
  FILE* output;
};

static void
handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	event_base_loopexit(base, NULL);
}

static void
random_string(char *s, const int len)
{
	int i;
	static const char alphanum[] =
		"0123456789abcdefghijklmnopqrstuvwxyz";
	for (i = 0; i < len-1; ++i)
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	s[len-1] = 0;
}

static void
client_submit_value(struct client* c)
{
  struct client_value v;
  
  gettimeofday(&v.t, NULL);
  v.size = c->value_size;
  random_string(v.value, v.size);

  int vsize = sizeof(size_t) + sizeof(struct timeval) + v.size;
  paxos_submit(c->bev, (char*) &v, vsize);
}


// Returns t2 - t1 in microseconds.
long timeval_diff(struct timeval* t1, struct timeval* t2) {
  long us;
  us = (t2->tv_sec - t1->tv_sec) * 1e6;
  if (us < 0) return 0;
  us += (t2->tv_usec - t1->tv_usec);
  return us;
}

static void
on_deliver(unsigned iid, char* value, size_t size, void* arg)
{
	struct client* c = arg;
	struct client_value* v = (struct client_value*)value;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long latency = timeval_diff(&v->t, &tv);
        long dt = tv.tv_sec - c->start_time.tv_sec;
        if (dt > 300) exit(0);
	fprintf(c->output, "%d,%ld,%ld,%ld\n", c->outstanding, v->size, dt, latency);
	c->stats.delivered++;
	c->stats.avg_latency = c->stats.avg_latency + ((latency - c->stats.avg_latency) / c->stats.delivered);
	
	client_submit_value(c);
}

static void
on_stats(evutil_socket_t fd, short event, void *arg)
{
	struct client* c = arg;
	double mbps = (double)((c->stats.delivered*c->value_size*8)+(sizeof(struct timeval)+sizeof(size_t)*8)) / (1024*1024);
        //#printf("%d value/sec, %.2f Mbps, %d avg latency us\n", c->stats.delivered, mbps, c->stats.avg_latency);
	c->stats.delivered = 0;
	c->stats.avg_latency = 0;
	event_add(c->stats_ev, &c->stats_interval);
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
	int i;
	struct client* c = arg;
	if (events & BEV_EVENT_CONNECTED) {
		printf("Connected to proposer\n");
		for (i = 0; i < c->outstanding; ++i)
			client_submit_value(c);
	} else {
		printf("%s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
	}
}

static struct bufferevent* 
connect_to_proposer(struct client* c, const char* config, int proposer_id)
{
	struct bufferevent* bev;
	struct evpaxos_config* conf = evpaxos_config_read(config);
	struct sockaddr_in addr = evpaxos_proposer_address(conf, proposer_id);
	bev = bufferevent_socket_new(c->base, -1, BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(bev, NULL, NULL, on_connect, c);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
	bufferevent_socket_connect(bev, (struct sockaddr*)&addr, sizeof(addr));
	return bev;
}

static struct client*
make_client(const char* config, int proposer_id, int outstanding, int value_size, int client_id)
{
	struct client* c;
	c = malloc(sizeof(struct client));
	c->base = event_base_new();
	
	memset(&c->stats, 0, sizeof(struct stats));
	c->bev = connect_to_proposer(c, config, proposer_id);
	if (c->bev == NULL)
		exit(1);
	
	c->value_size = value_size;
	c->outstanding = outstanding;
	
	gettimeofday(&c->start_time, NULL); 
        c->stats_interval = (struct timeval){1, 0};
        c->stats_ev = evtimer_new(c->base, on_stats, c);
	event_add(c->stats_ev, &c->stats_interval);
	
	paxos_config.learner_catch_up = 0;
	c->learner = evlearner_init(config, on_deliver, c, c->base);
	
	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);
        char outname[20];
        sprintf (outname, "client%d-%d-%dB.csv", client_id, outstanding, value_size);
	c->output = fopen(outname, "w");
	
	return c;
}

static void
client_free(struct client* c)
{
	bufferevent_free(c->bev);
	//	event_free(c->stats_ev);
	event_free(c->sig);
	event_base_free(c->base);
	free(c);
}

static void
usage(const char* name)
{
	char* opts = "config [proposer id] [# outstanding values] [value size] [client id]";
	printf("Usage: %s %s\n", name, opts);
	exit(1);
}

int
main(int argc, char const *argv[])
{
	int proposer_id = 0;
	int outstanding = 1;
	int value_size = 64;
        int client_id = 1;
	
	if (argc < 2 || argc > 6)
		usage(argv[0]);
	if (argc >= 3)
		proposer_id = atoi(argv[2]);
	if (argc >= 4)
		outstanding = atoi(argv[3]);
	if (argc >= 5)
		value_size = atoi(argv[4]);
	if (argc == 6)
		client_id = atoi(argv[5]);
	srand(time(NULL));
	
	struct client* client;
	client = make_client(argv[1], proposer_id, outstanding, value_size, client_id);	
	event_base_dispatch(client->base);
	client_free(client);
	
	return 0;
}
