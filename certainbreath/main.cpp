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


using namespace std;

static const float REF_V = 3.3; // Reference voltage to the ADC.

// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
static const int CHANNEL = 0;
// SPI communication speed.
static const int SPEED = 500000;
// SPI communication buffer.
unsigned char BUFFER[2]; // 2 bytes is enough to get the 10 bits from the ADC.

// Handle for network operations via libcurl
void *CURL_EASY_HANDLE;


struct Reading {
    float value;
    long time;

    string toJson() {
        return "{\"value\":" + to_string(value) + ", \"time\":" + to_string(time) + "}";
    }
};



Reading getVoltage() {
    wiringPiSPIDataRW(CHANNEL, BUFFER, 2); // Read 2 bytes.

    // We take the last 5 bits from the first byte and first 5 bits from the second byte
    // according to the ADC data sheet.
    int bin = (BUFFER[0] << 3 >> 3) * 32 + (BUFFER[1] >> 3);


    float value = bin / (float)1024 * REF_V; // convert the bin number to the voltage value.
    // Get milliseconds since epoch.
    long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {value, time};
}

Reading getRandomReading() {
    long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {(float)(rand() % 330) / 100, time};
}



string generateJson(vector<Reading> readings) {
    string toSend = "[";
    for (int i = 0; i < readings.size(); i++) {
        toSend += readings[i].toJson();
        if (i != readings.size() - 1) toSend += ",";
    }
    toSend += "]";

    return toSend;
}


void sendDummyData() {
    using easywsclient::WebSocket;
    WebSocket::pointer ws = WebSocket::from_url("ws://127.0.0.1:8080/ws");
    assert(ws);
    while(true) {
        ws->send(getRandomReading().toJson());
        ws->poll();
        this_thread::sleep_for(chrono::seconds(1));
    }

    ws->close();
}

int main(int argc, char** argv) {
    //int setupResult = wiringPiSPISetup(CHANNEL, SPEED);

    //if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    //else printf("wiringPi is working!\n");


//    while(true) {
//        unsentData.push_back(getVoltage());
//        sendData();
//        this_thread::sleep_for(chrono::seconds(1));
//    }

    srand(0);
    thread sendingData (sendDummyData);
    sendingData.join();
    return 0;
}