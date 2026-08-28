#include "freeze_wall.h"

#include <base/math.h>
#include <base/system.h>

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

CFreezeWallTuning CFreezeWall::ms_Tuning;

bool CFreezeWall::FreezeAt(CCollision *pCollision, float x, float y, const CFreezeWallCfg &Cfg)
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= pCollision->GetWidth() || Ty >= pCollision->GetHeight())
		return false; // off-map is a hard death, handled by HardDeath, not as freeze
	const int Index = pCollision->GetPureMapIndex(x, y);
	for(const int T : {pCollision->GetTileIndex(Index), pCollision->GetFrontTileIndex(Index)})
		if((Cfg.m_Freeze && T == TILE_FREEZE) || (Cfg.m_DeepFreeze && T == TILE_DFREEZE) || (Cfg.m_LiveFreeze && T == TILE_LFREEZE))
			return true;
	return false;
}

// A real, no-way-back death anywhere under the tee's body: a kill tile, deep freeze, a teleporter, or the
// map edge. Freeze is deliberately excluded — it has its own rules above.
static bool HardDeath(CCollision *pCollision, float x, float y, const CFreezeWallCfg &Cfg)
{
	const float R = CCharacterCore::PhysicalSize() / 2.0f;
	for(const float Ox : {-R, 0.0f, R})
		for(const float Oy : {-R, 0.0f, R})
		{
			const float Px = x + Ox;
			const float Py = y + Oy;
			const int Tx = (int)(Px / 32.0f);
			const int Ty = (int)(Py / 32.0f);
			if(Tx < 0 || Ty < 0 || Tx >= pCollision->GetWidth() || Ty >= pCollision->GetHeight())
				return true;
			const int Index = pCollision->GetPureMapIndex(Px, Py);
			if(Cfg.m_Tele && (pCollision->IsTeleport(Index) || pCollision->IsEvilTeleport(Index)))
				return true;
			for(const int T : {pCollision->GetTileIndex(Index), pCollision->GetFrontTileIndex(Index)})
				if((Cfg.m_Death && T == TILE_DEATH) || (Cfg.m_DeepFreeze && T == TILE_DFREEZE))
					return true;
		}
	return false;
}

