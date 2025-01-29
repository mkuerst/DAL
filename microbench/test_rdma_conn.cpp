#include "Timer.h"
#include "Tree.h"
#include "zipf.h"

#include <city.h>
#include <stdlib.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <utils.h>

int main(int argc, char *argv[]) {
    char *res_file_cum, *res_file_single;
    char *mn_ip, peer_ips[MAX_CLIENTS][MAX_IP_LENGTH];
    int nthreads, client, num_clients,
    num_runs, num_mem_runs, use_nodes,
    scope, mode, duration, nlocks;

    parse_cli_args(&nthreads, &num_clients, &nlocks, &client, &duration,
    &mode, &num_runs, &num_mem_runs, &res_file_cum, &res_file_single,
    &mn_ip, peer_ips, argc, argv
    );

    DSMConfig config;
    config.machineNR = kNodeCount;
    dsm = DSM::getInstance(config);


}