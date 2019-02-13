#include <stdio.h>
#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <thread>

// channel is the chip select pin. 0 or 1 depending on how the device is connected to the RPi.
static const int channel = 0;
static const int speed = 500000;

using namespace std;

int main(int argc, char** argv) {
    wiringPiSetup();
    int setupResult = wiringPiSPISetup(channel, speed);

    if (setupResult == -1) cout << "Error setting up wiringPi for SPi";
    else printf("wiringPi is working!\n");


    int bufferSize = 2;
    unsigned char buffer[bufferSize];

    for (int i = 0; i < bufferSize; i++) {
        buffer[i] = 0;
    }

    int result = -1;

    while (true) {
        result = wiringPiSPIDataRW(channel, buffer, 0);

        cout << "Trying to read data through SPI, successful?: " << result << "\n";

        cout << "received: ";
        for(int i = 0; i < bufferSize;i++) {
            cout << buffer[0] << " ";
        }
        cout << "\n";
        this_thread::sleep_for(chrono::seconds(3));
    }

    return 0;
}