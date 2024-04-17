
#include <list>
#include <vector>
#include <memory>
#include <iostream>
#include <map>

#include <unistd.h>
#include <sys/wait.h>

#include "ini.hpp"

namespace cust {
    class DAGJobs {
    protected:
        class Job {
        public:
            using ptr = std::shared_ptr<Job>;

        protected:
            class Result {
            public:
                using ptr = std::shared_ptr<Result>;
                int value{0};

                Result(int value);

                void print();
            };

        public:
            static uint64_t count;
            uint64_t const id;
            Result::ptr result;
            std::list<Job::ptr> jobsReq;

        public:
            template <typename... Args>
            Job(uint64_t id, Args... jobsReq);

            template <class... Args>
            static ptr makePointer(uint64_t id, Args... args);

            void launch();

            void printResult();

        protected:
            bool thereAreReqResults();
            
            void launchParentJobs();

            void launchJobProcess();

        public:
            class Config {
            public:
                uint64_t id;
                std::vector<uint64_t> jobReqIds;

            public:
                using ptr = std::shared_ptr<Config>;

                Config(uint64_t id) : id(id) {}

                static std::list<ptr> parse(std::string const& configFile) {
                    std::list<Config::ptr> configs;
                    inih::INIReader reader(configFile);

                    if (reader.ParseError() < 0) {
                        throw std::invalid_argument("Error: can't parse " + configFile);
                    }

                    try {
                        auto jobSections = reader.Sections();
                        for (auto jobSection : jobSections) {
                            auto id = std::stoull(reader.Get(jobSection, "id"));
                            Config::ptr config = std::make_shared<Config>(id);
                            std::vector<uint64_t> jobReqIds;
                            try {
                                auto jobsReq = reader.GetVector(jobSection, "jobsReq");
                                for (auto jobReq : jobsReq) {
                                    jobReqIds.emplace_back(std::stoull(jobReq));
                                }
                            } catch (...) {}
                            config->jobReqIds = jobReqIds;
                            configs.emplace_back(config);
                        }
                    } catch (...) {
                        throw std::invalid_argument("Error: invalid format " + configFile);
                    }

                    return configs;
                }

                bool isBackJob() const {
                    return jobReqIds.empty();
                }
            };

        };

    protected:
        std::list<Job::ptr> execOrderJobs;
        std::list<Job::ptr> frontJobs;
        uint64_t jobsCount;

    public:
        DAGJobs(std::string const& configFile);

        DAGJobs();

        void launch();

        void printJobsResults();

    protected:
        class Graph {
        public:
            typedef char flag;

            uint64_t jobsCount = 0;
            std::list<uint64_t> frontJobsIds;
            std::map<uint64_t, std::vector<uint64_t>> adjacency;

        public:
            Graph(std::string const& configFile) {
                std::list<Job::Config::ptr> jobConfigs = Job::Config::parse(configFile);
                ConstructGraph(jobConfigs); 
            }

            void addFrontNodeFrom(uint64_t id, std::vector<flag>& visited) {
                if (visited[id]) {
                    return;
                }
            }

            void ConstructGraph(std::list<Job::Config::ptr> jobConfigs) {
                jobsCount = jobConfigs.size();
                std::map<uint64_t, flag> isFront;
                for (auto jobConfig : jobConfigs) {
                    adjacency.insert({jobConfig->id, jobConfig->jobReqIds});
                    isFront.insert({jobConfig->id, 1});
                }
                for (auto jobConfig : jobConfigs) {
                    for (auto jobReq : jobConfig->jobReqIds) {
                        isFront[jobReq] = 0;
                    }
                }
                for (auto jobConfig : jobConfigs) {
                    if (isFront[jobConfig->id] == 1) {
                        frontJobsIds.emplace_back(jobConfig->id);
                    }
                }
            }

            void checkOnCycles() {
                std::vector<flag> visited(jobsCount, 0);
                for (auto frontJobId : frontJobsIds) {
                    if (isInCycle(frontJobId, visited)) {
                        throw std::invalid_argument("Error: givan jobs graph has cycles!\n");
                    }
                }
            }

            bool isInCycle(uint64_t id, std::vector<flag>& visited) {
                visited[id] = 1;
                bool result = false;
                for (auto jobReqId : adjacency[id]) {
                    if (visited[id] == 0) {
                        if (result = isInCycle(jobReqId, visited)) {
                            break;
                        }
                    } else if (visited[jobReqId] == 1) {
                        result = true;
                        break;
                    }
                }
                visited[id] = 2;
                return result;
            }

            std::list<Job::ptr> frontJobs() {
                std::map<uint64_t, Job::ptr> jobs;
                for (auto jobPair : adjacency) {
                    jobs.insert({jobPair.first, Job::makePointer(jobPair.first)});
                }

                std::list<Job::ptr> fJobs;
                for (auto jobId : frontJobsIds) {
                    jobs[jobId]->jobsReq = createReqJobs(jobId, jobs);
                    fJobs.emplace_front(jobs[jobId]);
                }
                return fJobs;
            }

            std::list<Job::ptr> createReqJobs(uint64_t id, std::map<uint64_t, Job::ptr>& jobs) {
                std::list<Job::ptr> jobsReq;
                for (auto jobReqId : adjacency[id]) {
                    jobs[jobReqId]->jobsReq = createReqJobs(jobReqId, jobs);
                    jobsReq.emplace_front(jobs[jobReqId]);
                }
                return jobsReq;
            }
    
        };

