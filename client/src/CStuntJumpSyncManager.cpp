#include "stdafx.h"
#include "CStuntJumpSyncManager.h"

#include "CMissionSessionClient.h"

#include <CBoundingBox.h>
#include <CPool.h>

#include <algorithm>
#include <cmath>

using namespace Packets::Stunts;

namespace
{
struct NativeStuntJump
{
    CBoundingBox start;
    CBoundingBox finish;
    CVector camera;
    int32_t reward;
    bool done;
    bool found;
    uint8_t padding[2];
};

static_assert(sizeof(NativeStuntJump) == 0x44, "GTA SA 1.0 US CStuntJump layout changed");
using NativeStuntPool = CPool<NativeStuntJump>;

NativeStuntPool* GetNativePool()
{
    return *reinterpret_cast<NativeStuntPool**>(0xA9A888);
}

NativeStuntJump*& GetNativeActiveJump()
{
    return *reinterpret_cast<NativeStuntJump**>(0xA9A88C);
}

bool& GetNativeHitReward()
{
    return *reinterpret_cast<bool*>(0xA9A891);
}

uint32_t& GetNativeTimer()
{
    return *reinterpret_cast<uint32_t*>(0xA9A894);
}

uint8_t& GetNativeJumpState()
{
    return *reinterpret_cast<uint8_t*>(0xA9A898);
}

int32_t& GetNativeJumpCount()
{
    return *reinterpret_cast<int32_t*>(0xA9A89C);
}

uint32_t& GetNativeCompletedCount()
{
    return *reinterpret_cast<uint32_t*>(0xA9A8A0);
}

bool IsPoolUsable(NativeStuntPool* pool)
{
    return pool != nullptr && pool->m_pObjects != nullptr && pool->m_byteMap != nullptr &&
           pool->m_nSize > 0 && pool->m_nSize <= STUNT_JUMP_CAPACITY;
}

StuntDefinition BuildDefinition(const NativeStuntJump& jump)
{
    StuntDefinition definition{};
    definition.start.minimum = jump.start.m_vecMin;
    definition.start.maximum = jump.start.m_vecMax;
    definition.finish.minimum = jump.finish.m_vecMin;
    definition.finish.maximum = jump.finish.m_vecMax;
    definition.camera = jump.camera;
    definition.reward = jump.reward;
    return definition;
}

NativeStuntJump* FindNativeJump(const StuntId& id)
{
    NativeStuntPool* pool = GetNativePool();
    if (!IsPoolUsable(pool) || id.slot >= static_cast<uint16_t>(pool->m_nSize))
    {
        return nullptr;
    }
    NativeStuntJump* jump = pool->GetAt(id.slot);
    if (jump == nullptr)
    {
        return nullptr;
    }
    const StuntDefinition definition = BuildDefinition(*jump);
    return definition.HasValidSemantics() && definition.CalculateFingerprint() == id.fingerprint ? jump : nullptr;
}

bool ReadMissionFlag()
{
    return CTheScripts::OnAMissionFlag != nullptr &&
           CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] != 0;
}

bool NativeBoxContains(const CBoundingBox& box, const CVector& position)
{
    return position.x >= box.m_vecMin.x && position.x <= box.m_vecMax.x &&
           position.y >= box.m_vecMin.y && position.y <= box.m_vecMax.y &&
           position.z >= box.m_vecMin.z && position.z <= box.m_vecMax.z;
}
}  // namespace

