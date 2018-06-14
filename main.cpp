/* COMP 304 Project 2 : MetroSimulation with POSIX Threads
 * -------------------------------
 * File: main.cpp
 * -------------------------------
 * Creators: Mert Atila Sakaogullari, Pinar Topcam
 */
#include <iostream>
#include <queue>
#include <fstream>

#define NUM_THREADS 5  // A, B, E, F, ControlCenter

using namespace std;

struct metroTime{ //Time structure used in this project
    int hour;
    int minute;
    int second;
};

struct metroTrain { //Train structure used in this project
    int ID;
    int length;
    char startPoint;
    char endPoint;
    metroTime startTime;
    metroTime endTime;
};

struct metroEvent{ //Event structure used in this project
    string eventName;
    metroTime eventTime;
    int trainID;
    queue<metroTrain> waitingTrains[4];
    int clearTime;
};

static double lengthProb = 0.7; //Probability of the newly created train being 100m long
static double startPointProb = 0.5; //Probability used to decide the startPoint of a train upon creation
static int threadA = 0;
static int threadB = 1;
static int threadE = 2;
static int threadF = 3;
static int threadControlCenter = 4;

double creationProb = 0.5; //Probability used upon initialization of trains, can be overwritten
int simTime = 60; //total simulation time in seconds, default is set, it can be overwritten
int trainIDCounter = 100;  //Train ID's begin from 100, just a matter of preference
int starvationFlag = 0; //Flag to raise when there is more than 10 trains on railways, except the tunnel
int alreadyBrokendown = 0; //Keeping track if the train got broken down or not
int cleareanceTime = 0; //Keeping the time required to clear all the railways

pthread_mutex_t  mtx = PTHREAD_MUTEX_INITIALIZER; //lock for the starvation condition as it is accessed and written by threads to prevent race condition
pthread_mutex_t  freshtx = PTHREAD_MUTEX_INITIALIZER; //lock for the freshTrains queue to provide stability, which is accessed by all the threads

metroTime tunnelOccupied; //semaphore of the tunnel's occupation, is the time when it will be available
metroTime initialTime; //time when the simulation begins
metroTime finitoTime; //time when the simulation ends

queue<metroTrain> trainQueue[4]; //queue of trains at C or D, index shows the start point
queue<metroTrain> freshTrains[4]; //queue of trains at A,B,E,F
queue<metroTrain> finitoTrains; //trains passed the tunnel
queue<metroEvent> eventQueue; //queue of events occurred

bool timeLessThan(tm* now, metroTime inputTime);
void createTrain(long threadID);
void logEverything();
void *trainAssigner(void *threadID);
void *controlCenter(void *threadID);
void calculateTimeInterval();
void createEvent(string name, int givenID, tm* now);
void adjustTime(metroTime &inputTime);
metroTime letTrainPass(tm* now);

int main(int argc, char* argv[]) {
    if(argc == 2){
        creationProb = atof(argv[1]); //gets user given probability
        cout <<  "No input for simulation time" << endl << "Assuming it's 60 seconds" << endl
             << "-----------------------------" << endl;
    } else if(argc == 4){
        creationProb = atof(argv[1]); //gets user given probability
        string flag(argv[2]); //gets user given flag
        if(flag == "-s"){
            simTime = atoi(argv[3]); //gets user given simulation time
        } else{
            cout << "INVALID INPUT" << endl;
            return 0;
        }
    } else if(argc == 1){
        cout <<  "No input for train creation probability and simulation time" << endl
             << "Assuming p = 0.5, simulation time is 60 seconds" << endl
             << "-----------------------------" << endl;
    } else{
        cout << "INVALID INPUT" << endl;
        return 0;
    }
    pthread_t threads[NUM_THREADS];
    calculateTimeInterval(); //marks when to begin and when to end the simulation
    int rc;
    int i;
    for(i = 0; i < NUM_THREADS; i++ ) { //creates all the threads
        cout << "Creating thread : " << i << endl;
        if(i != threadControlCenter) { //creates the train assigning threads
            rc = pthread_create(&threads[i], NULL, trainAssigner, (void *) i);
        } else { //creates the control center thread
            rc = pthread_create(&threads[i], NULL, controlCenter, (void *) i);
        }
        if (rc) {//if the thread couldn't be created, gives the error
            cout << "Error:unable to create thread," << rc << endl;
            exit(-1);
        }
    }
    cout << "-----------------------------" << endl;
    for(i = 0; i<NUM_THREADS; i++){
        pthread_join(threads[i], NULL); //waits until all the threads are finished
    }
    pthread_mutex_destroy(&mtx);
    pthread_mutex_destroy(&freshtx);
    logEverything(); //creates the log files
    return 0;
}

