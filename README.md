# An experiment with the UK rail fares data
I was interested in finding out the best value rail days out I could make from any London station, so I wrote some code to find out, and wrote a blog post about it on the [Imperial Computing Blog](https://blogs.imperial.ac.uk/computing/).

Unfortunately this didn't really give as much of an idea as I was hoping it would because of the quota controlled nature of the Advance fares (as discussed in the post, and much bemoaned on rail forums), but it was still a fun experiment and could be useful in theory.

If at any point the quota data becomes available I might revisit it.

In any case if you want to run this yourself for any reason you need to:
1. Sign up for National Rail Open Data (https://opendata.nationalrail.co.uk/) and Network Rail Open Data (https://publicdatafeeds.networkrail.co.uk/)
2. Get the fares data from National Rail Open Data using the `fetch.py` script
3. Download and unzip the CORPUS data from Network Rail
4. Run the `convert_corpus.py` script across the resulting JSON file to put the data in the target format for trains.cpp
5. Compile and run trains.cpp, providing the data files and a travel date (used to ensure we only cite fares from the fares period encompassing the target travel date). For example:
```
./a.out "../fetch/fares_data/RJFAF063" "../fetch/nlcs_corpus.csv" "16062024" "starting_stations.txt"
```

## Future Improvements
- Include non derivable fares and railcard discounts
- Try using a variety of tickets on each journey leg (e.g. the five best) and get a range of costs for a given journey.
- Treat clusters differently by adding direct station to station links for every cluster flow. This would enable us to benefit from clusters at the end of the journey as well.