std::array<CStuntJumpSyncManager::CachedState, STUNT_JUMP_CAPACITY> CStuntJumpSyncManager::m_states{};
std::array<CStuntJumpSyncManager::NativeBaseline, STUNT_JUMP_CAPACITY> CStuntJumpSyncManager::m_baseline{};
std::array<uint32_t, STUNT_JUMP_CAPACITY> CStuntJumpSyncManager::m_appliedAwardSequences{};
uintptr_t CStuntJumpSyncManager::m_nativeUpdateAddress = 0x49C490;
bool CStuntJumpSyncManager::m_wasAuthenticated = false;
bool CStuntJumpSyncManager::m_wasHost = false;
bool CStuntJumpSyncManager::m_baselineCaptured = false;
uint64_t CStuntJumpSyncManager::m_serverRunId = 0;
uint64_t CStuntJumpSyncManager::m_clientSessionNonce = 0;
uint16_t CStuntJumpSyncManager::m_announcedCatalogCount = 0;
uint32_t CStuntJumpSyncManager::m_announcedCatalogHash = 0;
uint16_t CStuntJumpSyncManager::m_publishCursor = 0;
uint32_t CStuntJumpSyncManager::m_nextPublishAt = 0;
uint32_t CStuntJumpSyncManager::m_nextCatalogRefreshAt = 0;
uint32_t CStuntJumpSyncManager::m_nextRequestId = 0;
bool CStuntJumpSyncManager::m_attemptActive = false;
StuntId CStuntJumpSyncManager::m_attemptId{};
uint32_t CStuntJumpSyncManager::m_attemptRequestId = 0;
uint32_t CStuntJumpSyncManager::m_attemptStartedAt = 0;
bool CStuntJumpSyncManager::m_attemptStartAccepted = false;
bool CStuntJumpSyncManager::m_attemptHitFinish = false;
bool CStuntJumpSyncManager::m_attemptHitFinishAccepted = false;
bool CStuntJumpSyncManager::m_attemptLanded = false;
bool CStuntJumpSyncManager::m_pendingActionActive = false;
eStuntAttemptAction CStuntJumpSyncManager::m_pendingAction = eStuntAttemptAction::START;
uint32_t CStuntJumpSyncManager::m_pendingActionSentAt = 0;
uint32_t CStuntJumpSyncManager::m_pendingActionRetryAt = 0;
uint8_t CStuntJumpSyncManager::m_pendingActionRetryCount = 0;
uint64_t CStuntJumpSyncManager::m_attemptMissionSessionId = 0;
bool CStuntJumpSyncManager::m_attemptMissionWasActive = false;
bool CStuntJumpSyncManager::m_attemptMissionFlag = false;
bool CStuntJumpSyncManager::m_presentationActive = false;
float CStuntJumpSyncManager::m_previousTimeScale = 1.0f;
uint32_t CStuntJumpSyncManager::m_previousBlurRed = 0;
uint32_t CStuntJumpSyncManager::m_previousBlurGreen = 0;
uint32_t CStuntJumpSyncManager::m_previousBlurBlue = 0;
uint32_t CStuntJumpSyncManager::m_previousBlurType = 0;
uint32_t CStuntJumpSyncManager::m_previousMotionBlur = 0;
uint32_t CStuntJumpSyncManager::m_previousMotionBlurAddAlpha = 0;

void CStuntJumpSyncManager::InjectHook()
{
    constexpr uintptr_t stuntUpdateCall = 0x53C0C1;
    const uintptr_t destination = injector::GetBranchDestination(stuntUpdateCall).as_int();
    if (destination != 0x49C490)
    {
        logger::warn("Skipped stunt sync hook because the GTA SA 1.0 US call site was not recognized");
        return;
    }
    m_nativeUpdateAddress = destination;
    patch::RedirectCall(stuntUpdateCall, ProcessNativeUpdate);
}

uint64_t CStuntJumpSyncManager::EnsureClientSessionNonce()
{
    if (m_clientSessionNonce == 0)
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        m_clientSessionNonce = static_cast<uint64_t>(counter.QuadPart) ^
                               (GetTickCount64() << 17) ^
                               (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^
                               0x434F4F505354554EULL;
        if (m_clientSessionNonce == 0)
        {
            m_clientSessionNonce = 1;
        }
    }
    return m_clientSessionNonce;
}

uint32_t CStuntJumpSyncManager::NextRequestId()
{
    do
    {
        ++m_nextRequestId;
    } while (m_nextRequestId == 0);
    return m_nextRequestId;
}

