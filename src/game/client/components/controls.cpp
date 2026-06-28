/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

#include <base/log.h>
#include <base/math.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>
#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/components/scoreboard.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

CControls::CControls()
{
	mem_zero(&m_aLastData, sizeof(m_aLastData));
	std::fill(std::begin(m_aMousePos), std::end(m_aMousePos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMousePosOnAction), std::end(m_aMousePosOnAction), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aTargetPos), std::end(m_aTargetPos), vec2(0.0f, 0.0f));
	std::fill(std::begin(m_aMouseInputType), std::end(m_aMouseInputType), EMouseInputType::ABSOLUTE);
}

void CControls::OnReset()
{
	ResetInput(0);
	ResetInput(1);

	for(int &AmmoCount : m_aAmmoCount)
		AmmoCount = 0;

	m_LastSendTime = 0;
}

void CControls::ResetInput(int Dummy)
{
	m_aLastData[Dummy].m_Direction = 0;
	// simulate releasing the fire button
	if((m_aLastData[Dummy].m_Fire & 1) != 0)
		m_aLastData[Dummy].m_Fire++;
	m_aLastData[Dummy].m_Fire &= INPUT_STATE_MASK;
	m_aLastData[Dummy].m_Jump = 0;
	m_aInputData[Dummy] = m_aLastData[Dummy];

	m_aInputDirectionLeft[Dummy] = 0;
	m_aInputDirectionRight[Dummy] = 0;
}

void CControls::OnPlayerDeath()
{
	for(int &AmmoCount : m_aAmmoCount)
		AmmoCount = 0;
	// Drop any pending rocket-save weapon restore so we don't switch weapons right after respawning.
	for(int &PrevWeapon : m_aAntiVoidRocketPrevWeapon)
		PrevWeapon = -1;
}

struct CInputState
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
};

void CControls::ConKeyInputState(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if(pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active)
		return;

	*pState->m_apVariables[g_Config.m_ClDummy] = pResult->GetInteger(0);
}

void CControls::ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData)
{
	CInputState *pState = (CInputState *)pUserData;

	if((pState->m_pControls->GameClient()->m_GameInfo.m_BugDDRaceInput && pState->m_pControls->GameClient()->m_Snap.m_SpecInfo.m_Active) || pState->m_pControls->GameClient()->m_Spectator.IsActive())
		return;

	int *pVariable = pState->m_apVariables[g_Config.m_ClDummy];
	if(((*pVariable) & 1) != pResult->GetInteger(0))
		(*pVariable)++;
	*pVariable &= INPUT_STATE_MASK;
}

struct CInputSet
{
	CControls *m_pControls;
	int *m_apVariables[NUM_DUMMIES];
	int m_Value;
};

void CControls::ConKeyInputSet(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	if(pResult->GetInteger(0))
	{
		*pSet->m_apVariables[g_Config.m_ClDummy] = pSet->m_Value;
	}
}

void CControls::ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CInputSet *pSet = (CInputSet *)pUserData;
	ConKeyInputCounter(pResult, pSet);
	pSet->m_pControls->m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = 0;
}

