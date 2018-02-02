#ifndef IQOPTIONTESTTASK_RATING_CALCULATOR_H
#define IQOPTIONTESTTASK_RATING_CALCULATOR_H

#include <memory>

struct CoreRatingData;
struct CoreDataSyncBlock;
struct IterationData;
struct IncomingDataDoubleBuffer;
class JobQueue;

class RatingCalculator {
public:

    RatingCalculator (CoreRatingData& userData, CoreDataSyncBlock& coreSync,
                      IterationData& iterationData, IncomingDataDoubleBuffer& incomingData, JobQueue& jobQueue);

    void recalculate (bool dropOldRating);

private:

    CoreRatingData& m_userData;
    CoreDataSyncBlock& m_coreSync;
    IterationData& m_iterationData;
    IncomingDataDoubleBuffer& m_incomingData;

    JobQueue& m_jobQueue;
};

#endif //IQOPTIONTESTTASK_RATING_CALCULATOR_H
