#include "dag_jobs.hpp"

using namespace cust;

std::string getConfigFile(char** argv) {
    return std::string(argv[1]);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Error: invalid arguments count!\n";
        exit(-1);
    }

    std::string const configFile = getConfigFile(argv);

    DAGJobs dj(configFile);

    dj.launch();

    std::cout << std::endl;

    dj.printJobsResults();

    return 0;
}