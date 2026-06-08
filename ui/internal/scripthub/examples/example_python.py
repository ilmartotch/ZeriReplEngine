import json

with open("./data.json", "r", encoding="utf-8") as file:
    payload = json.load(file)

for key, value in payload.items():
    print(f"{key}: {value}")
