/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "controls.h"

#include <game/freeze_wall.h>

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
	m_aInputHook[Dummy] = 0;
}

void CControls::OnPlayerDeath()
{
	AvoidReportOutcome("YOU DIED");
	for(int &AmmoCount : m_aAmmoCount)
		AmmoCount = 0;
	// Drop any pending rocket-save weapon restore so we don't switch weapons right after respawning.
	for(int &PrevWeapon : m_aAntiVoidRocketPrevWeapon)
		PrevWeapon = -1;
	// A rescue hook that was mid-save when we died must not keep hooking after respawn: drop the
	// latch AND release the forced m_Hook, which would otherwise stick in m_aInputData forever
	// (only real key events rewrite it).
	for(int d = 0; d < NUM_DUMMIES; ++d)
	{
		if(m_aRescueHookHeld[d])
		{
			m_aInputData[d].m_Hook = 0;
			m_aRescueHookHeld[d] = false;
		}
		m_aRescueHookHold[d] = 0;
		m_aAvoidHookSuppressed[d] = false; // fresh spawn: nothing is being held off anymore
	}
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

// TClient hole assist bind: track the raw held state, and in toggle mode flip the latch on each press edge.
// Both are kept up to date regardless of mode, so switching tc_hole_assist_hold mid-game just works.
void CControls::ConKeyHoleAssist(IConsole::IResult *pResult, void *pUserData)
{
	CControls *pControls = (CControls *)pUserData;
	const bool Pressed = pResult->GetInteger(0) != 0;
	const bool PressEdge = Pressed && !pControls->m_HoleAssistPressed;
	const bool ReleaseEdge = !Pressed && pControls->m_HoleAssistPressed;
	const bool WasActive = pControls->HoleAssistActive();
	if(PressEdge)
		pControls->m_HoleAssistToggled = !pControls->m_HoleAssistToggled;
	pControls->m_HoleAssistPressed = Pressed;
	// Fresh engage/disengage: forget any parked state so the next run re-approaches the gap cleanly.
	if(PressEdge || ReleaseEdge)
		for(bool &Settled : pControls->m_aHoleSettled)
			Settled = false;
	// Announce the on/off status in chat whenever the effective activation flips (both hold and toggle modes).
	const bool NowActive = pControls->HoleAssistActive();
	if(g_Config.m_TcHoleAssist && NowActive != WasActive && pControls->Client()->State() == IClient::STATE_ONLINE)
		pControls->GameClient()->Echo(NowActive ? "Hole assist: ON" : "Hole assist: OFF");
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
		// TClient: the hook key writes to the shadow m_aInputHook, from which m_aInputData.m_Hook is
		// rebuilt every tick (like +left/+right feed the direction). This keeps avoid's force-release from
		// sticking in m_aInputData forever — see m_aInputHook.
		static CInputState s_State = {this, {&m_aInputHook[0], &m_aInputHook[1]}};
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

	// TClient hole assist activation key (any bindable key; hold or toggle per tc_hole_assist_hold)
	Console()->Register("+tc_hole_assist", "", CFGFLAG_CLIENT, ConKeyHoleAssist, this, "Activate the hole assist (hold or toggle depending on tc_hole_assist_hold)");
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

		// TClient: rebuild the hook bit from the raw held key every tick (see m_aInputHook), so a hook that
		// avoid force-released on a previous tick comes back to the real held state before avoid re-decides.
		// Without this the forced 0 would persist in m_aInputData with no key event to restore it, killing a
		// held hook until you physically re-press — which is exactly why the old release could not "resume".
		m_aInputData[g_Config.m_ClDummy].m_Hook = m_aInputHook[g_Config.m_ClDummy];

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

	// Freeze is the outcome that actually matters on a gores map, and it is not a death, so nothing else
	// would ever report it. Edge-triggered: only the tick you go from free to frozen.
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const bool Frozen = LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Predicted.m_FreezeEnd != 0;
	if(g_Config.m_TcAntiVoidDebug && Frozen && !m_aAvoidWasFrozen[g_Config.m_ClDummy])
		AvoidReportOutcome("YOU GOT FROZEN");
	m_aAvoidWasFrozen[g_Config.m_ClDummy] = Frozen;

	// STAND DOWN WHILE FROZEN. The server throws your whole input away until you thaw, so every override
	// decided here is dead weight — and the one still sitting in the input on the tick you DO thaw steers
	// you for no reason you can see. The sim cannot notice this by itself: it runs on CCharacterCore, which
	// has no freeze state at all, so it keeps confidently predicting a tee that physically cannot move.
	// Drop the latches too, so nothing carries over from a decision taken while you were helpless.
	if(Frozen)
	{
		m_aAvoidHookSuppressed[g_Config.m_ClDummy] = false;
		m_aRescueHookHold[g_Config.m_ClDummy] = 0;
		m_aRescueHookThrown[g_Config.m_ClDummy] = false;
		m_aRescueHookHeld[g_Config.m_ClDummy] = false;
		return;
	}

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

	// Hole assist runs last so that, while you deliberately engage it, it wins the horizontal direction
	// over anti-void braking (which might otherwise steer you away from the very gap you are aiming at).
	if(g_Config.m_TcHoleAssist && HoleAssistActive())
		ApplyHoleAssist();
}

// TClient hole assist: is it engaged right now? Hold mode follows the key; toggle mode follows the latch.
bool CControls::HoleAssistActive() const
{
	return g_Config.m_TcHoleAssistHold ? m_HoleAssistPressed : m_HoleAssistToggled;
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

// TClient avoid: classify a single tile point. 0 = safe, 1 = freeze (you lose control and slide, but
// freeze itself is NOT death — you get hooked out or unfrozen), 2 = real death with no recovery:
// a kill tile, deep freeze, the map edge, or a teleporter. This split is the whole point: in gores
// you dive into freeze constantly and hook back out, so treating plain freeze as death made the bot
// fight every jump. Death is only when the frozen slide actually reaches a class-2 spot. Both game
// and front layers are read raw (GetCollisionAt never reports freeze tiles). With tc_avoid_unfreeze
// off, freeze is treated strictly (class 2) for players who really want to never touch it.
int CControls::AvoidDangerClassPoint(float x, float y, bool ForceFreezeRecoverable) const
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= Collision()->GetWidth() || Ty >= Collision()->GetHeight())
		return 2; // running off the map kills
	const int Index = Collision()->GetPureMapIndex(x, y);
	if(g_Config.m_TcAntiVoidTele && (Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index)))
		return 2;
	const bool FreezeRecoverable = g_Config.m_TcAvoidUnfreeze || ForceFreezeRecoverable;
	int Worst = 0;
	for(const int T : {Collision()->GetTileIndex(Index), Collision()->GetFrontTileIndex(Index)})
	{
		if((g_Config.m_TcAntiVoidDeath && T == TILE_DEATH) || (g_Config.m_TcAntiVoidDeepFreeze && T == TILE_DFREEZE))
			return 2;
		if((g_Config.m_TcAntiVoidFreeze && T == TILE_FREEZE) || (g_Config.m_TcAntiVoidLiveFreeze && T == TILE_LFREEZE))
			Worst = FreezeRecoverable ? 1 : 2;
	}
	return Worst;
}

// TClient avoid: is this exact point a hard, no-recovery death — a kill tile or off the map edge?
// Freeze is deliberately excluded here; it has its own, smaller corner margin below.
bool CControls::AvoidHardDeathPoint(float x, float y) const
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= Collision()->GetWidth() || Ty >= Collision()->GetHeight())
		return true; // off the map
	const int Index = Collision()->GetPureMapIndex(x, y);
	return (g_Config.m_TcAntiVoidDeath && (Collision()->GetTileIndex(Index) == TILE_DEATH || Collision()->GetFrontTileIndex(Index) == TILE_DEATH));
}

