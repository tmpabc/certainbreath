
#ifndef CERTAINBREATH_DATA_HANDLING_TIMERS_H
#define CERTAINBREATH_DATA_HANDLING_TIMERS_H

#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>

#include "CppTimer.h"
#include "data_structures.h"
#include "web_comm.h"

using namespace std;

/*
 * Timer used to transfer data to the web server through the websocket connection.
 */
class DataTransferTimer: public CppTimer {

    string url;
    mutex * datalock;
    vector<Reading> * dataBuffer;

public:
    DataTransferTimer(string url, mutex * datalock, vector<Reading> * dataBuffer) {
        this->url = url;
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
    }

    void timerEvent() {
        datalock->lock();

        string toSend = "[";
        bool comma = false;
        for (auto &datum : *dataBuffer) {
            if (!datum.status.sent) {
                if (comma) toSend+= ","; else comma = true; // Skip the first comma.
                toSend += datum.toJson();
                datum.status.sent = true;
            }
        }

        toSend += "]";
        datalock->unlock();

        // Only send if there's data.
        if (toSend.length() > 2) {
            //cout << "Sending" << "\n";
            sendData(toSend, url);
        }

    }
};

/*
 * DataBuffer clean-up timer.
 */
class DataCleanUpTimer: public CppTimer {
    ReadingStatus cleanable;
    vector<Reading> *dataBuffer;
    mutex *datalock;

public:
    /*
     * The cleanable parameter is used to determine the status a Reading needs to achieve to be cleaned from tha dataBuffer.
     */
    DataCleanUpTimer(ReadingStatus cleanable, mutex * datalock, vector<Reading> * dataBuffer) {
        this->cleanable = cleanable;
        this->dataBuffer = dataBuffer;
        this->datalock = datalock;
    }

    void timerEvent() {
        datalock->lock();
        auto it = dataBuffer->begin();
        while (it != dataBuffer->end()) {
            if (it->status == cleanable) {
                it = dataBuffer->erase(it);
            } else it++;
        }
        datalock->unlock();
    }
};

/*
 * Timer used to print sampled readings to the console.
 */
class DataPrintingTimer: public CppTimer {

    map<string, Reading>  latest;
    vector<string> keys;
    mutex * datalock;
    vector<Reading> * dataBuffer;

public:
    DataPrintingTimer(mutex * datalock, vector<Reading> * dataBuffer) {
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
    }

    void timerEvent() {
        datalock->lock();

        string toPrint = "";
        for (Reading datum : *dataBuffer) {
            if(!datum.status.printed) {
                latest[datum.type] = datum;
            }
            if (find(keys.begin(), keys.end(), datum.type) == keys.end()) {
                keys.push_back(datum.type);
            }
            datum.status.printed = true;
        }
        datalock->unlock();

        // Clean keys that are older than 600ms. Need this to have up to date information on alerts.
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        auto it = keys.begin();
        while(it != keys.end()) {
            if(latest[*it].time < now - 600) {
                it = keys.erase(it);
            } else it++;
        }

        for(auto &key: keys) {
            cout << key << ": " << setw(10) << latest[key].value << " | ";
        }
        cout << "\n";
    }
};


class DataRecordingTimer: public CppTimer {

    ofstream out;
    mutex * datalock;
    vector<Reading> * dataBuffer;

public:
    DataRecordingTimer(const string &filename, mutex * datalock, vector<Reading> * dataBuffer) {
        out.open(filename);
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
    }
    void timerEvent() {
        datalock->lock();

        for (auto &datum : *dataBuffer) {
            if (!datum.status.recorded) {
                out << datum.toJson() + "\n";
                datum.status.recorded = true;
            }
        }
        datalock->unlock();
    }
};

#endif //CERTAINBREATH_DATA_HANDLING_TIMERS_H
