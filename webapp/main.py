from flask import Flask
from flask import request

app = Flask(__name__)

databuffer = ""


@app.route('/')
def main():
    return str(databuffer)


@app.route('/data', methods=['POST'])
def data():
    global databuffer
    databuffer += str(request.json)
    databuffer += "\n"
    return ""
