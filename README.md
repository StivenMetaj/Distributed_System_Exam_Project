# Distributed Job Scheduling - Distributed System Project
A project related to the Distributed Systems exam at Politecnico di Milano about "Distributed Job Scheduling". 
***
The project description provided by the professor is the following:

> Implement an infrastructure to manage jobs submitted to a cluster of Executors. Each client may submit a job to any of the executors receiving a job id as a return value. Through such job id, clients may check (contacting the same executor they submitted the job to) if the job has been executed and may retrieve back the results produced by the job.

> Executors communicate and coordinate among themselves in order to share load such that at each time every Executor is running the same number of jobs (or a number as close as possible to that). Assume links are reliable but processes (i.e., Executors) may fail (and resume back, re-joining the system immediately after).

> Choose the strategy you find more appropriate to organize communication and coordination. Use stable storage to cope with failures of Executors.

> Implement the system in Java (or any other language you choose) only using basic communication facilities (i.e., sockets and RMI, in case of Java). Alternatively, implement the system in OMNeT++, using an appropriate, abstract model for the system (including the jobs themselves).
***
We decided to perform the simulation on OMNeT++, a framework that helps in simulating any sort of network or distributed protocol.

The solution proposed is a distributed protocol composed by *client* nodes that have some jobs to be executed and by *executors* nodes that receive the jobs and communicate with each other in order to balance the work for every node. Because of the possible failures problem, we implemented also a ping mechanism to discover these failures and to have the possibility to perform different behaviors depending on the situation.

#
To easy understand the code keep in mind that in a OMNeT++ protocol we have to define different types of messages and to code a switch case in order to perform different operations when receiving different types of messages.

#
In the repository you can also find the pdf of the presentation of the final version of the project. Note that in the presentation there were some animations that are not shown in the pdf.