void CControls::OnConsoleInit()
{
	// game commands
	{
		static CInputState s_State = {this, {&m_aInputDirectionLeft[0], &m_aInputDirectionLeft[1]}};
		Console()->Register("+left", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move left");
	}
	{
		static CInputState s_State = {this, {&m_aInputDirectionRight[0], &m_aInputDirectionRight[1]}};
		Console()->Register("+right", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Move right");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Jump, &m_aInputData[1].m_Jump}};
		Console()->Register("+jump", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Jump");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Hook, &m_aInputData[1].m_Hook}};
		Console()->Register("+hook", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Hook");
	}
	{
		static CInputState s_State = {this, {&m_aInputData[0].m_Fire, &m_aInputData[1].m_Fire}};
		Console()->Register("+fire", "", CFGFLAG_CLIENT, ConKeyInputCounter, &s_State, "Fire");
	}
	{
		static CInputState s_State = {this, {&m_aShowHookColl[0], &m_aShowHookColl[1]}};
		Console()->Register("+showhookcoll", "", CFGFLAG_CLIENT, ConKeyInputState, &s_State, "Show Hook Collision");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 1};
		Console()->Register("+weapon1", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to hammer");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 2};
		Console()->Register("+weapon2", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to gun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 3};
		Console()->Register("+weapon3", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to shotgun");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 4};
		Console()->Register("+weapon4", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to grenade");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_WantedWeapon, &m_aInputData[1].m_WantedWeapon}, 5};
		Console()->Register("+weapon5", "", CFGFLAG_CLIENT, ConKeyInputSet, &s_Set, "Switch to laser");
	}

	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_NextWeapon, &m_aInputData[1].m_NextWeapon}, 0};
		Console()->Register("+nextweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to next weapon");
	}
	{
		static CInputSet s_Set = {this, {&m_aInputData[0].m_PrevWeapon, &m_aInputData[1].m_PrevWeapon}, 0};
		Console()->Register("+prevweapon", "", CFGFLAG_CLIENT, ConKeyInputNextPrevWeapon, &s_Set, "Switch to previous weapon");
	}
}

void CControls::OnMessage(int Msg, void *pRawMsg)
{
	if(Msg == NETMSGTYPE_SV_WEAPONPICKUP)
	{
		CNetMsg_Sv_WeaponPickup *pMsg = (CNetMsg_Sv_WeaponPickup *)pRawMsg;
		if(g_Config.m_ClAutoswitchWeapons)
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = pMsg->m_Weapon + 1;
		// We don't really know ammo count, until we'll switch to that weapon, but any non-zero count will suffice here
		m_aAmmoCount[maximum(0, pMsg->m_Weapon % NUM_WEAPONS)] = 10;
	}
}

int CControls::SnapInput(int *pData)
{
	// update player state
	if(GameClient()->m_Chat.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_CHATTING;
	else if(GameClient()->m_Menus.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_IN_MENU;
	else
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags = PLAYERFLAG_PLAYING;

	if(GameClient()->m_Scoreboard.IsActive())
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SCOREBOARD;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Controls.m_aShowHookColl[g_Config.m_ClDummy])
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_AIM;

	if(Client()->ServerCapAnyPlayerFlag() && GameClient()->m_Camera.CamType() == CCamera::CAMTYPE_SPEC)
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_SPEC_CAM;

	switch(m_aMouseInputType[g_Config.m_ClDummy])
	{
	case CControls::EMouseInputType::AUTOMATED:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE;
		break;
	case CControls::EMouseInputType::ABSOLUTE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_ABSOLUTE | PLAYERFLAG_INPUT_MANUAL;
		break;
	case CControls::EMouseInputType::RELATIVE:
		m_aInputData[g_Config.m_ClDummy].m_PlayerFlags |= PLAYERFLAG_INPUT_MANUAL;
		break;
	}

	// TClient
	if(g_Config.m_TcHideChatBubbles && Client()->RconAuthed())
		for(auto &InputData : m_aInputData)
			InputData.m_PlayerFlags &= ~PLAYERFLAG_CHATTING;

	if(g_Config.m_TcNameplatePingCircle)
		for(auto &InputData : m_aInputData)
			InputData.m_PlayerFlags |= PLAYERFLAG_SCOREBOARD;

	bool Send = m_aLastData[g_Config.m_ClDummy].m_PlayerFlags != m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	m_aLastData[g_Config.m_ClDummy].m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;

	// we freeze the input if chat or menu is activated
	if(!(m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING))
	{
		if(!GameClient()->m_GameInfo.m_BugDDRaceInput)
			ResetInput(g_Config.m_ClDummy);

		mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

		// set the target anyway though so that we can keep seeing our surroundings,
		// even if chat or menu are activated
		vec2 Pos = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		if(g_Config.m_TcScaleMouseDistance && !GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
			if(MaxDistance > 5 && MaxDistance < 1000) // Don't scale if angle bind or reduces precision
				Pos *= 1000.0f / (float)MaxDistance;
		}
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)Pos.x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)Pos.y;

		if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
			m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;

		// TClient: keep the automatic safety features (anti-void, balancer, rocket counter) alive even while
		// chat or a menu is open. First rebuild the movement direction from the held keys, so a brake set on a
		// previous tick is released once the danger is gone instead of sticking, then let the features steer.
		if(g_Config.m_TcAntiVoid || g_Config.m_TcBalancer || g_Config.m_TcAntiVoidRocket)
		{
			m_aInputData[g_Config.m_ClDummy].m_Direction = 0;
			if(m_aInputDirectionLeft[g_Config.m_ClDummy] && !m_aInputDirectionRight[g_Config.m_ClDummy])
				m_aInputData[g_Config.m_ClDummy].m_Direction = -1;
			if(!m_aInputDirectionLeft[g_Config.m_ClDummy] && m_aInputDirectionRight[g_Config.m_ClDummy])
				m_aInputData[g_Config.m_ClDummy].m_Direction = 1;

			// Snapshot the (idle) input before the safety features run, so we can detect whether any of
			// them actually steered us on this tick.
			const int PreDirection = m_aInputData[g_Config.m_ClDummy].m_Direction;
			const int PreJump = m_aInputData[g_Config.m_ClDummy].m_Jump;
			const int PreFire = m_aInputData[g_Config.m_ClDummy].m_Fire;
			const int PreHook = m_aInputData[g_Config.m_ClDummy].m_Hook;
			const int PreWantedWeapon = m_aInputData[g_Config.m_ClDummy].m_WantedWeapon;

			ApplyAutoSafety();

			// The server silently drops BOTH movement and fire from any input that still carries
			// PLAYERFLAG_CHATTING (see CPlayer::OnPredictedInput / OnPredictedEarlyInput in
			// src/game/server/player.cpp). So while chat is open we present the input as PLAYING instead
			// of CHATTING — but ONLY on the ticks where a safety feature actually changed our input.
			// Stripping the flag every tick would flip it CHATTING<->PLAYING constantly, which forces the
			// input to be resent ~50x/s while you type: that floods the netchannel and was making chat
			// messages drop and the rocket counter misfire. Keeping the normal CHATTING flag on idle ticks
			// also leaves your "typing" bubble visible except during an actual save. Opt-in via
			// tc_safety_in_chat (on by default).
			const bool SafetyActed =
				m_aInputData[g_Config.m_ClDummy].m_Direction != PreDirection ||
				m_aInputData[g_Config.m_ClDummy].m_Jump != PreJump ||
				m_aInputData[g_Config.m_ClDummy].m_Fire != PreFire ||
				m_aInputData[g_Config.m_ClDummy].m_Hook != PreHook ||
				m_aInputData[g_Config.m_ClDummy].m_WantedWeapon != PreWantedWeapon;
			if(g_Config.m_TcSafetyInChat && SafetyActed && HaveLocalChar() && (m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_CHATTING))
				m_aInputData[g_Config.m_ClDummy].m_PlayerFlags =
					(m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & ~PLAYERFLAG_CHATTING) | PLAYERFLAG_PLAYING;

			// Send the moment a feature changed our input (or we flipped the chatting flag), so
			// braking/rockets react without waiting on the once-a-second heartbeat below.
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_PlayerFlags != m_aLastData[g_Config.m_ClDummy].m_PlayerFlags;
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_Direction != m_aLastData[g_Config.m_ClDummy].m_Direction;
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_Jump != m_aLastData[g_Config.m_ClDummy].m_Jump;
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_Fire != m_aLastData[g_Config.m_ClDummy].m_Fire;
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_Hook != m_aLastData[g_Config.m_ClDummy].m_Hook;
			Send = Send || m_aInputData[g_Config.m_ClDummy].m_WantedWeapon != m_aLastData[g_Config.m_ClDummy].m_WantedWeapon;
		}

		// send once a second just to be sure
		Send = Send || time_get() > m_LastSendTime + time_freq();
	}
	else
	{
		// TClient
		vec2 Pos;
		if(g_Config.m_ClSubTickAiming && m_aMousePosOnAction[g_Config.m_ClDummy] != vec2(0.0f, 0.0f))
		{
			Pos = GameClient()->m_Controls.m_aMousePosOnAction[g_Config.m_ClDummy];
			m_aMousePosOnAction[g_Config.m_ClDummy] = vec2(0.0f, 0.0f);
		}
		else
			Pos = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];

		m_FastInputHookAction = false;
		m_FastInputFireAction = false;

		if(g_Config.m_TcScaleMouseDistance && !GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
			if(MaxDistance > 5 && MaxDistance < 1000) // Don't scale if angle bind or reduces precision
				Pos *= 1000.0f / (float)MaxDistance;
		}
		m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)Pos.x;
		m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)Pos.y;

		if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
			m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;

		// set direction
		m_aInputData[g_Config.m_ClDummy].m_Direction = 0;
		if(m_aInputDirectionLeft[g_Config.m_ClDummy] && !m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = -1;
		if(!m_aInputDirectionLeft[g_Config.m_ClDummy] && m_aInputDirectionRight[g_Config.m_ClDummy])
			m_aInputData[g_Config.m_ClDummy].m_Direction = 1;

		// Anti-void braking, balancer, and the rocket counter. Factored into ApplyAutoSafety so the exact same
		// logic also runs while chat/menu is open (the frozen-input branch above) and while spectating.
		ApplyAutoSafety();

		// TClient: hook aim assist — while holding hook and the hook has not grabbed anything yet,
		// nudge the aim toward the best *reachable* player so it's easier to save someone.
		// A player only counts when ALL of these hold:
		//   - aim is within tc_hook_aim_angle degrees of them (the cone)
		//   - they are within hook range (the hook physically cannot reach further)
		//   - line of sight is clear (a solid block between us would just catch the hook first)
		// Among the candidates we pick the one closest to where you are already aiming.
		if(g_Config.m_TcHookAim && m_aInputData[g_Config.m_ClDummy].m_Hook != 0 &&
			!GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_pLocalCharacter)
		{
			const int HookState = GameClient()->m_PredictedChar.m_HookState;
			if(HookState == HOOK_IDLE || HookState == HOOK_FLYING)
			{
				const vec2 LocalPos = GameClient()->m_LocalCharacterPos;
				const vec2 AimVec((float)m_aInputData[g_Config.m_ClDummy].m_TargetX,
					(float)m_aInputData[g_Config.m_ClDummy].m_TargetY);
				if(length(AimVec) > 0.001f)
				{
					const vec2 AimDir = normalize(AimVec);
					const float MaxAngle = (float)g_Config.m_TcHookAimAngle * (pi / 180.0f);
					// The hook can never reach further than its tuned length, so anyone beyond it is ignored.
					const float MaxDist = GameClient()->m_aTuning[g_Config.m_ClDummy].m_HookLength;

					int BestId = -1;
					float BestAngle = MaxAngle;

					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
							continue;
						if(i == GameClient()->m_Snap.m_LocalClientId)
							continue;

						const vec2 PlayerPos = GameClient()->m_aClients[i].m_RenderPos;
						const vec2 ToPlayer = PlayerPos - LocalPos;
						const float Dist = length(ToPlayer);
						// Too close to derive a direction, or simply out of hook range.
						if(Dist < 1.0f || Dist > MaxDist)
							continue;

						// Must sit inside the cone and beat the current best candidate.
						const float Ang = acosf(std::clamp(dot(AimDir, ToPlayer / Dist), -1.0f, 1.0f));
						if(Ang >= BestAngle)
							continue;

						// Line of sight: if a solid tile sits between us and the player, the hook would
						// stick to that wall instead, so don't aim at this player.
						vec2 ColPos;
						if(Collision()->IntersectLine(LocalPos, PlayerPos, &ColPos, nullptr) && distance(LocalPos, ColPos) < Dist - 2.0f)
							continue;

						BestAngle = Ang;
						BestId = i;
					}

					if(BestId >= 0)
					{
						const vec2 ToPlayer = GameClient()->m_aClients[BestId].m_RenderPos - LocalPos;
						m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)ToPlayer.x;
						m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)ToPlayer.y;
						if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
							m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;
					}
				}
			}
		}

		// TClient: real weapon spin — rotate the SENT aim so other players also see the weapon spinning.
		// Keep the real aim on the exact ticks we hook or fire, so those actions still go where we point.
		if(g_Config.m_TcWeaponSpin && g_Config.m_TcWeaponSpinReal && !GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const bool Firing = (m_aInputData[g_Config.m_ClDummy].m_Fire & 1) != 0;
			const bool Hooking = m_aInputData[g_Config.m_ClDummy].m_Hook != 0;
			if(!Firing && !Hooking)
			{
				const vec2 Target((float)m_aInputData[g_Config.m_ClDummy].m_TargetX, (float)m_aInputData[g_Config.m_ClDummy].m_TargetY);
				float Mag = length(Target);
				if(Mag < 30.0f)
					Mag = 100.0f;
				const float Spin = WeaponSpinAngle(Mag > 0.001f ? angle(Target) : 0.0f, Client()->LocalTime());
				const vec2 Spun = direction(Spin) * Mag;
				m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)Spun.x;
				m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)Spun.y;
				if(!m_aInputData[g_Config.m_ClDummy].m_TargetX && !m_aInputData[g_Config.m_ClDummy].m_TargetY)
					m_aInputData[g_Config.m_ClDummy].m_TargetX = 1;
			}
		}

		// dummy copy moves
		if(g_Config.m_ClDummyCopyMoves)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;

			// Don't copy any input to dummy when spectating others
			if(!GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
			{
				pDummyInput->m_Direction = m_aInputData[g_Config.m_ClDummy].m_Direction;
				pDummyInput->m_Hook = m_aInputData[g_Config.m_ClDummy].m_Hook;
				pDummyInput->m_Jump = m_aInputData[g_Config.m_ClDummy].m_Jump;
				pDummyInput->m_PlayerFlags = m_aInputData[g_Config.m_ClDummy].m_PlayerFlags;
				pDummyInput->m_TargetX = m_aInputData[g_Config.m_ClDummy].m_TargetX;
				pDummyInput->m_TargetY = m_aInputData[g_Config.m_ClDummy].m_TargetY;
				pDummyInput->m_WantedWeapon = m_aInputData[g_Config.m_ClDummy].m_WantedWeapon;

				if(!g_Config.m_ClDummyControl)
					pDummyInput->m_Fire += m_aInputData[g_Config.m_ClDummy].m_Fire - m_aLastData[g_Config.m_ClDummy].m_Fire;

				pDummyInput->m_NextWeapon += m_aInputData[g_Config.m_ClDummy].m_NextWeapon - m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
				pDummyInput->m_PrevWeapon += m_aInputData[g_Config.m_ClDummy].m_PrevWeapon - m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
			}

			m_aInputData[!g_Config.m_ClDummy] = *pDummyInput;
		}

		if(g_Config.m_ClDummyControl)
		{
			CNetObj_PlayerInput *pDummyInput = &GameClient()->m_DummyInput;
			pDummyInput->m_Jump = g_Config.m_ClDummyJump;

			if(g_Config.m_ClDummyFire)
				pDummyInput->m_Fire = g_Config.m_ClDummyFire;
			else if((pDummyInput->m_Fire & 1) != 0)
				pDummyInput->m_Fire++;

			pDummyInput->m_Hook = g_Config.m_ClDummyHook;
		}

		// stress testing
		if(g_Config.m_DbgStress)
		{
			float t = Client()->LocalTime();
			mem_zero(&m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));

			m_aInputData[g_Config.m_ClDummy].m_Direction = ((int)t / 2) & 1;
			m_aInputData[g_Config.m_ClDummy].m_Jump = ((int)t);
			m_aInputData[g_Config.m_ClDummy].m_Fire = ((int)(t * 10));
			m_aInputData[g_Config.m_ClDummy].m_Hook = ((int)(t * 2)) & 1;
			m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = ((int)t) % NUM_WEAPONS;
			m_aInputData[g_Config.m_ClDummy].m_TargetX = (int)(std::sin(t * 3) * 100.0f);
			m_aInputData[g_Config.m_ClDummy].m_TargetY = (int)(std::cos(t * 3) * 100.0f);
		}

		// check if we need to send input
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Direction != m_aLastData[g_Config.m_ClDummy].m_Direction;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Jump != m_aLastData[g_Config.m_ClDummy].m_Jump;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Fire != m_aLastData[g_Config.m_ClDummy].m_Fire;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_Hook != m_aLastData[g_Config.m_ClDummy].m_Hook;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_WantedWeapon != m_aLastData[g_Config.m_ClDummy].m_WantedWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_NextWeapon != m_aLastData[g_Config.m_ClDummy].m_NextWeapon;
		Send = Send || m_aInputData[g_Config.m_ClDummy].m_PrevWeapon != m_aLastData[g_Config.m_ClDummy].m_PrevWeapon;
		Send = Send || time_get() > m_LastSendTime + time_freq() / 25; // send at least 25 Hz
		Send = Send || (GameClient()->m_Snap.m_pLocalCharacter && GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == WEAPON_NINJA && (m_aInputData[g_Config.m_ClDummy].m_Direction || m_aInputData[g_Config.m_ClDummy].m_Jump || m_aInputData[g_Config.m_ClDummy].m_Hook));
	}

	// copy and return size
	m_aLastData[g_Config.m_ClDummy] = m_aInputData[g_Config.m_ClDummy];

	if(!Send)
		return 0;

	m_LastSendTime = time_get();
	mem_copy(pData, &m_aInputData[g_Config.m_ClDummy], sizeof(m_aInputData[0]));
	return sizeof(m_aInputData[0]);
}

