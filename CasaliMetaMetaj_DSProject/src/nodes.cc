#include <stdio.h>
#include <string.h>
#include <omnetpp.h>
#include <algorithm>
#include <list>
#include <random>
#include <sstream>
#include "JobMsg_m.h"
#include "CollectStatusMsg_m.h"
#include "StatusMsg_m.h"
#include "JobReturnValueMsg_m.h"
#include "ClientAnswerMsg_m.h"
#include "LostJobMsg_m.h"
#include "Ping_m.h"
#include <sstream>

using namespace omnetpp;

//###############################################################################
// -------------------------------- CLIENT ------------------------------------
//###############################################################################

// We define the structs that are needed in order to save information in each node
struct infoJob {
    Job *job;
    int execIndex;
    int distrIndex;
    bool returnValue;
};

struct assignedStruct{
    int jobId;
    bool assigned = false;
};

class Client: public cSimpleModule {
private:
    // Self messages
    simsignal_t arrivalSignal;
    cMessage *initMessage;
    cMessage *newJobMessage;
    cMessage *failureMsg;
    cMessage *recoveryMsg;
    cMessage *periodicCheck;
    cMessage *collectionPongsTimeout;
    cMessage *recoveryAnswerTimeout;

    // Data structures
    std::list<infoJob> infoJobsList;
    std::list<int> idExecList;
    std::list<int> idActiveExecList;
    std::list<assignedStruct> assignedList;

    // States
    int jobCounter = 0;
    bool iAmDead = false;

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual JobMsg* generateMessage(int i);
};

Define_Module(Client);

