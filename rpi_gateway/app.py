from flask import Flask, render_template, jsonify
from mqtt_handler import get_latest_data

app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html", data=get_latest_data())

@app.route("/api/status")
def api_status():
    return jsonify(get_latest_data())

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)