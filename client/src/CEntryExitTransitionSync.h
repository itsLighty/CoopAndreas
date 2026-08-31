#pragma once

class CEntryExit;
class CPed;
class CNetworkPlayer;

namespace Packets::Players
{
class EnExTransition;
}

class CEntryExitTransitionSync
{
public:
    static void OnTransitionStarted(CEntryExit* pEntryExit, CPed* pPed);
    static void OnTransitionFinished(CEntryExit* pEntryExit, CPed* pPed);
    static void Receive(const Packets::Players::EnExTransition& packet);
    static void ReplayPending(CNetworkPlayer* player);
    static void Process();
    static void Reset();

private:
    static inline CEntryExit* ms_pLocalAnimatedTransition = nullptr;
};
