#include <stdlib.h>
#include "packet.h"

// Simulate packet drop based on probability
int drop(float prob) {
    return ((float)rand() / RAND_MAX) < prob;
}
