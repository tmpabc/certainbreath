#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <vector>
#include <mutex>
#include <fstream>
#include <string>
#include <iostream>

#include "CppTimer.h"
#include "data_structures.h"
#include "utils.h"
#include "web_comm.h"
#include "sensor_timers.h"
#include "data_handling_timers.h"
#include "analysis_timers.h"


using namespace std;



void rpInit(int channel, int speed, vector<int> outputPins, vector<int> motorPins) {
    int setupResult = wiringPiSPISetup(channel, speed);

    if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    else printf("wiringPi SPI is working!\n");

    setupResult = wiringPiSetup();
    if (setupResult == -1) cout << "Error setting up wiringPi for GPIO";
    else printf("wiringPi GPIO is working!\n");

    for (auto pin : outputPins) {
        pinMode(pin, OUTPUT);
    }

    for (auto pin : motorPins) {
        pinMode(pin, OUTPUT);
    }
}



int main() {

    vector<Reading> dataBuffer; // Buffer to store the readings.
    mutex pinslock;
    mutex datalock;


    // Read the configuration file.
    map<string, string> config;

    ifstream configFile("../config.cfg");
    string line;
    while(getline(configFile, line)) {
        if(line.empty() || line[0] == '#') continue;
        stringstream strs(line);
        string key;
        getline(strs, key, '=');
        string value;
        getline(strs, value);
        config[key] = value;
    }

    // A1 A0 A2 EN
    vector<int> multiplexerPins({stoi(config["MPPIN1"]), stoi(config["MPPIN2"]),
                                 stoi(config["MPPIN3"]), stoi(config["MPPIN4"])});

    vector<int> motorPins({stoi(config["MTPIN1"]), stoi(config["MTPIN2"])});



    if (config["RPIINIT"] == "true") {
        rpInit(stoi(config["SPI_CHANNEL"]), stoi(config["SPEED"]), multiplexerPins, motorPins);
    }

    FakeSensorTimer FSt(&datalock, &dataBuffer, config["FAKESENSORTYPE"]);
    if(config.find("FAKESENSORINTERVAL") != config.end()) {
        FSt.start(stoi(config["FAKESENSORINTERVAL"]) * 1000000);
    }

    PressureSensorTimer PSt_right(&pinslock, &datalock, &dataBuffer,
                                  multiplexerPins, vector<int>({stoi(config["PRESSURERIGHTPIN1"]), stoi(config["PRESSURERIGHTPIN2"]),
                                                                stoi(config["PRESSURERIGHTPIN3"]), stoi(config["PRESSURERIGHTPIN4"])}),
                                  stof(config["AMPGAIN"]), config["PRESSURERIGHTTYPE"]);
    if(config.find("PRESSURERIGHTINTERVAL") != config.end()) {

        PSt_right.start(stoi(config["PRESSURERIGHTINTERVAL"]) * 1000000);
    }

    PressureSensorTimer PSt_left(&pinslock, &datalock, &dataBuffer,
                                 multiplexerPins, vector<int>({stoi(config["PRESSURELEFTPIN1"]), stoi(config["PRESSURELEFTPIN2"]),
                                                               stoi(config["PRESSURELEFTPIN3"]), stoi(config["PRESSURELEFTPIN4"])}),
                                 stof(config["AMPGAIN"]), config["PRESSURELEFTTYPE"]);
    if(config.find("PRESSURELEFTINTERVAL") != config.end()) {

        PSt_left.start(stoi(config["PRESSURELEFTINTERVAL"]) * 1000000);
    }


    TempSensorTimer TSt_top(&pinslock, &datalock, &dataBuffer,
                            multiplexerPins, vector<int>({stoi(config["TEMPTOPPIN1"]), stoi(config["TEMPTOPPIN2"]),
                                                          stoi(config["TEMPTOPPIN3"]), stoi(config["TEMPTOPPIN4"])}),
                            stof(config["AMPGAIN"]));
    if(config.find("TEMPTOPINTERVAL") != config.end()) {

        TSt_top.start(stoi(config["TEMPTOPINTERVAL"]) * 1000000);

    }

    TempSensorTimer TSt_bot(&pinslock, &datalock, &dataBuffer,
                            multiplexerPins, vector<int>({stoi(config["TEMPBOTPIN1"]), stoi(config["TEMPBOTPIN2"]),
                                                          stoi(config["TEMPBOTPIN3"]), stoi(config["TEMPBOTPIN4"])}),
                            stof(config["AMPGAIN"]));
    if(config.find("TEMPBOTINTERVAL") != config.end()) {

        TSt_bot.start(stoi(config["TEMPBOTINTERVAL"]) * 1000000);

    }

    DataTransferTimer DTt(config["URL"], &datalock, &dataBuffer);
    if(config.find("DATATRANSFERINTERVAL") != config.end()) {
        DTt.start(stoi(config["DATATRANSFERINTERVAL"]) * 1000000);
    }

    DataPrintingTimer DPt(&datalock, &dataBuffer);
    if(config.find("DATAPRINTINGINTERVAL") != config.end()) {
        DPt.start(stoi(config["DATAPRINTINGINTERVAL"]) * 1000000);
    }

    DataRecordingTimer DRt(config["DATARECORDINGFILENAME"], &datalock, &dataBuffer);
    if(config.find("DATARECORDINGINTERVAL") != config.end()) {
        DRt.start(stoi(config["DATARECORDINGINTERVAL"]) * 1000000);
    }

    PressureAnalysisTimer PAt(&datalock, &dataBuffer,
            stoi(config["PRESSUREANALYSISRUNNINGTIME"]),
            stoi(config["PRESSUREANALYSISNOBREATHINGTHRESHOLD"]),
            stoi(config["PRESSUREANALYSISHYPERVENTILATIONTHRESHOLD"]),
            stoi(config["PRESSUREANALYSISNOISETHRESHOLD"]),
            stoi(config["PRESSUREANALYSISWEIGHTTHRESHOLD"]));

    if(config.find("PRESSUREANALYSISINTERVAL") != config.end()) {
        PAt.start(stoi(config["PRESSUREANALYSISINTERVAL"]) * 1000000);
    }

    TemperatureAnalysisTimer TAt(&datalock, &dataBuffer,
            stof(config["TEMPANALYSISTHRESHOLD"]),
            motorPins[0],
            motorPins[1]);

    if(config.find("TEMPANALYSISINTERVAL") != config.end()) {
        TAt.start(stoi(config["TEMPANALYSISINTERVAL"]) * 1000000);
    }

    // Data cleaning thread;
    ReadingStatus cleanable;

    // Choose which actions should be done with each Reading before discarding it.
    cleanable.analysed = config.find("PRESSUREANALYSISINTERVAL") != config.end();
    cleanable.sent = config.find("DATATRANSFERINTERVAL") != config.end();
    cleanable.printed = config.find("DATAPRINTINGINTERVAL") != config.end();
    cleanable.recorded = config.find("DATARECORDINGINTERVAL") != config.end();

    DataCleanUpTimer DCt(cleanable, &datalock, &dataBuffer);
    DCt.start(stoi(config["DATACLEANINTERVAL"]) * 1000000);


    while(1) {
        this_thread::sleep_for(chrono::seconds(1));
    }
}
