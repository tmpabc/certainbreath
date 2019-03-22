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


// Pins for the motors.
static const int MTPIN1 = 21;
static const int MTPIN2 = 22;

struct Reading {
    float value;
    long long time;
    string type;

    string toJson() {
        return "{\"value\":" + to_string(value) + ", \"time\":" + to_string(time) + ", \"type\": \"" + type +"\" }";
    }
};

vector<Reading> unsentData;

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

    return (Reading) {(float)(rand() % 330) / 100, time};
}


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
        unsentData.push_back(r);
        datalock.unlock();
    }
};

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
        unsentData.push_back(r);
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

class FakeSensorTimer: public CppTimer {

    void timerEvent() {

        Reading r = getRandomReading();
        datalock.lock();
        unsentData.push_back(r);
        datalock.unlock();
    }
};

string generateReadingListJson(vector<Reading> readings) {
    string toSend = "[";
    for (int i = 0; i < readings.size(); i++) {
        toSend += readings[i].toJson();
        if (i != readings.size() - 1) toSend += ",";
    }
    toSend += "]";

    return toSend;
}

void sendData(string data) {
    wslock.lock();

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

void dataTransfer(int millis) {
    while(true) {
        datalock.lock();
        string toSend = generateReadingListJson(unsentData);
        unsentData.clear();
        datalock.unlock();
        sendData(toSend);
        cout << "Sending" << "\n";
        // Timing is not crucial for network communications since there are no guarantees anyway.
        this_thread::sleep_for(chrono::milliseconds(millis));
    }
}


void dataPrinting(int millis) {

    map<string, float>  latest;
    vector<string> keys;

    while(true) {
        datalock.lock();

        string toPrint = "";
        for (int i = 0; i < unsentData.size(); i++) {
            latest[unsentData[i].type] = unsentData[i].value;
            if (find(keys.begin(), keys.end(), unsentData[i].type) == keys.end()) {
                keys.push_back(unsentData[i].type);
            }
        }
        unsentData.clear();
        datalock.unlock();

        for(int i = 0; i < keys.size(); i++) {
            cout << keys[i] << ": " << setw(10) << latest[keys[i]] << " | ";
        }
        cout << "\n";
        this_thread::sleep_for(chrono::milliseconds(millis));
    }
}


void dataRecording(string filename, int millis) {

    ofstream file(filename);

    while(true) {
        datalock.lock();

        for (int i = 0; i < unsentData.size(); i++) {
            file << unsentData[i].toJson() << "\n";
        }
        unsentData.clear();
        datalock.unlock();

        this_thread::sleep_for(chrono::milliseconds(millis));
    }
}

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
    //
    PSt_left.start(100 * 1000000);
    this_thread::sleep_for(chrono::milliseconds(50));
    //PSt_right.start(100 * 1000000);
    this_thread::sleep_for(chrono::milliseconds(50));
    TSt_top.start(500 * 1000000);
    this_thread::sleep_for(chrono::milliseconds(50));
    //TSt_bottom.start(500 * 1000000);


    // Data sending thread
    //thread DTthread(dataTransfer, 100);
    //DTthread.join();

    // Data printing thread
    thread DPthread(dataPrinting, 50);
    DPthread.join();

    //thread DRthread(dataRecording, "data2.txt", 100);
    //DRthread.join();

    return 0;
}