void Client::initialize() {

    WATCH(jobCounter);
    WATCH(iAmDead);


    // Get the parameters from the omnetpp.ini
    int NUMBER_OF_JOBS_TO_BE_DONE = getParentModule()->par("numberOfJobsToBeDone");
    double MIN_JOB_TIME = getParentModule()->par("minJobTime");
    double MAX_JOB_TIME = getParentModule()->par("maxJobTime");
    double realProb = getParentModule()->par("clientsDeadProbability");
    double maxDeathStart = getParentModule()->par("clientsMaxDeathStart");

    // We define a probability of death and we start a self message that will "shut down" some nodes
    double deadProbability = uniform(0, 1);
    if (deadProbability < realProb) {
        double randomDelay = uniform(1, maxDeathStart);
        failureMsg = new cMessage("failureMsg");
        EV << "Here is client[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }

    // Create randomly the infoJob element with the job and default values
    std::uniform_real_distribution<double> unif(MIN_JOB_TIME, MAX_JOB_TIME);
    std::default_random_engine re(this->getId());

    // Here we initialize all the new jobs to be executed and to be sent to the executors
    for (int i = 0; i < NUMBER_OF_JOBS_TO_BE_DONE; i++) {
        infoJob temp;
        assignedStruct temp2;
        temp.job = new Job(unif(re));

        temp2.jobId = temp.job->getId();
        assignedList.push_back(temp2);

        temp.execIndex = -1;
        temp.distrIndex = -1;
        temp.returnValue = false;
        infoJobsList.push_back(temp);
    }

    // DEBUG PRINTS------------------------------------------------------------------------------------------------------------------------------------
    EV << "******************INITIALIZATION EV****************************\n";
    EV << "Here is client[" + std::to_string(this->getIndex()) + "]: my job list has length equal to " + std::to_string(infoJobsList.size()) + "\n";
    EV << "******************\n";
    EV << "-------------------------------------------------------\n";
    EV << "******************\n";

    for (int i = 0; i < infoJobsList.size(); i++) {
        std::list<infoJob>::iterator it = infoJobsList.begin();
        std::advance(it, i);
        EV << "Job numero " + std::to_string(i + 1) + ": ID -> " + std::to_string((*it).job->getId()) + " | JOB_TIME -> " + std::to_string((*it).job->getJobTime()) + "\n";
    }
    EV << "******************\n";
    EV << "-------------------------------------------------------\n";
    EV << "******************\n";

    EV << "Lista dei moduli connessi ai gate del client: \n";
    int counter_printed = 1;
    for (cModule::GateIterator i(this); !i.end(); i++) {
        cGate *gate = *i;
        int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();

        if (h != this->getId()) {
            EV << std::to_string(counter_printed) + ". " + (gate)->getPathEndGate()->getOwnerModule()->getName() + "["
            + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "]\n";
            idExecList.push_back((gate)->getPathEndGate()->getOwnerModule()->getIndex());
            counter_printed++;
        }
    }
    EV << "******************\n";
    EV << "-------------------------------------------------------\n\n\n";
    // FINE DEBUG----------------------------------------------------------------------------------------------------------------------------------------

    // Initialization of first selfMessage in order to start to distribute the jobs
    initMessage = new cMessage("initMessage");
    double randomStartingDelay = uniform(0.1, 0.3);
    scheduleAt(simTime() + randomStartingDelay, initMessage);

    // Start the periodic check loop
    periodicCheck = new cMessage("Checking Jobs");
    randomStartingDelay = uniform(7, 10);
    scheduleAt(simTime() + randomStartingDelay, periodicCheck);
}

void Client::handleMessage(cMessage *msg) {
    JobMsg *jobmsg = dynamic_cast<JobMsg*>(msg);
    JobReturnValueMsg *jobreturnvaluemsg = dynamic_cast<JobReturnValueMsg*>(msg);
    Ping *pong = dynamic_cast<Ping*>(msg);
    LostJobMsg *lostjobmsg = dynamic_cast<LostJobMsg*>(msg);

    double MAX_WAIT_UNTIL_NEW_JOB = getParentModule()->par("maxWaitUntilNewJob");
    int NUMBER_OF_JOBS_TO_BE_DONE = getParentModule()->par( "numberOfJobsToBeDone");
    double maxDeathDuration = getParentModule()->par("clientsMaxDeathDuration");

    // ############################################### RECOVERY BEHAVIOUR ###############################################

    if (msg == failureMsg) {
        iAmDead = true;
        recoveryMsg = new cMessage("recoveryMsg");

        double randomFailureTime = uniform(5, maxDeathDuration);
        EV << "\nClient ID: [" + std::to_string(this->getIndex()) + "] is dead for about: [" + std::to_string(randomFailureTime) + "]\n";
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    else if (msg == recoveryMsg) {
        // Start the periodic check loop
        cancelAndDelete(periodicCheck);
        cancelAndDelete(newJobMessage);

        periodicCheck = new cMessage("Checking Jobs");
        double randomStartingDelay = uniform(2, 4);
        scheduleAt(simTime() + randomStartingDelay, periodicCheck);

        iAmDead = false;
        EV << "Here is client[" + std::to_string(this->getIndex()) + "]: I am no more dead... requesting the returnValues and restarting to give jobs...\n";

        for(auto it = infoJobsList.begin(); it != infoJobsList.end(); it++){
            if ((*it).returnValue == 0){
                int index = (*it).execIndex;
                if (index != -1){
                    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        int id = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string ex = "executor";

                        if (name == ex && id == index) {
                            EV << "Sending to executor[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex())
                            + "] (equal to " + std::to_string(index) + ") if he has the job with id " + std::to_string((*it).job->getId()) + " done (or in progress!!!!)...\n";

                            ClientAnswerMsg *status = new ClientAnswerMsg("Response");
                            status->setRecovery(true);
                            status->setJobID((*it).job->getId());
                            status->setClientIndex(this->getIndex());

                            send(status, gate->getName(), gate->getIndex());
                        }
                    }
                }
            }
        }

        EV << "Sending an auto recoveryAnswerTimeout message \n";
        recoveryAnswerTimeout = new cMessage("recoveryAnswerTimeout");

        double randomStartingDelay2 = 0.5;
        scheduleAt(simTime() + randomStartingDelay2, recoveryAnswerTimeout);
    }

    else if (iAmDead){
        EV << "At the moment I'm dead so I can't react to this message, sorry \n";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################

    else if (iAmDead == false) {
        if (msg == initMessage) {
            // Generate the first job message for the client
            JobMsg *jobMsg = generateMessage(0);
            send(jobMsg, "gate$o", jobMsg->getDestination());
            EV << "... the job is sent to executor[" + std::to_string(jobMsg->getDestination()) + "]!\n\n";

            // Schedule the next job, now we exit the special case of the first message and enter in the loop of newJobMessages
            double randomDelay = uniform(0.5, MAX_WAIT_UNTIL_NEW_JOB);
            newJobMessage = new cMessage("newJobMessage");
            scheduleAt(simTime() + randomDelay, newJobMessage);
        }

        else if (msg == newJobMessage) {
            if(jobCounter == NUMBER_OF_JOBS_TO_BE_DONE){
                EV << "I have finished sending my jobs!";
            } else {
                int i = 0;
                for (auto it = infoJobsList.begin(); it != infoJobsList.end(); it++) {
                    if((*it).distrIndex == -1){
                        JobMsg *jobMsg = generateMessage(i);
                        send(jobMsg, "gate$o", jobMsg->getDestination());

                        EV << "... the job is sent to " + std::to_string(jobMsg->getDestination()) + "!\n\n";
                        double randomDelay = uniform(0.5, MAX_WAIT_UNTIL_NEW_JOB);
                        EV << "Scheduling new message after " + std::to_string(randomDelay) + " seconds...\n";

                        newJobMessage = new cMessage("newJobMessage");
                        scheduleAt(simTime() + randomDelay, newJobMessage);
                        break;
                    }
                    i++;
                }
            }
        }


        // If the executor receives a ping, it answers with another ping with its id.
        // The client collects all the pings and understands if some executor is dead (it will reschedule the jobs assigned to dead executors).
        //
        // If returnValue == true, we end
        // If returnValue == false, we check execIndex in order to discover if it is dead or not.
        // Instead if execIndex == -1, we must check also distrIndex!

        else if(msg == recoveryAnswerTimeout){
            EV << "Here is client[" + std::to_string(this->getIndex()) + "]: I received my own timeout for the recovery answers.. let's check them!\n";

            int i = 0;
            for(auto it = infoJobsList.begin(); it != infoJobsList.end(); it++){
                if ((*it).returnValue == 0 && (*it).execIndex != -1){
                    bool reDistribute = true;
                    for (auto el = assignedList.begin(); el != assignedList.end(); el++){
                        if ((*el).jobId == (*it).job->getId() && (*el).assigned == true){
                            reDistribute = false;
                        }
                    }
                    if (reDistribute){
                        JobMsg *jobMsg = generateMessage(i);
                        send(jobMsg, "gate$o", jobMsg->getDestination());
                        EV << "Executor [" + std::to_string((*it).execIndex) + "] is replaced with executor [" + std::to_string(jobMsg->getDestination()) + "]\n";
                        (*it).execIndex = -1;
                        (*it).distrIndex = -1;

                    }
                }
                i++;
            }

            double randomDelay = uniform(0.5, MAX_WAIT_UNTIL_NEW_JOB);
            newJobMessage = new cMessage("newJobMessage");
            scheduleAt(simTime() + randomDelay, newJobMessage);
        }

        else if (lostjobmsg != nullptr){
            if (lostjobmsg->getAssigned() == true){
                for (auto el = assignedList.begin(); el != assignedList.end(); el++){
                    if ((*el).jobId == lostjobmsg->getJobId()){
                        (*el).assigned = true;
                    }
                }
            }
        }

        else if(msg == periodicCheck){
            EV << "Questo e' il jobCounter: " + std::to_string(jobCounter) + ", questo e' la sizeole di info: " + std::to_string(infoJobsList.size());
            if (jobCounter != infoJobsList.size()){
                // I send to all the executors a message asking if they have any of my jobs already completed
                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string ex = "executor";

                    if (name == ex) {
                        EV << "Sending to executor[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "] a ping message...\n";

                        Ping *ping = new Ping("ping");
                        ping->setClientIndex(this->getIndex());
                        ping->setExecIndex((gate)->getPathEndGate()->getOwnerModule()->getIndex());

                        send(ping, gate->getName(), gate->getIndex());
                    }
                }

                // Start the period for collecting the pongs
                // The distribution phase will end in 0.5s: 0.2 of roundtrip times + 0.24 of delays + little epsilon of margin
                collectionPongsTimeout = new cMessage("collectionPongTimeout");
                double networkDelay = 0.5;
                scheduleAt(simTime() + networkDelay, collectionPongsTimeout);

                // Schedule the next ping
                periodicCheck = new cMessage("Checking Jobs");
                double randomDelay = uniform(2, 4);
                scheduleAt(simTime() + randomDelay, periodicCheck);
            }
        }

        else if(pong != nullptr){
            idActiveExecList.push_back(pong->getExecIndex());
            EV << "The executor [" + std::to_string(pong->getExecIndex()) + "] is alive!!!!\n";
        }

        else if(msg == collectionPongsTimeout){
            std::list <int> idDeadExecList;
            // Fill the list with all the executors
            for(auto it = idExecList.begin(); it != idExecList.end(); it++){
                idDeadExecList.push_back(*it);
                EV << "Aggiungo l'executor[" + std::to_string((*it)) + "] a idDeadExecList\n";
            }

            // Delete all the active exec that send a pong
            for(auto it = idActiveExecList.begin(); it != idActiveExecList.end(); it++){
                idDeadExecList.remove(*it);
                EV << "Elimino l'executor[" + std::to_string(*it) + "] da iddeadExecList";
            }

            for(auto it = idDeadExecList.begin(); it != idDeadExecList.end(); it++){
                EV << "Dead executor [" + std::to_string(*it) + "]\n";
            }


            // Re-schedule the job to a new executor
            for(auto it = idDeadExecList.begin(); it != idDeadExecList.end(); it++){
                int i = 0;
                for(auto it1 = infoJobsList.begin(); it1 != infoJobsList.end(); it1++){
                    if((*it1).returnValue != true){
                        if((*it1).execIndex == *it){
                            //JobMsg *jobMsg = generateMessage((*it1).job->getId());
                            JobMsg *jobMsg = generateMessage(i);
                            (*it1).execIndex = -1;
                            (*it1).distrIndex = -1;
                            send(jobMsg, "gate$o", jobMsg->getDestination());
                            EV << "Executor [" + std::to_string(*it) + "] is replaced with executor [" + std::to_string(jobMsg->getDestination()) + "]\n";
                        }
                        else if((*it1).execIndex == -1){
                            if((*it1).distrIndex == *it){
                                (*it1).distrIndex = -1;
                                //JobMsg *jobMsg = generateMessage((*it1).job->getId());
                                JobMsg *jobMsg = generateMessage(i);
                                send(jobMsg, "gate$o", jobMsg->getDestination());
                                EV << "Executor [" + std::to_string(*it) + "] is replaced with executor [" + std::to_string(jobMsg->getDestination()) + "]\n";
                            }
                        }

                    }
                    i++;
                }
            }

            idActiveExecList.clear();
        }


        else if (jobreturnvaluemsg != nullptr) {

            /*
            We use this message for three purposes:
            1. If the return value is true, the job is finished
            2. If the execIndex is different from -1, someone has put the job in the execution list
            3. If the distrIndex is different from -1, someone has put the job in the distribution list
            */

            // Case 1: the job has finished
            if(jobreturnvaluemsg->getReturnValue() == true){
                EV << "Here is the client[" + std::to_string(this->getIndex()) + "]: someone must have finished my job with id: " + std::to_string(jobreturnvaluemsg->getJobId()) + "\n";

                bool realChange = true;
                // Mark the job as finished in the infoList
                for(auto it = infoJobsList.begin(); it != infoJobsList.end(); it++){
                    if((*it).job->getId() == jobreturnvaluemsg->getJobId()){

                        if ((*it).returnValue == true){
                            realChange = false;
                        }

                        EV << "Job with ID: " + std::to_string(jobreturnvaluemsg->getJobId()) + " has finished!\n";
                        (*it).returnValue = jobreturnvaluemsg->getReturnValue();
                        (*it).execIndex = jobreturnvaluemsg->getSource();
                    }
                }

                // Acknowledge the executor that the return value was correctly received
                ClientAnswerMsg *status = new ClientAnswerMsg("Response");
                status->setRecovery(false);
                status->setPing(false);
                status->setClientIndex(this->getIndex());
                status->setJobID(jobreturnvaluemsg->getJobId());

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string ex = "executor";

                    if (h == jobreturnvaluemsg->getSource() && name == ex) {
                        EV << "\nSending an ACK to executor[" + std::to_string(jobreturnvaluemsg->getSource()) + "] ... I am alive and I received correctly the returnValue! \n";
                        send(status, gate->getName(), gate->getIndex());
                    }
                }

                if (realChange){
                    jobCounter++;
                }

            } else { // Case 2: someone added the job in the execution list
                EV << "the message have these info -> execIndex= " + std::to_string(jobreturnvaluemsg->getExecIndex()) + ", distriIndex= " + std::to_string(jobreturnvaluemsg->getDistrIndex()) + "\n";
                if(jobreturnvaluemsg->getExecIndex() != -1){
                    for(auto it = infoJobsList.begin(); it != infoJobsList.end(); it++){
                        if((*it).job->getId() == jobreturnvaluemsg->getJobId()){
                            EV << "Someone added the following job in the execution list. JobID: " + std::to_string(jobreturnvaluemsg->getJobId()) + "\n";
                            (*it).distrIndex = jobreturnvaluemsg->getDistrIndex();
                            (*it).execIndex = jobreturnvaluemsg->getExecIndex();
                        }
                    }
                } else { // Case 3: someone added the job in the distribution list
                    for(auto it = infoJobsList.begin(); it != infoJobsList.end(); it++){
                        if((*it).job->getId() == jobreturnvaluemsg->getJobId()){
                            EV << "Someone added the following job in the distribution list. JobID: " + std::to_string(jobreturnvaluemsg->getJobId()) + "\n";
                            (*it).distrIndex = jobreturnvaluemsg->getDistrIndex();
                        }
                    }
                }
            }
            finish();
        } // End of the returnvaluemsg case
    } // End of the if(iAmDead == false)
} // End of the handleMessage()

