#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <vector>
#include <mutex>
#include <fstream>

#include "CppTimer.h"
#include "data_structures.h"
#include "utils.h"
#include "web_comm.h"
#include "sensor_timers.h"
#include "data_handling_timers.h"
#include "analysis_timers.h"


using namespace std;

// SPI communication speed.
static const int SPEED = 500000;

//static const float AMP_GAIN = 1.33;
static const float AMP_GAIN = 1.5;







void rpInit(int channel, vector<int> outputPins) {
    int setupResult = wiringPiSPISetup(channel, SPEED);

    if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    else printf("wiringPi SPI is working!\n");

    setupResult = wiringPiSetup();
    if (setupResult == -1) cout << "Error setting up wiringPi for GPIO";
    else printf("wiringPi GPIO is working!\n");

    for (auto pin : outputPins) {
        pinMode(pin, OUTPUT);
    }
}



int main() {

    // A1 A0 A2 EN
    vector<int> multiplexerPins({5, 4, 6, 1});

    //const string wsURL = "ws://0.0.0.0:5000/ws";
    //const string wsURL = "ws://certainbreath.herokuapp.com/ws";
    const string wsURL = "ws://192.168.1.146:5123/ws";

    const float ampGain = 1.5;

    //rpInit(0, multiplexerPins);

    vector<Reading> dataBuffer; // Buffer to store the readings.
    mutex pinslock;
    mutex datalock;


    FakeSensorTimer FSt(&datalock, &dataBuffer, "Pressure");
    FSt.start(100 * 1000000);
    //Breadboard
    //PressureSensorTimer PSt_left(LOW, HIGH, LOW, HIGH, "Pressure_left");
    //PressureSensorTimer PSt_right(LOW, LOW, LOW, HIGH, "Pressure_right");



    //PCB
    PressureSensorTimer PSt_left(&pinslock, &datalock, &dataBuffer, multiplexerPins, vector<int>({HIGH, HIGH, LOW, HIGH}), ampGain);
    //PressureSensorTimer PSt_left(HIGH, HIGH, LOW, HIGH, "Pressure");
    //                      A1 A0 A2 EN
    //TempSensorTimer TSt_top(LOW, LOW, HIGH, HIGH, "Temperature");
    TempSensorTimer TSt_top(&pinslock, &datalock, &dataBuffer, multiplexerPins, vector<int>({LOW, LOW, HIGH, HIGH}), ampGain);


    //Breadboard
    //TempSensorTimer TSt_top(HIGH, LOW, HIGH, HIGH, "Temperature_top");
    //TempSensorTimer TSt_bottom(HIGH, HIGH, HIGH, HIGH, "Temperature_bottom");

    // first number in the multiplication is the milliseconds.
    //PSt_left.start(100 * 1000000);
    //PSt_right.start(100 * 1000000);
    //TSt_top.start(300 * 1000000);
    //TSt_bottom.start(300 * 1000000);
    //FSt.start(100 * 1000000);


    DataTransferTimer DTt(wsURL, &datalock, &dataBuffer);
    //DTt.start(100 * 1000000);

    DataPrintingTimer DPt(&datalock, &dataBuffer);
    DPt.start(50 * 1000000);

    DataRecordingTimer DRt("data.txt", &datalock, &dataBuffer);
    //DRt.start(100 * 1000000);


    // Data cleaning thread;
    ReadingStatus cleanable;

    // Choose which actions should be done with each Reading before discarding it.
    cleanable.analysed = false;
    cleanable.sent = false;
    cleanable.printed = true;
    cleanable.recorded = false;

    DataCleanUpTimer DCt(cleanable, &datalock, &dataBuffer);
    DCt.start(500 * 1000000);


    PressureAnalysisTimer PAt(&datalock, &dataBuffer, 1000, 0.1, 0.7, 0.5, 1);
    PAt.start(1000 * 1000000);

    while(1) {
        this_thread::sleep_for(chrono::seconds(1));
    }
}
