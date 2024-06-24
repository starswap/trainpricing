#include <bits/stdc++.h>
using namespace std;

unordered_map<string, vector<int>> clusters;                   // Cluster NLC to list of station NLCs 
unordered_map<int, string> nlc_to_name;                        // Station NLC to name
unordered_map<string, int> name_to_nlc;                        // Reverse mapping of the above
unordered_map<string, int> nlc_to_index;                       // Map to compressed indices
unordered_map<int, tuple<int, int, bool>> flow_id_to_vertices; // Start NLC, End NLC, Reversible 
vector<pair<string, bool>> index_to_nlc;                       // true = cluster
vector<vector<int>> AM;                                        // Adjacency matrix on the compressed indices
vector<vector<pair<int, int>>> AL;                             // Adjacency list on the compressed indices

const int INF           = 1'000'000'000;                       // Infinity - no UK rail fare is more than 1 bn pence
const int N_NLC_APPROX  = 5000;                                // This does need to be >= the number of cluster + station NLCs total
const int MIN_SANE_FARE = 100;                                 // We eliminate all fares below £1 as these are typically special case fares which are not available in practice
char ticket_codes_using[N_NLC_APPROX][N_NLC_APPROX][4];        // Corresponds to the AM - ticket_codes_using[u][v] reveals the ticket code used to get the price stated in AM[u][v]

// Tracks the current index we are on (index of the next NLC) in the compressed graph representation.
int nlc_idx = 0;

// In an attempt to salvage something from the data I tried limiting myself
// to only fares which are not advances and therefore the prices are fixed and known.
// Check https://raileasy.co.uk/fare/CDS for example to find out what 
// these codes correspond to.
bool acceptable_ticket(const char *code) {
    return (strncmp("CDS", code, 3) == 0)  || (strncmp("CBB", code, 3) == 0) || (strncmp("SDS", code, 3) == 0) || (strncmp("SWS", code, 3) == 0);
}

// Utilities
// https://stackoverflow.com/questions/1120140/how-can-i-read-and-parse-csv-files-in-c
// For processing CSV files
vector<string> getNextLineAndSplitIntoTokens(istream& str)
{
    vector<string> result;
    string line;
    getline(str,line);

    stringstream lineStream(line);
    string cell;

    while(getline(lineStream,cell, ',')) {
        result.push_back(cell);
    }
    // Trailing comma with no data after it.
    if (!lineStream && cell.empty()) {
        result.push_back("");
    }
    return result;
}

// https://stackoverflow.com/questions/16826422/c-most-efficient-way-to-convert-string-to-int-faster-than-atoi
int fast_atoi(const char * str, int maxDigits) {
    int val = 0;
    while(*str && (maxDigits-- != 0) ) {
        val = val*10 + (*str++ - '0');
    }
    return val;
}
int fast_atoi(const char * str) {
    return fast_atoi(str, 50);
}

// https://stackoverflow.com/questions/1577475/c-sorting-and-keeping-track-of-indexes
template <typename T>
vector<size_t> sort_indexes(const vector<T> &v) {
  vector<size_t> idx(v.size());
  iota(idx.begin(), idx.end(), 0);
  stable_sort(idx.begin(), idx.end(), [&v](size_t i1, size_t i2) {return v[i1] < v[i2];});
  return idx;
}

// Converts a date of the form 01012024 to a number of seconds since the epoch
tuple<int, int, int> parseDateToTuple(string date_string) {
    int day = fast_atoi(date_string.substr(0, 2).c_str());
    int month = fast_atoi(date_string.substr(2, 2).c_str());
    int year = fast_atoi(date_string.substr(4, 4).c_str());
    return make_tuple(year, month, day);
}

// Determines if we are currently in the period starting on start_date_string
// and ending on end_date_string inclusive
bool is_active(string start_date_string, string travel_date_string,  string end_date_string) {    
    auto start = parseDateToTuple(start_date_string);
    auto travel = parseDateToTuple(travel_date_string);
    auto end = parseDateToTuple(end_date_string);
    return start <= travel && (end_date_string == "31122999" || travel <= end); // Optimisation in case parsing is slow
}

