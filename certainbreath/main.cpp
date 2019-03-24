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
#include <map>
#include <algorithm>
#include <iomanip>
#include <fstream>


using namespace std;

static const float REF_V = 3.3; // Reference voltage to the ADC.

// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
static const int CHANNEL = 0;
// SPI communication speed.
static const int SPEED = 500000;
// SPI communication buffer.
unsigned char BUFFER[2]; // 2 bytes is enough to get the 10 bits from the ADC.


// Define alert types
static const string NOBREATHINGALERT = "noB";
static const string HYPERVALERT = "hyperV";

//static const float AMP_GAIN = 1.33;
static const float AMP_GAIN = 1.5;

//static const string wsURL = "ws://0.0.0.0:5000/ws";
//static const string wsURL = "ws://certainbreath.herokuapp.com/ws";
static const string wsURL = "ws://192.168.1.146:5123/ws";


using easywsclient::WebSocket;
WebSocket::pointer webSocket;

static mutex pinslock;
static mutex wslock;
static mutex datalock;

// Pins for the multiplexer.
//static const int MPPIN1 = 2; // Physical 13 // A1
//static const int MPPIN2 = 0; // Physical 11 // A0
//static const int MPPIN3 = 3; // Physical 15 // A2
//static const int MPPIN4 = 7; // Physical 7  // EN

// Pins for the PCB
static const int MPPIN1 = 5; // Physical 18 // A1
static const int MPPIN2 = 4; // Physical 16 // A0
static const int MPPIN3 = 6; // Physical 22 // A2
static const int MPPIN4 = 1; // Physical 12  // EN

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

// Pins for the motors.
static const int MTPIN1 = 21;
static const int MTPIN2 = 22;

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

// for 13bit ADC
Reading getVoltage_13() {
    wiringPiSPIDataRW(CHANNEL, BUFFER, 2); // Read 2 bytes.

    int bin = (BUFFER[0]& 0x0F) * 256 + BUFFER[1];
    //int bin = BUFFER[1];

    float value = bin / (float)4096 * REF_V; // convert the bin number to the voltage value.
    // Get milliseconds since epoch.
    //float value = (float) bin;
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {value, time, "Voltage"};
}