// TClient avoid: danger for the whole tee body centered at (x, y). Three shells, small to big:
//  - centre: the true tile you stand on (freeze or death), exactly like the game.
//  - ±6px freeze margin: a freeze tile this close means your body is a pixel from freezing — the
//    game freezes on the centre only, but the lightweight sim can drift ~1px, so this small buffer
//    catches the "clipped the freeze corner by a pixel" case. Kept small (6px « 16px half-tile) so a
//    tight freeze channel you thread down the middle does NOT false-trigger.
//  - ±14px death corners: a kill tile / off-map anywhere under your full hitbox, so a death void that
//    only juts into the edge of your body is caught with a margin.
int CControls::AvoidDangerClass(float x, float y, bool ForceFreezeRecoverable) const
{
	int Worst = AvoidDangerClassPoint(x, y, ForceFreezeRecoverable);
	if(Worst == 2)
		return 2;
	// tc_avoid_freeze_margin: 0 reproduces the game exactly (freeze is decided by the centre tile alone,
	// so grazing freeze with the edge of the hitbox is legal and the bot stays out of it); anything larger
	// makes the bot treat freeze that close to your centre as already touched.
	const float FreezeMargin = (float)g_Config.m_TcAvoidFreezeMargin;
	if(FreezeMargin > 0.0f)
	{
		for(const float Ox : {-FreezeMargin, FreezeMargin})
			for(const float Oy : {-FreezeMargin, FreezeMargin})
				Worst = maximum(Worst, AvoidDangerClassPoint(x + Ox, y + Oy, ForceFreezeRecoverable));
		if(Worst == 2)
			return 2;
	}
	const float R = CCharacterCore::PhysicalSize() / 2.0f; // full half-hitbox = 14px
	for(const float Ox : {-R, R})
		for(const float Oy : {-R, R})
			if(AvoidHardDeathPoint(x + Ox, y + Oy))
				return 2;
	return Worst; // 0 = safe, or 1 = recoverable freeze within the body
}

// TClient avoid: run the real core physics forward. For the first DelayTicks the player's own raw
// input is held unchanged; from then on the override (BlockHook forces the hook off, ForceDir
// overrides direction -1/0/1, 2 = keep) is applied. Delay = 0 means "act now"; Delay = margin
// means "keep playing for a moment, then act" and is how we find the last safe moment. The core
// itself throws, flies, grabs and drags the hook exactly like the server, so a hook still in the
// air is covered, not just a grabbed one.
//
// Freeze is NOT a death: on the first freeze contact the tee loses control (input zeroed, hook
// dropped) and just slides/falls on physics. From there only a class-2 tile (kill / deep / edge /
// tele) is a real death; if the frozen slide reaches an unfreeze tile, comes to rest, or the
// window runs out without dying, the tee is alive (frozen but recoverable — you hook out). That is
// why diving into freeze off a platform no longer trips the bot. The freeze slide is followed past
// the normal window (capped) so a long frozen fall into the void below is still caught.
//
// Danger is sampled along each tick's movement segment with the center-tile rule, same as the game.
// Null world: other tees are ignored and the shared prediction state is never touched. Returns the
// tick at which death becomes unavoidable (the moment control is lost, if frozen), or -1 if safe.
int CControls::AvoidSimDeathTick(int DelayTicks, bool BlockHook, int ForceDir, const vec2 *pThrowHook, bool FreezeRecoverable, vec2 *pOutDeathPos, bool *pOutViaFreeze, bool *pOutHorizon) const
{
	// pOutDeathPos / pOutViaFreeze are only filled for the debug log: WHERE the sim ends up dying and
	// whether it got there on its own or after losing control in freeze.
	const CNetObj_PlayerInput RawInput = m_aInputData[g_Config.m_ClDummy];
	CCharacterCore Core = GameClient()->m_PredictedChar;
	Core.Init(nullptr, Collision());
	Core.SetHookedPlayer(-1);

	auto UnfreezeAt = [&](float px, float py) -> bool {
		const int Index = Collision()->GetPureMapIndex(px, py);
		for(const int T : {Collision()->GetTileIndex(Index), Collision()->GetFrontTileIndex(Index)})
			if(T == TILE_UNFREEZE || T == TILE_DUNFREEZE)
				return true;
		return false;
	};

	if(pOutHorizon)
		*pOutHorizon = false;
	const int CheckTicks = g_Config.m_TcAvoidCheckTicks;
	const int MaxFrozenSlide = g_Config.m_TcAvoidUnfreezeTicks; // follow a frozen fall this far past the window to catch the void below
	int FrozenAt = -1; // tick we first got frozen, -1 = still in control
	bool ThrewRescue = false; // pThrowHook phase: our throw has left the tee
	for(int i = 0; i < (FrozenAt < 0 ? CheckTicks : CheckTicks + MaxFrozenSlide); ++i)
	{
		Core.m_Input = RawInput;
		if(i >= DelayTicks)
		{
			if(BlockHook)
				Core.m_Input.m_Hook = 0;
			if(ForceDir != 2)
				Core.m_Input.m_Direction = ForceDir;
			if(pThrowHook)
			{
				// Rescue throw: release whatever hook was active first (it may be the killer), then on
				// the first IDLE tick throw toward the target and keep holding; a miss retracts, gets
				// released and re-thrown — the same cycle the real latch runs.
				if(Core.m_HookState == HOOK_IDLE)
				{
					Core.m_Input.m_Hook = 1;
					Core.m_Input.m_TargetX = round_to_int(pThrowHook->x * 256.0f);
					Core.m_Input.m_TargetY = round_to_int(pThrowHook->y * 256.0f);
					ThrewRescue = true;
				}
				else if(ThrewRescue && (Core.m_HookState == HOOK_FLYING || Core.m_HookState == HOOK_GRABBED))
					Core.m_Input.m_Hook = 1;
				else
					Core.m_Input.m_Hook = 0;
			}
		}
		if(FrozenAt >= 0)
		{
			// Frozen: the server ignores movement and drops the hook — a pure slide.
			Core.m_Input.m_Direction = 0;
			Core.m_Input.m_Hook = 0;
			Core.m_Input.m_Jump = 0;
		}
		const vec2 PrevPos = Core.m_Pos;
		Core.Tick(true);
		Core.Move();
		Core.Quantize(); // the real pipeline quantizes every tick; skipping it drifts the sim
		const int Steps = maximum(1, (int)(distance(PrevPos, Core.m_Pos) / 8.0f)); // 8px steps: fine enough that a thin void corner between ticks isn't skipped at speed
		for(int s = 1; s <= Steps; ++s)
		{
			const vec2 P = mix(PrevPos, Core.m_Pos, (float)s / (float)Steps);
			const int C = AvoidDangerClass(P.x, P.y, FreezeRecoverable);
			if(C == 2)
			{
				if(pOutDeathPos)
					*pOutDeathPos = P;
				if(pOutViaFreeze)
					*pOutViaFreeze = FrozenAt >= 0;
				return FrozenAt >= 0 ? FrozenAt : i;
			}
			if(C == 1 && FrozenAt < 0)
				FrozenAt = i;
			if(FrozenAt >= 0 && UnfreezeAt(P.x, P.y))
				return -1; // the frozen slide reaches an unfreeze tile: recovered
		}
		if(FrozenAt >= 0 && length(Core.m_Vel) < 1.0f)
			return -1; // frozen but come to rest without hitting death: stuck, not dead
	}
	// Ran out of window rather than reaching safety: the tee is still moving toward whatever is out there.
	// Reported separately because "-1" here means "I did not look far enough", not "you are safe" — a hook
	// released 150px under a freeze ceiling looked like a rescue for exactly this reason.
	if(pOutHorizon)
		*pOutHorizon = true;
	return -1;
}