// TClient: do we have a local tee on the map to act on? Normally that is m_pLocalCharacter, but while
// paused/spectating (press Q to watch others) the client leaves m_pLocalCharacter null even though our
// character is still in the snapshot — so also accept an active local character item. With no character at
// all (true spectator on the spectators team) this is false and the safety features stay idle.
bool CControls::HaveLocalChar() const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	return GameClient()->m_Snap.m_pLocalCharacter ||
	       (LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active);
}

// TClient: position of our own tee for the safety features. In normal play this is the smoothed render
// position (m_LocalCharacterPos). While paused/spectating (press Q to watch others) the client deliberately
// leaves that stale and m_pLocalCharacter null even though our tee is still in the snapshot, so we fall back
// to the predicted core position, which keeps updating as long as the local character is on the map.
vec2 CControls::LocalCharPos() const
{
	if(GameClient()->m_Snap.m_pLocalCharacter)
		return GameClient()->m_LocalCharacterPos;
	return GameClient()->m_PredictedChar.m_Pos;
}

// TClient: run the automatic safety features on the local tee. Factored out of SnapInput so the exact same
// logic runs both during normal play and while input is frozen by an open chat or menu, and so it also runs
// while paused/spectating — as long as our tee is still on the map. With no local tee it does nothing.
void CControls::ApplyAutoSafety()
{
	// We need a local tee to act on. With no character at all (true spectator) the features simply no-op.
	if(!HaveLocalChar())
		return;

	// Anti-void: brake (counter-strafe) and release hook when our trajectory leads into a dangerous tile.
	if(g_Config.m_TcAntiVoid)
		ApplyAntiVoid();

	// Balancer: runs after anti-void braking so that, when engaged, balancing onto the target's head wins
	// over anti-void trying to brake us away from the void we are standing over. We remember whether it is
	// engaged on a tee this tick so the rocket counter below can stand down.
	bool BalancerActive = false;
	if(g_Config.m_TcBalancer)
		BalancerActive = ApplyBalancer();

	// Rocket counter runs independently, even when the braking anti-void is disabled. But while the balancer
	// is holding us on someone's head over the void, an auto-fired rocket would blow us off it, so optionally
	// suppress the FIRING for as long as the balancer is engaged. We still CALL it (Suppressed) so it can
	// release a previously-held fire press and tick its cooldown — skipping the call entirely would leave the
	// fire bit stuck "held" and permanently break the counter once the balancer engaged right after a shot.
	if(g_Config.m_TcAntiVoidRocket)
		ApplyAntiVoidRocket(g_Config.m_TcBalancerDisableRocket && BalancerActive);
}