/* FUNCTION : logEverything
 * -------------------------------------------
 * This function uses the finitioTrains queue and creates the train.log file and then uses the eventQueue to create the
 * control-center.log fil just as it is requested in the project pdf file.
 */
void logEverything(){

    //first creates the train.log
    ofstream trainLog;
    trainLog.open ("train.log");
    trainLog << " Train ID  Starting Point  Destination Point  Length(m)  Arrival Time    Departure Time \n--------- "
             << " --------------  -----------------  ---------  ------------  -----------------\n";
    while(!(finitoTrains.empty())){
        metroTrain tempTrain = finitoTrains.front();
        finitoTrains.pop();
        trainLog << "   " << tempTrain.ID << "           "<< tempTrain.startPoint << "                 "
                 << tempTrain.endPoint << "             " << tempTrain.length << "       "
                 << tempTrain.startTime.hour << ":" << tempTrain.startTime.minute << ":";
        if(tempTrain.startTime.second >= 10){
            trainLog << tempTrain.startTime.second;
        } else {
            trainLog << tempTrain.startTime.second << "0";
        }
        trainLog << "        " << tempTrain.endTime.hour << ":" << tempTrain.endTime.minute << ":";
        if(tempTrain.endTime.second >= 10){
            trainLog << tempTrain.endTime.second;
        } else {
            trainLog << tempTrain.endTime.second << "0";
        }
        trainLog << "    \n";
    }
    trainLog.close();

    //then creates control-center.log
    ofstream centerLog;
    centerLog.open ("control-center.log");
    centerLog << "       Event         Event Time    Train ID             Trains Waiting Pasaj           \n"
              << "------------------  ------------  ----------  -----------------------------------------\n";

    string clearCheck = "Tunnel Cleared";
    while(!(eventQueue.empty())){
        metroEvent tempEvent = eventQueue.front();
        eventQueue.pop();
        centerLog << " " << tempEvent.eventName << "      ";
        for(int i =0; i<clearCheck.size()-tempEvent.eventName.size(); i++){
            centerLog << " ";
        }
        centerLog << " " << tempEvent.eventTime.hour << ":"
                  << tempEvent.eventTime.minute << ":";
        if(tempEvent.eventTime.second >= 10){
            centerLog << tempEvent.eventTime.second;
        } else {
            centerLog << tempEvent.eventTime.second << "0";
        }
        centerLog << "      " << "  ";
        if(tempEvent.trainID == -1){
            centerLog << " # "<< "      ";
        } else{
            centerLog << tempEvent.trainID<< "      ";
        }
        if(tempEvent.eventName == clearCheck){
            centerLog << "# Time to Clear: " << tempEvent.clearTime << " sec\n";
        } else{
            vector<int> trainIDs;
            for(int i = 0; i < 4; i++){
                if(!(tempEvent.waitingTrains[i].empty())){
                    for(int j=0; j< tempEvent.waitingTrains[i].size(); j++){
                        metroTrain tempTrain = tempEvent.waitingTrains[i].front();
                        tempEvent.waitingTrains[i].pop();
                        trainIDs.push_back(tempTrain.ID);
                    }
                }
            }
            if(trainIDs.size() == 0){
                centerLog << "\n";
            } else{
                sort(trainIDs.begin(), trainIDs.end());
                for(int i = 0; i < trainIDs.size(); i++){
                    centerLog << trainIDs[i];
                    if(i!= trainIDs.size()-1){
                        centerLog << " ";
                    } else {
                        centerLog << "\n";
                    }
                }
            }
        }
    }
    centerLog.close();
}

/* FUNCTION : timeLessThan
 * --------------------------------------------
 * This function accepts a tm pointer and an input of metroTime struct and then returns true if
 * the given pointer's time is earlier than the input of metroTime otherwise it returns false
 */