// TClient avoid: the freeze-wall save lives in game/freeze_wall.cpp so the offline harness can replay
// recorded situations against the real map with the exact same code. Everything here is glue: build the
// config from the cvars, run one decision, log it.
float CControls::AvoidFreezeSideDist(float Side, float MaxDist, float Band) const
{
	return CFreezeWall::SideDist(Collision(), GameClient()->m_PredictedChar.m_Pos, Side, MaxDist, Band, AvoidFreezeWallCfg());
}

CFreezeWallCfg CControls::AvoidFreezeWallCfg() const
{
	CFreezeWallCfg Cfg;
	Cfg.m_Mode = g_Config.m_TcAvoidDirection;
	Cfg.m_Freeze = g_Config.m_TcAntiVoidFreeze;
	Cfg.m_DeepFreeze = g_Config.m_TcAntiVoidDeepFreeze;
	Cfg.m_LiveFreeze = g_Config.m_TcAntiVoidLiveFreeze;
	Cfg.m_Death = g_Config.m_TcAntiVoidDeath;
	Cfg.m_Tele = g_Config.m_TcAntiVoidTele;
	return Cfg;
}

int CControls::AvoidFreezeWallSteer()
{
	const int Dummy = g_Config.m_ClDummy;
	const CCharacterCore &Core = GameClient()->m_PredictedChar;
	const bool WasLatched = m_aFreezeWallState[Dummy].m_Latch;
	const CFreezeWallDecision D = CFreezeWall::Decide(Collision(), Core, m_aInputData[Dummy], AvoidFreezeWallCfg(), m_aFreezeWallState[Dummy]);

	m_aAvoidFreezeWallLatch[Dummy] = m_aFreezeWallState[Dummy].m_Latch;
	m_aAvoidFreezeWallHit[Dummy] = D.m_Raw != 0;
	m_aAvoidFreezeWallPos[Dummy] = D.m_Pos;

	if(g_Config.m_TcAntiVoidDebug && (D.m_Raw != 0 || D.m_Dir != 2 || WasLatched))
	{
		const vec2 P = Core.m_Pos;
		char aWall[64] = "-";
		if(D.m_Raw != 0)
			AvoidTileDesc(D.m_Pos.x, D.m_Pos.y, aWall, sizeof(aWall));
		log_info("avoid", "fw %s | pos %.0f,%.0f vel %.2f,%.2f key=%+d latch=%d->%d | %s in %d ticks at %.0f,%.0f [%s] dx=%+.0fpx dy=%+.0fpx | freeze-free ticks: raw=%d stop=%d counter=%d best=%d(+%dt later=%d) | clamp=%.0fpx hook=%d | applying %s",
			D.m_pWhy, P.x, P.y, Core.m_Vel.x, Core.m_Vel.y, m_aInputData[Dummy].m_Direction, WasLatched ? 1 : 0, m_aFreezeWallState[Dummy].m_Latch ? 1 : 0,
			D.m_Raw == 1 ? "wall" : (D.m_Raw == 2 ? "freeze (top/bottom face or death)" : "nothing"),
			D.m_RawTick, D.m_Pos.x, D.m_Pos.y, aWall, D.m_Pos.x - P.x, D.m_Pos.y - P.y,
			D.m_RawTick, D.m_StopScore, D.m_CounterScore, D.m_BestScore, D.m_LateTicks, D.m_LateScore, D.m_ClampDist, Core.m_HookState,
			D.m_Dir == 2 ? "nothing" : (D.m_Dir == 0 ? "0 (key let go)" : (D.m_Dir < 0 ? "-1 (left)" : "+1 (right)")));
	}
	return D.m_Dir;
}

// TClient rescue hook: choose where to throw. Candidates are Segments rays spread across the FOV
// centered on the current cursor, tried nearest-to-cursor first so the save lands as close to where
// the player was already aiming as possible. A ray is only simulated when there is something to hit
// within hook range (open sky can't be grabbed). The first ray whose throw-and-hold survives the
// whole window wins. CanWait (when asked for): the winning throw would still work after sitting on
// our hands for the margin — i.e. this is not yet the last affordable tick. Only the winner is
// re-checked delayed; a farther ray could in theory still work later, but firing the nearest one a
// touch early beats gambling the rescue on that.
bool CControls::RescueHookAim(vec2 &OutDir, bool *pOutCanWait) const
{
	const int Dummy = g_Config.m_ClDummy;
	const int KEEP_DIR = 2;
	const vec2 Pos = GameClient()->m_PredictedChar.m_Pos;
	const float AimAngle = angle(m_aMousePos[Dummy]);
	const int Segments = g_Config.m_TcRescueHookSegments;
	const float Step = ((float)g_Config.m_TcRescueHookFov * pi / 180.0f) / (float)Segments;
	for(int k = 0; k < Segments; ++k)
	{
		const int Idx = (k + 1) / 2 * (k % 2 ? 1 : -1); // 0, +1, -1, +2, -2, ...: nearest to the cursor first
		const vec2 Dir = direction(AimAngle + Step * (float)Idx);
		// Range prefilter: default hook length is 380 units — a ray with nothing solid within reach
		// can't grab anything, no point running the expensive sim on it.
		if(!Collision()->IntersectLine(Pos, Pos + Dir * 400.0f, nullptr, nullptr))
			continue;
		if(AvoidSimDeathTick(0, false, KEEP_DIR, &Dir) >= 0)
			continue;
		OutDir = Dir;
		if(pOutCanWait)
			*pOutCanWait = AvoidSimDeathTick(g_Config.m_TcRescueHookMargin, false, KEEP_DIR, &Dir) < 0;
		return true;
	}
	return false;
}

// TClient rescue hook: one tick of latch upkeep. Phases: before our throw, force-release whatever
// hook was active (it may be the very thing dragging us down); on the first IDLE tick aim fresh and
// throw; then hold the key while it flies/hangs; a miss retracts, gets released and re-thrown. If no
// throw from the current position survives anymore, the latch gives up instead of spamming hopeless
// hooks. The owner (ApplyAntiVoid) releases the latch the moment the player's own raw input survives
// again, and the tick budget caps a pathological hold.
void CControls::RescueHookUpkeep(CNetObj_PlayerInput &Input)
{
	const int Dummy = g_Config.m_ClDummy;
	m_aRescueHookHold[Dummy]--;
	if(m_aRescueHookHold[Dummy] <= 0)
	{
		// Budget exhausted: actively release — a forced m_Hook would otherwise stick in m_aInputData.
		Input.m_Hook = 0;
		m_aRescueHookHeld[Dummy] = false;
		return;
	}
	const int State = GameClient()->m_PredictedChar.m_HookState;
	if(State == HOOK_IDLE)
	{
		vec2 Dir;
		if(!RescueHookAim(Dir))
		{
			m_aRescueHookHold[Dummy] = 0; // no saving throw from here: hand the input back
			Input.m_Hook = 0;
			m_aRescueHookHeld[Dummy] = false;
			return;
		}
		Input.m_Hook = 1;
		Input.m_TargetX = round_to_int(Dir.x * 256.0f);
		Input.m_TargetY = round_to_int(Dir.y * 256.0f);
		m_aRescueHookThrown[Dummy] = true;
		m_aRescueHookHeld[Dummy] = true;
		if(g_Config.m_TcAntiVoidDebug)
			log_info("avoid", "rescue hook: throw at %.0f deg", std::atan2(Dir.y, Dir.x) * 180.0f / pi);
		return;
	}
	if(m_aRescueHookThrown[Dummy] && (State == HOOK_FLYING || State == HOOK_GRABBED))
	{
		Input.m_Hook = 1;
		m_aRescueHookHeld[Dummy] = true;
		return;
	}
	Input.m_Hook = 0; // pre-throw with a foreign hook active, or a retracting miss: release so a (re)throw can arm
	m_aRescueHookHeld[Dummy] = false;
}