// Displays the station clusters defined by the imported fares data so that we can
// try to work out what they correspond to. [Debug / info]
void print_clusters() {
    cout << "Known clusters:" << '\n';
    for (const auto &[cluster_id, members] : clusters) {
        cout << "Cluster with ID " << cluster_id << " contains stations: " << '\n';
        for (int member : members) {
            cout << "  - " << member << ": " << nlc_to_name[member] << '\n';
        }
    }   
    cout << endl; // Don't flush until the end
}

// Describes the NLC corresponding to an index (which may be a cluster or a station with a name)
string get_name(int index) {
    auto [nlc, cluster] = index_to_nlc[index];
    string name;
    if (cluster) {
        name = "cluster with nlc ";
        name += nlc;
    } else {
        name = nlc_to_name[fast_atoi(nlc.c_str())];
    }
    return name;
} 

// Reads in all of the station clusters
void process_cluster_file(string base_fare_path, string travel_date_string) {
    int invalid_count = 0;
    int valid_count = 0;
    ifstream cluster(base_fare_path + ".FSC"); 
    string line;
    cout << "Processing FSC Cluster file" << endl;
    if (cluster.is_open()) {
        while (getline(cluster, line)) {
            if (line[0] == '/') continue; // skip comments
            string start_string = line.substr(17, 8);
            string end_string = line.substr(9, 8);
            if (!is_active(start_string, travel_date_string, end_string)) continue;
            
            string cluster_nlc = line.substr(1, 4); 
            string target_nlc = line.substr(5, 4);
            if (!(target_nlc[0] >= '0' && target_nlc[0] <= '9')) {
                // These correspond to stations that we don't have the NLC of.
                // Typically this is things that are not really stations like ferry terminals or bus stops.
                // As we lack the data and only really care about taking the train here we ignore these records. 
                invalid_count++; 
                continue;
            }; 
            valid_count++;
            clusters[cluster_nlc].push_back(fast_atoi(target_nlc.c_str()));
            if (!nlc_to_index.count(cluster_nlc)) {
                nlc_to_index[cluster_nlc] = nlc_idx++;
                index_to_nlc.emplace_back(cluster_nlc, true);
            }
        }
        cluster.close();
        cout << "Processed " << valid_count << " valid records with " << invalid_count << " skipped ones." << endl; 
        cout << "Total " << nlc_to_index.size() << " station clusters." << endl;
    }
}

// Get only the NLCs that are referenced in the flow data so we can
// delete the rest.
unordered_set<string> get_flow_relevant_nlcs(string base_fare_path) {
    string line;
    unordered_set<string> relevant_nlcs;
    ifstream flows(base_fare_path + ".FFL"); 
    if (flows.is_open()) {
        while (getline(flows, line)) {
            if (line[0] == '/') continue; // skip comments
            if (line[1] == 'F') {
                const string origin_nlc = line.substr(2, 4);
                const string dest_nlc = line.substr(6, 4);
                relevant_nlcs.insert(origin_nlc);
                relevant_nlcs.insert(dest_nlc);
            }
        }
        flows.close();
    } else {
        cout << "Failed to open flows file" << endl;
    }
    return relevant_nlcs;
}

// Corresponds the names of the stations with their NLCs and furthermore
// assigns each one an index in the compressed data representation
void process_nlc_data_file(string nlc_csv_file, string base_fare_path) {
    unordered_set<string> relevant_nlcs = get_flow_relevant_nlcs(base_fare_path);
    string line;
    cout << "Processing NLC csv data file" << endl;
    ifstream nlcs(nlc_csv_file);
    if (nlcs.is_open()) {
        bool first = true;
        while (nlcs.peek() != EOF) {
            if (first) {first = false; getline(nlcs, line); continue;}
            vector<string> results = getNextLineAndSplitIntoTokens(nlcs);
            if (!relevant_nlcs.count(results[1])) continue; // skip NLCs that we don't need; optimisation
            int nlc = fast_atoi(results[1].c_str());
            nlc_to_name[nlc] = results[0];
            name_to_nlc[results[0]] = nlc;
            string string_nlc = results[1].substr(0, 4);
            if (nlc_to_index.count(string_nlc)) {
                // Shouldn't happen if the NLC file is correctly generated though I did mess this up a few times.
                cout << "Duplicate NLC" << '\n'; 
                cout << results[0] << '\n';
            }
            nlc_to_index[string_nlc] = nlc_idx++;
            index_to_nlc.emplace_back(string_nlc, false);
        }
        nlcs.close();        
    }
}

