#ifndef EDGE_MANAGER_H
#define EDGE_MANAGER_H

#include "namespace.h"
#include <random>

class EdgeManager
{
    public:
        EdgeIdx currentEdgeId;
        std::vector<std::vector<VertexIdx>> edgeDupList;
        std::unordered_map<EdgeIdx, std::vector<VertexIdx>> edgeDupMap;
        std::unordered_map<std::vector<VertexIdx>, std::list<EdgeIdx>, vectorHash> edge2IdsList;
        vectorListMap mainEdgeSampledWeight;

        EdgeManager()
        {
            currentEdgeId = 0;
        }
        std::vector<EdgeIdx> getEdgeIdsAfterDuplication(std::vector<VertexIdx> &edgeV, Count dupFactor);
        std::vector<EdgeIdx> getEdgeIdsAfterWeightDuplication(std::vector<VertexIdx> &edgeV, Count origWeight, Count maxWeight, Count samplingProbConstant, int logn);
        std::vector<EdgeIdx> retrieveDuplicatedEdgeIds(std::vector<VertexIdx> &edgeV, Count dupFactor);
        std::vector<EdgeIdx> retrieveWeightDuplicatedEdgeIds(std::vector<VertexIdx> &edgeV);
        int removeEdgeFromMemory(std::vector<EdgeIdx> edgeDuplicatorIds);
};

#endif  // EDGE_MANAGER_H