JobMsg* Client::generateMessage(int i) {
    int src = getIndex();
    int n = getParentModule()->par("executors");
    int dest = intuniform(0, n - 1);
    char msgname[20];

    std::list<infoJob>::iterator it = infoJobsList.begin();
    std::advance(it, i);

    EV << "Here is the client[" + std::to_string(this->getIndex()) + "]: I'm generating the message containing the job with position in jobList equal to " + std::to_string(i) + "\n";

    JobMsg *msg = new JobMsg(msgname);
    msg->setSource(src);
    msg->setClientID(src);
    msg->setDestination(dest);
    msg->setAssigned(FALSE);
    msg->setJob(*((*it).job));

    EV << "The job has these information: sourceID: " + std::to_string(src) + ", destinationID: " + std::to_string(dest) + " jobID: " + std::to_string((*it).job->getId()) + ", jobWaitTime: " + std::to_string((*it).job->getJobTime()) + "\n";
    return msg;
}

void Client::finish() {}

//###############################################################################
// -------------------------------- EXECUTOR ------------------------------------
//###############################################################################

struct infoExec {
    int numJob;
    int execIndex;
    int clientId;
    Job job;
};

class Executor: public cSimpleModule {
private:

    // Data structures
    std::vector<infoExec> infoExecList;
    std::list<JobMsg> jobMsgsWaitingList;
    std::list<JobMsg> jobMsgsToDo;
    std::list<JobReturnValueMsg> jobMsgsDone;
    cLongHistogram jobsToDoStats;
    cOutVector jobsToDoVector;