// for 10 bit ADC
Reading getVoltage() {
    wiringPiSPIDataRW(CHANNEL, BUFFER, 2); // Read 2 bytes.

    // We take the last 5 bits from the first byte and first 5 bits from the second byte
    // according to the ADC data sheet.
    //int bin = (BUFFER[0] << 3 >> 3) * 32 + (BUFFER[1] >> 3);
    int bin = (BUFFER[0] & 0x1F) * 32 + (BUFFER[1] >> 3);


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
    float r1 = 6000;

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

    float r2 = 11000;
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

    int pin1, pin2, pin3, pin4;
    string type;

public:
    PressureSensorTimer(int pin1, int pin2, int pin3, int pin4, string type = "") {
        this->pin1 = pin1;
        this->pin2 = pin2;
        this->pin3 = pin3;
        this->pin4 = pin4;
        this->type = type;
    }

    void timerEvent() {
        pinslock.lock();
        digitalWrite(MPPIN1, this->pin1);
        digitalWrite(MPPIN2, this->pin2);
        digitalWrite(MPPIN3, this->pin3);
        digitalWrite(MPPIN4, this->pin4);
        //Reading r = getForce(getVoltage());
        Reading r = getVoltage();
        if (this-> type != "") {
            r.type = this->type;
        }
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

    int pin1, pin2, pin3, pin4;
    string type;

public:
    TempSensorTimer(int pin1, int pin2, int pin3, int pin4, string type = "") {
        this->pin1 = pin1;
        this->pin2 = pin2;
        this->pin3 = pin3;
        this->pin4 = pin4;
        this->type = type;
    }


    void timerEvent() {
        pinslock.lock();
        digitalWrite(MPPIN1, this->pin1);
        digitalWrite(MPPIN2, this->pin2);
        digitalWrite(MPPIN3, this->pin3);
        digitalWrite(MPPIN4, this->pin4);
        Reading r = getTemperature(getVoltage());
        //Reading r = getVoltage();
        if (this->type != "") {
            r.type = this->type;
        }
        pinslock.unlock();
        datalock.lock();
        dataBuffer.push_back(r);
        datalock.unlock();
        //motorCheck(r);
    }

    void motorCheck(Reading r) {
        if (r.type == "Temperature_top") {
            if (r.value > 26) {
                digitalWrite(MTPIN1, HIGH);
            } else {
                digitalWrite(MTPIN1, LOW);
            }
        } else if (r.type == "Temperature_bottom"){
            if (r.value > 26) {
                digitalWrite(MTPIN2, HIGH);
            } else {
                digitalWrite(MTPIN2, LOW);

            }
        }
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

    map<string, Reading>  latest;
    vector<string> keys;

    void timerEvent() {
        datalock.lock();

        string toPrint = "";
        for (Reading datum : dataBuffer) {
            if(!datum.status.printed) {
                latest[datum.type] = datum;
            }
            if (find(keys.begin(), keys.end(), datum.type) == keys.end()) {
                keys.push_back(datum.type);
            }
            datum.status.printed = true;
        }
        datalock.unlock();

        // Clean keys that are older than 600ms. Need this to have up to date information on alerts.
        auto it = latest.begin();
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

        while (it != latest.end()) {
            if (it->second.time < now - 600) {
                latest.erase(it);
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

public:
    DataRecordingTimer(string filename) {
        out.open(filename);
    }
    void timerEvent() {
        datalock.lock();

        for (auto &datum : dataBuffer) {
            if (!datum.status.recorded) {
                out << datum.toJson() + "\n";
                datum.status.recorded = true;
            }
        }
        datalock.unlock();
    }
};

class PressureAnalysisTimer: public CppTimer {

    vector<Reading> runningData;
    unsigned int runningTime;
    float noBreathingThreshold;
    float hyperVentilationThreshold;
    float noiseThreshold;
    float weightThreshold;

public:
    PressureAnalysisTimer(
            unsigned int runningTime,
            float noBreathingThreshold,
            float hyperVentilationThreshold,
            float noiseThreshold,
            float weightThreshold) {
        this->runningTime = runningTime;
        this->noBreathingThreshold = noBreathingThreshold;
        this->hyperVentilationThreshold = hyperVentilationThreshold;
        this->noiseThreshold = noiseThreshold;
        this->weightThreshold = weightThreshold;
    }

private:

    Reading maxReading() {
        float max = -HUGE_VALF;
        Reading maxR;
        for (const auto &datum: runningData) {
            if(datum.value > max) {
                max = datum.value;
                maxR = datum;
            }
        }
        return maxR;
    }


    void analysePressure() {
        int length = runningData.size();

        // Only do the analysis if we have enough data and the weight on the pad exceeds the threshold.
        if (length == 0 || runningData[length - 1].value < weightThreshold || runningData[length - 1].time - runningData[0].time < runningTime * 0.7) {
            return;
        }

        // Calculate the frequency of the breathing:
        // (We are assuming the data is in chronological order.

        int spikes = 0;
        float lastSpike = 0;
        float lastVal = 0;
        bool lastChangeUp = false;

        // Count the "spikes" in the running data.
        for (auto &datum : runningData) {

            bool upSpike = lastChangeUp && lastVal > datum.value;
            bool downSpike = !lastChangeUp && lastVal < datum.value;
            bool spikeDiff = abs(lastSpike - datum.value) > noiseThreshold;

            // Count the spike if it's of sufficient difference from the last one.
            if ((upSpike || downSpike) && spikeDiff) {
                spikes++;
            }
        }

        float breathingFreq = 1000.f * spikes / runningTime / 2; // Two spikes per one breath. Convert to breaths/sec.

        if (breathingFreq < noBreathingThreshold) {
            // No breathing detected
            alert(NOBREATHINGALERT);
        } else if (breathingFreq > hyperVentilationThreshold) {
            // Hyperventilation detected.
            alert(HYPERVALERT);
        }
    }

    void alert(string alertType) {
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        dataBuffer.push_back((Reading) {0, now, alertType});

    }

public:
    void timerEvent() {
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();


        datalock.lock();
        // Need to find which sensor provides higher measurements.
        // We only add measurements of the sensor that provides the max value (i.e. max pressure).

        string maxType = maxReading().type;

        //Add new data
        for (auto &datum : dataBuffer) {
            if (!datum.status.analysed && datum.type == maxType) {
                runningData.push_back(datum);
            }
            datum.status.analysed = true; // We won't need to analyse anything else.
        }
        datalock.unlock();


        // Filter out data that is too old from the all runningData.
        auto it = runningData.begin();
        while (it != runningData.end()) {
            if (it->time < now - runningTime) {
                it = runningData.erase(it);
            } else it++;
        }

        //Analyse the running data.
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

    pinMode (MTPIN1, OUTPUT);
    pinMode (MTPIN2, OUTPUT);
}



int main() {


    rpInit();

    FakeSensorTimer FSt;

    //Breadboard
    //PressureSensorTimer PSt_left(LOW, HIGH, LOW, HIGH, "Pressure_left");
    //PressureSensorTimer PSt_right(LOW, LOW, LOW, HIGH, "Pressure_right");

    //PCB
    PressureSensorTimer PSt_left(HIGH, HIGH, LOW, HIGH, "Pressure");
    //                      A1 A0 A2 EN
    TempSensorTimer TSt_top(LOW, LOW, HIGH, HIGH, "Temperature");

    //Breadboard
    //TempSensorTimer TSt_top(HIGH, LOW, HIGH, HIGH, "Temperature_top");
    //TempSensorTimer TSt_bottom(HIGH, HIGH, HIGH, HIGH, "Temperature_bottom");

    // first number in the multiplication is the milliseconds.
    PSt_left.start(100 * 1000000);
    //PSt_right.start(100 * 1000000);
    TSt_top.start(300 * 1000000);
    //TSt_bottom.start(300 * 1000000);
    //FSt.start(100 * 1000000);

    DataTransferTimer DTt;
    //DTt.start(100 * 1000000);

    DataPrintingTimer DPt;
    DPt.start(50 * 1000000);

    DataRecordingTimer DRt("data.txt");
    //DRt.start(100 * 1000000);


    // Data cleaning thread;
    ReadingStatus cleanable;

    // Choose which actions should be done with each Reading before discarding it.
    cleanable.analysed = false;
    cleanable.sent = false;
    cleanable.printed = true;
    cleanable.recorded = false;

    DataCleanUpTimer DCt(cleanable);
    DCt.start(500 * 1000000);


    PressureAnalysisTimer PAt(1000, 0.1, 0.7, 0.5, 1);
    PAt.start(1000 * 1000000);

    while(1);
}