// Reads in a simple text file of the names of the starting stations that 
// we want to consider.
unordered_set<int> get_starting_stations(int N, string starting_stations_file) {
    string line;
    cout << "Total " << N << " stations and clusters." << endl;
    ifstream starting(starting_stations_file); 
    cout << "Processing starting stations" << endl;
    unordered_set<int> starting_stations; 
    if (starting.is_open()) {
        while (getline(starting, line)) {
            if (!name_to_nlc.count(line)) {
                cout << "WARNING: Starting station " << line << " does not exist or has no fares associated with it." << endl;
            }
            starting_stations.insert(name_to_nlc[line]);
        }
        starting.close();
    }
    return starting_stations;
}

// Reads in the actual flows and fares data itself.
void process_flows_file(int N, string base_fare_path, string travel_date_string) {
    int unknown_nlc_flows = 0;
    int total_flows = 0;
    string line;
    AM.assign(N, vector<int>(N, INF));

    cout << "Processing FFL flow file:" << endl;
    ifstream flows(base_fare_path + ".FFL"); 
    if (flows.is_open()) {
        while (getline(flows, line)) {
            if (line[0] == '/') continue; // skip comments
            if (line[1] == 'F') {
                total_flows++;
                const string origin_nlc = line.substr(2, 4);
                const string dest_nlc = line.substr(6, 4);
                const char direction = line[19];
                const string end_date = line.substr(20, 8); 
                const string start_date = line.substr(28, 8); 
                const int flow_id = fast_atoi(line.substr(42, 7).c_str());

                if (!is_active(start_date, travel_date_string, end_date)) continue;
                if (!((nlc_to_index.count(origin_nlc) > 0) && (nlc_to_index.count(dest_nlc) > 0))) {
                    unknown_nlc_flows++;
                    continue;
                }
                // TODO: Note that potentially F records could be intermingled with T records so in theory you might have to do 
                // two passes over the data. It's not clear if that would ever arise from the specification but the data doesn't
                // seem to ever have this problem.
                flow_id_to_vertices[flow_id] = make_tuple(nlc_to_index[origin_nlc], nlc_to_index[dest_nlc], (direction == 'R'));
            } else if (line[1] == 'T') {
                const int flow_id = fast_atoi(&line[2], 7);
                if (!flow_id_to_vertices.count(flow_id)) continue;
                const int fare_in_pence = fast_atoi(&line[12], 8);
                auto [u, v, reversible] = flow_id_to_vertices[flow_id];

                for (int i = 0; i < 2; ++i) { // Try both ways round
                    if ((acceptable_ticket(&line[9])) && AM[u][v] > fare_in_pence && fare_in_pence > MIN_SANE_FARE) {
                        AM[u][v] = fare_in_pence;
                        strncpy(ticket_codes_using[u][v], &line[9], 3);
                    }
                    if (reversible) {swap(u, v);}
                }
            } else {
                cout << "Illegal line: " << line << endl;
            }
        }
        flows.close();
    } else {
        cout << "Failed to open flows file" << endl;
    }
}

// Adjacency list will be a more efficient representation for Dijkstra but AM is easier to
// build from the flow data.
void convert_AM_to_AL(int N) {
    AL.assign(N, vector<pair<int, int>>());
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (AM[i][j] < INF) {
                AL[i].emplace_back(j, AM[i][j]);
            }
        }
    }
}

