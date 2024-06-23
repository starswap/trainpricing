#include <bits/stdc++.h>
using namespace std;

unordered_map<string, vector<int>> clusters;
unordered_map<int, string> nlc_to_name;
unordered_map<string, int> name_to_nlc;
unordered_map<string, int> nlc_to_index;
unordered_map<int, tuple<int, int, bool>> flow_id_to_vertices;
vector<pair<string, bool>> index_to_nlc; // true = cluster
vector<vector<int>> AM;
vector<vector<pair<int, int>>> AL;
vector<vector<string>> ticket_codes_using;

const int INF = 1'000'000'000;

// We eliminate all fares below £1 as these are typically special case fares which are not available in practice
const int MIN_SANE_FARE = 100;

// Tracks the current index we are on (index of the next NLC) in the compressed graph representation.
int nlc_idx = 0;

// In an attempt to salvage something from the data I tried limiting myself
// to only fares which are not advances and therefore the prices are fixed and known.
// Check https://raileasy.co.uk/fare/CDS for example to find out what 
// these codes correspond to.
bool acceptable_ticket(string code) {
    return code == "CDS" || code == "CBB" || code == "SDS" || code == "SWS";
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

// https://stackoverflow.com/questions/1577475/c-sorting-and-keeping-track-of-indexes
template <typename T>
vector<size_t> sort_indexes(const vector<T> &v) {
  vector<size_t> idx(v.size());
  iota(idx.begin(), idx.end(), 0);
  stable_sort(idx.begin(), idx.end(), [&v](size_t i1, size_t i2) {return v[i1] < v[i2];});
  return idx;
}

// Converts a date of the form 01012024 to a number of seconds since the epoch
tuple<int, int, int> parse(string date_string) {
    int day = stoi(date_string.substr(0, 2));
    int month = stoi(date_string.substr(2, 2));
    int year = stoi(date_string.substr(4, 4));
    return make_tuple(year, month, day);
}

// Determines if we are currently in the period starting on start_date_string
// and ending on end_date_string inclusive
bool is_active(string start_date_string, string travel_date_string,  string end_date_string) {    
    auto start = parse(start_date_string);
    auto travel = parse(travel_date_string);
    auto end = parse(end_date_string);
    return start <= travel && (end_date_string == "31122999" || travel <= end);
}

// Displays the station clusters defined by the imported fares data so that we can
// try to work out what they correspond to. 
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
        name = nlc_to_name[stoi(nlc)];
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
                // These correspond to stations that we don't have the NLC of. Typically this is things that are not really stations like ferry terminals or bus stops. As we lack the data and only really care about taking the train here we ignore these records. 
                invalid_count++; 
                continue;
            }; 
            valid_count++;
            clusters[cluster_nlc].push_back(stoi(target_nlc));
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
            int nlc = stoi(results[1]);
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
    ticket_codes_using.assign(N, vector<string>(N, ""));
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
                const int flow_id = stoi(line.substr(42, 7));

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
                const int flow_id = stoi(line.substr(2, 7));
                if (!flow_id_to_vertices.count(flow_id)) continue;
                const string ticket_code = line.substr(9, 3);
                const int fare_in_pence = stoi(line.substr(12, 8));
                auto [u, v, reversible] = flow_id_to_vertices[flow_id];

                for (int i = 0; i < 2; ++i) { // Try both ways round
                    if (AM[u][v] > fare_in_pence && fare_in_pence > MIN_SANE_FARE && (acceptable_ticket(ticket_code))) {
                        AM[u][v] = fare_in_pence;
                        ticket_codes_using[u][v] = ticket_code;
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

// // Reads in the actual flows and fares data itself.
// void find_f_records(string base_fare_path) {
//     string line;
//     cout << "Finding start point" << endl;
//     ifstream flows(base_fare_path + ".FFL"); 
    
//     if (flows.is_open()) {
//         for (int i = 0; i < 4; ++i) { // skip first 3 comments
//             getline(flows, line);
//         }
        
//         long records;
//         sscanf(line.c_str(),"/!! Records: %ld",&records);
//         while (line[0] == '/') {
//             getline(flows, line);
//         }

//         int lineLength = line.size();
//         auto lo = flows.tellg();


//     // int lo = 0;
//     // int hi = 
    
//     //         if (line[1] == 'F') {
//     //         } else if (line[1] == 'T') {
//     //         } else {
//     //             cout << "Illegal line: " << line << endl;
//     //         }
//         // }
//         flows.close();
//     } else {
//         cout << "Failed to open flows file" << endl;
//     }
// }


int main(int argc, char** argv) {
    if (argc != 5) {
        cout << "Usage: <program executable> <base fare path> <nlc csv file> <travel date string> <starting stations file>" << '\n';
        cout << "For example: ./a.out fetch/fares_data/RJFAF063 fetch/nlcs_corpus.csv 16062024 starting_stations.txt" << endl;
        return -1;
    }

    string BASE_FARE_PATH(argv[1]);  // You need to download the fare data from https://opendata.nationalrail.co.uk/ (see fetch.py)
    string NLC_CSV_FILE(argv[2]); // You can get this from CORPUS Open Data which is owned by /Network/ Rail https://wiki.openraildata.com/index.php?title=Reference_Data#CORPUS:_Location_Reference_Data  
    string TRAVEL_DATE_STRING(argv[3]); // E.g. 29022024 for 29th February 2024
    string STARTING_STATIONS_FILE(argv[4]); // E.g. starting_stations.txt. The names used need to match those from CORPUS.

    // find_f_records(BASE_FARE_PATH);

    index_to_nlc.reserve(5000); // I know that there are about 5000 stations and clusters.

    process_cluster_file(BASE_FARE_PATH, TRAVEL_DATE_STRING);
    process_nlc_data_file(NLC_CSV_FILE, BASE_FARE_PATH);
    int N = nlc_to_index.size();
    unordered_set<int> starting_stations = get_starting_stations(N, STARTING_STATIONS_FILE);
    process_flows_file(N, BASE_FARE_PATH, TRAVEL_DATE_STRING);

    AL.assign(N, vector<pair<int, int>>());
    // Construct Adjacency List
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (AM[i][j] < INF) {
                AL[i].emplace_back(j, AM[i][j]);
            }
        }
    }
    
    cout << "Processed " << starting_stations.size() << " starting stations." << endl;
    vector<int> cost(N, INF);
    priority_queue<pair<int, int>> pq;

    for (int station : starting_stations) {
        int one_start_nlc = nlc_to_index[to_string(station)];
        cost[one_start_nlc] = 0;
        pq.emplace(-cost[one_start_nlc], one_start_nlc);
    }

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

    for (auto i: sort_indexes(cost)) {
        string name = get_name(i);

        if (cost[i] > 2000) break;
        if (index_to_nlc[i].second) continue;
        // cout << name << " - " << cost[i] << '\n';
        cout << name << '\n';

        // cout << "We can reach " << name << " for " << cost[i] << '\n';
        // vector<int> stops;
        // stops.push_back(i);
        // while (i != parent[i]) {
        //     i = parent[i];
        //     stops.push_back(i);
        // }
        // reverse(stops.begin(), stops.end());
        // int prev = -1;
        // for (int s : stops) {
        //     if (prev != -1) {
        //         cout << " - "  << index_to_nlc[s].first << "(" << get_name(s) << ")" << " (" << ticket_codes_using[prev][s]<< ", " <<  AM[prev][s] << "p)\n";
        //     } else {
        //         cout << " - "  << index_to_nlc[s].first << "(" << get_name(s) << ")"  << "\n";
        //     }

        //     prev = s;
        // }
    }

    return 0;
}