int CFreezeWall::Hit(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input, const CFreezeWallCfg &Cfg,
	int DelayTicks, int ForceDir, float HorizMargin, vec2 *pOutPos, int *pOutTick)
{
	CCharacterCore Sim = Core;
	Sim.Init(nullptr, pCollision);
	Sim.SetHookedPlayer(-1);
	if(pOutTick)
		*pOutTick = -1;

	const vec2 Start = Sim.m_Pos;
	vec2 Prev = Sim.m_Pos;
	for(int i = 0; i < ms_Tuning.m_Window; ++i)
	{
		Sim.m_Input = Input;
		if(i >= DelayTicks && ForceDir != 2)
			Sim.m_Input.m_Direction = ForceDir;
		const vec2 PrevPos = Sim.m_Pos;
		Sim.Tick(true);
		Sim.Move();
		Sim.Quantize(); // the real pipeline quantizes every tick; skipping it drifts the sim
		const int Steps = maximum(1, (int)(distance(PrevPos, Sim.m_Pos) / 4.0f)); // 4px steps: a block corner between two ticks is never stepped over
		for(int s = 1; s <= Steps; ++s)
		{
			const vec2 P = mix(PrevPos, Sim.m_Pos, (float)s / (float)Steps);
			if(HardDeath(pCollision, P.x, P.y, Cfg))
			{
				if(pOutPos)
					*pOutPos = P;
				if(pOutTick)
					*pOutTick = i;
				return 2;
			}
			if(FreezeAt(pCollision, P.x, P.y, Cfg))
			{
				// Brushing the RIM of a freeze mass does not count: there has to be freeze both above and
				// below the contact. Skimming under a ceiling rides one or two pixels below its edge every
				// single tick, which used to read as a wall and clamp the keys for the whole flight.
				if(!FreezeAt(pCollision, P.x, P.y - ms_Tuning.m_GrazePx, Cfg) || !FreezeAt(pCollision, P.x, P.y + ms_Tuning.m_GrazePx, Cfg))
				{
					Prev = P;
					continue;
				}
				if(pOutPos)
					*pOutPos = P;
				if(pOutTick)
					*pOutTick = i;
				// WALL or ceiling/floor? Measured against where the tee IS (not against the previous 4px
				// sub-step, which is always about zero and silently passed everything): the block must be
				// in a different tile column and within about a tile and a half vertically. Freeze ABOVE
				// only counts while the tee is really rising into it — hanging under a ceiling corner never
				// reaches it, climbing a wall at 8px/tick does.
				const float AboveBy = Start.y - P.y;
				// Freeze ABOVE the tee is the one thing steering must never fight: it is a ceiling you fly
				// under, and the tee only ever reaches it by rising into it. Everything above the centre that
				// the tee is not climbing into is reported as 2 and shuts the sim side up entirely.
				if(AboveBy > 0.0f && Core.m_Vel.y > -ms_Tuning.m_RiseVel)
					return 2;
				if(AboveBy > ms_Tuning.m_AbovePx)
					return 2;
				// A contact the tee drives into sideways is a wall (1). One in its own column, or far below,
				// is not a wall — but it is still freeze the tee is heading into, and steering may well fix
				// it, so it is reported separately (3) rather than lumped in with ceilings.
				if((int)(P.x / 32.0f) == (int)(Start.x / 32.0f) || -AboveBy > ms_Tuning.m_BelowPx)
					return 3;
				return 1;
			}
			// One pixel of lookahead on the side we are moving towards, so the tee ends up against the block
			// rather than a pixel inside it. Counted only when that shifted point crosses into the next tile
			// column itself, otherwise sliding along a wall would read as a hit forever.
			if(HorizMargin > 0.0f)
			{
				const float Dx = P.x - Prev.x;
				if(absolute(Dx) > 0.001f)
				{
					const float Side = Dx > 0.0f ? HorizMargin : -HorizMargin;
					if((int)((P.x + Side) / 32.0f) != (int)((Prev.x + Side) / 32.0f) &&
						(int)((P.x + Side) / 32.0f) != (int)(Start.x / 32.0f) && FreezeAt(pCollision, P.x + Side, P.y, Cfg))
					{
						if(pOutPos)
							*pOutPos = vec2(P.x + Side, P.y);
						if(pOutTick)
							*pOutTick = i;
						return 1;
					}
				}
			}
			Prev = P;
		}
	}
	return 0;
}

float CFreezeWall::SideDist(CCollision *pCollision, vec2 Pos, float Side, float MaxDist, float Band, const CFreezeWallCfg &Cfg)
{
	// Scanned at the centre and BELOW it, never above: freezing is decided by the tee's centre, so freeze
	// overhead is something you fly under, not something in your way. The band used to reach upwards too,
	// and a tee skimming one pixel under a freeze ceiling had that ceiling reported as a wall straight
	// ahead — the counter-steer under the roof that would not stop.
	for(float d = 4.0f; d <= MaxDist; d += 4.0f)
		for(const float Oy : {0.0f, Band})
			if(FreezeAt(pCollision, Pos.x + Side * d, Pos.y + Oy, Cfg))
				return d;
	return -1.0f;
}