// TClient: is this tile index one of the types the user asked anti-void to avoid?
bool CControls::AntiVoidBadTile(int T) const
{
	return (g_Config.m_TcAntiVoidDeath && T == TILE_DEATH) ||
	       (g_Config.m_TcAntiVoidFreeze && T == TILE_FREEZE) ||
	       (g_Config.m_TcAntiVoidDeepFreeze && T == TILE_DFREEZE) ||
	       (g_Config.m_TcAntiVoidLiveFreeze && T == TILE_LFREEZE);
}

// TClient: is the world position dangerous? Checks both game and front layer.
// NOTE: GetCollisionAt() only returns tiles in range [TILE_SOLID..TILE_NOLASER] and would never
// report freeze tiles, so we read the raw tile index via GetTileIndex/GetFrontTileIndex.
// Out-of-bounds counts as dangerous (running off the edge of the map kills you).
bool CControls::AntiVoidDangerAt(float x, float y) const
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= Collision()->GetWidth() || Ty >= Collision()->GetHeight())
		return true;
	const int Index = Collision()->GetPureMapIndex(x, y);
	return AntiVoidBadTile(Collision()->GetTileIndex(Index)) || AntiVoidBadTile(Collision()->GetFrontTileIndex(Index));
}

// TClient: measure the free (non-solid) corridor around the player along one axis, in tiles.
// Scans outward in both directions from the player's tile until it hits a solid wall (capped so
// we never walk across the whole map). Returns the span including the tile the player stands in.
int CControls::AntiVoidFreeSpan(bool Horizontal) const
{
	const vec2 Pos = LocalCharPos();
	const int Cap = 32; // tiles
	int Span = 1; // the player's own tile
	for(int Sign = -1; Sign <= 1; Sign += 2)
	{
		for(int i = 1; i <= Cap; ++i)
		{
			const float x = Pos.x + (Horizontal ? Sign * i * 32.0f : 0.0f);
			const float y = Pos.y + (Horizontal ? 0.0f : Sign * i * 32.0f);
			if(Collision()->CheckPoint(x, y)) // solid wall
				break;
			Span++;
		}
	}
	return Span;
}

// TClient: is the player in a tight spot? Narrow in either axis (configurable thresholds) counts.
bool CControls::AntiVoidInNarrowSpot() const
{
	if(!g_Config.m_TcAntiVoidNarrowDisable)
		return false;
	return AntiVoidFreeSpan(true) <= g_Config.m_TcAntiVoidNarrowWidth ||
	       AntiVoidFreeSpan(false) <= g_Config.m_TcAntiVoidNarrowHeight;
}

void CControls::ApplyAntiVoid()
{
	const int Dummy = g_Config.m_ClDummy;
	const vec2 CharPos = LocalCharPos();
	const vec2 Vel = GameClient()->m_PredictedChar.m_Vel;
	const float R = 28.0f; // tee half-size

	// Auto-disable in narrow/tight spots so we don't fight the player inside corridors.
	// Drop any held brake from the hysteresis so movement out of the tight spot stays free.
	if(AntiVoidInNarrowSpot())
	{
		m_aAntiVoidHold[Dummy] = 0;
		return;
	}

	// How far we look ahead grows with speed so we start braking earlier when moving fast.
	// Distances are in pixels (32px = 1 tile), configurable separately for side vs vertical;
	// speed can extend them further, clamped so we don't react from across the map.
	const float LookTicks = 8.0f;
	const float SideTriggerDist = (float)g_Config.m_TcAntiVoidSideDistance;
	const float VertTriggerDist = (float)g_Config.m_TcAntiVoidDistance;
	const float MaxLook = maximum(maximum(SideTriggerDist, VertTriggerDist), 32.0f * 8.0f);

	// Sample either a single point at distance, or (trajectory mode) every step along the way so
	// a thin void strip can't be "jumped over" between frames at high speed.
	auto DangerAlong = [&](float x0, float y0, float dx, float dy) -> bool {
		if(!g_Config.m_TcAntiVoidTrajectory)
			return AntiVoidDangerAt(x0 + dx, y0 + dy);
		const float Len = length(vec2(dx, dy));
		const int Steps = maximum(1, (int)(Len / 16.0f));
		for(int i = 1; i <= Steps; ++i)
		{
			const float t = (float)i / (float)Steps;
			if(AntiVoidDangerAt(x0 + dx * t, y0 + dy * t))
				return true;
		}
		return false;
	};

	// Horizontal danger toward Sign (-1 left, +1 right), sampled along the body center line only.
	auto DangerHorizontal = [&](int Sign) {
		const float Look = std::clamp(absolute(Vel.x) * LookTicks, SideTriggerDist, MaxLook);
		return DangerAlong(CharPos.x, CharPos.y, Sign * Look, 0.0f);
	};

	int &Dir = m_aInputData[Dummy].m_Direction;
	const float BrakeThreshold = 1.0f;
	bool Blocked = false;

	const bool DangerRight = DangerHorizontal(1);
	const bool DangerLeft = DangerHorizontal(-1);

	// Brake toward the dangerous side only, and never when steering away from it.
	if(DangerRight && Dir != -1 && (Dir == 1 || Vel.x > BrakeThreshold))
	{
		Dir = (Vel.x > BrakeThreshold) ? -1 : 0;
		Blocked = true;
	}
	else if(DangerLeft && Dir != 1 && (Dir == -1 || Vel.x < -BrakeThreshold))
	{
		Dir = (Vel.x < -BrakeThreshold) ? 1 : 0;
		Blocked = true;
	}

	// Column danger check (YSign -1 = up, +1 = down)
	auto DangerInColumn = [&](float YSign) {
		for(float d = R; d <= VertTriggerDist + R; d += 16.0f)
			if(AntiVoidDangerAt(CharPos.x, CharPos.y + YSign * d) || AntiVoidDangerAt(CharPos.x - R * 0.6f, CharPos.y + YSign * d) || AntiVoidDangerAt(CharPos.x + R * 0.6f, CharPos.y + YSign * d))
				return true;
		return false;
	};

	// Release hook ONLY when it is pulling us up into a void ceiling (padding into freeze), with safe below.
	// This is the "vertical" half of anti-void; turn it off to keep only the sideways braking above.
	if(g_Config.m_TcAntiVoidVertical && m_aInputData[Dummy].m_Hook)
	{
		const CCharacterCore &Core = GameClient()->m_PredictedChar;
		bool HookPullingUp;
		if(Core.m_HookState == HOOK_FLYING || Core.m_HookState == HOOK_GRABBED)
			HookPullingUp = Core.m_HookPos.y < Core.m_Pos.y - 1.0f;
		else
			HookPullingUp = m_aInputData[Dummy].m_TargetY < 0;
		if(HookPullingUp && DangerInColumn(-1.0f) && !DangerInColumn(1.0f))
		{
			m_aInputData[Dummy].m_Hook = 0;
			Blocked = true;
		}
	}

	// Optional: block jump if it would launch our head into a freeze/death ceiling (also vertical anti-void)
	if(g_Config.m_TcAntiVoidVertical && g_Config.m_TcAntiVoidBlockJump && m_aInputData[Dummy].m_Jump && DangerInColumn(-1.0f) && !DangerInColumn(1.0f))
	{
		m_aInputData[Dummy].m_Jump = 0;
		Blocked = true;
	}

	// Optional: fall protection. Simulate the gravity trajectory; if it ends in a void, brake the
	// horizontal velocity so we don't run/slide off a ledge into the void below.
	if(g_Config.m_TcAntiVoidFallProtection)
	{
		vec2 p = CharPos;
		vec2 v = Vel;
		const float Gravity = 0.5f;
		const int SimTicks = 18;
		bool WillFallIntoVoid = false;
		for(int i = 0; i < SimTicks; ++i)
		{
			v.y += Gravity;
			p += v;
			if(AntiVoidDangerAt(p.x, p.y) || AntiVoidDangerAt(p.x, p.y + R) || AntiVoidDangerAt(p.x - R * 0.6f, p.y + R) || AntiVoidDangerAt(p.x + R * 0.6f, p.y + R))
			{
				WillFallIntoVoid = true;
				break;
			}
		}
		if(WillFallIntoVoid && absolute(Vel.x) > BrakeThreshold)
		{
			Dir = (Vel.x > 0.0f) ? -1 : 1; // counter the horizontal drift toward the ledge
			Blocked = true;
		}
	}

	// Optional: hysteresis. Hold the brake direction for a few extra ticks so it doesn't flicker on the edge.
	if(g_Config.m_TcAntiVoidSmoothing > 0)
	{
		if(Blocked)
		{
			m_aAntiVoidHold[Dummy] = g_Config.m_TcAntiVoidSmoothing;
			m_aAntiVoidHoldDir[Dummy] = Dir;
		}
		else if(m_aAntiVoidHold[Dummy] > 0)
		{
			m_aAntiVoidHold[Dummy]--;
			Dir = m_aAntiVoidHoldDir[Dummy];
			Blocked = true;
		}
	}

	// Optional debug log, only on state change so it doesn't spam.
	if(g_Config.m_TcAntiVoidDebug)
	{
		static bool s_aWasBlocked[NUM_DUMMIES] = {false, false};
		if(Blocked != s_aWasBlocked[Dummy])
		{
			s_aWasBlocked[Dummy] = Blocked;
			log_info("anti-void", "%s", Blocked ? "braking (danger on trajectory)" : "clear");
		}
	}
}

