#include <stdio.h>
#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <thread>
#include <vector>
#include <chrono>
#include <curl/curl.h>
#include <string.h>
#include "easywsclient.hpp"
#include "easywsclient.cpp"
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <mutex>
#include "CppTimer.h"
#include <algorithm>


using namespace std;

static const float REF_V = 3.3; // Reference voltage to the ADC.

// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
static const int CHANNEL = 0;
// SPI communication speed.
static const int SPEED = 500000;
// SPI communication buffer.
unsigned char BUFFER[2]; // 2 bytes is enough to get the 10 bits from the ADC.


static const float AMP_GAIN = 1.33;

static const string wsURL = "ws://127.0.0.1:5000/ws";
//static const string wsURL = "ws://certainbreath.herokuapp.com/ws";

using easywsclient::WebSocket;
WebSocket::pointer webSocket;

static mutex pinslock;
static mutex wslock;
static mutex datalock;

static const int MPPIN1 = 2; // Physical 13 // A1
static const int MPPIN2 = 0; // Physical 11 // A0
static const int MPPIN3 = 3; // Physical 15 // A2
static const int MPPIN4 = 7; // Physical 7  // EN

/*
 * Structure that defines the usage flags of a Reading. That is, how has the Reading been handled.
 */
struct ReadingStatus {
    bool analysed = false;
    bool sent = false;
    bool printed = false;
    bool recorded = false;

    bool operator==(const ReadingStatus& other) {
        return (
                this->analysed == other.analysed &&
                this->sent == other.sent &&
                this->printed == other.printed &&
                this->recorded == other.recorded
                );
    }

};

struct Reading {
    float value;
    long long time;
    string type;

    ReadingStatus status;

    string toJson() {
        return "{\"value\":" + to_string(value) + ", \"time\":" + to_string(time) + ", \"type\": \"" + type +"\" }";
    }

};

vector<Reading> dataBuffer; // Buffer to store the readings.


Reading getVoltage() {
    wiringPiSPIDataRW(CHANNEL, BUFFER, 2); // Read 2 bytes.

    // We take the last 5 bits from the first byte and first 5 bits from the second byte
    // according to the ADC data sheet.
    int bin = (BUFFER[0] << 3 >> 3) * 32 + (BUFFER[1] >> 3);


    float value = bin / (float)1024 * REF_V; // convert the bin number to the voltage value.
    // Get milliseconds since epoch.
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {value, time, "Voltage"};
}

/*
 * Function to get the force in grams from given by the voltage reading from a FSR sensor.
 */
Reading getForce(Reading voltage) {

    // log(F) = a*log(r) + b

    float v_in = voltage.value / AMP_GAIN;
    float r1 = 2200;

    // Obtained by 1st degree polynomial fit to log(F) vs log(r)
    float a = -1.3077116639139494;
    float b = 7.141611411251033;

    // R_fsr
    //float r = (r1 * v_in) / (3.3 - v_in);
    float r = r1 * (3.3 - v_in) / v_in;

    float F = exp(log(r) * a + b);

    //return (Reading) {F, voltage.time, "Pressure"};
    return (Reading) {F, voltage.time, "Pressure"};
}

/*
 * Function to get the temperature in Celsius given by the voltage reading from a termistor.
 */
Reading getTemperature(Reading voltage) {

    float v_in = voltage.value / AMP_GAIN;

    float r2 = 6800;
    float r0 = 10000;
    //float r_th = (r2 * v_in) / (3.3 - v_in);
    float r_th = r2 * (3.3 - v_in) / v_in;

    float t_0 = 298;
    float beta = 3977;


    float t_K = (t_0 * beta) / (t_0 * log(r_th / r0) + beta);

    float t_C = t_K - 273.15;

    return (Reading) {t_C, voltage.time, "Temperature"};
}

Reading getRandomReading() {
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {(float)(rand() % 330) / 100, time, "fake"};
}

/*
 * Used to sample pressure Readings.
 */
class PressureSensorTimer: public CppTimer {

    void timerEvent() {
        pinslock.lock();
        digitalWrite(MPPIN1, LOW);
        digitalWrite(MPPIN2, LOW);
        digitalWrite(MPPIN3, LOW);
        digitalWrite(MPPIN4, HIGH);
        Reading r = getForce(getVoltage());
        pinslock.unlock();
        datalock.lock();
        dataBuffer.push_back(r);
        datalock.unlock();
    }
};

/*
 * Used to sample temperature Readings.
 */
class TempSensorTimer: public CppTimer {

    void timerEvent() {
        pinslock.lock();
        digitalWrite(MPPIN1, LOW);
        digitalWrite(MPPIN2, HIGH);
        digitalWrite(MPPIN3, LOW);
        digitalWrite(MPPIN4, HIGH);
        Reading r = getTemperature(getVoltage());
        pinslock.unlock();
        datalock.lock();
        dataBuffer.push_back(r);
        datalock.unlock();
    }
};

/*
 * Used to sample fake Readings (for testing purposes).
 */
class FakeSensorTimer: public CppTimer {

    void timerEvent() {

        Reading r = getRandomReading();
        datalock.lock();
        dataBuffer.push_back(r);
        datalock.unlock();
    }
};