CFreezeWallDecision CFreezeWall::Decide(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input,
	const CFreezeWallCfg &Cfg, CFreezeWallState &State)
{
	const int KEEP_DIR = 2;
	CFreezeWallDecision Out;
	const bool WasLatched = State.m_Latch;
	if(Cfg.m_Mode <= 0)
	{
		State.m_Latch = false;
		Out.m_pWhy = "off";
		return Out;
	}

	// Airborne only: on the ground you stop instantly yourself, so your walk is never touched.
	const vec2 P = Core.m_Pos;
	const float HalfSize = CCharacterCore::PhysicalSize() / 2.0f;
	Out.m_Grounded = pCollision->CheckPoint(P.x + HalfSize, P.y + HalfSize + 5.0f) || pCollision->CheckPoint(P.x - HalfSize, P.y + HalfSize + 5.0f);
	if(Out.m_Grounded)
	{
		State.m_Latch = false;
		Out.m_pWhy = "grounded, keys are yours";
		return Out;
	}

	Out.m_Raw = Hit(pCollision, Core, Input, Cfg, 0, KEEP_DIR, ms_Tuning.m_Margin, &Out.m_Pos, &Out.m_RawTick);
	// A wall ARMS the save; that filter is what keeps it off your keys under a freeze ceiling. It must not
	// DISARM it though: once the brake is on, freeze entered through the top or bottom face keeps it alive
	// for a short grace window, because the log had a tee braked against one block and then carried into the
	// next one by its own key while that second contact read as "from above".
	if(Out.m_Raw == 1 || Out.m_Raw == 3)
		State.m_Grace = ms_Tuning.m_GraceTicks;
	else if(State.m_Grace > 0)
		State.m_Grace--;
	// ARMED means the simulation has a case for steering: a wall now, or a brake already engaged that is
	// still inside its grace window. Anything else — no contact at all, or freeze taken through the top or
	// bottom face (a ceiling you fly under, a floor you drop onto) — leaves the sim side silent. Only the
	// straight-ahead clamp below may still act, and only right up against the block.
	const bool Armed = Out.m_Raw == 1 || Out.m_Raw == 3 || (Out.m_Raw == 2 && WasLatched && State.m_Grace > 0);
	if(!Armed)
	{
		State.m_Latch = false;
		Out.m_pWhy = Out.m_Raw == 0 ? "your own input is clear of freeze for the whole window" : "ceiling above you, not a wall: keys are yours";
	}

	const int Counter = Core.m_Vel.x > 0.0f ? -1 : 1;
	if(Armed)
	{
		// How long does each key keep us out of freeze? A candidate that never touches freeze inside the
		// window scores the full window, otherwise it scores the tick it goes in. "Must clear it completely"
		// was the wrong bar: in a tight freeze corridor nothing is ever clear, so the save stood down exactly
		// where it was needed (raw contact in 3 ticks, dropping the key pushed it to 9, countering to 11).
		auto Score = [&](int Delay, int SimDir) -> int {
			int Tick = -1;
			const int Res = Hit(pCollision, Core, Input, Cfg, Delay, SimDir, ms_Tuning.m_Margin, nullptr, &Tick);
			return Res == 0 ? ms_Tuning.m_Window : Tick;
		};
		Out.m_StopScore = Score(0, 0);
		Out.m_CounterScore = Cfg.m_Mode >= 2 ? Score(0, Counter) : -1;
		// Mildest thing that does the job: if blocking your key alone keeps you clear for the whole window,
		// that is the answer and the opposite key is never pressed. Otherwise take whichever lasts longest.
		const bool UseCounter = Out.m_StopScore < ms_Tuning.m_Window && Out.m_CounterScore > Out.m_StopScore;
		const int BestDir = UseCounter ? Counter : 0;
		Out.m_BestScore = UseCounter ? Out.m_CounterScore : Out.m_StopScore;
		// How far ahead "could I still brake later?" is tested. Fixed at one tick it was fine at walking
		// speed and useless at full speed, where the state jumped straight from "the counter saves me
		// completely" to "nothing does".
		Out.m_LateTicks = std::clamp((int)(length(Core.m_Vel) / ms_Tuning.m_LatePerVel), ms_Tuning.m_LateTicks, ms_Tuning.m_LateMax) +
				  (WasLatched ? ms_Tuning.m_LateHoldTicks - ms_Tuning.m_LateTicks : 0);
		Out.m_LateScore = Score(Out.m_LateTicks, BestDir);

		if(Out.m_RawTick > ms_Tuning.m_ActTicks)
			Out.m_pWhy = "the wall is not imminent, you are still flying your own line";
		else if(Out.m_BestScore < Out.m_RawTick + (WasLatched ? ms_Tuning.m_HoldGain : ms_Tuning.m_GainTicks))
			Out.m_pWhy = "no key buys real time here, braking would only take your movement away";
		else if(Out.m_LateScore >= ms_Tuning.m_Window)
			Out.m_pWhy = WasLatched ? "releasing: braking later still gets you out completely" : "waiting: braking later still gets you out completely";
		else
		{
			Out.m_Dir = BestDir;
			Out.m_pWhy = BestDir == 0 ? "block your key" : "counter-steer: blocking the key alone does not stop you in time";
		}

		// Right up against it: never hand the key back in the last couple of ticks while the contact is
		// still there. Blocking costs nothing — it is your own key and the contact is coming either way.
		if(Out.m_Dir == KEEP_DIR && Out.m_RawTick <= ms_Tuning.m_GrazeTicks && Out.m_StopScore > Out.m_RawTick)
		{
			Out.m_Dir = 0;
			Out.m_pWhy = "block your key: the block is right there, not handing your key back now";
		}
	}

	// LAST-RESORT CLAMP, no simulation at all: real freeze on the side you are pressing towards, at body
	// height, within roughly your braking distance. The sim's vertical prediction is the weak link — it once
	// had the tee rising over a block it then flew straight into, reporting nothing until 14px before the
	// hit — so this scan is what actually guarantees the key is not driving you into freeze.
	if(Out.m_Dir == KEEP_DIR || Out.m_Dir == 0)
	{
		const int Key = Input.m_Direction;
		// Only when that key is actually taking you TOWARDS the freeze. Holding left while flying right is
		// braking — the log had the clamp fighting exactly that, blocking the brake because there happened
		// to be freeze 17px to the left, on the side the tee was moving away from.
		// ...and only while you are genuinely CLOSING on it. "There is freeze beside me" is the normal state
		// of affairs in a gores tunnel — measuring that alone made the clamp fire in two thirds of ordinary
		// situations. What matters is whether the current sideways speed actually carries you in.
		const float Closing = Key != 0 ? (float)Key * Core.m_Vel.x : 0.0f;
		const bool KeyGoesIn = Closing > ms_Tuning.m_ClosingVel;
		// The clamp is a BACKSTOP, not the main behaviour. Given free rein it fired on everything: in a
		// gores tunnel there is freeze within a braking distance of you almost always, and the log came back
		// with 1527 clamp overrides against 150 from the simulation — that is the "it steers in random
		// places" complaint. So its full reach only applies when the simulation also sees a contact; on its
		// own it may only act when the freeze is right up against you, which is the case the sim keeps
		// losing (a wall 9px away that the predicted path drifts around).
		// Reach is how far that closing speed carries you in the next few ticks, so it is a time-to-contact
		// test rather than a distance test: fast means look far, crawling means look barely at all, standing
		// still means the clamp says nothing.
		float Reach = std::clamp(Closing * ms_Tuning.m_ReachPerVel, 0.0f, ms_Tuning.m_ReachMax);
		// Full reach only when the simulation agrees this is a WALL. When it says "ceiling or floor" (or sees
		// nothing at all), the clamp may still act, but only when the freeze is right up against you — that
		// is the case the sim genuinely loses. Giving it full reach on ceiling contacts is what kept the keys
		// busy while flying along under a freeze roof.
		if(Out.m_Raw == 2 || Out.m_Raw == 0)
			Reach = minimum(Reach, ms_Tuning.m_ClampPx);
		Out.m_ClampDist = KeyGoesIn ? SideDist(pCollision, P, (float)Key, Reach, ms_Tuning.m_ClampBand, Cfg) : -1.0f;
		if(Out.m_ClampDist >= 0.0f)
		{
			// Close enough that letting go no longer stops the drift: push the other way — but only while
			// still travelling towards the block, otherwise the counter walks you back out of the spot you
			// were trying to hold.
			const float Into = (float)Key * Core.m_Vel.x;
			const bool NeedCounter = Cfg.m_Mode >= 2 && Into > 0.5f &&
						 Out.m_ClampDist < maximum(absolute(Core.m_Vel.x) * ms_Tuning.m_CounterReach, ms_Tuning.m_ClampPx);
			if(NeedCounter || Out.m_Dir == KEEP_DIR)
			{
				Out.m_Dir = NeedCounter ? -Key : 0;
				Out.m_pWhy = NeedCounter ? "counter-steer: real freeze straight ahead, too close to just let go" : "block your key: real freeze straight ahead at body height";
			}
		}
	}

	State.m_Latch = Out.m_Dir != KEEP_DIR;
	return Out;
}
