#include "job.h"

void Job::incrementCounter(){
    this->counter++;
}

void Job::resetCounter(){
    this->counter = 0;
}

int Job::getId(){
    return this->id;
}

double Job::getJobTime(){
    return this->jobTime;
}

std::string Job::getDescription(){
    return this->description;
}

int Job::counter = 0;

std::ostream& operator<<(std::ostream &os, const Job &job) {
    os << std::to_string(job.id) + ", " + std::to_string(job.jobTime);
    return os;
}
