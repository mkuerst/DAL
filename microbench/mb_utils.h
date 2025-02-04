
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256  // Ensure HOST_NAME_MAX is defined
#endif

#ifdef DEBUG
#undef DEBUG
#define DEBUG(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "%s : %s : %d : " msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)

#define debug(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "%s : %s : %d : " msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)
#else
#define DEBUG(...)
#define debug(...)
#endif

// #define DEBUG_PTHREAD(...) fprintf(stderr, ## __VA_ARGS__)
#define DEBUG_PTHREAD(...)

#define _error(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m" msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
    exit(EXIT_FAILURE);\
} while(0)

#define __error(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m" msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
} while(0)

int getNodeNumber();

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR,
    int *lockNR, int *node_id, int* duration,
    int* mode, int* num_runs, int *num_mem_runs,
    char **res_file_cum, char **res_file_single,
    int argc, char **argv
);