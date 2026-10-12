#include "namespace.h"
#include "EdgeManager.h"

std::vector<EdgeIdx> EdgeManager :: getEdgeIdsAfterDuplication(std::vector<VertexIdx> &edgeV, Count dupFactor)
{

    std::vector<EdgeIdx> dupEdgeIds;
    for(Count i = 0; i < dupFactor; i++)
    {
        // edgeDupList.push_back(edgeV);
        dupEdgeIds.push_back(currentEdgeId);
        edgeDupMap[currentEdgeId] = edgeV;
        currentEdgeId += 1;
    }
    std::list<EdgeIdx>::iterator listIt = edge2IdsList[edgeV].end();
    edge2IdsList[edgeV].insert(listIt, dupEdgeIds.begin(), dupEdgeIds.end());
    return dupEdgeIds;
}

std::vector<EdgeIdx> EdgeManager :: getEdgeIdsAfterWeightDuplication(std::vector<VertexIdx> &edgeV, Count origWeight, Count maxWeight, Count samplingProbConstant, int logn)
{
    float probVal, finalProbVal;
    Count successVal, dupFactor;

    if(origWeight > 1)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        probVal = (samplingProbConstant * logn * 1.0)/maxWeight;
        finalProbVal = probVal > 1 ? 1 : probVal;
        std::binomial_distribution<> binDist(origWeight, finalProbVal);
        successVal = binDist(gen);
        dupFactor = successVal > 0 ? successVal : 1;
    }
    else
    {
        dupFactor = 1;
    }
    mainEdgeSampledWeight[edgeV].push_back(dupFactor);
    
    // std::cout << origWeight << " " << dupFactor << " " << probVal << std::endl;  
    std::vector<EdgeIdx> dupEdgeIds;
    for(Count i = 0; i < dupFactor; i++)
    {
        // edgeDupList.push_back(edgeV);
        dupEdgeIds.push_back(currentEdgeId);
        edgeDupMap[currentEdgeId] = edgeV;
        currentEdgeId += 1;
    }
    std::list<EdgeIdx>::iterator listIt = edge2IdsList[edgeV].end();
    edge2IdsList[edgeV].insert(listIt, dupEdgeIds.begin(), dupEdgeIds.end());
    return dupEdgeIds;
}

std::vector<EdgeIdx> EdgeManager :: retrieveDuplicatedEdgeIds(std::vector<VertexIdx> &edgeV, Count dupFactor)
{
    std::vector<EdgeIdx> dupEdgeIds;
    for(Count i = 0; i < dupFactor; i++)
    {
        dupEdgeIds.push_back(edge2IdsList[edgeV].front());
        edge2IdsList[edgeV].pop_front();
    }
    return dupEdgeIds;
}

std::vector<EdgeIdx> EdgeManager :: retrieveWeightDuplicatedEdgeIds(std::vector<VertexIdx> &edgeV)
{
    Count dupFactor = mainEdgeSampledWeight[edgeV].front();
    std::vector<EdgeIdx> dupEdgeIds;
    for(Count i = 0; i < dupFactor; i++)
    {
        dupEdgeIds.push_back(edge2IdsList[edgeV].front());
        edge2IdsList[edgeV].pop_front();
    }
    mainEdgeSampledWeight[edgeV].pop_front();
    return dupEdgeIds;
}


int EdgeManager :: removeEdgeFromMemory(std::vector<EdgeIdx> edgeDuplicatorIds)
{
    for(Count i = 0; i < edgeDuplicatorIds.size(); i++)
    {
        edgeDupMap.erase(edgeDuplicatorIds[i]);
    }

    return 0;
}

// int main()
// {
//     EdgeManager EM;
//     std::vector<VertexIdx> e{1,2,3,4};
//     std::vector<EdgeIdx> eidx = EM.abc(e, 1);
//     return 0;
// }