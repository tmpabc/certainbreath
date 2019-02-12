#include <stdio.h>
#include <wiringPi.h>

int main(int argc, char** argv)
{
    wiringPiSetup();

    printf("wiringPi is working!\n");

    return 0;
}