void CStuntJumpSyncManager::CaptureNativeBaseline()
{
    if (m_baselineCaptured)
    {
        return;
    }
    NativeStuntPool* pool = GetNativePool();
    if (!IsPoolUsable(pool))
    {
        return;
    }

    m_baseline = {};
    for (uint16_t slot = 0; slot < static_cast<uint16_t>(pool->m_nSize); ++slot)
    {
        NativeStuntJump* jump = pool->GetAt(slot);
        if (jump == nullptr)
        {
            continue;
        }
        const StuntDefinition definition = BuildDefinition(*jump);
        if (!definition.HasValidSemantics())
        {
            continue;
        }
        m_baseline[slot].valid = true;
        m_baseline[slot].fingerprint = definition.CalculateFingerprint();
        m_baseline[slot].done = jump->done;
        m_baseline[slot].found = jump->found;
    }
    m_baselineCaptured = true;
}

void CStuntJumpSyncManager::RestoreNativeBaseline()
{
    if (!m_baselineCaptured)
    {
        return;
    }
    NativeStuntPool* pool = GetNativePool();
    if (!IsPoolUsable(pool))
    {
        return;
    }
    for (uint16_t slot = 0; slot < static_cast<uint16_t>(pool->m_nSize); ++slot)
    {
        NativeStuntJump* jump = pool->GetAt(slot);
        if (jump == nullptr || !m_baseline[slot].valid)
        {
            continue;
        }
        const StuntDefinition definition = BuildDefinition(*jump);
        if (definition.HasValidSemantics() &&
            definition.CalculateFingerprint() == m_baseline[slot].fingerprint)
        {
            jump->done = m_baseline[slot].done;
            jump->found = m_baseline[slot].found;
        }
    }
    RecountNativeCompleted();
}

void CStuntJumpSyncManager::BeginServerRun(uint64_t serverRunId)
{
    RestoreNativeBaseline();
    m_states = {};
    m_appliedAwardSequences = {};
    m_serverRunId = serverRunId;
    FinishAttempt(false, false);
}

void CStuntJumpSyncManager::HandleState(const StuntStateEvent& state)
{
    if (!state.HasValidPayload() || !state.FitsSerializedBudget())
    {
        logger::warn("Rejected invalid stunt state");
        return;
    }
    CaptureNativeBaseline();
    if (m_serverRunId == 0 || m_serverRunId != state.serverRunId)
    {
        BeginServerRun(state.serverRunId);
    }

    CachedState& cached = m_states[state.id.slot];
    if (cached.valid && cached.state.serverRunId == state.serverRunId &&
        !IsStuntRevisionNewer(state.revision, cached.state.revision))
    {
        return;
    }
    cached.valid = true;
    cached.state = state;
    ApplyStateToNative(cached.state);
}

void CStuntJumpSyncManager::ApplyAwardOnce(const StuntStateEvent& state)
{
    if (!state.completed || state.awardSequence == 0 ||
        state.completedByPlayerId != CNetworkPlayerManager::m_nMyId ||
        state.collectorSessionNonce != EnsureClientSessionNonce() ||
        m_appliedAwardSequences[state.id.slot] == state.awardSequence)
    {
        return;
    }
    CPlayerPed* player = FindPlayerPed(0);
    CPlayerInfo* playerInfo = player ? player->GetPlayerInfoForThisPlayerPed() : nullptr;
    if (playerInfo == nullptr)
    {
        return;
    }
    m_appliedAwardSequences[state.id.slot] = state.awardSequence;
    playerInfo->m_nMoney += state.rewardAmount;
    CStats::IncrementStat(STAT_UNIQUE_JUMPS_DONE, 1.0f);
    AudioEngine.ReportFrontendAudioEvent(AE_FRONTEND_PART_MISSION_COMPLETE);
    if (state.allCompleted)
    {
        if (char* text = TheText.Get("USJ_ALL"))
        {
            CHud::SetHelpMessage(text, false, false, false);
        }
    }
    if (char* text = TheText.Get("USJ"))
    {
        CMessages::AddBigMessageQ(text, 5000, STYLE_MIDDLE_SMALLER_HIGHER);
    }
    if (char* text = TheText.Get("REWARD"))
    {
        CMessages::AddBigMessageWithNumber(
            text, 6000, STYLE_WHITE_MIDDLE_SMALLER, state.rewardAmount, -1, -1, -1, -1, -1);
    }
}

