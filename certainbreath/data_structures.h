
#ifndef CERTAINBREATH_DATA_STRUCTURES_H
#define CERTAINBREATH_DATA_STRUCTURES_H

using namespace std;

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

/*
 * Main data structure storing the reading data.
 */
struct Reading {
    float value;
    long long time;
    string type;

    ReadingStatus status;

    string toJson() {
        return "{\"value\":" + to_string(value) + ", \"time\":" + to_string(time) + ", \"type\": \"" + type +"\" }";
    }

};

#endif //CERTAINBREATH_DATA_STRUCTURES_H
