#include "stdafx.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <chrono>
#endif

namespace
{
uint32_t GetMonotonicMilliseconds()
{
#if defined(_WIN32)
    return GetTickCount();
#else
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
#endif
}
}

server_time_t g_serverTime = 0;

uint32_t CServerTime::m_serverStartedAt = 0;

void CServerTime::Init()
{
    m_serverStartedAt = GetMonotonicMilliseconds();
}

void CServerTime::Update()
{
    g_serverTime = GetMonotonicMilliseconds() - m_serverStartedAt;
}

PACKET_HANDLER(ePacketType::SERVER_TIME_REQUEST, Packets::System::ServerTimeRequest* pServerTimeRequest,
    CNetworkPlayer* pNetworkPlayer)
{
    pServerTimeRequest->serverTimeRespond = g_serverTime;
    GetPacketFactory().Send(*pServerTimeRequest, pNetworkPlayer);
}