bool timeLessThan(tm* now, metroTime inputTime){
    if(now->tm_hour > inputTime.hour){
        return false;
    } else if(now->tm_hour < inputTime.hour) {
        return true;
    } else{
        if(now->tm_min > inputTime.minute){
            return false;
        } else if(now->tm_min < inputTime.minute){
            return true;
        } else{
            if(now->tm_sec > inputTime.second){
                return false;
            } else if(now->tm_sec < inputTime.second){
                return true;
            }
        }
    }
    return false;
}

/* FUNCTION : createTrain
 * --------------------------------------------
 * This function accepts a threadID as input and gets called by the train assigning threads to do the main job.
 * Logic behind this function is first using the creationProb, whether a train will be generated or not is decided and
 * also the endpoint according to the threadID. First the creation time is marked, the length is decided and then
 * the startPoint is calculated. At the end, the train is added to the freshQueue, according to it's startPoint.
 */
void createTrain(long threadID){

    double randTrain =  ((double) rand() / (RAND_MAX));

    if((threadID != 1 && randTrain <= creationProb) ||( threadID == 1 && randTrain > creationProb)){

        time_t t = std::time(0);   // get time now
        tm* now = std::localtime(&t);

        metroTrain tempTrain;
        tempTrain.ID = trainIDCounter++;

        metroTime creationTime;
        creationTime.hour = now->tm_hour;
        creationTime.minute = now->tm_min;
        creationTime.second = now->tm_sec;
        tempTrain.startTime = creationTime;

        double randLength =  ((double) rand() / (RAND_MAX));
        if(randLength <= lengthProb){
            tempTrain.length = 100;
        } else {
            tempTrain.length = 200;
        }
        double randStartPoint = ((double) rand() / (RAND_MAX));
        pthread_mutex_lock(&freshtx);
        if(threadID == threadA){ //coming to A
            tempTrain.endPoint = 'A';
            if(randStartPoint <= startPointProb){
                tempTrain.startPoint = 'E';
                freshTrains[2].push(tempTrain);
            } else{
                tempTrain.startPoint = 'F';
                freshTrains[3].push(tempTrain);
            }
        } else if(threadID == threadB){ // coming to B
            tempTrain.endPoint = 'B';
            if(randStartPoint <= startPointProb){
                tempTrain.startPoint = 'E';
                freshTrains[2].push(tempTrain);
            } else{
                tempTrain.startPoint = 'F';
                freshTrains[3].push(tempTrain);
            }
        } else if(threadID == threadE){ //coming to E
            tempTrain.endPoint = 'E';
            if(randStartPoint <= startPointProb){
                tempTrain.startPoint = 'A';
                freshTrains[0].push(tempTrain);
            } else{
                tempTrain.startPoint = 'B';
                freshTrains[1].push(tempTrain);
            }
        } else if(threadID == threadF){ //coming to F
            tempTrain.endPoint = 'F';
            if(randStartPoint <= startPointProb){
                tempTrain.startPoint = 'A';
                freshTrains[0].push(tempTrain);
            } else{
                tempTrain.startPoint = 'B';
                freshTrains[1].push(tempTrain);
            }
        }
        pthread_mutex_unlock(&freshtx);
    }
}

/* FUNCTION : trainAssigner
 * --------------------------------------------
 * This function accepts a threadID as input and it is the train assigning threads' pointed function upon creation.
 * Using this function, each train assigning thread first marks the time it began and then will continue until the
 * simulation is over. Each time, it first locks the mutex mtx to check the starvation. If there is a starvation, it
 * simply doesn't assign any train and if there is no starvation is calls the createTrain function. Then releases the
 * mutex and wait till the beginning of the next second.
 */
void *trainAssigner(void *threadID) {

    time_t t = std::time(0);   // get time upon creation
    tm* now = std::localtime(&t);

    while(timeLessThan(now, finitoTime)){ // thread keeps working till the end of the simulation

        int tempTotal = now->tm_sec + now->tm_min*60 + now->tm_hour * 24 * 60; //used to work once in a second
        int tempBound = tempTotal + 1;

        pthread_mutex_lock(&mtx); // locks mutex to check the starvationflag
        if(starvationFlag == 0) {
            createTrain((long) threadID);
        }
        pthread_mutex_unlock(&mtx); // releases lock after checking the flag


        while(tempTotal < tempBound){ //waits until next seconds
            t = std::time(0);
            now = std::localtime(&t);
            tempTotal = now->tm_sec + now->tm_min*60 + now->tm_hour * 24 * 60;
        }
    }
    pthread_exit(NULL);
}