void CStuntJumpSyncManager::ApplyStateToNative(const StuntStateEvent& state)
{
    NativeStuntJump* jump = FindNativeJump(state.id);
    if (jump == nullptr)
    {
        return;
    }
    jump->done = state.completed;
    if (state.completed)
    {
        jump->found = true;
    }
    RecountNativeCompleted();
    ApplyAwardOnce(state);
}

void CStuntJumpSyncManager::ApplyCachedStates()
{
    for (const CachedState& cached : m_states)
    {
        if (cached.valid)
        {
            ApplyStateToNative(cached.state);
        }
    }
}

void CStuntJumpSyncManager::RecountNativeCompleted()
{
    NativeStuntPool* pool = GetNativePool();
    if (!IsPoolUsable(pool))
    {
        return;
    }
    uint32_t completed = 0;
    for (uint16_t slot = 0; slot < static_cast<uint16_t>(pool->m_nSize); ++slot)
    {
        NativeStuntJump* jump = pool->GetAt(slot);
        completed += jump != nullptr && jump->done ? 1u : 0u;
    }
    GetNativeCompletedCount() = completed;
}

void CStuntJumpSyncManager::PublishCatalog()
{
    NativeStuntPool* pool = GetNativePool();
    if (!IsPoolUsable(pool))
    {
        return;
    }

    const bool isHost = CLocalPlayer::m_bIsHost;
    if (isHost != m_wasHost)
    {
        HandleAuthorityChanged();
        m_wasHost = isHost;
    }
    if (!isHost)
    {
        return;
    }

    uint16_t count = 0;
    uint32_t hash = 2166136261u;
    for (uint16_t slot = 0; slot < static_cast<uint16_t>(pool->m_nSize); ++slot)
    {
        NativeStuntJump* jump = pool->GetAt(slot);
        if (jump == nullptr)
        {
            continue;
        }
        const StuntDefinition definition = BuildDefinition(*jump);
        if (!definition.HasValidSemantics())
        {
            continue;
        }
        StuntId id{slot, definition.CalculateFingerprint()};
        hash = AccumulateCatalogHash(hash, id);
        ++count;
    }
    if (count == 0)
    {
        return;
    }
    if (hash == 0)
    {
        hash = 1;
    }

    const uint32_t now = GetTickCount();
    if (count != m_announcedCatalogCount || hash != m_announcedCatalogHash ||
        static_cast<int32_t>(now - m_nextCatalogRefreshAt) >= 0)
    {
        m_announcedCatalogCount = count;
        m_announcedCatalogHash = hash;
        m_publishCursor = 0;
        m_nextCatalogRefreshAt = now + 5000;
    }
    if (static_cast<int32_t>(now - m_nextPublishAt) < 0 || m_publishCursor >= pool->m_nSize)
    {
        return;
    }

    uint8_t sent = 0;
    while (m_publishCursor < pool->m_nSize && sent < 2)
    {
        const uint16_t slot = m_publishCursor++;
        NativeStuntJump* jump = pool->GetAt(slot);
        if (jump == nullptr)
        {
            continue;
        }
        const StuntDefinition definition = BuildDefinition(*jump);
        if (!definition.HasValidSemantics())
        {
            continue;
        }
        StuntDefinitionAnnounce packet{};
        packet.catalogCount = count;
        packet.catalogHash = hash;
        packet.id = {slot, definition.CalculateFingerprint()};
        packet.definition = definition;
        packet.initiallyCompleted = m_baseline[slot].valid ? m_baseline[slot].done : jump->done;
        GetPacketFactory().Send(packet);
        ++sent;
    }
    m_nextPublishAt = now + 50;
}

bool CStuntJumpSyncManager::MissionContextChanged()
{
    const auto& mission = CMissionSessionClient::GetState();
    return mission.sessionId != m_attemptMissionSessionId ||
           mission.IsActive() != m_attemptMissionWasActive || ReadMissionFlag() != m_attemptMissionFlag;
}

