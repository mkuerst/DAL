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
#include <stdlib.h>

int kReadRatio;
int kThreadCount;
int kNodeCount = 1;
uint64_t kKeySpace = 64 * define::MB;
double kWarmRatio = 0.8;
double zipfan = 0;
int nthreads, client, num_clients,
num_runs, num_mem_runs,
mode, duration, nlocks;

DSM *dsm;

int main(int argc, char *argv[]) {
    char *res_file_cum, *res_file_single;
    char *mn_ip, peer_ips[MAX_CLIENTS][MAX_IP_LENGTH];
    system("sudo bash /nfs/DAL/restartMemc.sh");

    parse_cli_args(&nthreads, &num_clients, &nlocks, &client, &duration,
    &mode, &num_runs, &num_mem_runs, &res_file_cum, &res_file_single,
    &mn_ip, peer_ips, argc, argv
    );

    DSMConfig config;
    config.machineNR = kNodeCount;
    dsm = DSM::getInstance(config);
    fprintf(stderr, "IT WORKED\n");
}