    // Self Messages
    cMessage *notMoreDistributing;
    cMessage *notMoreExecuting;
    cMessage *jobFinished;
    cMessage *failureMsg;
    cMessage *recoveryMsg;
    cMessage *collectionTimeout;

    int executorsNumber;

    // States
    bool distributing_job;
    bool executing_job;
    bool iAmDead;

protected:
    virtual void handleMessage(cMessage *msg) override;
    virtual void initialize() override;
    virtual void finish() override;
    void distributeJob(JobMsg *jobmsg);
    void executeJob(JobMsg *jobmsg);
};

Define_Module(Executor);

void Executor::initialize() {
    WATCH_LIST(jobMsgsToDo);
    WATCH_LIST(jobMsgsWaitingList);
    WATCH_LIST(jobMsgsDone);
    WATCH(distributing_job);
    WATCH(executing_job);
    WATCH(iAmDead);

    jobsToDoStats.setName("Jobs To Do Stats");
    jobsToDoVector.setName("Jobs To Do Vector");
    jobsToDoVector.record(jobMsgsToDo.size());
    jobsToDoStats.collect(jobMsgsToDo.size());

    double realProb = getParentModule()->par("executorsDeadProbability");
    double maxDeathStart = getParentModule()->par("executorsMaxDeathStart");
    executorsNumber = getParentModule()->par("executors");

    // States
    distributing_job = false;
    executing_job = false;
    iAmDead = false;

    double deadProbability = uniform(0, 1);
    if (deadProbability < realProb) {
        double randomDelay = uniform(1, maxDeathStart);
        failureMsg = new cMessage("failureMsg");
        EV << "Here is executor[" + std::to_string(this->getIndex()) + "]: I will be dead in " + std::to_string(randomDelay) + " seconds...\n";
        scheduleAt(simTime() + randomDelay, failureMsg);
    }
}

void Executor::executeJob(JobMsg *jobmsg) {
    executing_job = true;
    EV << "The job with id " + std::to_string((jobmsg->getJob()).getId()) + " is assigned to me!!\n";

    jobFinished = new cMessage("jobFinished");
    double randomStartingDelay = uniform(0.1, 0.3);
    EV << "Starting to do the job!\n\n";
    scheduleAt(simTime() + randomStartingDelay + jobmsg->getJob().getJobTime(), jobFinished);
}

void Executor::distributeJob(JobMsg *jobmsg) {
    if (executorsNumber == 1){
        infoExec values;
        values.numJob = jobMsgsToDo.size();
        values.execIndex = this->getIndex();
        values.job = jobmsg->getJob();
        values.clientId = jobmsg->getClientID();
        infoExecList.push_back(values);

        collectionTimeout = new cMessage("collectionTimeout");
        double networkDelay = 0.01;
        scheduleAt(simTime() + networkDelay, collectionTimeout);
    } else {
        distributing_job = true;

        EV << "\nFirst of all, I add my own information!\n";

        infoExec values;
        values.numJob = jobMsgsToDo.size();
        values.execIndex = this->getIndex();
        values.job = jobmsg->getJob();
        values.clientId = jobmsg->getClientID();
        infoExecList.push_back(values);

        EV << "The collection of the executors status is now long " + std::to_string(infoExecList.size()) + ". I added MY(!!) following element: \n";
        EV << "JobsNumber: " + std::to_string(values.numJob);
        EV << ", ExecutorIndex: " + std::to_string(values.execIndex) + ",\n";
        EV << "JobID: " + std::to_string(infoExecList[0].job.getId()) + "\n";
        EV << "\nLet's collect information from other executors...\n";

        CollectStatusMsg *mss = new CollectStatusMsg("Collect");
        mss->setJob(jobmsg->getJob());
        mss->setSource(jobmsg->getDestination());
        mss->setClientID(jobmsg->getClientID());

        EV << "List of the connected executors:\n";

        // The distribution phase will end in 0.5s: 0.2 of roundtrip times + 0.24 of delays + little epsilon of margin
        collectionTimeout = new cMessage("collectionTimeout");
        double networkDelay = 0.5;
        scheduleAt(simTime() + networkDelay, collectionTimeout);

        int counter_printed = 1;
        for (cModule::GateIterator i(this); !i.end(); i++) {
            cGate *gate = *i;
            int h = (gate)->getPathEndGate()->getOwnerModule()->getId();
            const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
            std::string ex = "executor";

            // to avoid self msg and msg to client
            if (h != this->getId() && name == ex) {
                mss->setDestination((gate)->getPathEndGate()->getOwnerModule()->getIndex());
                EV << std::to_string(counter_printed) + ". " + (gate)->getPathEndGate()->getOwnerModule()->getName() + "[" + std::to_string((gate)->getPathEndGate()->getOwnerModule()->getIndex()) + "]\n";

                send(mss->dup(), gate->getName(), gate->getIndex());
                counter_printed++;
            }
        }
        EV << "\nRichiesta di status mandata a tutti gli altri executor!\n";
    }
}

