#include "namespace.h"
#include "DynOpManager.h"
#include "EdgeManager.h"
#include "DynamicGraph.h"
#include <iostream>

// using namespace std;

DynOpManager :: DynOpManager(int maxId, std::string file_name)
{
    active= 0;
    maxInstanceId = maxId;
    outFile.open(file_name.c_str(), std::ios::out);
    outFile << std::setprecision(17);
    outFile << "Label  MaxInDeg  OneMinusEpsMaxInDeg  MaxDensity  Size  TimePerWindow(micros)  NumOpPerWindow  TimePerOp\n";
    outFile.flush();
}

int DynOpManager :: bindGraph(std::vector<DynamicGraph> &G)
{
    DG = G;
    return 0;
}

int DynOpManager :: addEdge(edgeVector &currentEdge, std::vector<EdgeIdx> &eDupId, EdgeManager &EM)
{
    double rho, rhoEst, rhoEstActive;
    // Duplication of edges...
    for(unsigned int dup = 0; dup < eDupId.size(); ++dup)
    {
        DG[1].insertEdge(currentEdge, eDupId[dup], EM);
    }
    return 0;
}

int DynOpManager :: removeEdge(edgeVector &currentEdge, std::vector<EdgeIdx> &eDupId, EdgeManager &EM)
{
    double rho, rhoEst, rhoEstActive;
    for(int dup = 0; dup < eDupId.size(); ++dup)
    {
        // EdgeIdx delEdgeId = DG[0].checkEdgeExistence(currentEdge);
        EdgeIdx delEdgeId = eDupId[dup];
        if(delEdgeId != NullEdgeIdx)
        {
            VertexIdx lastVertex = DG[1].deleteEdge(currentEdge, delEdgeId, EM);
        }
        else
        {
            std::cout << "Edge does not exists to delete...\n";
            break;
        }
    }
    return 0;
}


int DynOpManager :: getDensityEstimate(EdgeManager &EM, double localAlpha, vectorListMap &mainEdge2Ids, double micros, Count numOpPerWindow, std::string label)
{
    double maxInDeg, OneMinusEpsMaxInDeg;
    maxInDeg = (DG[1].getMaxLabel()*1.0)/localAlpha;
    OneMinusEpsMaxInDeg = DG[1].getDensity()/localAlpha;
    
    Count duplicationFactor = (Count)localAlpha;
    std::set<VertexIdx> denseSubgraph = DG[1].querySubgraph(localAlpha);
    unsigned int denseSubgraphSize = denseSubgraph.size();
    double estimatedDensity =  DG[1].getDensityOfInducedSubgraph(denseSubgraph, mainEdge2Ids, duplicationFactor)/localAlpha;

    std::cout << "Getting max partition density....\n";
    std::pair<double, unsigned int> maxPartitionedDensity = DG[1].getMaxPartitionDensity(mainEdge2Ids, duplicationFactor);

    std::cout << label << " -- Reporting Density... - " << (maxPartitionedDensity.first)/duplicationFactor << " " << maxPartitionedDensity.second << std::endl;

    outFile << label << " " << std::setw(10) << maxInDeg << " " << std::setw(10) << OneMinusEpsMaxInDeg << " " << std::setw(10) << (maxPartitionedDensity.first)/duplicationFactor << " " << maxPartitionedDensity.second << " " << std::setw(10) << micros << " " << numOpPerWindow << " " << std::setw(10) << micros/numOpPerWindow << "\n";
    
    return 0;
}


int DynOpManager :: getWeightedDensityEstimate(EdgeManager &EM, double localAlpha, vectorListMap &mainEdge2Ids, double micros, Count numOpPerWindow, std::string label, Count maxWeight, int logn, int setNewEta = 0)
{
    double maxInDeg, OneMinusEpsMaxInDeg, maxInDegUnScaled;
    double constMultiplier = maxWeight/logn;
    double dividingFactor = (localAlpha * 1.0)/constMultiplier;
    if (dividingFactor > 1.0)
    {
        dividingFactor = 1.0;
    }
    
    maxInDeg = (DG[1].getMaxLabel()*1.0)/dividingFactor;
    OneMinusEpsMaxInDeg = (DG[1].getDensity()*1.0)/dividingFactor;
    maxInDegUnScaled = maxInDeg * dividingFactor;

    Count duplicationFactor = (Count)localAlpha;
    std::set<VertexIdx> denseSubgraph = DG[1].querySubgraph(localAlpha);
    unsigned int denseSubgraphSize = denseSubgraph.size();
    double estimatedDensity =  DG[1].getWeightedDensityOfInducedSubgraph(denseSubgraph, mainEdge2Ids, EM)/dividingFactor;

    std::cout << "Getting max partition density....\n";
    std::pair<double, unsigned int> maxPartitionedDensity = DG[1].getWeightedMaxPartitionDensity(mainEdge2Ids, EM, constMultiplier, dividingFactor);

    std::cout << label << " -- MaxInDeg... - " << maxInDeg  << " -- unScaled -- " << maxInDegUnScaled << std::endl;
    std::cout << label << " -- Estimated Density... - " << estimatedDensity << std::endl;
    std::cout << label << " -- Reporting Density... - " << maxPartitionedDensity.first << " " << maxPartitionedDensity.second << std::endl;

    outFile << label << " " << std::setw(10) << maxInDeg << " " << std::setw(10) << OneMinusEpsMaxInDeg << " " << std::setw(10) << maxPartitionedDensity.first << " " << maxPartitionedDensity.second << " " << std::setw(10) << micros << " " << numOpPerWindow << " " << std::setw(10) << micros/numOpPerWindow << "\n";
    outFile.flush();

    if(setNewEta == 1)
    {
        // int newEtaVal = maxInDegUnScaled/2;
        DG[1].setEta(maxInDegUnScaled);
    }

    return 0;
}


// not used...
int DynOpManager :: addPendingEdgesToActiveInstance(EdgeManager &EM)
{
    // if(DG[DOM.active].getPendingCount() > 0)
    if(DG[active].pendingListOfEdges.size() > 0)
    {
        // std::cout << "I am coming here.." << DG[active].getPendingCount() << "\n";
        DG[active].insertListOfPendingEdges(EM);
        // std::cout << "I am coming here.." << DG[active].getPendingCount() << "\n";
    }
    return 0;
}