// TClient balancer: is this tee "in the void"? The question is really "does the tee have somewhere safe to
// stand?". We scan a few columns across the tee's body width (not just the single center column, which gave
// false positives when the tee stood at the edge of a platform with its center hanging over the drop). For
// each column we go down from the tee's center: a death/freeze tile or the map edge means that column is
// deadly; safe solid ground means that column has footing. If ANY column reaches safe solid ground before
// anything deadly, the tee has a foothold and is NOT in the void. Only when every column is deadly/open
// (no safe ground under the whole body) is the tee actually in the void.
bool CControls::BalancerTeeInVoid(vec2 TeePos) const
{
	const float R = 28.0f; // tee half-size
	auto Bad = [](int T) { return T == TILE_DEATH || T == TILE_FREEZE || T == TILE_DFREEZE || T == TILE_LFREEZE; };
	const float MaxY = TeePos.y + (float)g_Config.m_TcBalancerVoidDepth * 32.0f;
	const float aColX[] = {TeePos.x - R * 0.7f, TeePos.x, TeePos.x + R * 0.7f};
	for(const float ColX : aColX)
	{
		for(float y = TeePos.y; y <= MaxY; y += 16.0f)
		{
			const int Tx = (int)(ColX / 32.0f);
			const int Ty = (int)(y / 32.0f);
			if(Tx < 0 || Ty < 0 || Tx >= Collision()->GetWidth() || Ty >= Collision()->GetHeight())
				break; // off the map edge => this column is deadly, try the next one
			const int Index = Collision()->GetPureMapIndex(ColX, y);
			if(Bad(Collision()->GetTileIndex(Index)) || Bad(Collision()->GetFrontTileIndex(Index)))
				break; // death/freeze before any ground => this column is deadly, try the next one
			if(Collision()->CheckPoint(ColX, y))
				return false; // safe solid ground under the body => the tee has a foothold, not the void
		}
	}
	return true; // no column had safe footing => the tee is in the void
}

// TClient balancer: only steps in when we are about to slide off the head of a tee that is over the void.
// It does NOT take over our movement: while we are comfortably within the head, our input is left alone so
// we can still walk/jump on the model. Only once we drift past the edge threshold (predicting one tick
// ahead so it reacts as we *start* to slide) does it counter-steer back toward the center to save us.
bool CControls::ApplyBalancer()
{
	const int Dummy = g_Config.m_ClDummy;
	const vec2 CharPos = LocalCharPos();
	const float MaxDist = (float)g_Config.m_TcBalancerDistance;
	const float Edge = (float)g_Config.m_TcBalancerEdge;

	// Pick the nearest in-void tee we are standing on. Smaller Y is higher up, so "below us" is TeePos.y > CharPos.y.
	int BestId = -1;
	float BestDist = MaxDist;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == GameClient()->m_Snap.m_LocalClientId)
			continue;
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;

		const vec2 TeePos = GameClient()->m_aClients[i].m_RenderPos;
		if(g_Config.m_TcBalancerOnlyAbove && TeePos.y <= CharPos.y)
			continue;
		const float Dist = distance(CharPos, TeePos);
		if(Dist > BestDist)
			continue;
		// Requirement: only ever balance while the tee is actually in the void.
		if(!BalancerTeeInVoid(TeePos))
			continue;

		BestDist = Dist;
		BestId = i;
	}

	static bool s_aRescuing[NUM_DUMMIES] = {false, false};
	if(BestId < 0)
	{
		if(g_Config.m_TcBalancerDebug && s_aRescuing[Dummy])
			log_info("balancer", "idle (no in-void tee under you)");
		s_aRescuing[Dummy] = false;
		return false; // not engaged on any tee
	}

	// How far off the head center are we (+ = right of center), looked one tick ahead so we react as we
	// start to slide rather than after we already have.
	const float OffsetX = CharPos.x - GameClient()->m_aClients[BestId].m_RenderPos.x;
	const float PredOffsetX = OffsetX + GameClient()->m_PredictedChar.m_Vel.x;

	int &Dir = m_aInputData[Dummy].m_Direction;
	bool Rescue = false;
	if(PredOffsetX > Edge)
	{
		Dir = -1; // sliding off to the right -> push back left
		Rescue = true;
	}
	else if(PredOffsetX < -Edge)
	{
		Dir = 1; // sliding off to the left -> push back right
		Rescue = true;
	}
	// else: within the safe zone -> leave m_Direction exactly as the player set it (no paralysis).

	if(g_Config.m_TcBalancerDebug && Rescue != s_aRescuing[Dummy])
		log_info("balancer", "%s (offset %.0fpx, edge %.0f)", Rescue ? "rescuing" : "released (within safe zone)", OffsetX, Edge);
	s_aRescuing[Dummy] = Rescue;
	return true; // engaged on an in-void tee (whether actively rescuing or holding within the safe zone)
}

