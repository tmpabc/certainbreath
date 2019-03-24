
#ifndef CERTAINBREATH_WEB_COMM_H
#define CERTAINBREATH_WEB_COMM_H

#include <chrono>
#include <mutex>
#include <thread>
#include "easywsclient.hpp"
#include "easywsclient.cpp"


using namespace std;

static mutex wslock;
using easywsclient::WebSocket;
WebSocket::pointer webSocket;

void sendData(const string &data, const string &url) {
    wslock.lock();

    // Try to reconnect every second if the connection is not available.
    if(!webSocket || webSocket->getReadyState() != WebSocket::readyStateValues::OPEN) {
        while(true) {
            webSocket = WebSocket::from_url(url);
            this_thread::sleep_for(chrono::seconds(1));
            if (webSocket) break;
        }
    }

    webSocket->send(data);
    webSocket->poll();
    wslock.unlock();
}

#endif //CERTAINBREATH_WEB_COMM_H
