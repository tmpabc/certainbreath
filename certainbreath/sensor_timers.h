
#ifndef CERTAINBREATH_SENSOR_TIMERS_H
#define CERTAINBREATH_SENSOR_TIMERS_H

#include <mutex>
#include <vector>

#include "CppTimer.h"
#include "data_structures.h"
#include "utils.h"

using namespace std;


/*
 * Used to sample temperature Readings.
 */
class TempSensorTimer: public CppTimer {

    vector<int> pins, pinVals;
    string type;
    mutex *pinslock, *datalock;
    vector<Reading> *dataBuffer;
    float ampGain;

public:
    TempSensorTimer(mutex *pinslock, mutex *datalock, vector<Reading> *dataBuffer,
                    const vector<int> &pins, const vector<int> &pinVals, float ampGain=1.5, string type="Temperature") {
        this->pins=pins;
        this->pinVals=pinVals;
        this->type = type;
        this->pinslock = pinslock;
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
        this->ampGain = ampGain;
    }


    void timerEvent() {
        pinslock->lock();
        writePins(pins, pinVals);
        Reading r = getForce(getVoltage(), ampGain);
        r.type = this->type;
        pinslock->unlock();
        datalock->lock();
        dataBuffer->push_back(r);
        datalock->unlock();
    }

};

/*
 * Used to sample pressure Readings.
 */
class PressureSensorTimer: public CppTimer {

    vector<int> pins, pinVals;
    string type;
    mutex *pinslock, *datalock;
    vector<Reading> *dataBuffer;
    float ampGain;


public:
    PressureSensorTimer(mutex *pinslock, mutex *datalock, vector<Reading> *dataBuffer,
                        const vector<int> &pins, const vector<int> &pinVals, float ampGain=1.5,
                        const string &type="Pressure") {
        this->pins=pins;
        this->pinVals=pinVals;
        this->type = type;
        this->pinslock = pinslock;
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
        this->ampGain = ampGain;


    }

    void timerEvent() {
        pinslock->lock();
        writePins(pins, pinVals);
        //Reading r = getForce(getVoltage(), ampGain);
        Reading r = getVoltage();
        r.type = this->type;
        pinslock->unlock();
        datalock->lock();
        dataBuffer->push_back(r);
        datalock->unlock();
    }
};


/*
 * Used to sample fake Readings (for testing purposes).
 */
class FakeSensorTimer: public CppTimer {

    mutex *datalock;
    vector<Reading> * dataBuffer;
    string type;

public:
    FakeSensorTimer(mutex * datalock, vector<Reading> * dataBuffer, const string &type) {
        this->type = type;
        this->dataBuffer = dataBuffer;
        this->datalock = datalock;
    }

    void timerEvent() {
        Reading r = getRandomReading();
        r.type = this->type;
        datalock->lock();
        dataBuffer->push_back(r);
        datalock->unlock();
    }
};



#endif //CERTAINBREATH_DATA_HANDLING_TIMERS_H
