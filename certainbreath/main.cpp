#include <stdio.h>
#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <thread>
#include <vector>
#include <chrono>
#include <curl/curl.h>
#include <string.h>

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

// Network communication buffer.
vector<Reading> unsentData;
vector<Reading> generatedData;


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


/*
 * Initialise libcurl and its options
 */
void init_curl() {
    curl_global_init(CURL_GLOBAL_ALL);

     CURL_EASY_HANDLE = curl_easy_init();

    curl_easy_setopt(CURL_EASY_HANDLE, CURLOPT_URL, "http://127.0.0.1:5000/data");
    //curl_easy_setopt(CURL_EASY_HANDLE, CURLOPT_VERBOSE, 1L);
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

void sendData() {

    struct curl_slist *headers=NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    string toSend = generateJson(unsentData);
    curl_easy_setopt(CURL_EASY_HANDLE, CURLOPT_POSTFIELDS, toSend.data());
    curl_easy_setopt(CURL_EASY_HANDLE, CURLOPT_POSTFIELDSIZE, toSend.length());
    curl_easy_setopt(CURL_EASY_HANDLE, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(CURL_EASY_HANDLE);

    curl_slist_free_all(headers);

    unsentData.clear();
}


int main(int argc, char** argv) {
    init_curl();
    //int setupResult = wiringPiSPISetup(CHANNEL, SPEED);

    //if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    //else printf("wiringPi is working!\n");


    while(true) {
        unsentData.push_back(getVoltage());
        sendData();
        this_thread::sleep_for(chrono::seconds(1));
    }

    return 0;
}