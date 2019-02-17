import aiohttp
from aiohttp import web
import matplotlib.pyplot as plt
from json import loads
from io import StringIO
import aiohttp_jinja2
import jinja2

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
@aiohttp_jinja2.template("main.jinja2")
async def hello(request):
    # return web.Response(body=f"<body> {plot_data()} </body>".encode("utf-8"), content_type="text/html")
    return {}


async def update_clients():
    for ws in app["socket_clients"]:
        if ws.prepared:
            await ws.send_json({"time": times[-1], "reading": readings[-1]})
            print("sent a message to a client.")
        else:
            print("ws not yet prepared.")


@routes.get("/ws_update")
async def client_updater(request):
    print("received a connection request from a client")
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    request.app["socket_clients"].append(ws)
    async for msg in ws:
        print("received a message from the raspberry.")
        if msg.type == aiohttp.WSMsgType.TEXT:
            if msg.data == 'close':
                await ws.close()
            else:
                print("reiceived message from the client: " + msg.data)
        elif msg.type == aiohttp.WSMsgType.ERROR:
            print('ws connection closed with exception' + str(ws.exception()))
    return ws



@routes.get("/ws")
async def websocket_handler(request):
    global readings
    global times
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    async for msg in ws:
        print("received a message from the raspberry.")
        if msg.type == aiohttp.WSMsgType.TEXT:
            if msg.data == 'close':
                await ws.close()
            else:
                data = loads(msg.data)
                readings.append(data["value"])
                times.append(data["time"])
                await update_clients()
                if (len(readings)) > MAX_DATAPOINTS:
                    readings = readings[-MAX_DATAPOINTS:]
                    times = times[-MAX_DATAPOINTS:]

        elif msg.type == aiohttp.WSMsgType.ERROR:
            print('ws connection closed with exception' + str(ws.exception()))
    return ws


if __name__ == "__main__":
    import os
    os.environ["PYTHONASYNCIODEBUG"] = "1"
    app = web.Application()
    aiohttp_jinja2.setup(app, loader=jinja2.FileSystemLoader('templates/'))
    app["socket_clients"] = []
    app.add_routes(routes)
    app.add_routes([web.static("/js", "js/")])
    web.run_app(app)

