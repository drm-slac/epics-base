/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/*
 *  $Id$
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */

#define epicsAssertAuthor "Jeff Hill johill@lanl.gov"

#include "iocinf.h"
#include "cac.h"
#include "virtualCircuit.h"

//
// the recv watchdog timer is active when this object is created
//
tcpRecvWatchdog::tcpRecvWatchdog 
    ( epicsMutex & cbMutexIn, cacContextNotify & ctxNotifyIn,
            epicsMutex & mutexIn, tcpiiu & iiuIn, 
            double periodIn, epicsTimerQueue & queueIn ) :
        period ( periodIn ), timer ( queueIn.createTimer () ),
        cbMutex ( cbMutexIn ), ctxNotify ( ctxNotifyIn ), 
        mutex ( mutexIn ), iiu ( iiuIn ), 
        probeResponsePending ( false ), beaconAnomaly ( true ), 
        probeTimeoutDetected ( false )
{
}

tcpRecvWatchdog::~tcpRecvWatchdog ()
{
    this->timer.destroy ();
}

epicsTimerNotify::expireStatus
tcpRecvWatchdog::expire ( const epicsTime & /* currentTime */ ) // X aCC 361
{
    callbackManager mgr ( this->ctxNotify, this->cbMutex );
    if ( this->probeResponsePending ) {
        if ( this->iiu.bytesArePendingInOS() ) {
            this->iiu.printf ( mgr.cbGuard,
    "The CA client library's server inactivity timer initiated server disconnect\n" );
            this->iiu.printf ( mgr.cbGuard,
    "despite the fact that messages from this server are pending for processing in\n" );
            this->iiu.printf ( mgr.cbGuard,
    "the client library. Here are some possible causes of the unnecessary disconnect:\n" );
            this->iiu.printf ( mgr.cbGuard,
    "o ca_pend_event() or ca_poll() have not been called for %f seconds\n", 
                this->period  );
            this->iiu.printf ( mgr.cbGuard,
    "o application is blocked in a callback from the client library\n" );
        }
        {
            epicsGuard < epicsMutex > guard ( this->mutex );
#           ifdef DEBUG
                char hostName[128];
                this->iiu.hostName ( guard, hostName, sizeof (hostName) );
                debugPrintf ( ( "CA server \"%s\" unresponsive after %g inactive sec"
                            "- disconnecting.\n", 
                    hostName, this->period ) );
#           endif
            this->iiu.receiveTimeoutNotify ( mgr, guard );
            this->probeTimeoutDetected = true;
        }
        return noRestart;
    }
    else {
        {
            epicsGuard < epicsMutex > guard ( this->mutex );
            this->probeTimeoutDetected = false;
            this->probeResponsePending = this->iiu.setEchoRequestPending ( guard );
        }
        debugPrintf ( ("circuit timed out - sending echo request\n") );
        return expireStatus ( restart, CA_ECHO_TIMEOUT );
    }
}

void tcpRecvWatchdog::beaconArrivalNotify ( 
    epicsGuard < epicsMutex > & guard, const epicsTime & currentTime )
{
    guard.assertIdenticalMutex ( this->mutex );
    if ( ! this->beaconAnomaly && ! this->probeResponsePending ) {
        epicsGuardRelease < epicsMutex > unguard ( guard );
        this->timer.start ( *this, currentTime + this->period );
        debugPrintf ( ("saw a normal beacon - reseting circuit receive watchdog\n") );
    }
}

//
// be careful about using beacons to reset the connection
// time out watchdog until we have received a ping response 
// from the IOC (this makes the software detect reconnects
// faster when the server is rebooted twice in rapid 
// succession before a 1st or 2nd beacon has been received)
//
void tcpRecvWatchdog::beaconAnomalyNotify ( 
    epicsGuard < epicsMutex > & guard )
{
    guard.assertIdenticalMutex ( this->mutex );
    this->beaconAnomaly = true;
    debugPrintf ( ("Saw an abnormal beacon\n") );
}

void tcpRecvWatchdog::messageArrivalNotify ( 
    const epicsTime & currentTime )
{
    bool restartNeeded = false;
    {
        epicsGuard < epicsMutex > guard ( this->mutex );
        if ( ! this->probeResponsePending ) {
            this->beaconAnomaly = false;
            restartNeeded = true;
        }
    }
    // dont hold the lock for fear of deadlocking 
    // because cancel is blocking for the completion
    // of the recvDog expire which takes the lock
    // - it take also the callback lock
    if ( restartNeeded ) {
        this->timer.start ( *this, currentTime + this->period );
        debugPrintf ( ("received a message - reseting circuit recv watchdog\n") );
    }
}