void CStuntJumpSyncManager::StartLocalSlowMotionPresentation(const CVector& camera, CVehicle* vehicle)
{
    if (!m_presentationActive)
    {
        m_previousTimeScale = CTimer::ms_fTimeScale;
        m_previousBlurRed = TheCamera.m_nBlurRed;
        m_previousBlurGreen = TheCamera.m_nBlurGreen;
        m_previousBlurBlue = TheCamera.m_nBlurBlue;
        m_previousBlurType = TheCamera.m_nBlurType;
        m_previousMotionBlur = TheCamera.m_nMotionBlur;
        m_previousMotionBlurAddAlpha = TheCamera.m_nMotionBlurAddAlpha;
        m_presentationActive = true;
    }
    // CTimer is process-local. This value is never serialized or broadcast, so only the collecting
    // client's presentation slows; the authoritative server clock and every other client remain normal.
    CTimer::ms_fTimeScale = 0.3f;
    const CVector zero{};
    TheCamera.SetCamPositionForFixedMode(&camera, &zero);
    TheCamera.TakeControl(vehicle, MODE_FIXED, SWITCHTYPE_JUMPCUT, 1);
    ApplyLocalSlowMotionPresentation();
}

void CStuntJumpSyncManager::ApplyLocalSlowMotionPresentation()
{
    if (!m_presentationActive)
    {
        return;
    }
    CTimer::ms_fTimeScale = 0.3f;
    const uint32_t visualTime = (GetTickCount() - m_attemptStartedAt) * 3u / 10u;
    const uint32_t triangle = visualTime % 800u;
    const uint32_t pulse = triangle <= 400u ? triangle : 800u - triangle;
    TheCamera.m_nBlurRed = 36;
    TheCamera.m_nBlurGreen = 42;
    TheCamera.m_nBlurBlue = 52;
    TheCamera.m_nBlurType = 1;
    TheCamera.m_nMotionBlur = 18u + pulse / 40u;
    TheCamera.m_nMotionBlurAddAlpha = 8u + pulse / 50u;
}

void CStuntJumpSyncManager::StopLocalSlowMotionPresentation()
{
    if (!m_presentationActive)
    {
        return;
    }
    TheCamera.m_nBlurRed = m_previousBlurRed;
    TheCamera.m_nBlurGreen = m_previousBlurGreen;
    TheCamera.m_nBlurBlue = m_previousBlurBlue;
    TheCamera.m_nBlurType = m_previousBlurType;
    TheCamera.m_nMotionBlur = m_previousMotionBlur;
    TheCamera.m_nMotionBlurAddAlpha = m_previousMotionBlurAddAlpha;
    TheCamera.RestoreWithJumpCut();
    CTimer::ms_fTimeScale = m_previousTimeScale;
    m_presentationActive = false;
}

bool CStuntJumpSyncManager::SendAttempt(eStuntAttemptAction action)
{
    if (!m_attemptActive)
    {
        return false;
    }
    CPlayerPed* player = FindPlayerPed(0);
    CVehicle* vehicle = player ? player->m_pVehicle : nullptr;
    CNetworkVehicle* networkVehicle = vehicle ? CNetworkVehicleManager::GetVehicle(vehicle) : nullptr;
    if (vehicle == nullptr || networkVehicle == nullptr || networkVehicle->m_nVehicleId < 0)
    {
        return false;
    }
    StuntAttempt packet{};
    packet.requestId = m_attemptRequestId;
    packet.clientSessionNonce = EnsureClientSessionNonce();
    packet.action = action;
    packet.id = m_attemptId;
    packet.vehicleId = static_cast<uint16_t>(networkVehicle->m_nVehicleId);
    packet.position = vehicle->GetPosition();
    packet.moveSpeed = vehicle->m_vecMoveSpeed;
    GetPacketFactory().Send(packet);
    return true;
}

