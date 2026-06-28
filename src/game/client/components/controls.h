/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>

class CControls : public CComponent
{
public:
	float GetMinMouseDistance() const;
	float GetMaxMouseDistance() const;

	enum class EMouseInputType
	{
		ABSOLUTE,
		RELATIVE,
		AUTOMATED,
	};

	vec2 m_aMousePos[NUM_DUMMIES];
	vec2 m_aMousePosOnAction[NUM_DUMMIES];
	vec2 m_aTargetPos[NUM_DUMMIES];

	EMouseInputType m_aMouseInputType[NUM_DUMMIES];

	int m_aAmmoCount[NUM_WEAPONS];

	int64_t m_LastSendTime;
	CNetObj_PlayerInput m_aInputData[NUM_DUMMIES];
	CNetObj_PlayerInput m_aLastData[NUM_DUMMIES];
	int m_aInputDirectionLeft[NUM_DUMMIES];
	int m_aInputDirectionRight[NUM_DUMMIES];
	int m_aShowHookColl[NUM_DUMMIES];

	// TClient
	CNetObj_PlayerInput m_aFastInput[NUM_DUMMIES];
	bool m_FastInputHookAction = false;
	bool m_FastInputFireAction = false;

	// TClient anti-void
	bool AntiVoidBadTile(int TileIndex) const;
	bool AntiVoidDangerAt(float x, float y) const;
	int AntiVoidFreeSpan(bool Horizontal) const; // free (non-solid) corridor size around the player, in tiles
	bool AntiVoidInNarrowSpot() const; // true if the corridor is at/below the configured narrow thresholds
	void ApplyAntiVoid(); // modifies m_aInputData[g_Config.m_ClDummy]
	void ApplyAntiVoidRocket(bool Suppressed = false); // rocket (grenade) counter; runs independently of the braking anti-void. Suppressed: do upkeep (release fire, tick cooldown) but don't arm/fire
	// True if our own tee is still on the map (normal play, or paused/spectating). The client leaves
	// m_pLocalCharacter null in spec, so we also accept an active local character item in the snapshot.
	bool HaveLocalChar() const;
	// Position of our own tee for the safety features. Normally the smoothed render position, but while
	// paused/spectating (press Q to watch others) the client leaves that stale, so we fall back to the
	// predicted core position, which stays valid as long as our tee is still on the map.
	vec2 LocalCharPos() const;
	// Runs the automatic safety features (anti-void, balancer, rocket counter) on the local tee if there is
	// one. Shared by the normal-play path and the frozen-input (chat/menu) path so they work in both, and in
	// spec. With no local tee at all it does nothing.
	void ApplyAutoSafety();

	// TClient balancer: while standing on a tee that is over the void, only steer us back when we start to
	// slide off its rounded head, so we don't fall in. Hands-off otherwise.
	bool BalancerTeeInVoid(vec2 TeePos) const; // true if the tee has no safe solid ground below it
	bool ApplyBalancer(); // modifies m_aInputData[g_Config.m_ClDummy].m_Direction; returns true if engaged on a tee
	int m_aAntiVoidHold[NUM_DUMMIES] = {0, 0}; // remaining hysteresis ticks
	int m_aAntiVoidHoldDir[NUM_DUMMIES] = {0, 0}; // braking direction to hold during hysteresis
	int m_aAntiVoidRocketCooldown[NUM_DUMMIES] = {0, 0}; // ticks left before the rocket counter may fire again
	bool m_aAntiVoidRocketReleasePending[NUM_DUMMIES] = {false, false}; // we pressed fire last tick and must release it
	int m_aAntiVoidRocketFireValue[NUM_DUMMIES] = {0, 0}; // the m_Fire value we set, so we only release our own press
	int m_aAntiVoidRocketPrevWeapon[NUM_DUMMIES] = {-1, -1}; // weapon to switch back to once the rocket save is done (-1 = none)

	// TClient weapon spinner. Single source of truth for the spin angle so the local visual
	// (players.cpp) and the optional "real" sent aim (SnapInput) always agree.
	// RealAngle = the player's actual aim; some modes (pendulum/jitter) orbit around it.
	static constexpr int NUM_WEAPON_SPIN_MODES = 8;
	static float WeaponSpinAngle(float RealAngle, float Time);

	CControls();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnConsoleInit() override;
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	void ResetInput(int Dummy);
	bool CheckNewInput();

private:
	static void ConKeyInputState(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputCounter(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);
};
#endif
