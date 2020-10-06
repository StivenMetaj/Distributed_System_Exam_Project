//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package casalimetametaj_dsproject;

simple Client {
    parameters:
        @display("i=block/routing");
    gates:
        inout gate[];
}

simple Executor {
    parameters:
        @display("i=block/process");
    gates:
        inout gate[];
}

network Schedule {
    parameters:
        int clients @prompt("Number of clients") = default(1);
        int executors @prompt("Number of workers") = default(3);
        int numberOfJobsToBeDone = default(2);
        double minJobTime = default(3.0);
        double maxJobTime = default(6.0);
        double maxWaitUntilNewJob = default(3.0);
        double clientsDeadProbability = default(0.8);
        double executorsDeadProbability = default(0.001);
        double clientsMaxDeathStart = default(6);
		double clientsMaxDeathDuration = default(25);
		double executorsMaxDeathStart = default(25);
		double executorsMaxDeathDuration = default(25);
    types:
        channel C extends ned.DelayChannel {
            delay = 100ms;
        }
    submodules:
        client[clients]: Client {
            parameters:
                @display("i=,cyan");
        }
        executor[executors]: Executor {
            parameters:
                @display("i=,gold");
        }
    connections:
        for i=0..clients-1, for j=0..executors-1 {
            client[i].gate++ <--> C <--> executor[j].gate++;
        }
        for i=0..executors-1, for j=i+1..executors-1 {
            executor[i].gate++ <--> C <--> executor[j].gate++;
        }
}




//network Random
//{
//parameters:
//    int n @prompt("Number of nodes") = default(10);
//
//    volatile int posX = intuniform (0,100);
//    volatile int posY = intuniform (0,100);
//submodules:
//    node[n]: Node{
//        parameters:
//            @display("p=$posX,$posY");
//    }
//}