void sendData(string data) {
    wslock.lock();

    // Try to reconnect every second if the connection is not available.
    if(!webSocket || webSocket->getReadyState() != WebSocket::readyStateValues::OPEN) {
        while(true) {
            webSocket = WebSocket::from_url(wsURL);
            this_thread::sleep_for(chrono::seconds(1));
            if (webSocket) break;
        }
    }

    webSocket->send(data);
    webSocket->poll();
    wslock.unlock();
}

/*
 * Timer used to trasfer data to the web server through the websocket connection.
 */
class DataTransferTimer: public CppTimer {

    void timerEvent() {
        datalock.lock();

        string toSend = "[";
        bool comma = false;
        for (auto &datum : dataBuffer) {
            if (!datum.status.sent) {
                if (comma) toSend+= ","; else comma = true; // Skip the first comma.
                toSend += datum.toJson();
                datum.status.sent = true;
            }
        }

        toSend += "]";
        datalock.unlock();

        // Only send if there's data.
        if (toSend.length() > 2) {
            cout << "Sending" << "\n";
            sendData(toSend);
        }

    }
};

/*
 * DataBuffer clean-up timer.
 */
class DataCleanUpTimer: public CppTimer {
    ReadingStatus cleanable;

public:
    /*
     * The cleanable parameter is used to determine the status a Reading needs to achieve to be cleaned from tha dataBuffer.
     */
    DataCleanUpTimer(ReadingStatus cleanable) {
        this->cleanable = cleanable;
    }

    void timerEvent() {
        datalock.lock();
        auto it = dataBuffer.begin();
        while (it != dataBuffer.end()) {
            if (it->status == cleanable) {
                it = dataBuffer.erase(it);
            } else it++;
        }
        datalock.unlock();
    }
};

/*
 * Timer used to print sampled readings to the console.
 */
class DataPrintingTimer: public CppTimer {

    void timerEvent() {
        datalock.lock();

        string toPrint = "";
        for (auto &datum : dataBuffer) {
            if (!datum.status.printed) {
                toPrint += datum.toJson() + "\n";
                datum.status.printed = true;
            }
        }
        datalock.unlock();

        cout << toPrint;
    }
};

class PressureAnalysisTimer: public CppTimer {

    vector<Reading> runningData;
    unsigned int runningTime;
    float noBreathingThreshold;
    float hyperVentilationThreshold;

public:
    PressureAnalysisTimer(unsigned int runningTime, float noBreathingThreshold, float hyperVentilationThreshold) {
        this->runningTime = runningTime;
        this->noBreathingThreshold = noBreathingThreshold;
        this->hyperVentilationThreshold = hyperVentilationThreshold;
    }

private:
    float min() {
        float min = HUGE_VALF;
        for (const auto &datum: runningData) {
            if(datum.value < min) min = datum.value;
        }
        return min;
    }

    float max() {
        float max = -HUGE_VALF;
        for (const auto &datum: runningData) {
            if(datum.value > max) max = datum.value;
        }
        return max;
    }

    float mean() {
        float sum = 0;
        for (const auto &datum: runningData) {
            sum += datum.value;
        }
        return sum / runningData.size();
    }

    float std() {
        float mu = mean();
        float res = 0;
        for (const auto &datum: runningData) {
            res += pow(datum.value - mu, 2);
        }
        return sqrt(res / (runningData.size() - 1));
    }


    void analysePressure() {
        if(runningData.size() > 2) {
        }
    }

public:
    void timerEvent() {
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

        //Add new data
        datalock.lock();
        for (auto &datum : dataBuffer) {
            if (!datum.status.analysed) {
                runningData.push_back(datum);
                datum.status.printed = true;
            }
        }
        datalock.unlock();

        // Filter out data that is too old:
        auto it = runningData.begin();
        while (it != runningData.end()) {
            if (it->time < now - runningTime) {
                it = runningData.erase(it);
            } else it++;
        }

        //Analyse the running data
        analysePressure();
    }

};


void rpInit() {
    int setupResult = wiringPiSPISetup(CHANNEL, SPEED);

    if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    else printf("wiringPi SPI is working!\n");

    setupResult = wiringPiSetup();
    if (setupResult == -1) cout << "Error setting up wiringPi for GPIO";
    else printf("wiringPi GPIO is working!\n");

    pinMode (MPPIN1, OUTPUT);
    pinMode (MPPIN2, OUTPUT);
    pinMode (MPPIN3, OUTPUT);
    pinMode (MPPIN4, OUTPUT);
}



int main() {


    //rpInit();

    FakeSensorTimer FSt;
    PressureSensorTimer PSt;
    TempSensorTimer TSt;

    // first number in the multiplication is the milliseconds.
    PSt.start(100 * 1000000);
    //TSt.start(300 * 1000000);
    //FSt.start(100 * 1000000);

    DataTransferTimer DTt;
    DTt.start(100 * 1000000);

    DataPrintingTimer DPt;
    DPt.start(50 * 1000000);


    // Data cleaning thread
    ReadingStatus cleanable;

    // Choose which actions should be done with each Reading before discarding it.
    cleanable.analysed = true;
    cleanable.sent = true;
    cleanable.printed = true;
    cleanable.recorded = false;

    //thread DCthread(dataCleaning, cleanable, 1000);

    DataCleanUpTimer DCt(cleanable);
    DCt.start(500 * 1000000);


    PressureAnalysisTimer PAt(500, 1, 1);
    PAt.start(1000 * 1000000);

    while(1);

    return 0;
}