bool CStuntJumpSyncManager::IsRetryableResult(eStuntAttemptResultReason reason)
{
    return reason == eStuntAttemptResultReason::RATE_LIMITED ||
           reason == eStuntAttemptResultReason::CATALOG_NOT_READY ||
           reason == eStuntAttemptResultReason::DRIVER_SNAPSHOT_NOT_READY;
}

void CStuntJumpSyncManager::QueueAttemptAction(eStuntAttemptAction action)
{
    if (!m_attemptActive || m_pendingActionActive)
    {
        return;
    }
    m_pendingActionActive = true;
    m_pendingAction = action;
    m_pendingActionSentAt = 0;
    m_pendingActionRetryAt = GetTickCount();
    m_pendingActionRetryCount = 0;
    ProcessPendingAttempt();
}

void CStuntJumpSyncManager::ProcessPendingAttempt()
{
    constexpr uint32_t ACK_TIMEOUT_MS = 750;
    constexpr uint8_t MAX_RETRIES = 3;
    if (!m_attemptActive || !m_pendingActionActive)
    {
        return;
    }

    const uint32_t now = GetTickCount();
    if (m_pendingActionSentAt != 0)
    {
        if (now - m_pendingActionSentAt < ACK_TIMEOUT_MS)
        {
            return;
        }
        if (m_pendingActionRetryCount >= MAX_RETRIES)
        {
            FinishAttempt(false, false);
            return;
        }
        ++m_pendingActionRetryCount;
        m_pendingActionSentAt = 0;
        m_pendingActionRetryAt = now;
    }
    if (static_cast<int32_t>(now - m_pendingActionRetryAt) < 0)
    {
        return;
    }
    if (!SendAttempt(m_pendingAction))
    {
        FinishAttempt(false, false);
        return;
    }
    m_pendingActionSentAt = now == 0 ? 1 : now;
    m_pendingActionRetryAt = 0;
}

void CStuntJumpSyncManager::AdvanceAttemptProtocol()
{
    if (!m_attemptActive || m_pendingActionActive)
    {
        return;
    }
    if (!m_attemptStartAccepted)
    {
        QueueAttemptAction(eStuntAttemptAction::START);
        return;
    }
    if (m_attemptHitFinish && !m_attemptHitFinishAccepted)
    {
        QueueAttemptAction(eStuntAttemptAction::HIT_FINISH);
        return;
    }
    if (m_attemptLanded)
    {
        if (m_attemptHitFinishAccepted)
        {
            QueueAttemptAction(eStuntAttemptAction::COMPLETE);
        }
        else if (!m_attemptHitFinish)
        {
            FinishAttempt(false, true);
        }
    }
}

void CStuntJumpSyncManager::HandleAttemptResult(const StuntAttemptResult& result)
{
    if (!result.HasValidPayload() || !m_attemptActive || result.requestId != m_attemptRequestId ||
        result.clientSessionNonce != EnsureClientSessionNonce() || result.id != m_attemptId)
    {
        return;
    }

    constexpr uint8_t MAX_RETRIES = 3;
    if (!result.accepted)
    {
        if (m_pendingActionActive && result.action == m_pendingAction && IsRetryableResult(result.reason) &&
            m_pendingActionRetryCount < MAX_RETRIES)
        {
            ++m_pendingActionRetryCount;
            m_pendingActionSentAt = 0;
            m_pendingActionRetryAt = GetTickCount() + std::max<uint16_t>(result.retryAfterMs, 50);
            return;
        }
        FinishAttempt(false, false);
        return;
    }

    if (!m_pendingActionActive || result.action != m_pendingAction)
    {
        return;
    }
    m_pendingActionActive = false;
    m_pendingActionSentAt = 0;
    m_pendingActionRetryAt = 0;
    m_pendingActionRetryCount = 0;
    if (result.action == eStuntAttemptAction::START)
    {
        m_attemptStartAccepted = true;
    }
    else if (result.action == eStuntAttemptAction::HIT_FINISH)
    {
        m_attemptHitFinishAccepted = true;
    }
    else if (result.action == eStuntAttemptAction::COMPLETE)
    {
        FinishAttempt(true, false);
        return;
    }
    else if (result.action == eStuntAttemptAction::CANCEL)
    {
        FinishAttempt(false, false);
        return;
    }
    AdvanceAttemptProtocol();
}

