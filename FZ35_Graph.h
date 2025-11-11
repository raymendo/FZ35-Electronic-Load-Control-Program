#pragma once
#include <Arduino.h>

/**
 * @file FZ35_Graph.h
 * @brief Ring buffer storage for recent measurement samples (voltage/current/power).
 * GRAPH_POINTS defines capacity; addGraphPoint advances index circularly.
 */

// 5-minute buffer: at ~300 ms sample => 5*60*1000 / 300 = 1000 samples
#define GRAPH_POINTS 1000

/**
 * @struct GraphData
 * @brief Holds circular arrays for plotting plus current write index.
 */
struct GraphData {
    float voltage[GRAPH_POINTS];
    float current[GRAPH_POINTS];
    float power[GRAPH_POINTS];
    int idx;
};

extern GraphData graph;

/**
 * @brief Push one sample into the ring buffer.
 * @param v Voltage (V)
 * @param i Current (A)
 * @param p Power (W)
 */
void addGraphPoint(float v, float i, float p){
    graph.voltage[graph.idx] = v;
    graph.current[graph.idx] = i;
    graph.power[graph.idx] = p;
    graph.idx = (graph.idx + 1) % GRAPH_POINTS;
}
