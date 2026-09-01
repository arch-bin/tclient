/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H

#include <base/vmath.h>

#include <engine/client.h>
#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/freeze_wall.h>

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
	// TClient: the raw held state of the hook key. The +hook bind writes HERE, not straight into
	// m_aInputData.m_Hook, and every tick m_aInputData.m_Hook is rebuilt from it (exactly like the
	// direction is rebuilt from the left/right keys). This is what lets avoid force the hook off for as
	// long as it is dangerous without the forced 0 sticking forever: the next tick starts from the real
	// held key again, so the instant hooking is safe the player's still-held hook simply resumes.
	int m_aInputHook[NUM_DUMMIES];
	int m_aShowHookColl[NUM_DUMMIES];

	// TClient
	CNetObj_PlayerInput m_aFastInput[NUM_DUMMIES];
	bool m_FastInputHookAction = false;
	bool m_FastInputFireAction = false;

	// TClient avoid (KRX-style)
	bool AntiVoidBadTile(int TileIndex) const;
	bool AntiVoidDangerAt(float x, float y) const;
	int AvoidDangerClassPoint(float x, float y, bool ForceFreezeRecoverable = false) const; // classify one tile point: 0 = safe, 1 = recoverable freeze, 2 = lethal. ForceFreezeRecoverable: freeze counts as class 1 even with tc_avoid_unfreeze off
	bool AvoidHardDeathPoint(float x, float y) const; // is this point a kill tile or off-map (the hitbox-corner death test, freeze excluded)
	int AvoidDangerClass(float x, float y, bool ForceFreezeRecoverable = false) const; // same but for the whole tee body at (x,y): centre + 4 hitbox corners, worst wins
	int AvoidSimDeathTick(int DelayTicks, bool BlockHook, int ForceDir, const vec2 *pThrowHook = nullptr, bool FreezeRecoverable = false, vec2 *pOutDeathPos = nullptr, bool *pOutViaFreeze = nullptr, bool *pOutHorizon = nullptr) const; // sim: raw input for DelayTicks, then the override (ForceDir 2 = keep; pThrowHook: release whatever hook is active and throw toward that direction, then hold); returns tick of first unavoidable death, -1 = safe. FreezeRecoverable: treat freeze as class 1 (slide) even in strict mode, so a death is only reported when the trajectory (or the frozen slide) reaches a REAL void — used to ask "would keeping this hook pull me into an actual void, or just into freeze I can hook out of?"
	CFreezeWallCfg AvoidFreezeWallCfg() const; // the freeze-wall save's config, built from the avoid cvars // freeze-wall sim: 0 = clear, 1 = the centre would cross the SIDE face of a freeze tile (either side; *pOutPos where, *pOutTick in how many ticks), 2 = ends in a real death or in freeze taken from above/below. HorizMargin = px of lookahead on the side we are moving toward, counted only when that shifted point crosses the tile column itself
	int AvoidFreezeWallSteer(); // the whole horizontal save, in one call: returns the direction key to force this tick (2 = keep the player's), engaging at the latest moment that still clears the wall and latching until it is clear with room to spare
	float AvoidFreezeSideDist(float Side, float MaxDist, float Band) const; // distance to real freeze straight to one side, scanned at the centre and +-Band, up to MaxDist; -1 = none. Used both for the debug line and for the last-resort clamp
	bool RescueHookAim(vec2 &OutDir, bool *pOutCanWait = nullptr) const; // pick the surviving hook throw closest to the cursor (segments over the FOV); CanWait = the chosen throw still works after the margin
	void RescueHookUpkeep(CNetObj_PlayerInput &Input); // while the latch is held: release the old hook, throw ours, keep it pressed, re-throw on a miss
	int m_aRescueHookHold[NUM_DUMMIES] = {0, 0}; // rescue-hook latch: ticks left to keep forcing the hook (0 = off); released early as soon as dropping the hook is survivable (see hold mode)
	bool m_aRescueHookThrown[NUM_DUMMIES] = {false, false}; // latch phase: false = still releasing whatever hook was active before our throw
	bool m_aRescueHookHeld[NUM_DUMMIES] = {false, false}; // we left m_Hook forced to 1 last tick; m_Hook persists in m_aInputData between ticks, so seeing it back at 0 means a REAL key release — the player is taking over
	bool m_aAvoidHookSuppressed[NUM_DUMMIES] = {false, false}; // avoid is currently holding YOUR hook off because throwing/keeping it dies. With tc_avoid_resume_hook it clears the instant hooking is safe again (your still-held key resumes); without it, it stays until you physically release the hook key
	int m_aAvoidHookKeyPrev[NUM_DUMMIES] = {0, 0}; // m_aInputHook as of last tick, so we can spot the exact tick you RELEASE the hook key (a falling edge) — used by the rescue latch to hand control back the moment you take over
	bool m_aAvoidFreezeWallLatch[NUM_DUMMIES] = {false, false}; // the freeze-wall brake is engaged; it stays engaged (with a bigger clearance requirement) until the wall is really behind us, so a held key can't push you back into the corner the moment the sim goes quiet
	bool m_aAvoidFreezeWallHit[NUM_DUMMIES] = {false, false}; // this tick's raw input runs into a freeze wall (for the debug log)
	CFreezeWallState m_aFreezeWallState[NUM_DUMMIES]; // the save's own state (brake engaged, grace window), carried between ticks
	int m_aAvoidFreezeWallLastDir[NUM_DUMMIES] = {2, 2}; // direction the freeze-wall save forced last tick (2 = none), so the log only fires on a change
	vec2 m_aAvoidFreezeWallPos[NUM_DUMMIES] = {vec2(0.0f, 0.0f), vec2(0.0f, 0.0f)}; // where the wall was hit in the sim, for the debug log
	void AvoidTileDesc(float x, float y, char *pBuf, int Size) const; // human-readable tile under a world point, for the debug log
	void AvoidDebugDump(const char *pVerdict, int RawDeath, int DoDir = 2, bool DoBlockHook = false) const; // the verbose tc_anti_void_debug report for one decision (DoDir 2 = keep)
	// Outcome tracking for the debug log. The report above only ever fires on a DECISION, so a log full of
	// perfect-looking saves said nothing about the fall that followed. These remember the last decision so a
	// death or a freeze can be reported against it: "you froze 7 ticks after avoid released your hook".
	char m_aAvoidLastVerdict[64] = "none";
	int m_AvoidLastVerdictTick = -1;
	bool m_aAvoidWasFrozen[NUM_DUMMIES] = {false, false};
	void AvoidReportOutcome(const char *pWhat) const; // one line: what happened, where, and how long after the last decision
	void ApplyAntiVoid(); // modifies m_aInputData[g_Config.m_ClDummy]
	void ApplyAntiVoidRocket(bool Suppressed = false); // rocket (grenade) counter; runs independently of the braking anti-void. Suppressed: do upkeep (release fire, tick cooldown) but don't arm/fire
	static constexpr int MAX_LASER_BOUNCES = 12;
	void ApplyAntiVoidLaser(bool Suppressed = false); // laser self-ricochet counter; runs independently of the braking anti-void
	int TraceLaserPath(vec2 From, vec2 AimDir, int MaxBounces, vec2 *pSegStart, vec2 *pSegEnd) const;
	bool FindLaserSelfBounce(const vec2 *pTargetPos, const bool *pValid, int MaxBounces, int BounceDelayTicks, vec2 &OutAimDir, float &OutDistToWall, vec2 &OutBouncePos, vec2 &OutReflDir, float &OutTeeHitOffset, int &OutBounces, int &OutArrivalTicks) const;
	bool TeeFullyClearOfFreeze(vec2 Pos) const; // true if the full 28px tee body has zero intersection with freeze/death tiles
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

	// TClient hole assist: while the +tc_hole_assist bind is active (held or toggled, see
	// tc_hole_assist_hold), find the nearest narrow gap in the surrounding walls and steer so we come to
	// rest centered on it. FindNearestHoleX returns the world-x of the best gap center (nearest to us) or
	// false if none in range. ApplyHoleAssist sets m_Direction accordingly.
	bool FindNearestHoleX(float &OutX) const;
	void ApplyHoleAssist();
	bool HoleAssistActive() const; // resolves hold-vs-toggle mode into "is it engaged right now?"
	bool m_HoleAssistPressed = false; // the bound key is physically held right now
	bool m_HoleAssistToggled = false; // toggle-mode state, flipped on each key press
	bool m_aHoleSettled[NUM_DUMMIES] = {false, false}; // hysteresis latch: we are parked on the gap, hold still
	CNetObj_PlayerInput m_aAvoidPrevInput[NUM_DUMMIES] = {}; // last seen input, for the AFK protection
	float m_aAvoidLastActivity[NUM_DUMMIES] = {0.0f, 0.0f}; // local time of the last input change
	CNetObj_PlayerInput m_aAvoidRawInput[NUM_DUMMIES] = {}; // the player's input as it was before avoid touched it, for the overlay
	int m_aAntiVoidRocketCooldown[NUM_DUMMIES] = {0, 0}; // ticks left before the rocket counter may fire again
	bool m_aAntiVoidRocketReleasePending[NUM_DUMMIES] = {false, false}; // we pressed fire last tick and must release it
	int m_aAntiVoidRocketFireValue[NUM_DUMMIES] = {0, 0}; // the m_Fire value we set, so we only release our own press
	int m_aAntiVoidRocketPrevWeapon[NUM_DUMMIES] = {-1, -1}; // weapon to switch back to once the rocket save is done (-1 = none)
	int m_aAntiVoidLaserCooldown[NUM_DUMMIES] = {0, 0}; // ticks left before the laser counter may fire again
	bool m_aAntiVoidLaserReleasePending[NUM_DUMMIES] = {false, false}; // we pressed fire last tick and must release it
	int m_aAntiVoidLaserFireValue[NUM_DUMMIES] = {0, 0}; // the m_Fire value we set, so we only release our own press
	int m_aAntiVoidLaserPrevWeapon[NUM_DUMMIES] = {-1, -1}; // weapon to switch back to once the laser save is done (-1 = none)

	struct CRescueLaserTracker
	{
		bool m_Active = false;
		int m_FireTick = -1;
		int m_ArrivalTick = -1;
		vec2 m_FirePos = vec2(0.0f, 0.0f);
		vec2 m_TargetPos = vec2(0.0f, 0.0f);
		vec2 m_WallPos = vec2(0.0f, 0.0f);
		vec2 m_ReflDir = vec2(0.0f, 0.0f);
		float m_DistToWall = 0.0f;
		float m_TeeHitOffset = 0.0f;
		int m_Bounces = 0;
	};
	CRescueLaserTracker m_aLaserTracker[NUM_DUMMIES];

	// TClient weapon spinner. Single source of truth for the spin angle so the local visual
	// (players.cpp) and the optional "real" sent aim (SnapInput) always agree.
	// RealAngle = the player's actual aim; some modes (pendulum/jitter) orbit around it.
	static constexpr int NUM_WEAPON_SPIN_MODES = 8;
	static float WeaponSpinAngle(float RealAngle, float Time);

	// TClient avoid overlay. CControls is an INPUT component: it sits near the top of the
	// component list and therefore renders long before the map layers, so drawing the overlay
	// from CControls::OnRender painted it underneath the tilemap, i.e. it never showed up. The
	// drawing lives in this small render proxy instead, which is registered after the map
	// layers (same pattern as CParticles::m_RenderTrail) and just calls back into CControls.
	void RenderAvoidOverlay();
	class CAvoidOverlay : public CComponent
	{
	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
	};
	CAvoidOverlay m_AvoidOverlay;

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
	static void ConKeyHoleAssist(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputSet(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyInputNextPrevWeapon(IConsole::IResult *pResult, void *pUserData);
};
#endif
