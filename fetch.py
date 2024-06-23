import requests
import shutil
import os

def download_file(url, headers={}):
    local_filename = url.split('/')[-1]
    with requests.get(url, stream=True, headers = headers) as r:
        with open(local_filename, 'wb') as f:
            shutil.copyfileobj(r.raw, f)

    return local_filename

AUTH_URL = "https://opendata.nationalrail.co.uk/authenticate"
USERNAME = "<USERNAME>"
PASSWORD = "<PASSWORD>"
FARE_URL = " https://opendata.nationalrail.co.uk/api/staticfeeds/2.0/fares"

r = requests.post(AUTH_URL, data = {"username": USERNAME, "password": PASSWORD})
TOKEN = r.json()["token"]
print("Acquired token!")

download_file(FARE_URL, {"X-Auth-Token": TOKEN})
print("Downloaded file")

FARES_DIR_NAME = "fares_data"

os.system(f"rm -rf {FARES_DIR_NAME} && mkdir {FARES_DIR_NAME} && unzip fares -d {FARES_DIR_NAME} && rm fares")

