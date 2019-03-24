
#ifndef CERTAINBREATH_ANALYSIS_TIMERS_H
#define CERTAINBREATH_ANALYSIS_TIMERS_H

#include <vector>
#include <cmath>
#include <string>
#include <chrono>
#include <mutex>

#include "CppTimer.h"
#include "data_structures.h"


// Define alert types
static const string NOBREATHINGALERT = "noB";
static const string HYPERVALERT = "hyperV";

using namespace std;

class PressureAnalysisTimer: public CppTimer {

    vector<Reading> runningData;
    unsigned int runningTime;
    float noBreathingThreshold;
    float hyperVentilationThreshold;
    float noiseThreshold;
    float weightThreshold;
    mutex * datalock;
    vector<Reading> * dataBuffer;

public:
    PressureAnalysisTimer(
            mutex * datalock,
            vector<Reading> * dataBuffer,
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
        this->datalock = datalock;
        this->dataBuffer = dataBuffer;
    }

private:

    Reading maxReading(string key) {
        float max = -HUGE_VALF;
        Reading maxR;
        for (const auto &datum: *dataBuffer) {
            if(datum.type.find(key) != string::npos && datum.value > max) {
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
            alert(NOBREATHINGALERT, breathingFreq);
        } else if (breathingFreq > hyperVentilationThreshold) {
            // Hyperventilation detected.
            alert(HYPERVALERT, breathingFreq);
        }
    }

    void alert(const string &alertType, float value) {
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        datalock->lock();
        dataBuffer->push_back((Reading) {value, now, alertType});
        datalock->unlock();

    }

public:
    void timerEvent() {
        long long now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

        string key = "Pressure";
        datalock->lock();
        // Need to find which sensor provides higher measurements.
        // We only add measurements of the sensor that provides the max value (i.e. max pressure).

        string maxType = maxReading(key).type;

        //Add new data
        for (auto &datum : *dataBuffer) {
            if (datum.type.find(key) != string::npos && !datum.status.analysed && datum.type == maxType) {
                runningData.push_back(datum);
            }
            datum.status.analysed = true; // We won't need to analyse anything else.
        }
        datalock->unlock();


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

#endif //CERTAINBREATH_ANALYSIS_TIMERS_H
