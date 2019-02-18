import aiohttp
from aiohttp import web
from json import loads
import aiohttp_jinja2
import jinja2

routes = web.RouteTableDef()

#WSURL = "ws://127.0.0.1:5000/ws_update"
WSURL = "ws://certainbreath.herokuapp.com/ws_update"

@routes.get("/")
@aiohttp_jinja2.template("main.jinja2")
async def hello(request):
    # return web.Response(body=f"<body> {plot_data()} </body>".encode("utf-8"), content_type="text/html")
    return {"wsURL": WSURL}


async def update_clients(data):
    for ws in app["socket_clients"]:
        if ws.prepared:
            await ws.send_json(data)
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

    ws = web.WebSocketResponse()
    await ws.prepare(request)

    async for msg in ws:

        if msg.type == aiohttp.WSMsgType.TEXT:
            if msg.data == 'close':
                await ws.close()
            else:
                print("received a message from the raspberry: " + msg.data)
                data = loads(msg.data)
                await update_clients(data)

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
    port = int(os.environ.get("PORT", 5000))
    web.run_app(app, port=port)