/* FUNCTION : letTrainPass
 * --------------------------------------------
 * This function accepts a time pointer to the time it is called and returns a metroTime, showing the time when
 * the tunnel will be available again. First it checks the number of trains waiting with regarding to their startPoints,
 * and then if there is no train, it returns a metroTime with a second of -1, which is the indicator that the tunnel
 * is still available, or it decides which train should pass. After that decision, it calculates the endTime of the train,
 * moves is to the finitoQueue and returns the metroTime upto when the tunnel is occupied.
 */
metroTime letTrainPass(tm* now){

    long lenA = trainQueue[0].size();
    long lenB = trainQueue[1].size();
    long lenE = trainQueue[2].size();
    long lenF = trainQueue[3].size();

    if(lenA + lenB + lenE + lenF == 0 ){
        metroTime nullTime;
        nullTime.minute = 0;
        nullTime.hour = 0;
        nullTime.second = -1;
        return nullTime;
    }

    int nextTrainIndex;
    if(lenF > lenE && lenF > lenB && lenF > lenA){ // F train's turn
        nextTrainIndex = 3;
    } else if (lenE > lenA && lenE > lenB){ // E train's turn
        nextTrainIndex = 2;
    } else if(lenB > lenA){ //B train's turn
        nextTrainIndex = 1;
    } else{ // A's turn
        nextTrainIndex = 0;
    }

    metroTrain nextTrain = trainQueue[nextTrainIndex].front();
    trainQueue[nextTrainIndex].pop();

    createEvent("TunnelPassing", nextTrain.ID, now);

    int occupationTime = (nextTrain.length + 100 ) / 100;

    nextTrain.endTime.second = now->tm_sec + 1 + occupationTime + 1;
    nextTrain.endTime.minute = now->tm_min;
    nextTrain.endTime.hour = now->tm_hour;

    adjustTime(nextTrain.endTime);
    finitoTrains.push(nextTrain);

    metroTime occupationFinito;
    occupationFinito.second = now->tm_sec + occupationTime;
    occupationFinito.minute = now->tm_min;
    occupationFinito.hour = now->tm_hour;

    adjustTime(occupationFinito);
    return occupationFinito;
}

/* FUNCTION : createEvent
 * --------------------------------------------
 * This function is to keep an extra queue so that it can be used to give the control-center.log output file. It
 * accepts the event's name, train's ID which the event happened to and then then time it occurred. It copies the
 * current trainQueue to the event's queue which will be processedd later.
 */
void createEvent(string name, int givenID, tm* now){
    metroEvent tempEvent;
    tempEvent.eventName = name;
    tempEvent.trainID = givenID;
    tempEvent.eventTime.hour = now->tm_hour;
    tempEvent.eventTime.minute = now->tm_hour;
    tempEvent.eventTime.second = now->tm_sec;
    tempEvent.waitingTrains[0] = trainQueue[0];
    tempEvent.waitingTrains[1] = trainQueue[1];
    tempEvent.waitingTrains[2] = trainQueue[2];
    tempEvent.waitingTrains[3] = trainQueue[3];
    tempEvent.clearTime = cleareanceTime;
    eventQueue.push(tempEvent);
}

/* FUNCTION : controlCenter
 * --------------------------------------------
 * This function is the control center thread's function, first it checks the time of creation and then runs till
 * the end of the simulation. To prevent the race condition, it locks the mutex and either updates the starvation flag
 * or the clearenceTime. Then according to the occupation of the tunnel, if the tunnel is available it decides which
 * trains should pass from the tunnel, if it is occupied checks the probability of that tunnel passing train to be
 * broken down or not, or updates the occupation of the tunnel.
 */
