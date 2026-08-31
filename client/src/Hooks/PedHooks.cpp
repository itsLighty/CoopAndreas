#include "stdafx.h"
#include "PedHooks.h"
#include "CNetworkPed.h"
#include <CPedGroups.h>

namespace
{
constexpr uint32_t PLAYER_VOICE_COMMAND_DEBOUNCE_MS = 250;

struct PlayerVoiceCommandDebounce
{
    ENetPeer* peer = nullptr;
    uint32_t connectId = 0;
    int playerId = -1;
    eGlobalSpeechContexts context = CONTEXT_GLOBAL_NO_SPEECH;
    uint32_t acceptedAt = 0;
    bool hasAcceptedCommand = false;
};

PlayerVoiceCommandDebounce g_playerVoiceCommandDebounce{};

void ResetPlayerVoiceCommandDebounce()
{
    g_playerVoiceCommandDebounce = {};
}

bool ShouldRelayPlayerVoiceCommand(CPed* ped, eGlobalSpeechContexts context)
{
    if (!CNetwork::m_bAuthenticated || CNetwork::m_pPeer == nullptr ||
        CNetworkPlayerManager::m_nMyId < 0 || CNetworkPlayerManager::m_nMyId >= Config::MAX_SERVER_PLAYERS ||
        ped == nullptr || ped != FindPlayerPed(0) || !ped->IsAlive())
    {
        ResetPlayerVoiceCommandDebounce();
        return false;
    }

    auto& state = g_playerVoiceCommandDebounce;
    const uint32_t connectId = CNetwork::m_pPeer->connectID;
    if (state.peer != CNetwork::m_pPeer || state.connectId != connectId ||
        state.playerId != CNetworkPlayerManager::m_nMyId)
    {
        ResetPlayerVoiceCommandDebounce();
        state.peer = CNetwork::m_pPeer;
        state.connectId = connectId;
        state.playerId = CNetworkPlayerManager::m_nMyId;
    }

    const uint32_t now = CTimer::GetTimeInMS();
    if (state.hasAcceptedCommand && state.context == context &&
        now - state.acceptedAt < PLAYER_VOICE_COMMAND_DEBOUNCE_MS)
    {
        return false;
    }

    state.context = context;
    state.acceptedAt = now;
    state.hasAcceptedCommand = true;
    return true;
}
}  // namespace

static void __cdecl CPopulation__Update_Hook(bool generate)
{
    if (CNetwork::m_bAuthenticated)
        CPopulation::Update(generate);
}

CPed* pPed = nullptr;
CNetworkPed* _pNetworkPed = nullptr;
eMoveState nMoveState = (eMoveState)0;
static void __declspec(naked) CPed__SetMoveState_Hook()
{
    __asm
    {
        mov pPed, ecx
        mov eax, [esp+4]
        mov nMoveState, eax
        pushad
    }
 
    if (CNetwork::m_bAuthenticated && !pPed->IsPlayer())
    {
        _pNetworkPed = CNetworkPedManager::GetPed(pPed);
        if (_pNetworkPed && !_pNetworkPed->m_bSyncing)
        {

            pPed->m_nMoveState = _pNetworkPed->m_nMoveState;
            __asm
            {
                popad
                mov eax, 0x5DEC0A
                jmp eax
            }
        }
    }

    pPed->m_nMoveState = nMoveState;

    __asm
    {
        popad
        mov eax, 0x5DEC0A
        jmp eax
    }
}

bool __fastcall CWeapon__Fire_Hook(CWeapon* This, SKIP_EDX, CPed* owner, CVector* vecOrigin, CVector* vecEffectPosn, CEntity* targetEntity, CVector* vecTarget, CVector* arg_14)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(owner);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_bSyncing)
        {
            Packets::Peds::PedShotSync packet{};
            packet.pedid = pNetworkPed->m_nPedId;
            packet.weaponType = This->m_eWeaponType;
            packet.origin = *vecOrigin;
            packet.effect = *vecEffectPosn;
            if (vecTarget)
                packet.target = *vecTarget;
            else if(targetEntity)
                packet.target = targetEntity->GetPosition();

            GetPacketFactory().Send(packet);

            return This->Fire(owner, vecOrigin, vecEffectPosn, targetEntity, vecTarget, arg_14);
        }
    }
    else
    {
        return This->Fire(owner, vecOrigin, vecEffectPosn, targetEntity, vecTarget, arg_14);
    }

    return false;
}