// TClient avoid: name the tile under a world point for the debug log. Reports what the danger
// classifier actually reacted to (both layers), not just "dangerous".
void CControls::AvoidTileDesc(float x, float y, char *pBuf, int Size) const
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= Collision()->GetWidth() || Ty >= Collision()->GetHeight())
	{
		str_copy(pBuf, "off-map", Size);
		return;
	}
	const int Index = Collision()->GetPureMapIndex(x, y);
	const char *pName = "air";
	for(const int T : {Collision()->GetTileIndex(Index), Collision()->GetFrontTileIndex(Index)})
	{
		if(T == TILE_DEATH)
			pName = "kill";
		else if(T == TILE_DFREEZE)
			pName = "deep freeze";
		else if(T == TILE_LFREEZE)
			pName = "live freeze";
		else if(T == TILE_FREEZE)
			pName = "freeze";
		else if(T == TILE_UNFREEZE || T == TILE_DUNFREEZE)
			pName = "unfreeze";
		else if(T == TILE_SOLID || T == TILE_NOHOOK)
			pName = "solid";
	}
	if(Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index))
		pName = "tele";
	str_format(pBuf, Size, "%s @tile %d,%d", pName, Tx, Ty);
}

// TClient avoid: the verbose report behind tc_anti_void_debug. One decision = four lines, enough to
// replay it afterwards without guessing: where the tee is and how it moves, what the sim thinks kills
// it and where, what every alternative input would have done, and which gates were open or shut.
// Costs a handful of extra simulations, so it only ever runs while the debug setting is on.
void CControls::AvoidDebugDump(const char *pVerdict, int RawDeath, int DoDir, bool DoBlockHook) const
{
	const int Dummy = g_Config.m_ClDummy;
	str_copy(const_cast<char *>(m_aAvoidLastVerdict), pVerdict, sizeof(m_aAvoidLastVerdict));
	const_cast<int &>(m_AvoidLastVerdictTick) = Client()->PredGameTick(Dummy);
	const int KEEP_DIR = 2;
	const CCharacterCore &Core = GameClient()->m_PredictedChar;
	const CNetObj_PlayerInput &Input = m_aInputData[Dummy];
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	const bool Grounded = Collision()->CheckPoint(Core.m_Pos.x + HalfSize, Core.m_Pos.y + HalfSize + 5.0f) ||
			      Collision()->CheckPoint(Core.m_Pos.x - HalfSize, Core.m_Pos.y + HalfSize + 5.0f);
	const int Counter = Core.m_Vel.x > 0.0f ? -1 : 1;
	const char *pHookState = "?";
	switch(Core.m_HookState)
	{
	case HOOK_RETRACTED: pHookState = "retracted"; break;
	case HOOK_IDLE: pHookState = "idle"; break;
	case HOOK_FLYING: pHookState = "flying"; break;
	case HOOK_GRABBED: pHookState = "grabbed"; break;
	default: pHookState = "retracting"; break;
	}

	// What the sim sees for the untouched input, and where exactly it ends.
	vec2 DeathPos = vec2(0.0f, 0.0f);
	bool ViaFreeze = false;
	bool RawHorizon = false;
	const int Death = AvoidSimDeathTick(0, false, KEEP_DIR, nullptr, false, &DeathPos, &ViaFreeze, &RawHorizon);
	char aDeathTile[64] = "-";
	if(Death >= 0)
		AvoidTileDesc(DeathPos.x, DeathPos.y, aDeathTile, sizeof(aDeathTile));
	char aHere[64];
	AvoidTileDesc(Core.m_Pos.x, Core.m_Pos.y, aHere, sizeof(aHere));

	// Every alternative the decision logic can pick, so a wrong choice is visible in the numbers. A "-1"
	// is written as "-1h" when the tee merely outlived the window instead of reaching safety: for a tee
	// that is not frozen EVERY survival is of that kind, so a short tc_avoid_check_ticks turns "rescued"
	// into "not looked at", and the bot lets go of you a few ticks before the freeze it never saw.
	char aStop[16], aCounter[16], aHookOff[16], aHookOffStop[16], aWait[16], aRaw[16];
	auto Sim = [&](int Delay, bool Block, int Dir, char *pBuf) {
		bool Horizon = false;
		const int T = AvoidSimDeathTick(Delay, Block, Dir, nullptr, false, nullptr, nullptr, &Horizon);
		str_format(pBuf, 16, "%d%s", T, (T < 0 && Horizon) ? "h" : "");
		return T;
	};
	str_format(aRaw, sizeof(aRaw), "%d%s", RawDeath, (RawDeath < 0 && RawHorizon) ? "h" : "");
	Sim(0, false, 0, aStop);
	Sim(0, false, Counter, aCounter);
	Sim(0, true, KEEP_DIR, aHookOff);
	Sim(0, true, 0, aHookOffStop);
	Sim(g_Config.m_TcAvoidKickTicks, true, KEEP_DIR, aWait);
	const bool RealVoid = AvoidSimDeathTick(0, false, KEEP_DIR, nullptr, true) >= 0;

	// Where the cursor points relative to straight up, and whether that still lets the hook come back.
	const vec2 AimVec((float)Input.m_TargetX, (float)Input.m_TargetY);
	const float AngleFromUp = length(AimVec) > 0.001f ? std::acos(std::clamp(-normalize(AimVec).y, -1.0f, 1.0f)) * 180.0f / pi : 0.0f;
	const bool InCone = g_Config.m_TcAvoidResumeCone >= 360 || AngleFromUp <= (float)g_Config.m_TcAvoidResumeCone * 0.5f;

	const bool HookHeld = m_aInputHook[Dummy] != 0 && Input.m_Hook != 0;
	const bool AllowHook = g_Config.m_TcAvoidHook && HookHeld &&
			       (Core.m_HookState == HOOK_GRABBED || Core.m_HookState == HOOK_FLYING || Core.m_HookState == HOOK_IDLE);

	// Spell out what is actually being written into the input this tick. "counter-steer" alone never said
	// which way, and a steer that only removes your key looks the same in the log as one that reverses it.
	char aApplied[96];
	if(DoDir == KEEP_DIR && !DoBlockHook)
		str_copy(aApplied, "nothing (your input goes through untouched)", sizeof(aApplied));
	else
		str_format(aApplied, sizeof(aApplied), "dir %+d -> %s%s%s", Input.m_Direction,
			DoDir == KEEP_DIR ? "kept" : (DoDir == 0 ? "0 (key let go)" : (DoDir < 0 ? "-1 (pushed left)" : "+1 (pushed right)")),
			DoBlockHook ? ", hook " : "", DoBlockHook ? "1 -> 0 (released)" : "");
	log_info("avoid", "== %s (death in %d) tick %d | applying: %s", pVerdict, RawDeath, Client()->PredGameTick(Dummy), aApplied);
	log_info("avoid", "   self: pos %.0f,%.0f [%s] vel %.2f,%.2f speed %.1f ground=%d hook=%s(%d) keys: dir=%+d hookkey=%d jump=%d",
		Core.m_Pos.x, Core.m_Pos.y, aHere, Core.m_Vel.x, Core.m_Vel.y, length(Core.m_Vel), Grounded ? 1 : 0,
		pHookState, Core.m_HookState, Input.m_Direction, m_aInputHook[Dummy], Input.m_Jump);
	// WHY that point counts as deadly: the classifier checks three shells around it (the centre tile like
	// the game does, the tc_avoid_freeze_margin slack for freeze, and the full hitbox corners for kill
	// tiles / the map edge). Without naming the one that fired, a death reported on a plain "air" tile
	// looks like a bug when it is really the slack reaching into the neighbouring tile.
	char aTrigger[128] = "unknown";
	if(Death >= 0)
	{
		char aTileBuf[64];
		bool Found = false;
		if(AvoidDangerClassPoint(DeathPos.x, DeathPos.y) == 2)
		{
			str_copy(aTrigger, "centre tile (same rule as the game)", sizeof(aTrigger));
			Found = true;
		}
		const float M = (float)g_Config.m_TcAvoidFreezeMargin;
		if(!Found && M > 0.0f)
		{
			for(const float Ox : {-M, M})
				for(const float Oy : {-M, M})
					if(!Found && AvoidDangerClassPoint(DeathPos.x + Ox, DeathPos.y + Oy) == 2)
					{
						AvoidTileDesc(DeathPos.x + Ox, DeathPos.y + Oy, aTileBuf, sizeof(aTileBuf));
						str_format(aTrigger, sizeof(aTrigger), "freeze slack +-%.0fpx -> %s (tc_avoid_freeze_margin)", M, aTileBuf);
						Found = true;
					}
		}
		const float R = CCharacterCore::PhysicalSize() / 2.0f;
		if(!Found)
		{
			for(const float Ox : {-R, R})
				for(const float Oy : {-R, R})
					if(!Found && AvoidHardDeathPoint(DeathPos.x + Ox, DeathPos.y + Oy))
					{
						AvoidTileDesc(DeathPos.x + Ox, DeathPos.y + Oy, aTileBuf, sizeof(aTileBuf));
						str_format(aTrigger, sizeof(aTrigger), "hitbox corner +-%.0fpx -> %s", R, aTileBuf);
						Found = true;
					}
		}
		if(!Found)
			str_copy(aTrigger, "the frozen slide reached it", sizeof(aTrigger));
	}
	if(Death >= 0)
		log_info("avoid", "   dies: in %d ticks at %.0f,%.0f [%s] %s, %.0fpx away | triggered by: %s", Death, DeathPos.x, DeathPos.y, aDeathTile,
			ViaFreeze ? "after losing control in freeze" : "flying straight into it", distance(Core.m_Pos, DeathPos), aTrigger);
	else
		log_info("avoid", "   dies: %s", RawHorizon ? "not within the window — the sim simply stopped looking after tc_avoid_check_ticks, it did NOT reach safety" : "no: the tee reaches solid ground or an unfreeze tile inside the window");
	log_info("avoid", "   what-if (-1 = survives, -1h = only outlived the %d tick window): raw=%s stop=%s counter(%+d)=%s hookoff=%s hookoff+stop=%s wait%d+hookoff=%s realvoid=%d",
		g_Config.m_TcAvoidCheckTicks, aRaw, aStop, Counter, aCounter, aHookOff, aHookOffStop, g_Config.m_TcAvoidKickTicks, aWait, RealVoid ? 1 : 0);
	log_info("avoid", "   gates: hook=%d(held=%d) dir=%d(air=%d freezewall=%d latched=%d key=%+d) supp=%d resume=%d rescuehold=%d nsif=%d unfreeze=%s thread=%d slack=%dpx check=%d kick=%d hookrelease=%d aim=%.0fdeg-from-up cone=%d(in=%d)",
		AllowHook ? 1 : 0, HookHeld ? 1 : 0,
		m_aAvoidFreezeWallLastDir[Dummy] != KEEP_DIR ? 1 : 0,
		Grounded ? 0 : 1, m_aAvoidFreezeWallHit[Dummy] ? 1 : 0, m_aAvoidFreezeWallLatch[Dummy] ? 1 : 0, m_aAvoidFreezeWallLastDir[Dummy],
		m_aAvoidHookSuppressed[Dummy] ? 1 : 0, g_Config.m_TcAvoidResumeHook, m_aRescueHookHold[Dummy],
		g_Config.m_TcAvoidNsif, g_Config.m_TcAvoidUnfreeze ? "lenient" : "strict",
		g_Config.m_TcAvoidThreadFreeze, g_Config.m_TcAvoidFreezeMargin, g_Config.m_TcAvoidCheckTicks, g_Config.m_TcAvoidKickTicks,
		(AllowHook && (!g_Config.m_TcAvoidThreadFreeze || RealVoid)) ? 1 : 0,
		AngleFromUp, g_Config.m_TcAvoidResumeCone, InCone ? 1 : 0);
}

