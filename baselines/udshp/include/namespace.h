#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <map>
#include <queue>
#include <algorithm>
#include <string>
#include <set>
#include <list>
#include <chrono>
#include <iomanip>

// using namespace std;

using VertexIdx = int;
using EdgeIdx   = int;
using Count     = int;
using Weight 	= int;
using ePair 	= std::pair<VertexIdx, VertexIdx>;
using edgeVector	= std::vector<VertexIdx>;

const VertexIdx NullVertexIdx = -1;
const EdgeIdx NullEdgeIdx = -1;

typedef struct EdgeUpdate
{
    edgeVector vertices;
    int timestamp;
    int weight;
	bool is_add;
	bool is_report;
	std::string report_label;
} EdgeUpdate;

struct vectorHash
{
    std::size_t operator()(std::vector<VertexIdx> const& vec) const 
    {
        std::size_t seed = vec.size();
        for(auto& i : vec) 
        {
            seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

// std::unordered_map<std::vector<VertexIdx>, EdgeIdx, vectorHash> mainEdge2Ids;

using VEPair 	= std::pair<VertexIdx, EdgeIdx>;
using eTupleUnWeighted	= std::tuple<VertexIdx, VertexIdx>;
using eTupleWeighted	= std::tuple<VertexIdx, VertexIdx, Count>;

using revItMapCountSetVertices = std::map<Count, std::set<VertexIdx>>::reverse_iterator;
using vectorListMap = std::unordered_map<std::vector<VertexIdx>, std::list<EdgeIdx>, vectorHash>;

#endif      // NAMESPACE_H