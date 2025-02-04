#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <regex>

int getNodeNumber() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname failed");
        return -1;  // Return -1 on error
    }

    std::string hostStr(hostname);
    std::regex pattern(R"(node(\d+))");  // Match "node" followed by digits
    std::smatch match;

    if (std::regex_search(hostStr, match, pattern)) {
        return std::stoi(match[1]);  // Extract and convert number
    }

    return -1;  // Return -1 if no match
}

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR,
    int *lockNR, int *node_id, int* duration,
    int* mode, int* num_runs, int *num_mem_runs,
    char **res_file_cum, char **res_file_single,
    int argc, char **argv
) {
    int option;
    int i = 0;
    char *addresses;
    char *token;
	while ((option = getopt(argc, argv,
    "p:o:c:t:l:i:d:s:m:r:e:f:g:")) != -1) 
    {
		switch (option) {
			case 'f':
                *res_file_cum = optarg; 
				break;
			case 'g':
                *res_file_single = optarg; 
				break;
			case 'n':
				*nodeNR = atoi(optarg);
				break;
			case 'r':
				*num_runs = atoi(optarg);
				break;
			case 'e':
				*num_mem_runs = atoi(optarg);
				break;
			case 'm':
				*mode = atoi(optarg);
				break;
			case 'd':
				*duration = atoi(optarg);
				break;
            case 'i':
                *node_id = atoi(optarg);
                break;
			case 't':
				*threadNR = atoi(optarg);
				break;
			case 'l':
				*lockNR = atoi(optarg);
				break;
			default:
				break;
		}
	}
}