// TClient: rocket (grenade) counter. Independent of the braking anti-void (tc_anti_void): if we carry
// the grenade launcher and are flying into the void, auto-fire a rocket along our movement direction so
// the explosion sits between us and the void and knocks us straight back. Works even when the basic
// anti-void is turned off. Each shot is a clean single press, rate-limited by a cooldown.
void CControls::ApplyAntiVoidRocket(bool Suppressed)
{
	const int Dummy = g_Config.m_ClDummy;
	const vec2 CharPos = LocalCharPos();
	const vec2 Vel = GameClient()->m_PredictedChar.m_Vel;
	const float R = 28.0f; // tee half-size

	// First, release the fire press we made on the previous tick. Without this the fire bit stays "held",
	// which on a full-auto-grenade server keeps firing forever even after we have left the void. We only
	// release our OWN press (the value is unchanged), so a manual click in between is left untouched.
	if(m_aAntiVoidRocketReleasePending[Dummy])
	{
		m_aAntiVoidRocketReleasePending[Dummy] = false;
		if((m_aInputData[Dummy].m_Fire & 1) != 0 && m_aInputData[Dummy].m_Fire == m_aAntiVoidRocketFireValue[Dummy])
			m_aInputData[Dummy].m_Fire++;
	}

	if(m_aAntiVoidRocketCooldown[Dummy] > 0)
		m_aAntiVoidRocketCooldown[Dummy]--;

	// Suppressed by the balancer: the essential upkeep above (releasing our held fire press and ticking the
	// cooldown) is done; don't scan for void, arm the grenade or fire while the balancer is holding us.
	if(Suppressed)
		return;

	const bool HaveGrenade = GameClient()->m_PredictedChar.m_aWeapons[WEAPON_GRENADE].m_Got &&
		GameClient()->m_PredictedChar.m_aWeapons[WEAPON_GRENADE].m_Ammo != 0;

	const float FireDist = (float)g_Config.m_TcAntiVoidRocketDistance / 10.0f; // stored in tenths of a pixel
	const float MoveEps = 1.0f;
	const float Speed = length(Vel);
	// Begin the weapon switch EARLY — before the precise fire moment — so the grenade is already in hand by
	// the time we actually want to shoot. Without this lead the shot would go off on the previous gun while
	// the switch is still in flight. The lead grows with speed to cover the switch latency.
	const float ArmLead = std::clamp(Speed * 5.0f, 0.0f, 200.0f);

	// Scan for the nearest void ahead of us, out to the (larger) arm distance. Toward = direction to fire,
	// NearestDist = how far that void is.
	vec2 Toward(0.0f, 0.0f);
	float NearestDist = 1e9f;
	if(HaveGrenade && Speed > MoveEps)
	{
		const vec2 MoveDir = Vel / Speed;
		const float MaxScan = R + FireDist + ArmLead;
		if(g_Config.m_TcAntiVoidRocketAimVoid)
		{
			// Aim at the actual VOID, not blindly along the velocity vector. We scan the directions we are
			// moving toward (the forward hemisphere of our velocity) and pick the NEAREST dangerous tile, then
			// fire the rocket straight at it — the explosion lands on the void and knocks us directly away from it.
			// Example: flying mostly sideways but a bit up into a ceiling void — the nearest danger is UP, so we
			// shoot up (push down) instead of sideways where there is no void.
			const int Dirs = 16;
			for(int i = 0; i < Dirs; i++)
			{
				const vec2 Dir = direction((float)i / (float)Dirs * 2.0f * pi);
				if(dot(MoveDir, Dir) <= 0.05f) // only voids we are actually heading into
					continue;
				for(float d = R; d <= MaxScan; d += 16.0f)
				{
					if(AntiVoidDangerAt(CharPos.x + Dir.x * d, CharPos.y + Dir.y * d))
					{
						if(d < NearestDist)
						{
							NearestDist = d;
							Toward = Dir;
						}
						break; // nearest danger along this ray
					}
				}
			}
		}
		else
		{
			// Fire straight along the velocity vector (inertia): if the void lies ahead on the exact line we
			// are flying, shoot the rocket there. Simpler, follows momentum exactly (diagonals included).
			for(float d = R; d <= MaxScan; d += 16.0f)
			{
				if(AntiVoidDangerAt(CharPos.x + MoveDir.x * d, CharPos.y + MoveDir.y * d))
				{
					NearestDist = d;
					Toward = MoveDir;
					break;
				}
			}
		}
	}

	const bool DangerInArm = Toward.x != 0.0f || Toward.y != 0.0f; // void ahead within the (larger) arm range
	const bool DangerInFire = DangerInArm && NearestDist <= R + FireDist; // void close enough to actually shoot
	// Use the PREDICTED active weapon (updates in ~1 tick, no ping wait) to know when the grenade is really in hand.
	const bool GrenadeReady = GameClient()->m_PredictedChar.m_ActiveWeapon == WEAPON_GRENADE;

	if(DangerInArm && m_aAntiVoidRocketCooldown[Dummy] == 0)
	{
		// Remember the weapon we had before the save (recorded once, kept across multiple rockets).
		if(m_aAntiVoidRocketPrevWeapon[Dummy] < 0)
			m_aAntiVoidRocketPrevWeapon[Dummy] = GameClient()->m_PredictedChar.m_ActiveWeapon;

		// Arm: start (and keep) switching to the grenade now so it is ready in hand by the fire moment.
		m_aInputData[Dummy].m_WantedWeapon = WEAPON_GRENADE + 1;

		// Fire ONLY once the grenade is actually the active weapon and we are within the precise fire distance.
		// This guarantees the rocket goes off — never the previous gun. One clean press (released next tick).
		if(GrenadeReady && DangerInFire && (m_aInputData[Dummy].m_Fire & 1) == 0)
		{
			const vec2 Aim = normalize(Toward) * 100.0f;
			m_aInputData[Dummy].m_TargetX = (int)Aim.x;
			m_aInputData[Dummy].m_TargetY = (int)Aim.y;
			if(!m_aInputData[Dummy].m_TargetX && !m_aInputData[Dummy].m_TargetY)
				m_aInputData[Dummy].m_TargetX = 1;
			m_aInputData[Dummy].m_Fire++; // even -> odd = one press
			m_aAntiVoidRocketFireValue[Dummy] = m_aInputData[Dummy].m_Fire;
			m_aAntiVoidRocketReleasePending[Dummy] = true;
			m_aAntiVoidRocketCooldown[Dummy] = g_Config.m_TcAntiVoidRocketCooldown;
		}
	}
	else if(m_aAntiVoidRocketPrevWeapon[Dummy] >= 0 && !DangerInArm && !m_aAntiVoidRocketReleasePending[Dummy])
	{
		// Save is over: take the grenade back out and switch to the weapon we had before the rocket(s),
		// so the grenade doesn't linger in our hands. Keep requesting it until the switch is confirmed.
		const int Prev = m_aAntiVoidRocketPrevWeapon[Dummy];
		m_aInputData[Dummy].m_WantedWeapon = Prev + 1;
		if(GameClient()->m_PredictedChar.m_ActiveWeapon == Prev)
			m_aAntiVoidRocketPrevWeapon[Dummy] = -1; // restored
	}
}

