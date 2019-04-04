
#ifndef CERTAINBREATH_UTILS_H
#define CERTAINBREATH_UTILS_H

#include "data_structures.h"
#include <math.h>
#include <chrono>


using namespace std;

static const float REF_V = 3.3; // Reference voltage to the ADC.

// for 13bit ADC
// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
Reading getVoltage(int channel=0) {
    unsigned char buffer[2];
    wiringPiSPIDataRW(channel, buffer, 2); // Read 2 bytes.

    int bin = (buffer[0]& 0x0F) * 256 + buffer[1];
    //int bin = BUFFER[1];

    float value = bin / (float)4096 * REF_V; // convert the bin number to the voltage value.
    // Get milliseconds since epoch.
    //float value = (float) bin;
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    //return (Reading) {value, time, "Voltage"};
    return (Reading) {value, time, "Voltage"};

}

// for 10 bit ADC
// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
Reading getVoltage_10(int channel=0) {
    unsigned char buffer[2];
    wiringPiSPIDataRW(channel, buffer, 2); // Read 2 bytes.

    // We take the last 5 bits from the first byte and first 5 bits from the second byte
    // according to the ADC data sheet.
    int bin = (buffer[0] & 0x1F) * 32 + (buffer[1] >> 3);


    float value = bin / (float)1024 * 3.3f; // convert the bin number to the voltage value.
    // Get milliseconds since epoch.
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {value, time, "Voltage"};
}

/*
 * Function to get the force in grams from given by the voltage reading from a FSR sensor.
 */
Reading getForce(const Reading &voltage, float ampGain=1.5) {

    // log(F) = a*log(r) + b

    float v_in = voltage.value / ampGain;
    float r1 = 6000;

    // Obtained by 1st degree polynomial fit to log(F) vs log(r)
    float a = -1.3077116639139494f;
    float b = 7.141611411251033f;

    // R_fsr
    //float r = (r1 * v_in) / (3.3 - v_in);
    float r = r1 * (REF_V - v_in) / v_in;

    float F = exp(log(r) * a + b);

    //return (Reading) {F, voltage.time, "Pressure"};
    return voltage;
}

/*
 * Function to get the temperature in Celsius given by the voltage reading from a termistor.
 */
Reading getTemperature(const Reading &voltage, float ampGain=1.5, float r2=11000) {

    float v_in = voltage.value / ampGain;

    float r0 = 10000;
    float r_th = r2 * (REF_V - v_in) / v_in;

    float t_0 = 298;
    float beta = 3977;


    float t_K = (t_0 * beta) / (t_0 * log(r_th / r0) + beta);

    float t_C = t_K - 273.15f;

    return (Reading) {t_C, voltage.time, "Temperature"};
}

Reading getRandomReading() {
    long long time = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    return (Reading) {(float)(rand() % 330) / 100, time, "fake"};
}


void writePins(const vector<int> &pins, const vector<int> &pinVals) {

    for(int i = 0; i < pins.size(); i++) {
        digitalWrite(pins[i], pinVals[i]);
    }

}


#endif //CERTAINBREATH_UTILS_H