bool CStuntJumpSyncManager::StartAttempt(uint16_t slot)
{
    const auto& mission = CMissionSessionClient::GetState();
    if (mission.IsActive() || ReadMissionFlag() || slot >= m_states.size() ||
        !m_states[slot].valid || m_states[slot].state.completed)
    {
        return false;
    }
    NativeStuntJump* jump = FindNativeJump(m_states[slot].state.id);
    CPlayerPed* player = FindPlayerPed(0);
    CVehicle* vehicle = player ? player->m_pVehicle : nullptr;
    CNetworkVehicle* networkVehicle = vehicle ? CNetworkVehicleManager::GetVehicle(vehicle) : nullptr;
    if (jump == nullptr || player == nullptr || vehicle == nullptr || networkVehicle == nullptr ||
        networkVehicle->m_nVehicleId < 0)
    {
        return false;
    }

    m_attemptActive = true;
    m_attemptId = m_states[slot].state.id;
    m_attemptRequestId = NextRequestId();
    m_attemptStartedAt = GetTickCount();
    m_attemptStartAccepted = false;
    m_attemptHitFinish = false;
    m_attemptHitFinishAccepted = false;
    m_attemptLanded = false;
    m_pendingActionActive = false;
    m_pendingActionSentAt = 0;
    m_pendingActionRetryAt = 0;
    m_pendingActionRetryCount = 0;
    m_attemptMissionSessionId = mission.sessionId;
    m_attemptMissionWasActive = mission.IsActive();
    m_attemptMissionFlag = ReadMissionFlag();

    GetNativeJumpState() = 1;
    GetNativeActiveJump() = jump;
    GetNativeTimer() = 0;
    GetNativeHitReward() = false;
    if (!jump->found)
    {
        jump->found = true;
        CStats::IncrementStat(STAT_UNIQUE_JUMPS_FOUND, 1.0f);
    }
    StartLocalSlowMotionPresentation(jump->camera, vehicle);
    QueueAttemptAction(eStuntAttemptAction::START);
    return true;
}

void CStuntJumpSyncManager::FinishAttempt(bool completed, bool notifyServer)
{
    if (!m_attemptActive && !m_presentationActive)
    {
        return;
    }
    if (notifyServer && m_attemptActive)
    {
        SendAttempt(completed ? eStuntAttemptAction::COMPLETE : eStuntAttemptAction::CANCEL);
    }
    StopLocalSlowMotionPresentation();
    m_attemptActive = false;
    m_attemptId = {};
    m_attemptRequestId = 0;
    m_attemptStartedAt = 0;
    m_attemptStartAccepted = false;
    m_attemptHitFinish = false;
    m_attemptHitFinishAccepted = false;
    m_attemptLanded = false;
    m_pendingActionActive = false;
    m_pendingAction = eStuntAttemptAction::START;
    m_pendingActionSentAt = 0;
    m_pendingActionRetryAt = 0;
    m_pendingActionRetryCount = 0;
    GetNativeJumpState() = 0;
    GetNativeActiveJump() = nullptr;
    GetNativeTimer() = 0;
    GetNativeHitReward() = false;
}