    protected:

        void defineFrontJobs();
        void launchJobs();
        void launchJobsInOrder();

        template <class T>
        void bfs(std::list<Job::ptr> queOrigin, uint64_t jobsCount, T& functor);

        struct PushJobToExecOrder {
        public:
            std::list<Job::ptr>& execOrderJobs;
            PushJobToExecOrder(std::list<Job::ptr>& execOrderJobs);

            void operator()(Job::ptr job);
        };

        struct PrintJob {
            void operator()(Job::ptr job);
        };

    };

    /// DAGJobs begin ///

    DAGJobs::DAGJobs(std::string const& configFile) {
        Graph graph(configFile);
        graph.checkOnCycles();
        frontJobs = std::move(graph.frontJobs());
        jobsCount = graph.jobsCount;
    }

    void DAGJobs::printJobsResults() {
        PrintJob printJob;
        bfs(frontJobs, jobsCount, printJob);
    }

    void DAGJobs::launch() {
        if (execOrderJobs.empty()) {
            PushJobToExecOrder pushJobToExecOrder(execOrderJobs);
            bfs(frontJobs, jobsCount, pushJobToExecOrder);
        }
        launchJobsInOrder();
    }

    void DAGJobs::launchJobsInOrder() {
        for (auto job : execOrderJobs) {
            try {
                job->launch();
            } catch (std::exception const& e) {
                std::cout << "Error: job " << job->id << " crashed!\n";
                break;
            }
        }
    }

    /// DAGJobs end ///

    /// bfs begin ///

    template <class T>
    void DAGJobs::bfs(std::list<Job::ptr> queOrigin, uint64_t jobsCount, T& functor) {
        typedef char flag;
        std::vector<flag> visited(jobsCount, 0);
        auto& que = queOrigin;

        for (auto job : queOrigin) {
            visited[job->id] = 1;
        }

        while (not que.empty()) {
            auto job = que.front(); que.pop_front();
            functor(job);
            for (auto jobReq : job->jobsReq) {
                if (not visited[jobReq->id]) {
                    visited[jobReq->id] = 1;
                    que.emplace_back(jobReq);
                }
            }
        }
    }

    /// bfs end ///

    /// Job begin ///
    uint64_t DAGJobs::Job::count = 0;

    template <typename... Args>
    DAGJobs::Job::Job(uint64_t id, Args... jobsReq)
        : id(id), jobsReq({jobsReq...}) {
        ++count;
    }

    template <class... Args>
    DAGJobs::Job::ptr DAGJobs::Job::makePointer(uint64_t id, Args... args) {
        return std::make_shared<Job>(id, args...);
    }

    void DAGJobs::Job::launch() {
        if (not result) {
            std::cout << "Job " << id << " launched\n";
            if (not thereAreReqResults()) {
                launchParentJobs();
            }
            launchJobProcess();
        }
    }

    bool DAGJobs::Job::thereAreReqResults() {
        for (auto job : jobsReq) {
            if (not job->result) {
                return false;
            }
        }
        return true;
    }
       
    void DAGJobs::Job::launchParentJobs() {
        for (auto job : jobsReq) {
            job->launch();
        }
    }

#define WRITE 1
#define READ  0

    void DAGJobs::Job::launchJobProcess() {
        int _pipe[2];
        if (pipe(_pipe) == -1) {
            throw std::runtime_error("Error: pipe failed");
        }

        pid_t pid = fork();

        if (pid < 0) {
            throw std::runtime_error("Error: fork failed");
        } else if (pid == 0) {
    // Child process
            close(_pipe[READ]);
            srand((unsigned)time(NULL) + getpid());
            int result = rand() % 100;
            char res[2];
            res[0] = result % 10;
            res[1] = ((result % 100) - res[0]) / 10;
            write(_pipe[WRITE], res, sizeof(char) * 2);
            close(_pipe[WRITE]);
            exit(0);
        } else {
    // Parent process
            close(_pipe[WRITE]);
            char res[2];
            read(_pipe[READ], res, sizeof(char) * 2);
            close(_pipe[READ]);
            int result = res[0] + res[1] * 10;
            std::cout << "> Job " << id << " generated " << result << "\n";
            for (auto jobReq : jobsReq) {
                result += jobReq->result->value;
            }
            this->result = std::make_shared<Result>(result);
        }
    }


    void DAGJobs::Job::printResult() {
        std::cout << "Job " << id << ", result: ";
        result->print();
    }

    /// Result begin ///

    DAGJobs::Job::Result::Result(int value) : value(value) {}
    
    void DAGJobs::Job::Result::print() {
        std::cout << value;
    }

    /// Result end ///

    /// Job end ///

    /// functors begin ///

    DAGJobs::PushJobToExecOrder::PushJobToExecOrder(std::list<Job::ptr>& execOrderJobs)
        : execOrderJobs(execOrderJobs) {}

    void DAGJobs::PushJobToExecOrder::operator()(Job::ptr job) {
        execOrderJobs.emplace_front(job);
    }

    void DAGJobs::PrintJob::operator()(Job::ptr job) {
        job->printResult();
        std::cout << std::endl;
    }

    /// functors end ///

}

