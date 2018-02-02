#ifndef IQOPTIONTESTTASK_OVERSEER_H
#define IQOPTIONTESTTASK_OVERSEER_H

#include "core_data.h"

// --------------------------------------------------------------------- //
/*
 *  Overseer class
 *
 *  The "top manager" of all the service activity and the storage of all
 *  the actual rating data. Instantiate it and let it run. Once it's done,
 *  it means that something irreversible has happened and the service
 *  is to be terminated
 */
// --------------------------------------------------------------------- //

struct PluggableInfrastructure;

class Overseer {
public:

    // this is done to allow m_pluggable to compile with an incomplete type
    Overseer ();
    ~Overseer ();

    void run (unsigned short portNumberToBindTo);

private:

    CoreRatingData m_coreData;
    CoreDataSyncBlock m_syncBlock;
    IterationData m_iterationData;

    //std::unique_ptr<IncomingDataDoubleBuffer> m_incomingData;

    std::unique_ptr<PluggableInfrastructure> m_pluggable;
};

#endif //IQOPTIONTESTTASK_OVERSEER_H
