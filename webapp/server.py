import aiohttp
from aiohttp import web
import matplotlib.pyplot as plt
from json import loads
from io import StringIO

MAX_DATAPOINTS = 500

routes = web.RouteTableDef()

readings = []
times = []


def plot_data():
    plt.plot(times, readings, "r--")
    img = StringIO()
    plt.savefig(img, format="svg")
    img.seek(0)

    print(img.getvalue().split(">", 1)[1])
    return img.getvalue().split(">", 1)[1]


@routes.get("/")
async def hello(request):
    return web.Response(body=f"<body> {plot_data()} </body>".encode("utf-8"), content_type="text/html")


@routes.get("/ws")
async def websocket_handler(request):
    global readings
    global times
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    async for msg in ws:
        if msg.type == aiohttp.WSMsgType.TEXT:
            if msg.data == 'close':
                await ws.close()
            else:
                data = loads(msg.data)
                readings.append(data["value"])
                times.append(data["time"])
                if (len(readings)) > MAX_DATAPOINTS:
                    readings = readings[-MAX_DATAPOINTS:]
                    times = times[-MAX_DATAPOINTS:]

        elif msg.type == aiohttp.WSMsgType.ERROR:
            print('ws connection closed with exception %s' %
                  ws.exception())


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app)