// Compute cheapest routes to each location
void do_dijkstra(int N, string starting_stations_file, int cost_limit) {
    /* Load in the starting stations */
    cout << "Processing starting stations " << endl;
    unordered_set<int> starting_stations = get_starting_stations(N, starting_stations_file);
    cout << "Processed " << starting_stations.size() << " starting stations." << endl;
    
    /* Set up the initial costs for the starting stations as 0 */
    vector<int> cost(N, INF);
    priority_queue<pair<int, int>> pq;

    for (int station : starting_stations) {
        int one_start_nlc = nlc_to_index[to_string(station)];
        cost[one_start_nlc] = 0;
        pq.emplace(-cost[one_start_nlc], one_start_nlc);
    }

    
    /* Compute the costs to each station */
    cout << "About to start dijkstra" << endl;
    vector<unsigned int> parent(N);
    iota(parent.begin(), parent.end(), 0);
    while (!pq.empty()) {
        auto [mcost, u] = pq.top(); pq.pop();
        int this_cost = -mcost;
        if (this_cost != cost[u]) continue; // stale
        for (auto [v, w] : AL[u]) {
            if (this_cost + w < cost[v]) {
                cost[v] = this_cost + w;
                parent[v] = u;
                pq.emplace(-cost[v], v);
            }
        }
    }
    cout << "Done Dijkstra" << endl;

    /* Display the results */
    for (auto i: sort_indexes(cost)) {
        string name = get_name(i);

        if (cost[i] > cost_limit) break;       // Stop outputting once we reach the user's cost threshold
        if (index_to_nlc[i].second) continue;  // Don't output clusters - this is arbitrary; you might want to 

        cout << "We can reach " << name << " for " << cost[i] << '\n';

        /* Calculate the route to this destination */
        vector<int> stops;
        stops.push_back(i);
        while (i != parent[i]) {
            i = parent[i];
            stops.push_back(i);
        }
        reverse(stops.begin(), stops.end());

        /* Display the route to this destination */
        int prev = -1;
        for (int s : stops) {
            if (prev != -1) {
                cout << " - "  << index_to_nlc[s].first << "(" << get_name(s) << ")" << " (" << ticket_codes_using[prev][s] << ", " <<  AM[prev][s] << "p)\n";
            } else {
                cout << " - "  << index_to_nlc[s].first << "(" << get_name(s) << ")"  << "\n";
            }
            prev = s;
        }
    }
}

int main(int argc, char** argv) {
    
    /* Check CMD Line arguments */
    if (argc != 6) {
        cout << "Usage: <program executable> <base fare path> <nlc csv file> <travel date string> <starting stations file> <cost limit>" << '\n';
        cout << "For example: ./a.out fetch/fares_data/RJFAF063 fetch/nlcs_corpus.csv 16062024 starting_stations.txt 2000" << endl;
        return -1;
    }

    /* Process CMD Line arguments */
    string BASE_FARE_PATH(argv[1]);         // You need to download the fare data from https://opendata.nationalrail.co.uk/ (see fetch.py)
    string NLC_CSV_FILE(argv[2]);           // You can get this from CORPUS Open Data which is owned by /Network/ Rail https://wiki.openraildata.com/index.php?title=Reference_Data#CORPUS:_Location_Reference_Data  
    string TRAVEL_DATE_STRING(argv[3]);     // E.g. 29022024 for 29th February 2024
    string STARTING_STATIONS_FILE(argv[4]); // E.g. starting_stations.txt. The names used need to match those from CORPUS.
    int COST_LIMIT = fast_atoi(argv[5]);    // E.g. 2000 for 20 pounds. (in pence)

    /* Process Fares Data */
    index_to_nlc.reserve(N_NLC_APPROX);
    process_cluster_file(BASE_FARE_PATH, TRAVEL_DATE_STRING);
    process_nlc_data_file(NLC_CSV_FILE, BASE_FARE_PATH);
    int N = nlc_to_index.size();
    process_flows_file(N, BASE_FARE_PATH, TRAVEL_DATE_STRING);
    convert_AM_to_AL(N);

    /* Get and display the results! */
    do_dijkstra(N, STARTING_STATIONS_FILE, COST_LIMIT);

    return 0;
}


