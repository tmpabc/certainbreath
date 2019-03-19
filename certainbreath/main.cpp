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


using namespace std;

static const float REF_V = 3.3; // Reference voltage to the ADC.

// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
static const int CHANNEL = 0;
// SPI communication speed.
static const int SPEED = 500000;
// SPI communication buffer.
unsigned char BUFFER[2]; // 2 bytes is enough to get the 10 bits from the ADC.


static const float AMP_GAIN = 1.33;

//static const string wsURL = "ws://127.0.0.1:8080/ws";
static const string wsURL = "ws://certainbreath.herokuapp.com/ws";

using easywsclient::WebSocket;
WebSocket::pointer webSocket;

static mutex pinslock;
static mutex wslock;
static mutex datalock;

static const int MPPIN1 = 2; // Physical 13 // A1
static const int MPPIN2 = 0; // Physical 11 // A0
static const int MPPIN3 = 3; // Physical 15 // A2
static const int MPPIN4 = 7; // Physical 7  // EN


struct Reading {
    float value;
    long long time;
    string type;

    string toJson() {
        return "{\"value\":" + to_string(value) + ", \"time\":" + to_string(time) + ", \"type\": \"" + type +"\" }";
    }
};

vector<Reading> unsentData;


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

    return (Reading) {(float)(rand() % 330) / 100, time};
}


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
        unsentData.push_back(r);
        datalock.unlock();
    }
};

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
        unsentData.push_back(r);
        datalock.unlock();
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
    while(true) {
        datalock.lock();

        string toPrint = "";
        for (int i = 0; i < unsentData.size(); i++) {
            toPrint += unsentData[i].toJson() + "\n";
        }
        unsentData.clear();
        datalock.unlock();

        cout << toPrint;
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
}



int main() {


    rpInit();

    FakeSensorTimer FSt;
    PressureSensorTimer PSt;
    TempSensorTimer TSt;

    // first number in the multiplication is the milliseconds.
    PSt.start(100 * 1000000);
    this_thread::sleep_for(chrono::milliseconds(100));
    //TSt.start(300 * 1000000);

    // Data sending thread
    thread DTthread(dataTransfer, 100);
    DTthread.join();

    // Data printing thread
    //thread DPthread(dataPrinting, 50);
    //DPthread.join();
    return 0;
}