void tcpRecvWatchdog::probeResponseNotify ( 
    epicsGuard < epicsMutex > & cbGuard, 
    const epicsTime & currentTime )
{
    bool restartNeeded = false;
    double restartDelay = DBL_MAX;
    {
        epicsGuard < epicsMutex > guard ( this->mutex );
        if ( this->probeResponsePending ) {
            restartNeeded = true;
            if ( this->probeTimeoutDetected ) {
                this->probeTimeoutDetected = false;
                this->probeResponsePending = this->iiu.setEchoRequestPending ( guard );
                restartDelay = CA_ECHO_TIMEOUT;
                debugPrintf ( ("late probe response - sending another probe request\n") );
            }
            else {
                this->probeResponsePending = false;
                restartDelay = this->period;
                this->iiu.responsiveCircuitNotify ( cbGuard, guard );
                debugPrintf ( ("probe response on time - circuit will be tagged reponsive if unresponsive\n") );
            }
        }
    }
    if ( restartNeeded ) {
        // timer callback takes the callback mutex and the cac mutex
        epicsGuardRelease < epicsMutex > cbGuardRelease ( cbGuard );
        epicsTime expireTime = currentTime + restartDelay;
        this->timer.start ( *this, expireTime );
    }
}

//
// The thread for outgoing requests in the client runs 
// at a higher priority than the thread in the client
// that receives responses. Therefore, there could 
// be considerable large array write send backlog that 
// is delaying departure of an echo request and also 
// interrupting delivery of an echo response. 
// We must be careful not to timeout the echo response as 
// long as we see indication of regular departures of  
// message buffers from the client in a situation where 
// we know that the TCP send queueing has been exceeded. 
// The send watchdog will be responsible for detecting 
// dead connections in this case.
//
void tcpRecvWatchdog::sendBacklogProgressNotify ( 
    epicsGuard < epicsMutex > & guard,
    const epicsTime & currentTime )
{
    guard.assertIdenticalMutex ( this->mutex );

    // We dont set "beaconAnomaly" to be false here because, after we see a
    // beacon anomaly (which could be transiently detecting a reboot) we will 
    // not trust the beacon as an indicator of a healthy server until we 
    // receive at least one message from the server.
    if ( this->probeResponsePending ) {
        // we avoid calling this with the lock applied because
        // it restarts the recv wd timer, this might block
        // until a recv wd timer expire callback completes, and 
        // this callback takes the lock
        epicsGuardRelease < epicsMutex > unguard ( guard );
        this->timer.start ( *this, currentTime + CA_ECHO_TIMEOUT );
        debugPrintf ( ("saw heavy send backlog - reseting circuit recv watchdog\n") );
    }
}

void tcpRecvWatchdog::connectNotify ()
{
    this->timer.start ( *this, this->period );
    debugPrintf ( ("connected to the server - initiating circuit recv watchdog\n") );
}

void tcpRecvWatchdog::sendTimeoutNotify ( 
    epicsGuard < epicsMutex > & cbGuard,
    epicsGuard < epicsMutex > & guard,
    const epicsTime & currentTime )
{
    guard.assertIdenticalMutex ( this->mutex );

    bool restartNeeded = false;
    if ( ! this->probeResponsePending ) {
        this->probeResponsePending = this->iiu.setEchoRequestPending ( guard );
        restartNeeded = true;
    }
    if ( restartNeeded ) {
        epicsGuardRelease < epicsMutex > unguard ( guard );
        {
            epicsGuardRelease < epicsMutex > cbUnguard ( cbGuard );
            this->timer.start ( *this, currentTime + CA_ECHO_TIMEOUT );
        }
    }
    debugPrintf ( ("TCP send timed out - sending echo request\n") );
}

void tcpRecvWatchdog::cancel ()
{
    this->timer.cancel ();
    debugPrintf ( ("canceling TCP recv watchdog\n") );
}

double tcpRecvWatchdog::delay () const
{
    return this->timer.getExpireDelay ();
}

void tcpRecvWatchdog::show ( unsigned level ) const
{
    epicsGuard < epicsMutex > guard ( this->mutex );

    ::printf ( "Receive virtual circuit watchdog at %p, period %f\n",
        static_cast <const void *> ( this ), this->period );
    if ( level > 0u ) {
        ::printf ( "\t%s %s %s\n",
            this->probeResponsePending ? "probe-response-pending" : "", 
            this->beaconAnomaly ? "beacon-anomaly-detected" : "",
            this->probeTimeoutDetected ? "probe-response-timeout" : "" );
    }
}

