import json

# Extracts just the NLC data from the CORPUS JSON file and puts it into CSV.
# You can get this data from https://www.networkrail.co.uk/who-we-are/transparency-and-ethics/transparency/open-data-feeds/ 
with open("nlcs_corpus.csv", "w") as g:
    g.write("Feature Name,NLC\n")
    with open("corpus.json") as f:
        json_parsed = json.loads(f.read())
        for location in json_parsed["TIPLOCDATA"]:
            if (location["NLC"] % 100 == 0):
                g.write(location["NLCDESC"].replace(",", "-") + "," + str(location["NLC"] // 100).zfill(4) + "\n")
