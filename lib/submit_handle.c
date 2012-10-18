#include <stdlib.h>

#include "libpaxos.h"
#include "libpaxos_priv.h"
#include "paxos_net.h"

paxos_submit_handle * pax_submit_handle_init() {
    //TODO print errors, 
    paxos_submit_handle * psh = malloc(sizeof(paxos_submit_handle));
    if(psh == NULL) {
        return NULL;
    }
    
	address submit_net[] = PAXOS_SUBMIT_NET;
    psh->sendbuf = sendbuf_new(1, submit_net);
    if(psh->sendbuf == NULL) {
        return NULL;
    }
    
    return psh;
}

int pax_submit_nonblock(paxos_submit_handle * h, char * value, size_t val_size) {
    udp_send_buffer* sb = (udp_send_buffer*)h->sendbuf;
    sendbuf_clear(sb, submit, 0);
    sendbuf_add_submit_val(sb, value, val_size);
    sendbuf_flush(sb);
    return 0;
}