void Executor::handleMessage(cMessage *msg) {
    // Understand which is the type of the incoming message
    JobMsg *jobmsg = dynamic_cast<JobMsg*>(msg);
    CollectStatusMsg *collectStatusMsg = dynamic_cast<CollectStatusMsg*>(msg);
    StatusMsg *statusMsg = dynamic_cast<StatusMsg*>(msg);
    ClientAnswerMsg *clientAnswerMsg = dynamic_cast<ClientAnswerMsg*>(msg);
    Ping *ping = dynamic_cast<Ping*>(msg);

    // ############################################### RECOVERY BEHAVIOUR ###############################################

    if (msg == failureMsg) {
        // Change status
        iAmDead = true;
        executing_job = false;
        distributing_job = false;

        // Clear the lists
        jobMsgsToDo.clear();
        jobMsgsWaitingList.clear();

        // Schedule Recovery Message
        recoveryMsg = new cMessage("recoveryMsg");
        double maxDeathDuration = getParentModule()->par("executorsMaxDeathDuration");
        double randomFailureTime = uniform(5, maxDeathDuration);
        EV << "\nExecutor[" + std::to_string(this->getIndex()) + "] is dead for about: [" + std::to_string(randomFailureTime) + "]\n";
        scheduleAt(simTime() + randomFailureTime, recoveryMsg);
    }

    else if (msg == recoveryMsg) {
        iAmDead = false;
        EV << "I'm back, let's start working again!\n";
    }

    // ################################################ NORMAL BEHAVIOUR ################################################

    else if (iAmDead){
        EV << "At the moment I'm dead so I can't react to this message, sorry \n";
    }

    else if (!iAmDead){
        if (jobmsg != nullptr) {
            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received the job with jobID: " + std::to_string((jobmsg->getJob()).getId()) + ", jobWaitTime: " + std::to_string((jobmsg->getJob()).getJobTime()) + "\n";

            // I don't have jobs in my list so I start executing
            if (jobMsgsToDo.size() == 0) {
                EV << "It is the first job in my hands.. let's do it!! (calling the executeJob function)\n";
                jobmsg->setAssigned(TRUE);
                jobMsgsToDo.push_back(*jobmsg);


                jobsToDoVector.record(jobMsgsToDo.size());
                jobsToDoStats.collect(jobMsgsToDo.size());

                // Send to client update that I am the (fake) distributor and true executor
                JobReturnValueMsg *jrv = new JobReturnValueMsg("Executing");
                jrv->setSource(this->getIndex());
                jrv->setDestination(jobmsg->getClientID());
                jrv->setJobId((jobmsg->getJob()).getId());
                jrv->setDistrIndex(this->getIndex());
                jrv->setExecIndex(this->getIndex());
                jrv->setReturnValue(false);

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";

                    // To send the msg only to the client chosen, not the exec with same index
                    if (h == jobmsg->getClientID() && name == cl) {
                        EV << "\n##################### \n";
                        EV << "... answering to the client[" + std::to_string(jobmsg->getClientID())
                        + "] to inform that I will be the executor (and fake distributor) of the jobID: " + std::to_string(jrv->getJobId());
                        EV << "\n##################### \n";

                        send(jrv, gate->getName(), gate->getIndex());
                    }
                }
                executeJob(jobmsg);
            }

            // Not the first job in my list, but I have to execute it
            else if (jobmsg->getAssigned() == TRUE) {
                EV << "... ok, the job is assigned to me! Let's add it to the job list!!\n";
                jobMsgsToDo.push_back(*jobmsg);

                jobsToDoVector.record(jobMsgsToDo.size());
                jobsToDoStats.collect(jobMsgsToDo.size());

                // Inform the client that I'm the executor of this job
                JobReturnValueMsg *jrv = new JobReturnValueMsg("In execution list");
                jrv->setSource(this->getIndex());
                jrv->setDestination(jobmsg->getClientID());
                jrv->setJobId((jobmsg->getJob()).getId());
                jrv->setDistrIndex(jobmsg->getSource());
                jrv->setExecIndex(this->getIndex());
                jrv->setReturnValue(false);

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";

                    // To send the msg only to the client chosen, not the exec with same index
                    if (h == jobmsg->getClientID() && name == cl) {
                        EV << "\n##################### \n";
                        EV << "... answering to the client[" + std::to_string(jobmsg->getClientID())
                        + "] to inform that I will execute in the future the jobID: " + std::to_string(jrv->getJobId());
                        EV << "\n##################### \n";

                        send(jrv, gate->getName(), gate->getIndex());
                    }
                }
            }

            // I have to distribute it
            else {
                if (!distributing_job) {
                    EV << "... all good! I'm not distributing and I start to distribute the job.\n";
                    jobMsgsWaitingList.push_back(*jobmsg);

                    JobReturnValueMsg *jrv = new JobReturnValueMsg("Distributing");
                    jrv->setSource(this->getIndex());
                    jrv->setDestination(jobmsg->getClientID());
                    jrv->setJobId((jobmsg->getJob()).getId());
                    jrv->setDistrIndex(this->getIndex());
                    jrv->setExecIndex(-1);
                    jrv->setReturnValue(false);

                    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string cl = "client";

                        // To send the msg only to the client chosen, not the exec with same index
                        if (h == jobmsg->getClientID() && name == cl) {
                            EV << "\n##################### \n";
                            EV << "... answering to the client[" + std::to_string(jobmsg->getClientID())
                            + "] to inform that I am distributing the jobID: " + std::to_string(jrv->getJobId());
                            EV << "\n##################### \n";

                            send(jrv, gate->getName(), gate->getIndex());
                        }
                    }
                    distributeJob(jobmsg);
                } else {
                    EV
                    << "... oh no! I'm already distributing another job! I will save this job in my waiting list :) :)\n";
                    jobMsgsWaitingList.push_back(*jobmsg);

                    JobReturnValueMsg *jrv = new JobReturnValueMsg("In distributing List");
                    jrv->setSource(this->getIndex());
                    jrv->setDestination(jobmsg->getClientID());
                    jrv->setJobId((jobmsg->getJob()).getId());
                    jrv->setDistrIndex(this->getIndex());
                    jrv->setExecIndex(-1);
                    jrv->setReturnValue(false);

                    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string cl = "client";

                        // To send the msg only to the client chosen, not the exec with same index
                        if (h == jobmsg->getClientID() && name == cl) {
                            EV << "\n##################### \n";
                            EV << "... answering to the client[" + std::to_string(jobmsg->getClientID())
                            + "] to inform that I will distribute the jobID: " + std::to_string(jrv->getJobId());
                            EV << "\n##################### \n";

                            send(jrv, gate->getName(), gate->getIndex());
                        }
                    }
                }
            }
        }

        // I finished distributing a job and I can procede to distribute another one
        else if (msg == notMoreDistributing) {
            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received my own self \"notMoreDistributing\" message!!" + "\n";

            if (!distributing_job) {
                if (jobMsgsWaitingList.size() != 0) {
                    JobMsg *firstWaitingJobMsg = &(jobMsgsWaitingList.front());

                    EV << "... TOP! The first element of my waiting list is the new job I have to process! Here the job information: JobId:"
                    + std::to_string((firstWaitingJobMsg->getJob()).getId()) + ", jobWaitTime: "
                    + std::to_string((firstWaitingJobMsg->getJob()).getJobTime()) + "\n";

                    distributeJob(firstWaitingJobMsg);
                } else {
                    EV << "... the waiting list is empty!! \n";
                }
            } else {
                // in teoria qui non si arriva mai!
                EV << ".. oh oh, it seems like I am already distributing another job.. let's wait few seconds and retry (I continue to retry until the executor is not distributing jobs)!";
                double randomStartingDelay = uniform(3, 5);
                notMoreDistributing = new cMessage("notMoreDistributing");
                scheduleAt(simTime() + randomStartingDelay, notMoreDistributing);
            }
        }

        // prima controllavo assigned, qui invece devo controllare se la lista di todolist e' vuota o no
        else if (msg == notMoreExecuting) {
            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received my own self \"notMoreExecuting\" message!!" + "\n";

            if (!executing_job) {
                if (jobMsgsToDo.size() != 0) {
                    JobMsg *firstWaitingJobMsg = &(jobMsgsToDo.front());

                    EV << "... TOP! The first element of my todo list is the new job I have to process! Here the job information:\nJobId:"
                    + std::to_string( (firstWaitingJobMsg->getJob()).getId()) + ", jobWaitTime: "
                    + std::to_string( (firstWaitingJobMsg->getJob()).getJobTime()) + "\n";

                    executeJob(firstWaitingJobMsg);
                } else {
                    EV << "... the todo list is empty!! \n";
                }
            } else {
                // in teoria qui non si arriva mai!
                EV << ".. oh oh, it seems like I am already executing another job.. let's wait few seconds and retry" " (I continue to retry until the executor is not distributing jobs)!";
                double randomStartingDelay = uniform(3, 5);
                notMoreExecuting = new cMessage("notMoreExecuting");
                scheduleAt(simTime() + randomStartingDelay, notMoreExecuting);
            }
        }

        // return the value to the client and schedule a notMoreExecuting self message
        else if (msg == jobFinished) {
            if (jobMsgsToDo.size() != 0){
                EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received my own self \"jobFinished\" message!! \n";

                bool returnValue = true;
                JobMsg *finishedJobMsg = &(jobMsgsToDo.front());

                JobReturnValueMsg *jrv = new JobReturnValueMsg("Return Value");
                jrv->setSource(this->getIndex());
                jrv->setDestination(finishedJobMsg->getClientID());
                jrv->setJobId(finishedJobMsg->getJob().getId());
                jrv->setReturnValue(returnValue);

                jobMsgsDone.push_back(*jrv);

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";

                    // To send the msg only to the client chosen, not the exec with same index
                    if (h == finishedJobMsg->getClientID() && name == cl) {
                        EV << "\n##################### \n";
                        EV << "... answering to the client[" + std::to_string(finishedJobMsg->getClientID())
                        + "] with the return value of the jobID: " + std::to_string(jrv->getJobId());
                        EV << "\n##################### \n";


                        send(jrv, gate->getName(), gate->getIndex());
                    }
                }

                // tolgo il lavoro finito da quelli in esecuzione
                jobMsgsToDo.pop_front();

                jobsToDoVector.record(jobMsgsToDo.size());
                jobsToDoStats.collect(jobMsgsToDo.size());

                executing_job = false;

                double randomStartingDelay = uniform(0.001, 0.003);
                notMoreExecuting = new cMessage("notMoreExecuting");
                EV << "... scheduling a \"notMoreExecuting\" self message...\n\n";
                scheduleAt(simTime() + randomStartingDelay, notMoreExecuting);
            }
        }

        else if (clientAnswerMsg != nullptr) {
            /*
             Three possible scenarios:
             1. The client wants to acknowledge the correct receiving of a finished job
             2. The client is in recovery phase
             3. The client pings the executor
             */

            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received a clientAnswerMessage!! \n";
            EV << "Let's check if it is a recovery or an ack message...\n";

            if (clientAnswerMsg->getRecovery() == false) { // Not recovery

               EV << "It is a ack!! Let's check the jobMsgsDone list to erase the returnValues correctly received by the client\n";
               if (jobMsgsDone.size() != 0){
                   std::list<JobReturnValueMsg> tmpList1;
                   for (auto it = jobMsgsDone.begin(); it != jobMsgsDone.end(); it++) {
                       if (clientAnswerMsg->getClientIndex() == (*it).getDestination() && clientAnswerMsg->getJobID() == (*it).getJobId()) {
                           EV << "Found a job to erase!\nThe job with id " + std::to_string((*it).getJobId())
                           + " has been already read from client[" + std::to_string(clientAnswerMsg->getClientIndex()) + "]\n";

                           JobReturnValueMsg *forwardAgainMsg = &(*it);
                       } else {
                           tmpList1.push_back(*it);
                       }
                   }
                   jobMsgsDone = tmpList1;
               }

            } else { // Recovery phase
                LostJobMsg *lostjobmsg = new LostJobMsg("lostjobmsg");
                lostjobmsg->setAssigned(false);

                EV << "It is a recovery message!! Let's check the jobMsgsDone list to send the returnValues to the client..\n***\nLet's print the jobs I have done:\n";

                for (auto it = jobMsgsDone.rbegin(); it != jobMsgsDone.rend(); it++) {
                    EV << "JobID: " + std::to_string((*it).getJobId()) + ", done by executor[" + std::to_string(this->getIndex()) + "] (well... myself) with ClientID: " + std::to_string((*it).getDestination()) + "\n";
                }

                EV << "Lunghezza jodDoneList: " + std::to_string(jobMsgsDone.size()) + "\n";

                if (jobMsgsToDo.size() != 0){
                    for (auto el = jobMsgsToDo.begin(); el != jobMsgsToDo.end(); el++){
                        if ((*el).getJob().getId() == clientAnswerMsg->getJobID()){
                            EV << "Found a job in my to do list! The job has id " + std::to_string((*el).getJob().getId()) + "\n";
                            int temp = clientAnswerMsg->getJobID();
                            lostjobmsg->setJobId(temp);
                            lostjobmsg->setAssigned(true);
                        }
                    }
                }

                if (jobMsgsDone.size() != 0) {
                    std::list<JobReturnValueMsg> tmpList;

                    for (auto it = jobMsgsDone.begin(); it != jobMsgsDone.end(); it++) {
                        if (clientAnswerMsg->getClientIndex() == (*it).getDestination() && (*it).getJobId() == clientAnswerMsg->getJobID()) {


                            lostjobmsg->setJobId((*it).getJobId());
                            lostjobmsg->setAssigned(true);
                            EV << "\nFOUND!! The job with id " + std::to_string((*it).getJobId()) + " is going finally back to client["
                            + std::to_string(clientAnswerMsg->getClientIndex()) + "] \n\n";

                            JobReturnValueMsg *forwardAgainMsg = &(*it);
                            for (cModule::GateIterator i(this); !i.end(); i++) {
                                cGate *gate = *i;
                                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                                std::string cl = "client";

                                // to send the msg only to the client chosen, not the exec with same index
                                if (h == (*it).getDestination() && name == cl) {
                                    EV << "Answering to the client[" + std::to_string((*it).getDestination()) + "] with the return value of the jobID: [" + std::to_string((*it).getJobId()) + "]\n";

                                    send(forwardAgainMsg->dup(), gate->getName(), gate->getIndex());
                                }
                            }
                        }
                        tmpList.push_back(*it);
                    }
                    jobMsgsDone = tmpList;
                } else {
                    EV << "The list of jobs done is empty!!!\n";
                }

                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string cl = "client";

                    // to send the msg only to the client chosen, not the exec with same index
                    if (h == clientAnswerMsg->getClientIndex() && name == cl) {
                        EV << "Answering to the client[" + std::to_string(h) + "] with the jobmsg of the jobID: [" + std::to_string((clientAnswerMsg)->getJobID())
                        + "] ... assigned is " + std::to_string(lostjobmsg->getAssigned()) + "\n";


                        send(lostjobmsg, gate->getName(), gate->getIndex());
                    }
                }
            }
        }

        else if (ping != nullptr){
            int clientId = ping->getClientIndex();

            Ping *pong = new Ping("pong");
            pong->setClientIndex(clientId);
            pong->setExecIndex(this->getIndex());
            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string cl = "client";
                // To send the msg only to the client chosen, not the exec with same index
                if (h == ping->getClientIndex() && name == cl) {
                    EV << "\n##################### \n";
                    EV << "... answering to the client[" + std::to_string(pong->getClientIndex()) + "]";
                    EV << "\n##################### \n";

                    send(pong, gate->getName(), gate->getIndex());
                }
            }
        }

        // #####################################################################################################################################################################################

        else if (collectStatusMsg != nullptr) {
            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received the collectStatusMsg from executor[" + std::to_string(collectStatusMsg->getSource()) + "]: \n";

            jobsToDoVector.record(jobMsgsToDo.size());
            jobsToDoStats.collect(jobMsgsToDo.size());

            StatusMsg *status = new StatusMsg("Response");
            status->setSource(collectStatusMsg->getDestination());
            status->setJobs(jobMsgsToDo.size());
            status->setClientID(collectStatusMsg->getClientID());
            status->setJob(collectStatusMsg->getJob());

            for (cModule::GateIterator i(this); !i.end(); i++) {
                cGate *gate = *i;
                int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                std::string ex = "executor";

                if (h == collectStatusMsg->getSource() && name == ex) {
                    EV << "Answering to executor[" + std::to_string(collectStatusMsg->getSource()) + "] with my lenght: " + std::to_string(status->getJobs())
                    + " regarding the jobid: " + std::to_string(collectStatusMsg->getJob().getId()) + "\n";

                    status->setDestination(collectStatusMsg->getSource());
                    send(status, gate->getName(), gate->getIndex());
                }
            }
        }

        else if (statusMsg != nullptr) {
            EV << "Here is the executor[" + std::to_string(this->getIndex()) + "]: I received the status from executor[" + std::to_string(statusMsg->getSource()) + "]: \n";

            // Filling the info list
            infoExec values;
            values.numJob = statusMsg->getJobs();
            values.execIndex = statusMsg->getSource();
            values.job = statusMsg->getJob();
            values.clientId = statusMsg->getClientID();
            infoExecList.push_back(values);

            EV << "The collection of the executors status is now long " + std::to_string(infoExecList.size()) + ". \nI added the following element: \n";
            EV << "JobsNumber: " + std::to_string(values.numJob);
            EV << ", ExecutorIndex: " + std::to_string(values.execIndex) + "\n";
        }

        else if(msg == collectionTimeout){
            Job j = infoExecList[0].job;
            int cId = infoExecList[0].clientId;

            int distriIndex = infoExecList[0].execIndex;

            // To avoid to select always the executor with the lowest index
            std::random_shuffle(infoExecList.begin(), infoExecList.end());

            int min = infoExecList[0].numJob;
            int index = infoExecList[0].execIndex;
            for (int i = 1; i < infoExecList.size(); i++) {
                if (infoExecList[i].numJob < min) {
                    min = infoExecList[i].numJob;
                    index = infoExecList[i].execIndex;
                }
            }

            infoExecList.clear();
            EV << "-------------\nThe laziest is: " + std::to_string(index) + " (with a number of jobs: " + std::to_string(min) + ")\n-------------\n";

            char msgname[20];
            JobMsg *forwardmsg = new JobMsg(msgname);
            forwardmsg->setSource(this->getIndex()); //elis avevi messo getID... ma da quanto ho capito bisogna usare index!
            forwardmsg->setJob(j);
            forwardmsg->setAssigned(TRUE);
            forwardmsg->setClientID(cId); //importante! Qui ci dovrebbe essere l'id da usare quando ricevo il job da fare

            // Non ho scelto che sono io il più lazy, quindi posso mandare il messaggio in uscita tranquillamente
            if (index != this->getIndex()) {
                // cycle to recover the id of the laziest executor
                for (cModule::GateIterator i(this); !i.end(); i++) {
                    cGate *gate = *i;
                    int genericIndex = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                    const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                    std::string ex = "executor";

                    if (genericIndex == index && name == ex && index != this->getIndex()) {
                        forwardmsg->setDestination(index);


                        send(forwardmsg, gate->getName(), gate->getIndex());

                        EV << "... the job is finally sent to executor[" + std::to_string( forwardmsg->getDestination())
                        + "] (the job has clientID: " + std::to_string( forwardmsg->getClientID()) + ", and jobID: " + std::to_string(j.getId()) + ")\n\n";
                    }
                }
            } else { // Oh, sono io il più pigro!! Devo darmi il job
                if (jobMsgsToDo.size() == 0) {
                    EV << "SONO IL PIU PIGRO E NON HO NESSUN JOB DA FARE, MO LO AGGIUNGO E LO FACCIO!";
                    forwardmsg->setAssigned(TRUE);
                    jobMsgsToDo.push_back(*forwardmsg);

                    jobsToDoVector.record(jobMsgsToDo.size());
                    jobsToDoStats.collect(jobMsgsToDo.size());

                    // Inform the client that I'm the executor of this job
                    JobReturnValueMsg *jrv = new JobReturnValueMsg("Executing");
                    jrv->setSource(this->getIndex());
                    jrv->setDestination(cId);
                    jrv->setJobId(j.getId());
                    jrv->setExecIndex(this->getIndex());
                    jrv->setDistrIndex(distriIndex);
                    jrv->setReturnValue(false);

                    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string cl = "client";

                        // To send the msg only to the client chosen, not the exec with same index
                        if (h == cId && name == cl) {
                            EV << "\n##################### \n";
                            EV << "... answering to the client[" + std::to_string(cId)
                            + "] to inform that I am executing the jobID: " + std::to_string(jrv->getJobId());
                            EV << "\n##################### \n";

                            send(jrv, gate->getName(), gate->getIndex());
                        }
                    }

                    executeJob(forwardmsg);

                } else { // Altrimenti lo aggiungo e basta alla lista da fare
                    EV << "ASPETTA UN ATTIMO, HO ALCUNE COSE DA FARE PRIMA MA POI MI OCCUPO DI TE!";
                    forwardmsg->setAssigned(TRUE);
                    jobMsgsToDo.push_back(*forwardmsg);

                    jobsToDoVector.record(jobMsgsToDo.size());
                    jobsToDoStats.collect(jobMsgsToDo.size());

                    // Inform the client that I'm the executor of this job
                    JobReturnValueMsg *jrv = new JobReturnValueMsg("In execution list");
                    jrv->setSource(this->getIndex());
                    jrv->setDestination(cId);
                    jrv->setJobId(j.getId());
                    jrv->setExecIndex(this->getIndex());
                    jrv->setDistrIndex(distriIndex);
                    jrv->setReturnValue(false);

                    for (cModule::GateIterator i(this); !i.end(); i++) {
                        cGate *gate = *i;
                        int h = (gate)->getPathEndGate()->getOwnerModule()->getIndex();
                        const char *name = (gate)->getPathEndGate()->getOwnerModule()->getName();
                        std::string cl = "client";

                        // To send the msg only to the client chosen, not the exec with same index
                        if (h == cId && name == cl) {
                            EV << "\n##################### \n";
                            EV << "... answering to the client[" + std::to_string(cId)
                            + "] to inform that I will execute the jobID: " + std::to_string(jrv->getJobId()) + " (in fact execIndex = " + std::to_string(jrv->getExecIndex()) + ")";
                            EV << "\n##################### \n";


                            send(jrv, gate->getName(), gate->getIndex());
                        }
                    }
                }
            }

            jobMsgsWaitingList.pop_front();
            distributing_job = false;

            double randomStartingDelay = uniform(0.001, 0.003);
            notMoreDistributing = new cMessage("notMoreDistributing");
            EV << "... scheduling a \"notMoreDistributing\" self message...\n\n";
            scheduleAt(simTime() + randomStartingDelay, notMoreDistributing);
        }
    }
}

void Executor::finish() {
    jobsToDoStats.recordAs("jobs to do");
}