void *controlCenter(void *threadID) {

    time_t t = std::time(0);   // get time now
    tm* now = std::localtime(&t);

    while(timeLessThan(now, finitoTime)) { //works till the end of the simulation time
        int tempTotal = now->tm_sec + now->tm_min * 60 + now->tm_hour * 24 * 60;
        int tempBound = tempTotal + 1;

        pthread_mutex_lock(&mtx); //updates starvation
        pthread_mutex_lock(&freshtx);
        long totalTrains = freshTrains[0].size() + freshTrains[1].size() + freshTrains[2].size()
                           + freshTrains[3].size() + trainQueue[0].size() + trainQueue[1].size()
                           + trainQueue[2].size() + trainQueue[3].size();
        pthread_mutex_unlock(&freshtx);
        if (starvationFlag == 1 && totalTrains == 0) {
            starvationFlag = 0;
            createEvent("Tunnel Cleared", -1, now);
        } else if(totalTrains > 10 && starvationFlag == 0){
            starvationFlag = 1;
            cleareanceTime = 0;
            createEvent("Starvation",-1, now);
        } else if(starvationFlag == 1){
            cleareanceTime++;
        }
        pthread_mutex_unlock(&mtx);

        if(tunnelOccupied.second == -1){ //if the tunnel is available:
            tunnelOccupied = letTrainPass(now);
        } else if (tunnelOccupied.hour <= now->tm_hour && tunnelOccupied.minute <= now->tm_min &&
                   tunnelOccupied.second <= now->tm_sec){ //the tunnel just got available
            tunnelOccupied.second = -1;
            alreadyBrokendown = 0;
        } else { //the tunnel is not available
            if(alreadyBrokendown == 0){ //the train in the tunnel can break down anytime
                double randBreakDown =  ((double) rand() / (RAND_MAX));
                if(randBreakDown <= 0.1){ // Train got broken down
                    finitoTrains.back().endTime.second += 4; //4 seconds delay for repair
                    adjustTime(finitoTrains.back().endTime);
                    tunnelOccupied.second += 4;
                    adjustTime(tunnelOccupied);
                    alreadyBrokendown = 1;

                    createEvent("BreakDown", finitoTrains.back().ID, now);
                }
            }
        }

        for(int i = 0; i < 4; i++) { //traverse the freshTrains and update required trains to move to C or D
            pthread_mutex_lock(&freshtx); //lock because freshTrains queue should now change now
            if (!freshTrains[i].empty()) {
                metroTrain tempTrain = freshTrains[i].front();
                int secPassed = now->tm_sec + now->tm_min * 60 + now->tm_hour * 24;
                int trainStartTime = tempTrain.startTime.second + tempTrain.startTime.minute * 60 +
                                     tempTrain.startTime.hour * 24 * 60;
                if (trainStartTime + 1 >= secPassed) { //1 second passed after creation so train should move to C or D
                    trainQueue[i].push(tempTrain);
                    freshTrains[i].pop();
                    i--; //to check the next train in the freshTrains queue
                }
            }
            pthread_mutex_unlock(&freshtx);
        }

        while(tempTotal < tempBound){ //waits for the next second and doesn't work more than once in a second
            t = std::time(0);
            now = std::localtime(&t);
            tempTotal = now->tm_sec + now->tm_min*60 + now->tm_hour * 24 * 60;
        }
    }
    pthread_exit(NULL);
}

/* FUNCTION : calculateTimeInterval
 * --------------------------------------------
 * This function gets the time when the simulation began and calculates when it will end
 */
void calculateTimeInterval(){

    time_t t = std::time(0);   // get time now
    tm* now = std::localtime(&t);

    cout << "Initial Time => "
         << now->tm_hour<< ':'
         << (now->tm_min) << ':'
         <<  now->tm_sec
         << "\n";

    initialTime.hour = now->tm_hour;
    initialTime.minute = now->tm_min;
    initialTime.second = now->tm_sec;

    finitoTime.second = initialTime.second + simTime;
    finitoTime.minute = initialTime.minute;
    finitoTime.hour = initialTime.hour;

    adjustTime(finitoTime);

    cout << "Finito time => "
         << finitoTime.hour << ':'
         << finitoTime.minute << ':'
         <<  finitoTime.second
         << "\n";

    cout << "-----------------------------" << endl;
}

/* FUNCTION : adjustTime
 * --------------------------------------------
 * This function accepts a type of metroTime input and then updates it.
 */
void adjustTime(metroTime &inputTime){
    while(inputTime.second > 59 || inputTime.minute > 59 || inputTime.hour > 23){
        if(inputTime.second > 59){
            inputTime.second -= 60;
            inputTime.minute++;
        } else if(inputTime.minute > 59){
            inputTime.minute -= 60;
            inputTime.hour++;
        } else {
            inputTime.hour -= 24;
        }
    }
}