// Deterministic pseudo-random in [0, 1) from an integer seed. Used so the "snap and hold" spin
// modes stay put within a time bucket (instead of jittering every frame from rand()), and so the
// rendered weapon and the sent ("real") aim compute the exact same angle for a given moment.
static float SpinHash01(int Seed)
{
	uint32_t x = (uint32_t)Seed * 747796405u + 2891336453u;
	x = ((x >> ((x >> 28) + 4u)) ^ x) * 277803737u;
	x = (x >> 22) ^ x;
	return (float)(x & 0x00ffffffu) / (float)0x01000000;
}

// Single source of truth for the weapon spinner angle (cosmetic). RealAngle is the player's actual
// aim; pendulum/jitter modes orbit around it, the rest ignore it. Time is Client()->LocalTime().
float CControls::WeaponSpinAngle(float RealAngle, float Time)
{
	const float Speed = g_Config.m_TcWeaponSpinSpeed / 10.0f; // base rate (rad/s for spin modes)
	const float Rand = g_Config.m_TcWeaponSpinRandom / 100.0f; // 0..1 extra chaos overlay
	const float Tau = 2.0f * pi;
	float Angle = RealAngle;

	switch(g_Config.m_TcWeaponSpinMode)
	{
	default:
	case 0: // spin clockwise
		Angle = Time * Speed;
		break;
	case 1: // spin counter-clockwise
		Angle = -Time * Speed;
		break;
	case 2: // pendulum: sweep back and forth around the real aim
		Angle = RealAngle + std::sin(Time * Speed * 0.5f) * (pi * 0.75f);
		break;
	case 3: // random flicks: snap to a random direction, hold, then flick to a new one
	{
		const float Rate = maximum(0.5f, Speed * 0.25f); // flicks per second
		Angle = SpinHash01((int)(Time * Rate)) * Tau;
		break;
	}
	case 4: // jitter: small fast shake around the real aim
	{
		const float Rate = maximum(1.0f, Speed); // shakes per second
		Angle = RealAngle + (SpinHash01((int)(Time * Rate)) - 0.5f) * (pi * (0.25f + Rand));
		break;
	}
	case 5: // snap through the 8 cardinal/diagonal directions in order
	{
		const float Rate = maximum(0.5f, Speed * 0.25f);
		Angle = (float)(((int)(Time * Rate)) & 7) * (Tau / 8.0f);
		break;
	}
	case 6: // random drift: smoothly wander between random directions
	{
		const float Rate = maximum(0.25f, Speed * 0.15f);
		const float T = Time * Rate;
		const int B = (int)T;
		const float Frac = T - (float)B;
		const float A0 = SpinHash01(B) * Tau;
		float Delta = SpinHash01(B + 1) * Tau - A0;
		while(Delta > pi)
			Delta -= Tau;
		while(Delta < -pi)
			Delta += Tau;
		Angle = A0 + Delta * (Frac * Frac * (3.0f - 2.0f * Frac)); // smoothstep
		break;
	}
	case 7: // chaos: variable-speed spin combined with random flicks
	{
		const float SpinPart = Time * Speed * (0.5f + SpinHash01((int)(Time * 2.0f)));
		const float FlickPart = (SpinHash01((int)(Time * maximum(1.0f, Speed * 0.5f))) - 0.5f) * Tau;
		Angle = SpinPart + FlickPart;
		break;
	}
	}

	// Universal randomness overlay on top of any mode (modes 3/4 are already random enough).
	if(Rand > 0.0f && g_Config.m_TcWeaponSpinMode != 3 && g_Config.m_TcWeaponSpinMode != 4)
		Angle += (SpinHash01((int)(Time * 40.0f)) - 0.5f) * Tau * Rand * 0.5f;

	return Angle;
}