// TClient avoid: report what actually HAPPENED (a death, a freeze), tied to the last decision. Without
// this the debug log only ever showed decisions, so a run that ended in the void looked identical in the
// log to one that went perfectly — "it released my hook at the last tick" and "I fell" were never on the
// same page.
void CControls::AvoidReportOutcome(const char *pWhat) const
{
	if(!g_Config.m_TcAntiVoidDebug)
		return;
	const int Dummy = g_Config.m_ClDummy;
	const CCharacterCore &Core = GameClient()->m_PredictedChar;
	const int Now = Client()->PredGameTick(Dummy);
	char aHere[64];
	AvoidTileDesc(Core.m_Pos.x, Core.m_Pos.y, aHere, sizeof(aHere));
	if(m_AvoidLastVerdictTick >= 0)
		log_info("avoid", "!! %s at %.0f,%.0f [%s] vel %.2f,%.2f tick %d — %d ticks after the last decision (\"%s\")",
			pWhat, Core.m_Pos.x, Core.m_Pos.y, aHere, Core.m_Vel.x, Core.m_Vel.y, Now, Now - m_AvoidLastVerdictTick, m_aAvoidLastVerdict);
	else
		log_info("avoid", "!! %s at %.0f,%.0f [%s] vel %.2f,%.2f tick %d — avoid had not acted at all",
			pWhat, Core.m_Pos.x, Core.m_Pos.y, aHere, Core.m_Vel.x, Core.m_Vel.y, Now);
}