void CStuntJumpSyncManager::ProcessOnlineJump()
{
    NativeStuntPool* pool = GetNativePool();
    CPlayerPed* player = FindPlayerPed(0);
    CPlayerInfo* playerInfo = player ? player->GetPlayerInfoForThisPlayerPed() : nullptr;
    CVehicle* vehicle = player ? player->m_pVehicle : nullptr;
    if (!IsPoolUsable(pool) || player == nullptr || playerInfo == nullptr)
    {
        FinishAttempt(false, true);
        return;
    }

    if (!m_attemptActive)
    {
        const auto& mission = CMissionSessionClient::GetState();
        if (mission.IsActive() || ReadMissionFlag() ||
            playerInfo->m_nPlayerState != PLAYERSTATE_PLAYING || !player->IsAlive() ||
            !player->m_nPedFlags.bInVehicle || vehicle == nullptr || vehicle->m_pDriver != player ||
            vehicle->m_nVehicleSubType == VEHICLE_BOAT || vehicle->m_nVehicleSubType == VEHICLE_PLANE ||
            vehicle->m_nVehicleSubType == VEHICLE_HELI || vehicle->m_nNumEntitiesCollided > 0 ||
            vehicle->m_vecMoveSpeed.Magnitude() * 50.0f < 20.0f)
        {
            return;
        }
        for (uint16_t slot = 0; slot < static_cast<uint16_t>(pool->m_nSize); ++slot)
        {
            NativeStuntJump* jump = pool->GetAt(slot);
            if (jump != nullptr && NativeBoxContains(jump->start, vehicle->GetPosition()) && StartAttempt(slot))
            {
                break;
            }
        }
        return;
    }

    ProcessPendingAttempt();
    if (!m_attemptActive)
    {
        return;
    }
    NativeStuntJump* jump = FindNativeJump(m_attemptId);
    const uint32_t elapsed = GetTickCount() - m_attemptStartedAt;
    const bool invalidVehicle = jump == nullptr || playerInfo->m_nPlayerState != PLAYERSTATE_PLAYING ||
        !player->IsAlive() || !player->m_nPedFlags.bInVehicle || vehicle == nullptr ||
        vehicle->m_pDriver != player || vehicle->GetStatus() == STATUS_WRECKED ||
        vehicle->vehicleFlags.bIsDrowning || vehicle->physicalFlags.bSubmergedInWater;
    if (invalidVehicle || MissionContextChanged() || elapsed > 20000)
    {
        FinishAttempt(false, true);
        return;
    }

    const bool landed = vehicle->m_nNumEntitiesCollided != 0 && elapsed >= 100;
    if (!landed && NativeBoxContains(jump->finish, vehicle->GetPosition()))
    {
        if (!m_attemptHitFinish)
        {
            m_attemptHitFinish = true;
        }
        GetNativeHitReward() = true;
    }
    GetNativeTimer() = elapsed;
    GetNativeActiveJump() = jump;
    if (landed)
    {
        if (!m_attemptLanded)
        {
            m_attemptLanded = true;
            StopLocalSlowMotionPresentation();
        }
    }
    else
    {
        ApplyLocalSlowMotionPresentation();
    }
    AdvanceAttemptProtocol();
}

void CStuntJumpSyncManager::ProcessNativeUpdate()
{
    if (!CNetwork::m_bAuthenticated)
    {
        if (m_wasAuthenticated)
        {
            ResetNetworkState();
        }
        plugin::CallDyn(m_nativeUpdateAddress);
        return;
    }

    m_wasAuthenticated = true;
    CaptureNativeBaseline();
    EnsureClientSessionNonce();
    PublishCatalog();
    ApplyCachedStates();
    ProcessOnlineJump();
}

void CStuntJumpSyncManager::HandleAuthorityChanged()
{
    // A host epoch change invalidates every in-flight server attempt. Abort locally and restore the
    // collector's exact pre-stunt time scale before publishing or consuming the new authority epoch.
    FinishAttempt(false, CNetwork::m_bAuthenticated);
    m_announcedCatalogCount = 0;
    m_announcedCatalogHash = 0;
    m_publishCursor = 0;
    m_nextPublishAt = 0;
    m_nextCatalogRefreshAt = 0;
}

void CStuntJumpSyncManager::ResetNetworkState()
{
    FinishAttempt(false, false);
    RestoreNativeBaseline();
    m_states = {};
    m_baseline = {};
    m_baselineCaptured = false;
    m_wasAuthenticated = false;
    m_wasHost = false;
    HandleAuthorityChanged();
    // Keep serverRunId and applied award sequences across a credentialed reconnect. A different server-run ID
    // clears both in BeginServerRun, preventing ID reuse from duplicating another player's reward.
}