void CControls::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_ClAutoswitchWeaponsOutOfAmmo && !GameClient()->m_GameInfo.m_UnlimitedAmmo && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Keep track of ammo count, we know weapon ammo only when we switch to that weapon, this is tracked on server and protocol does not track that
		m_aAmmoCount[maximum(0, GameClient()->m_Snap.m_pLocalCharacter->m_Weapon % NUM_WEAPONS)] = GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount;
		// Autoswitch weapon if we're out of ammo
		if(m_aInputData[g_Config.m_ClDummy].m_Fire % 2 != 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_AmmoCount == 0 &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_HAMMER &&
			GameClient()->m_Snap.m_pLocalCharacter->m_Weapon != WEAPON_NINJA)
		{
			int Weapon;
			for(Weapon = WEAPON_LASER; Weapon > WEAPON_GUN; Weapon--)
			{
				if(Weapon == GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
					continue;
				if(m_aAmmoCount[Weapon] > 0)
					break;
			}
			if(Weapon != GameClient()->m_Snap.m_pLocalCharacter->m_Weapon)
				m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = Weapon + 1;
		}
	}

	// update target pos
	if(GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// make sure to compensate for smooth dyncam to ensure the cursor stays still in world space if zoomed
		vec2 DyncamOffsetDelta = GameClient()->m_Camera.m_DyncamTargetCameraOffset - GameClient()->m_Camera.m_aDyncamCurrentCameraOffset[g_Config.m_ClDummy];
		float Zoom = GameClient()->m_Camera.m_Zoom;
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_LocalCharacterPos + m_aMousePos[g_Config.m_ClDummy] - DyncamOffsetDelta + DyncamOffsetDelta / Zoom;
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
	{
		m_aTargetPos[g_Config.m_ClDummy] = GameClient()->m_Snap.m_SpecInfo.m_Position + m_aMousePos[g_Config.m_ClDummy];
	}
	else
	{
		m_aTargetPos[g_Config.m_ClDummy] = m_aMousePos[g_Config.m_ClDummy];
	}

	// TClient: anti-void visual overlay (also shown while paused/spectating, where the feature still runs)
	if(g_Config.m_TcAntiVoid && g_Config.m_TcAntiVoidShow && HaveLocalChar())
	{
		const vec2 Center = GameClient()->m_Camera.m_Center;
		const float Zoom = GameClient()->m_Camera.m_Zoom;
		float aSavedScreen[4];
		Graphics()->GetScreen(&aSavedScreen[0], &aSavedScreen[1], &aSavedScreen[2], &aSavedScreen[3]);
		Graphics()->MapScreenToInterface(Center.x, Center.y, Zoom);

		const vec2 CharPos = LocalCharPos();
		const vec2 Vel = GameClient()->m_PredictedChar.m_Vel;
		const float R = 28.0f;

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();

		// Translucent fill over every tile anti-void currently treats as dangerous (near the player)
		const int Range = 16;
		const int Cx = (int)(CharPos.x / 32.0f);
		const int Cy = (int)(CharPos.y / 32.0f);
		Graphics()->SetColor(1.0f, 0.0f, 0.0f, 0.22f);
		for(int ty = Cy - Range; ty <= Cy + Range; ++ty)
		{
			for(int tx = Cx - Range; tx <= Cx + Range; ++tx)
			{
				if(tx < 0 || ty < 0 || tx >= Collision()->GetWidth() || ty >= Collision()->GetHeight())
					continue;
				if(!AntiVoidDangerAt(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f))
					continue;
				IGraphics::CQuadItem Quad(tx * 32.0f, ty * 32.0f, 32.0f, 32.0f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}
		}

		// Sample-point markers (green = clear, red = danger)
		const float LookTicks = 8.0f;
		const float SideTriggerDist = (float)g_Config.m_TcAntiVoidSideDistance;
		const float VertTriggerDist = (float)g_Config.m_TcAntiVoidDistance;
		const float MaxLook = maximum(maximum(SideTriggerDist, VertTriggerDist), 32.0f * 8.0f);
		const float SideLook = std::clamp(absolute(Vel.x) * LookTicks, SideTriggerDist, MaxLook);

		auto DrawPoint = [&](float x, float y) {
			if(AntiVoidDangerAt(x, y))
				Graphics()->SetColor(1.0f, 0.2f, 0.2f, 0.95f);
			else
				Graphics()->SetColor(0.2f, 1.0f, 0.2f, 0.95f);
			IGraphics::CQuadItem Quad(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
			Graphics()->QuadsDrawTL(&Quad, 1);
		};
		DrawPoint(CharPos.x + SideLook, CharPos.y);
		DrawPoint(CharPos.x - SideLook, CharPos.y);
		DrawPoint(CharPos.x, CharPos.y - (VertTriggerDist + R));
		DrawPoint(CharPos.x, CharPos.y + (VertTriggerDist + R));

		Graphics()->QuadsEnd();
		Graphics()->MapScreen(aSavedScreen[0], aSavedScreen[1], aSavedScreen[2], aSavedScreen[3]);
	}
}

bool CControls::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
		return false;

	if(CursorType == IInput::CURSOR_JOYSTICK && g_Config.m_InpControllerAbsolute && GameClient()->m_Snap.m_pGameInfoObj && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		vec2 AbsoluteDirection;
		if(Input()->GetActiveJoystick()->Absolute(&AbsoluteDirection.x, &AbsoluteDirection.y))
		{
			m_aMousePos[g_Config.m_ClDummy] = AbsoluteDirection * GetMaxMouseDistance();
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::ABSOLUTE;
		}
		return true;
	}

	float Factor = 1.0f;
	if(g_Config.m_ClDyncam && g_Config.m_ClDyncamMousesens)
	{
		Factor = g_Config.m_ClDyncamMousesens / 100.0f;
	}
	else
	{
		switch(CursorType)
		{
		case IInput::CURSOR_MOUSE:
			Factor = g_Config.m_InpMousesens / 100.0f;
			break;
		case IInput::CURSOR_JOYSTICK:
			Factor = g_Config.m_InpControllerSens / 100.0f;
			break;
		default:
			dbg_assert_failed("CControls::OnCursorMove CursorType %d", (int)CursorType);
		}
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
		Factor *= GameClient()->m_Camera.m_Zoom;

	m_aMousePos[g_Config.m_ClDummy] += vec2(x, y) * Factor;
	GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::RELATIVE;
	ClampMousePos();
	return true;
}

void CControls::ClampMousePos()
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < 0)
	{
		m_aMousePos[g_Config.m_ClDummy].x = std::clamp(m_aMousePos[g_Config.m_ClDummy].x, -201.0f * 32, (Collision()->GetWidth() + 201.0f) * 32.0f);
		m_aMousePos[g_Config.m_ClDummy].y = std::clamp(m_aMousePos[g_Config.m_ClDummy].y, -201.0f * 32, (Collision()->GetHeight() + 201.0f) * 32.0f);
	}
	else
	{
		const float MouseMin = GetMinMouseDistance();
		const float MouseMax = GetMaxMouseDistance();

		float MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance < 0.001f)
		{
			m_aMousePos[g_Config.m_ClDummy].x = 0.001f;
			m_aMousePos[g_Config.m_ClDummy].y = 0;
			MouseDistance = 0.001f;
		}
		if(MouseDistance < MouseMin)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMin;
		MouseDistance = length(m_aMousePos[g_Config.m_ClDummy]);
		if(MouseDistance > MouseMax)
			m_aMousePos[g_Config.m_ClDummy] = normalize_pre_length(m_aMousePos[g_Config.m_ClDummy], MouseDistance) * MouseMax;

		if(g_Config.m_TcLimitMouseToScreen)
		{
			float Width, Height;
			Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), 1.0f, &Width, &Height);
			Height /= 2.0f;
			Width /= 2.0f;
			if(g_Config.m_TcLimitMouseToScreen == 2)
				Width = Height;
			m_aMousePos[g_Config.m_ClDummy].y = std::clamp(m_aMousePos[g_Config.m_ClDummy].y, -Height, Height);
			m_aMousePos[g_Config.m_ClDummy].x = std::clamp(m_aMousePos[g_Config.m_ClDummy].x, -Width, Width);
		}
	}
}

float CControls::GetMinMouseDistance() const
{
	return g_Config.m_ClDyncam ? g_Config.m_ClDyncamMinDistance : g_Config.m_ClMouseMinDistance;
}

float CControls::GetMaxMouseDistance() const
{
	float CameraMaxDistance = 200.0f;
	float FollowFactor = (g_Config.m_ClDyncam ? g_Config.m_ClDyncamFollowFactor : g_Config.m_ClMouseFollowfactor) / 100.0f;
	float DeadZone = g_Config.m_ClDyncam ? g_Config.m_ClDyncamDeadzone : g_Config.m_ClMouseDeadzone;
	float MaxDistance = g_Config.m_ClDyncam ? g_Config.m_ClDyncamMaxDistance : g_Config.m_ClMouseMaxDistance;
	return minimum((FollowFactor != 0 ? CameraMaxDistance / FollowFactor + DeadZone : MaxDistance), MaxDistance);
}

bool CControls::CheckNewInput()
{
	bool NewInput[2] = {};
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		CNetObj_PlayerInput TestInput = m_aInputData[Dummy];
		if(Dummy == g_Config.m_ClDummy)
		{
			TestInput.m_Direction = 0;
			if(m_aInputDirectionLeft[Dummy] && !m_aInputDirectionRight[Dummy])
				TestInput.m_Direction = -1;
			if(!m_aInputDirectionLeft[Dummy] && m_aInputDirectionRight[Dummy])
				TestInput.m_Direction = 1;
		}

		if(m_aFastInput[Dummy].m_Direction != TestInput.m_Direction)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_Hook != TestInput.m_Hook)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_Fire != TestInput.m_Fire)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_Jump != TestInput.m_Jump)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_NextWeapon != TestInput.m_NextWeapon)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_PrevWeapon != TestInput.m_PrevWeapon)
			NewInput[Dummy] = true;
		if(m_aFastInput[Dummy].m_WantedWeapon != TestInput.m_WantedWeapon)
			NewInput[Dummy] = true;

		bool SetMousePos = false;
		// We need to be careful about how we manage the mouse position to avoid mispredicted hooks and fires
		// on the first tick that they activate before we know what mouse position we actually sent to the server
		if(Dummy == g_Config.m_ClDummy)
		{
			if(m_aFastInput[Dummy].m_Hook == 0 && TestInput.m_Hook == 1)
			{
				m_FastInputHookAction = true;
				SetMousePos = true;
			}
			if(m_aFastInput[Dummy].m_Fire != TestInput.m_Fire && TestInput.m_Fire % 2 == 1)
			{
				m_FastInputFireAction = true;
				SetMousePos = true;
			}
			if(!m_FastInputHookAction && !m_FastInputFireAction)
			{
				SetMousePos = true;
			}
		}

		if(SetMousePos)
		{
			TestInput.m_TargetX = (int)m_aMousePos[Dummy].x;
			TestInput.m_TargetY = (int)m_aMousePos[Dummy].y;
		}
		else
		{
			TestInput.m_TargetX = m_aFastInput[Dummy].m_TargetX;
			TestInput.m_TargetY = m_aFastInput[Dummy].m_TargetY;
		}

		m_aFastInput[Dummy] = TestInput;
	}

	if(NewInput[0] || NewInput[1])
		return true;
	else
		return false;
}