// TClient avoid, KRX-style Blatant. No distance heuristics: the decision is "does my current input
// eventually kill me, and if so, is THIS the last tick where reacting can still save me?".
//  1. Simulate the raw input for the whole Check window. Survives -> pass through untouched. This
//     is what makes it blatant and what preserves every legit move: a stable hang flush under a
//     freeze ceiling, a normal wall swing, a brief hook tap into safe space — none of those die in
//     the sim, so none are touched.
//  2. It does die. If we could hold the raw input for Kick more ticks and STILL have a rescue
//     afterwards, the last moment hasn't arrived yet: keep playing, don't touch anything.
//  3. It's the last moment (or we're already committed). Apply the mildest override that survives
//     when applied right now — stop steering, counter-steer, release hook, hook+direction. With
//     NSIF, if nothing fully survives, apply whatever buys the most time.
// Direction overrides are ordered before hook ones so a ceiling hang / hook is kept alive whenever
// steering alone is enough. Once the hook is force-released it stays suppressed until the key is
// physically let go, so a still-held hook can't instantly re-throw back into the same void.
void CControls::ApplyAntiVoid()
{
	const int Dummy = g_Config.m_ClDummy;
	CNetObj_PlayerInput &Input = m_aInputData[Dummy];
	m_aAvoidRawInput[Dummy] = Input; // snapshot before we touch anything, for the debug overlay

	// AFK protection: stop intervening once the inputs have been idle for the configured time.
	if(g_Config.m_TcAvoidAfkProtection)
	{
		const float Now = Client()->LocalTime();
		if(mem_comp(&Input, &m_aAvoidPrevInput[Dummy], sizeof(CNetObj_PlayerInput)) != 0)
			m_aAvoidLastActivity[Dummy] = Now;
		m_aAvoidPrevInput[Dummy] = Input;
		if(Now - m_aAvoidLastActivity[Dummy] > (float)g_Config.m_TcAvoidAfkSeconds)
			return;
	}

	// Hooked into another tee: the simulation can't model player physics, hands off entirely.
	if(GameClient()->m_PredictedChar.HookedPlayer() != -1)
		return;


	// Falling edge of the real hook key (held last tick, let go now). The hook key feeds m_aInputHook, so
	// this is how "the player let go / took over" is detected now that key events no longer touch
	// m_aInputData.m_Hook directly. Computed once per tick here since ApplyAntiVoid runs exactly once.
	const bool HookKeyReleaseEdge = m_aAvoidHookKeyPrev[Dummy] != 0 && m_aInputHook[Dummy] == 0;
	m_aAvoidHookKeyPrev[Dummy] = m_aInputHook[Dummy];

	const int KEEP_DIR = 2;
	const int Margin = g_Config.m_TcAvoidKickTicks;
	static bool s_aWasBlocking[NUM_DUMMIES] = {false, false};

	// FREEZE-WALL STEERING — the horizontal save, decided first and completely independently of the generic
	// death logic below. It has to fire even when that logic is perfectly happy (a wall the tee only clips
	// with the corner of the block, or lenient freeze mode where freeze isn't a death at all), and it has to
	// keep working while a rescue-hook latch is running, so it is applied on EVERY exit path.
	const int FreezeWallDir = AvoidFreezeWallSteer();
	auto ApplyFreezeWall = [&]() {
		m_aAvoidFreezeWallLastDir[Dummy] = FreezeWallDir;
		if(FreezeWallDir != KEEP_DIR)
			Input.m_Direction = FreezeWallDir; // AvoidFreezeWallSteer already logged the full why
	};

	// Rescue-hook latch: a rescue throw is in progress. The m_Hook we force PERSISTS in m_aInputData
	// between ticks (only real key events rewrite it), which has two consequences here: a physical
	// hook-key release shows up as m_Hook back at 0 under a hold we forced — that is the player taking
	// over, hand everything back at once; and "safe on their own" must be simulated with the hook
	// DROPPED (BlockHook), not with our own forced hold baked into the "raw" input.
	if(m_aRescueHookHold[Dummy] > 0)
	{
		if(m_aRescueHookHeld[Dummy] && HookKeyReleaseEdge)
		{
			m_aRescueHookHold[Dummy] = 0; // the player let go of the hook key: it is theirs again
			m_aRescueHookHeld[Dummy] = false;
		}
		else
		{
			const bool DropSafe = AvoidSimDeathTick(0, true, KEEP_DIR) < 0;
			const vec2 LatchPos = GameClient()->m_PredictedChar.m_Pos;
			const float LatchHalf = CCharacterCore::PhysicalSize() / 2.0f;
			const bool LatchGrounded = Collision()->CheckPoint(LatchPos.x + LatchHalf, LatchPos.y + LatchHalf + 5.0f) || Collision()->CheckPoint(LatchPos.x - LatchHalf, LatchPos.y + LatchHalf + 5.0f);
			// Hold mode: while actually hanging on the rescue hook, don't let go the moment a drop
			// would merely survive — that on/off cycle was the weak-hook balancing spam over a pit.
			// Hang on until there is real footing under us, the player taps hook to take over, or the
			// budget runs out. Without hold mode the first drop-safe moment hands back straight away.
			const bool Hanging = m_aRescueHookThrown[Dummy] && GameClient()->m_PredictedChar.m_HookState == HOOK_GRABBED;
			if(DropSafe && (!g_Config.m_TcRescueHookHoldMode || !Hanging || LatchGrounded))
			{
				m_aRescueHookHold[Dummy] = 0;
				m_aRescueHookHeld[Dummy] = false;
				Input.m_Hook = 0; // actively release: our forced hold would otherwise stick in m_aInputData
			}
			else
			{
				RescueHookUpkeep(Input);
				if(m_aRescueHookHold[Dummy] > 0)
				{
					ApplyFreezeWall(); // the rescue hook owns the hook key, the freeze-wall save still owns left/right
					s_aWasBlocking[Dummy] = true;
					return;
				}
			}
		}
	}

	// Resume-hook latch. m_aAvoidHookSuppressed remembers avoid interrupted YOUR held hook (the hook bit is
	// rebuilt from the key each tick and can't carry that intent forward).
	//  - You let go of the hook key (m_aInputHook == 0): the interruption is over, forget it.
	//  - tc_avoid_resume_hook ON: resume the instant hooking is GENUINELY safe again (sim with the hook
	//    survives the whole window). If hooking still dies, keep it forced off — NEVER resume into a death.
	//    This is the whole point: a re-throw is only allowed back once it truly won't drill into the void.
	//  - tc_avoid_resume_hook OFF: keep the hook forced off until you physically release the key, full stop.
	// Resume cone: an upward throw is the one that gets you out; a sideways or downward one usually goes
	// straight back into the pull avoid just broke. So the hook only comes back while you are actually
	// looking up, inside tc_avoid_resume_cone degrees measured around vertical. Screen space has +y down,
	// so straight up is (0,-1) and the angle from it is acos(-y) of the normalised aim.
	bool AimInResumeCone = true;
	if(g_Config.m_TcAvoidResumeCone < 360)
	{
		const vec2 Aim((float)Input.m_TargetX, (float)Input.m_TargetY);
		if(length(Aim) > 0.001f)
		{
			const float AngleFromUp = std::acos(std::clamp(-normalize(Aim).y, -1.0f, 1.0f));
			AimInResumeCone = AngleFromUp <= (float)g_Config.m_TcAvoidResumeCone * 0.5f * (pi / 180.0f);
		}
	}

	bool StrictSuppress = false;
	if(m_aAvoidHookSuppressed[Dummy])
	{
		if(m_aInputHook[Dummy] == 0)
			m_aAvoidHookSuppressed[Dummy] = false; // you let go of the hook key: hand it back
		else if(!g_Config.m_TcAvoidResumeHook)
			StrictSuppress = true; // OFF: stay suppressed until you physically release the key
		else if(!AimInResumeCone)
			StrictSuppress = true; // looking sideways or down: keep it off until you aim up again
		else if(g_Config.m_TcAvoidResumeHook >= 2)
		{
			// 2: hand it back only once re-hooking is genuinely survivable. This is the mode where a
			// release is worth anything: the rescue the sim promises assumes the hook stays off, so
			// throwing it straight back re-creates the very pull that was about to kill you.
			if(AvoidSimDeathTick(0, false, KEEP_DIR) < 0)
				m_aAvoidHookSuppressed[Dummy] = false;
			else
				StrictSuppress = true;
		}
		else
		{
			// ON: hand the hook straight back and let THIS tick's decision take it away again only if it
			// still has to. The latch used to demand that hooking be safe for the whole check window before
			// resuming, which is a far stricter bar than the one that took the hook away in the first place
			// (that one only fires on the last tick where releasing still rescues you). The gap between the
			// two bars was dead time: the hook sat disabled through ticks where throwing it was perfectly
			// fine, and long swings never came back at all. Handing it back unconditionally makes the two
			// bars the same one, so the hook is only ever off on the exact ticks it would kill you, and a
			// window of safety one tick long is enough to get it back.
			m_aAvoidHookSuppressed[Dummy] = false;
		}
	}
	if(StrictSuppress)
		Input.m_Hook = 0;

	// 1. Is the raw input safe over the whole window? Then never interfere.
	if(AvoidSimDeathTick(0, false, KEEP_DIR) < 0)
	{
		if(!StrictSuppress)
			m_aAvoidHookSuppressed[Dummy] = false; // hooking is safe: nothing is being held off
		if(g_Config.m_TcAntiVoidDebug >= 2 || (g_Config.m_TcAntiVoidDebug && s_aWasBlocking[Dummy]))
			AvoidDebugDump(StrictSuppress ? "clear: safe now, but your hook is still held off" : "clear: raw input survives the window", -1);
		ApplyFreezeWall();
		s_aWasBlocking[Dummy] = false;
		return;
	}

	const int RawDeath = AvoidSimDeathTick(0, false, KEEP_DIR); // tick the raw input dies (for the log)
	const int Counter = GameClient()->m_PredictedChar.m_Vel.x > 0.0f ? -1 : 1;

	// Touch the hook whenever it is GENUINELY HELD — the real key is physically down (m_aInputHook; a
	// released tap reads 0 here and is never touched) — and the simulation says holding it kills you.
	// This deliberately covers a FLYING or about-to-throw (IDLE) held hook too, not only a GRABBED one:
	// a fresh throw or an instant re-throw that flies up, grabs the ceiling and drags you into the void
	// has to be caught BEFORE it grabs — with kick=1, a GRABBED-only rule reacts a tick too late and you
	// are already in the freeze/void (exactly the "hook drills into the upper void / re-hooks instantly
	// and I die" reports). Genuine tap-swings are still safe: the last-tick logic only ever fires when
	// waiting would LOSE the rescue, and a harmless swing predicts no death, so nothing is blocked.
	const bool HookHeld = m_aInputHook[Dummy] != 0 && Input.m_Hook != 0;
	const int PredHookState = GameClient()->m_PredictedChar.m_HookState;
	const bool AllowHook = g_Config.m_TcAvoidHook && HookHeld &&
		(PredHookState == HOOK_GRABBED || PredHookState == HOOK_FLYING || PredHookState == HOOK_IDLE);
	// Grounded is still needed by the rescue hook below (a surprise throw while you stand on solid ground
	// would be pure interference). Steering itself no longer looks at the generic death logic at all: the
	// horizontal save is the freeze-wall one above, and nothing else may touch your movement keys.
	const vec2 P = GameClient()->m_PredictedChar.m_Pos;
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	const bool Grounded = Collision()->CheckPoint(P.x + HalfSize, P.y + HalfSize + 5.0f) || Collision()->CheckPoint(P.x - HalfSize, P.y + HalfSize + 5.0f);

	bool DoBlockHook = false;
	const char *pReason = "";

	// A hook is only EVER released when the sim (treating freeze as recoverable) says holding it drags you
	// into a REAL void — a kill/deep/edge, directly or via a frozen slide (tc_avoid_thread_freeze, pure
	// logic, no distance). If it merely pulls you through freeze you can hook out of, the hook is never
	// touched: you can hook a side wall right next to freeze and swing off it freely, and you never get left
	// waiting on a re-grab after a needless release. This gate covers EVERY release path below — the
	// direction combos, NSIF, and the standalone — not only the standalone one.
	const bool HookLeadsToRealVoid = AvoidSimDeathTick(0, false, KEEP_DIR, nullptr, true) >= 0;
	const bool AllowHookRelease = AllowHook && (!g_Config.m_TcAvoidThreadFreeze || HookLeadsToRealVoid);

	// HOOK RELEASE. The hook is the only input this part of avoid still touches: a hook that drags you into
	// a real void is released at the last affordable tick (Kick). AllowHookRelease already carries the
	// thread-freeze gate, so a hook that only pulls you through freeze is never ripped out here.
	if(AllowHookRelease &&
		AvoidSimDeathTick(0, true, KEEP_DIR) < 0 && AvoidSimDeathTick(Margin, true, KEEP_DIR) >= 0)
	{
		DoBlockHook = true;
		pReason = "release hook (last tick)";
	}

	if(!DoBlockHook)
	{
		// Last resort, the rescue hook: nothing milder saves us. If some throw fully saves us AND this
		// is the last affordable tick for it, engage the latch. Airborne only — on the ground you can
		// simply stop walking, a surprise hook there would be pure interference. And it must be a
		// GENUINE free-fall: if merely stopping or countering the held keys survives (falling from
		// high up onto a safe floor while holding a direction key is the classic case), the danger is
		// steerable — the direction logic or the player handles it, a hook there is pure noise.
		if(g_Config.m_TcRescueHook && !Grounded &&
			AvoidSimDeathTick(0, false, 0) >= 0 && AvoidSimDeathTick(0, false, Counter) >= 0)
		{
			vec2 RescueDir;
			bool CanWait;
			if(RescueHookAim(RescueDir, &CanWait) && !CanWait)
			{
				m_aRescueHookHold[Dummy] = 250; // hard cap ~5s; normally released the moment dropping is safe again
				m_aRescueHookThrown[Dummy] = false;
				m_aRescueHookHeld[Dummy] = false;
				RescueHookUpkeep(Input); // act this very tick: throw straight away, or release the old hook first
				if(g_Config.m_TcAntiVoidDebug)
					AvoidDebugDump("rescue hook engaged", RawDeath);
				ApplyFreezeWall();
				s_aWasBlocking[Dummy] = true;
				return;
			}
		}
		// Nothing to do: either we can still wait, or we're committed with no rescue. Keep player input.
		if(!StrictSuppress)
			m_aAvoidHookSuppressed[Dummy] = false; // we are not holding your hook off this tick
		if(g_Config.m_TcAntiVoidDebug >= 2 || (g_Config.m_TcAntiVoidDebug && s_aWasBlocking[Dummy]))
			AvoidDebugDump("wait: death is coming but the last moment has not arrived", RawDeath);
		ApplyFreezeWall();
		s_aWasBlocking[Dummy] = false;
		return;
	}

	// Report BEFORE the override is written into the input: the dump re-runs the simulation, so it has to
	// see the untouched player input — otherwise every what-if number would already contain our own change.
	if(g_Config.m_TcAntiVoidDebug >= 2 || (g_Config.m_TcAntiVoidDebug && !s_aWasBlocking[Dummy]))
		AvoidDebugDump(pReason, RawDeath, KEEP_DIR, DoBlockHook);

	if(DoBlockHook)
	{
		Input.m_Hook = 0;
		m_aAvoidHookSuppressed[Dummy] = true; // remember we are holding your hook off, for the resume latch
	}
	else if(!StrictSuppress)
		m_aAvoidHookSuppressed[Dummy] = false;
	ApplyFreezeWall();

	s_aWasBlocking[Dummy] = true;
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

// TClient hole assist: scan the tiles in a box around the tee for the nearest narrow gap. A "gap" is a run
// of open (non-solid) tiles in a single row that is bounded by a solid tile on BOTH ends and is at most
// 2 tiles wide — i.e. a slot in a wall you could fall/fly through, not the open corridor you are standing
// in. Returns the world-x of the center of the gap whose center is horizontally closest to us. Off-map
// tiles count as solid so a gap right at the map edge is still bounded correctly.
bool CControls::FindNearestHoleX(float &OutX) const
{
	// Use the predicted core position (tick-quantized), not the render-smoothed LocalCharPos, so the choice
	// of gap and all the steering below are frame-rate independent.
	const vec2 Pos = GameClient()->m_PredictedChar.m_Pos;
	const int Tx0 = (int)(Pos.x / 32.0f);
	const int Ty0 = (int)(Pos.y / 32.0f);
	const int Rx = 10; // search range sideways, in tiles
	const int Up = 8, Down = 8; // search range above/below, in tiles
	const int MaxW = 2; // widest opening that still counts as a "hole"
	const int W = Collision()->GetWidth();
	const int H = Collision()->GetHeight();

	auto Solid = [&](int tx, int ty) -> bool {
		if(tx < 0 || ty < 0 || tx >= W || ty >= H)
			return true; // off the map = wall boundary, so edge gaps are still bounded
		return Collision()->CheckPoint(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f);
	};

	bool Found = false;
	float BestDist = 1e30f;
	for(int ty = Ty0 - Up; ty <= Ty0 + Down; ++ty)
	{
		int col = Tx0 - Rx;
		while(col <= Tx0 + Rx)
		{
			if(Solid(col, ty)) // walls are not gaps; step past
			{
				++col;
				continue;
			}
			// Start of an open run at [Start .. End]. Extend it while still open and within the window.
			const int Start = col;
			int End = Start;
			while(End + 1 <= Tx0 + Rx && !Solid(End + 1, ty))
				++End;
			const int Len = End - Start + 1;
			// Must be pinched by a wall on both sides and no wider than the configured max to count as a hole.
			if(Len <= MaxW && Solid(Start - 1, ty) && Solid(End + 1, ty))
			{
				const float Cx = (float)(Start + End + 1) * 16.0f; // world-x of the run's center
				const float Dist = absolute(Cx - Pos.x);
				if(Dist < BestDist)
				{
					BestDist = Dist;
					OutX = Cx;
					Found = true;
				}
			}
			col = End + 1;
		}
	}
	return Found;
}

// TClient hole assist: steer left/right/stop so the tee comes to rest centered on the nearest gap.
// Counter-strafe controller: while we'd still overshoot the gap at the current speed, brake against our
// motion; otherwise drive toward it. Everything runs on the predicted core position/velocity (tick-quantized)
// so it behaves identically at any frame rate. A small dead zone plus a settle latch stop it from jittering
// by a pixel once parked, without the passive-coast prediction that misjudged airborne moves.
// Only ever touches m_Direction; jump/hook stay yours.
void CControls::ApplyHoleAssist()
{
	const int Dummy = g_Config.m_ClDummy;

	float TargetX;
	if(!FindNearestHoleX(TargetX))
	{
		m_aHoleSettled[Dummy] = false;
		return;
	}

	const CCharacterCore &Core = GameClient()->m_PredictedChar;
	const float Px = Core.m_Pos.x;
	const float Vx = Core.m_Vel.x;
	const float Err = TargetX - Px; // + = gap is to our right
	const float Lock = 3.0f; // dead zone half-width around the gap center, in pixels
	const float Reengage = 10.0f; // must drift this far off-center before we start correcting again
	const float VelEps = 0.4f;
	// Braking distance grows LINEARLY with speed (ground friction decays velocity geometrically, so the real
	// stop distance is ~proportional to speed, not speed^2). tc_hole_assist_brake (tenths) is the proportion:
	// 10 => brake when the gap is closer than 1.0x our current speed. Lower brakes later / carries more speed.
	const float StopDist = ((float)g_Config.m_TcHoleAssistBrake / 10.0f) * absolute(Vx);

	int &Dir = m_aInputData[Dummy].m_Direction;

	// Parked on the gap: hold still until we drift well off center, so we don't micro-correct every tick.
	if(m_aHoleSettled[Dummy])
	{
		if(absolute(Err) > Reengage)
			m_aHoleSettled[Dummy] = false;
		else
		{
			Dir = 0;
			return;
		}
	}

	// Centered and nearly stopped -> lock in and stop steering.
	if(absolute(Err) <= Lock && absolute(Vx) <= VelEps)
	{
		Dir = 0;
		m_aHoleSettled[Dummy] = true;
	}
	else if(Err > 0.0f)
	{
		// Gap to the right: brake if we're heading right fast enough to overshoot it, else drive right.
		Dir = (Vx > VelEps && StopDist >= Err) ? -1 : 1;
	}
	else
	{
		// Gap to the left: brake if we're heading left fast enough to overshoot it, else drive left.
		Dir = (Vx < -VelEps && StopDist >= -Err) ? 1 : -1;
	}
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
}

void CControls::CAvoidOverlay::OnRender()
{
	GameClient()->m_Controls.RenderAvoidOverlay();
}

// TClient: anti-void visual overlay (also shown while paused/spectating, where the feature still runs).
// Called from CControls::CAvoidOverlay, which is registered after the map layers so this actually
// ends up on top of the tilemap instead of under it.
void CControls::RenderAvoidOverlay()
{
	if(!g_Config.m_TcAntiVoid || !g_Config.m_TcAntiVoidShow || !HaveLocalChar())
		return;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;
	float aSavedScreen[4];
	Graphics()->GetScreen(&aSavedScreen[0], &aSavedScreen[1], &aSavedScreen[2], &aSavedScreen[3]);
	Graphics()->MapScreenToInterface(Center.x, Center.y, Zoom);

	const vec2 CharPos = LocalCharPos();

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
			if(AvoidDangerClass(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f) == 0)
				continue;
			IGraphics::CQuadItem Quad(tx * 32.0f, ty * 32.0f, 32.0f, 32.0f);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
	}

	// Predicted path for the current input: one dot per simulated tick, red once the sim dies
	{
		CCharacterCore Core = GameClient()->m_PredictedChar;
		Core.Init(nullptr, Collision());
		Core.SetHookedPlayer(-1);
		// Use the pre-avoid input so the path shows what your OWN input would do (not the hook
		// avoid may have already zeroed this frame), which is what you want to eyeball.
		Core.m_Input = m_aAvoidRawInput[g_Config.m_ClDummy];
		bool Dead = false;
		for(int i = 0; i < g_Config.m_TcAvoidCheckTicks; ++i)
		{
			Core.Tick(true);
			Core.Move();
			Core.Quantize();
			if(!Dead && AvoidDangerClass(Core.m_Pos.x, Core.m_Pos.y) != 0)
				Dead = true;
			Graphics()->SetColor(Dead ? 1.0f : 0.2f, Dead ? 0.2f : 1.0f, 0.2f, 0.95f);
			IGraphics::CQuadItem Quad(Core.m_Pos.x - 2.0f, Core.m_Pos.y - 2.0f, 4.0f, 4.0f);
			Graphics()->QuadsDrawTL(&Quad, 1);
		}
	}

	Graphics()->QuadsEnd();
	Graphics()->MapScreen(aSavedScreen[0], aSavedScreen[1], aSavedScreen[2], aSavedScreen[3]);
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
			// TClient: mirror the SnapInput rebuild so fast-input detects a press/release of the hook key
			// straight away, even though the key now feeds the shadow m_aInputHook instead of m_aInputData.
			TestInput.m_Hook = m_aInputHook[Dummy];
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
