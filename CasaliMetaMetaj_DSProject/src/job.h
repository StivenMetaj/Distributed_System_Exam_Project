#include <string.h>
#include <random>

#pragma once

// This will be the job class, each job has an unique id, a possible description and a jobTime (this will be the time necessary to execute the job).

class Job {
    private:
        int id;
        std::string description;
        double jobTime;

    public:
        static int counter;

        // We have different constructors with different possible parameters
        Job(double time=4.0){
            this->id = counter;
            incrementCounter();
            this->description = "Default description";
            this->jobTime = time;
        }

        Job(std::string description, double time=4.0){
            this->id = counter;
            incrementCounter();
            this->description = description;
            this->jobTime = time;
        }

        void incrementCounter();
        void resetCounter();
        int getId();
        double getJobTime();
        std::string getDescription();

        friend std::ostream& operator<<(std::ostream& os, const Job& job);
};