void CStreaming__RequestSpecialModel_Hook(int modelid, const char* txdName, int flags)
{
    CStreaming::RequestSpecialModel(modelid, txdName, flags);

    if (modelid >= 290 && modelid <= 299)
    {
        char* specialModel = PedHooks::ms_aszLoadedSpecialModels[modelid - 290];

        // copy characters and convert to uppercase
        int i = 0;
        for (; txdName[i] != '\0' && i < 7; i++)
        {
            specialModel[i] = std::toupper(txdName[i]);
        }

        // null-terminate the string
        specialModel[i] = '\0';

        // fill remaining elements with null characters
        for (int j = i + 1; j < 8; j++)
        {
            specialModel[j] = '\0';
        }
    }
}

int16_t __fastcall CAEPedSpeechAudioEntity__AddSayEvent_Hook(CAEPedSpeechAudioEntity* This, SKIP_EDX, eAudioEvents audioEvent, int16_t gCtx, uint32_t startTimeDelay, float probability, bool overideSilence, bool isForceAudible, bool isFrontEnd)
{
    CPed* pPed = (CPed*)((uintptr_t)This - offsetof(CPed, m_pedSpeech));

    if (!pPed->IsPlayer())
    {
        if (auto pNetworkPed = CNetworkPedManager::GetPed(pPed))
        {
            if (!pNetworkPed->m_bSyncing)
            {
                return -1;
            }
        }
    }

    auto result = plugin::CallMethodAndReturn<int16_t, 0x4E6550>(This, audioEvent, gCtx, startTimeDelay, probability, overideSilence, isForceAudible, isFrontEnd);
    
    if (result == -1)
    {
        return result;
    }

    // Calling the native implementation first preserves stock speech selection and repeat-time suppression. All
    // keyboard and gamepad group controls converge on this CPed::Say call site, so an accepted command is observed
    // once without adding input-specific hooks. Direct remote playback bypasses this hook and cannot feed back.
    if (!CNetwork::m_bAuthenticated)
    {
        ResetPlayerVoiceCommandDebounce();
        return result;
    }

    const auto context = static_cast<eGlobalSpeechContexts>(gCtx);
    if (pPed->IsPlayer())
    {
        if (pPed != FindPlayerPed(0))
        {
            return result;
        }

        const bool isPlayerCommand = Packets::Peds::IsDeliberatePlayerVoiceCommand(context);
        if (isPlayerCommand && !Packets::Peds::HasStockPlayerVoiceCommandArguments(
                context, startTimeDelay, overideSilence, isForceAudible, isFrontEnd))
        {
            return result;
        }

        if (!pPed->IsAlive())
        {
            ResetPlayerVoiceCommandDebounce();
            if (isPlayerCommand)
                return result;
        }
        else if (isPlayerCommand && !ShouldRelayPlayerVoiceCommand(pPed, context))
        {
            return result;
        }
    }

    Packets::Peds::PedSay packet{};
    packet.phraseId = context;
    packet.startTimeDelay = startTimeDelay;
    packet.overrideSilence = overideSilence;
    packet.isForceAudible = isForceAudible;
    packet.isFrontEnd = isFrontEnd;
    packet.entity.SetEntity(pPed);
    GetPacketFactory().Send(packet);

    return result;
}

void PedHooks::InjectHooks()
{
    // ped hooks
    patch::RedirectCall(0x53C030, CPopulation__Update_Hook);
    patch::RedirectCall(0x53C054, CPopulation__Update_Hook);
    
    patch::RedirectJump(0x5DEC00, CPed__SetMoveState_Hook);

    patch::RedirectCall(0x61ECCD, CWeapon__Fire_Hook);
    patch::RedirectCall(0x628328, CWeapon__Fire_Hook);
    patch::RedirectCall(0x62B109, CWeapon__Fire_Hook);
    patch::RedirectCall(0x62B12A, CWeapon__Fire_Hook);

    patch::RedirectJump(0x40B45E, CStreaming__RequestSpecialModel_Hook);

    patch::RedirectCall(0x5F000B, CAEPedSpeechAudioEntity__AddSayEvent_Hook);
}
