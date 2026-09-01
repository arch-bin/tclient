#include "rocket_save.h"

#include <base/math.h>

#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>

CRocketSaveTuning CRocketSave::ms_Tuning;

static bool DangerAt(CCollision *pCollision, float x, float y, const CRocketSaveCfg &Cfg)
{
	const int Tx = (int)(x / 32.0f);
	const int Ty = (int)(y / 32.0f);
	if(Tx < 0 || Ty < 0 || Tx >= pCollision->GetWidth() || Ty >= pCollision->GetHeight())
		return true; // off the map is as deadly as it gets
	const int Index = pCollision->GetPureMapIndex(x, y);
	for(const int T : {pCollision->GetTileIndex(Index), pCollision->GetFrontTileIndex(Index)})
		if((Cfg.m_Freeze && T == TILE_FREEZE) || (Cfg.m_DeepFreeze && T == TILE_DFREEZE) ||
			(Cfg.m_LiveFreeze && T == TILE_LFREEZE) || (Cfg.m_Death && T == TILE_DEATH))
			return true;
	return false;
}

// How much open space around the tee still counts as "clear": beyond this, one escape is as good as
// another and the score should not split hairs over it.
static constexpr float ROCKET_CLEARANCE_MAX = 200.0f;

// Minimum push, in px/tick, for a shot to count as a save at all: below this the blast is too far or too
// weak to change anything, and firing would only waste the grenade.
static constexpr float ROCKET_MIN_KICK = 4.0f;

// Fly the grenade the way the game does and return where it goes off. Trajectory is the projectile maths
// from gamecore (CalcPos); detonation is the first SOLID tile it touches. *pHitSolid says whether it hit
// anything at all — a grenade that burns out in mid-air, or sails through freeze (freeze is not solid, so a
// rocket aimed straight into it just passes through), gives no push worth having and is not a candidate.
static vec2 BlastPos(CCollision *pCollision, vec2 From, vec2 Dir, const CRocketSaveCfg &Cfg, bool *pHitSolid)
{
	const vec2 Start = From + Dir * 28.0f * 0.75f; // the game spawns the projectile just outside the tee
	vec2 Prev = Start;
	const float Step = 1.0f / 200.0f; // fine enough that a tile is never skipped at grenade speed
	if(pHitSolid)
		*pHitSolid = false;
	for(float t = Step; t <= Cfg.m_Lifetime; t += Step)
	{
		const vec2 P = CalcPos(Start, Dir, Cfg.m_Curvature, Cfg.m_Speed, t);
		if(pCollision->CheckPoint(P.x, P.y))
		{
			if(pHitSolid)
				*pHitSolid = true;
			return Prev; // detonates against the surface it just hit
		}
		Prev = P;
	}
	return Prev; // burned out in mid-air: no surface, no real kick
}

// The explosion force the game would apply to the tee, straight from CGameWorld::CreateExplosion.
static vec2 BlastForce(vec2 TeePos, vec2 Blast, const CRocketSaveCfg &Cfg)
{
	const float Radius = 135.0f;
	const float InnerRadius = 48.0f;
	const vec2 Diff = TeePos - Blast;
	const float l = length(Diff);
	const vec2 ForceDir = l > 0.0f ? Diff / l : vec2(0.0f, -1.0f);
	const float Falloff = 1.0f - std::clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
	const float Dmg = Cfg.m_ExplosionStrength * Falloff;
	if((int)Dmg == 0)
		return vec2(0.0f, 0.0f); // the game ignores explosions this weak entirely
	return ForceDir * Dmg * 2.0f;
}

// Run the tee forward and score how the situation ends. Higher is better: a run that never touches freeze
// scores by how much room it keeps around it, a run that freezes scores by how long it lasted.
static float Outcome(CCollision *pCollision, CCharacterCore Core, const CNetObj_PlayerInput &Input, const CRocketSaveCfg &Cfg, int Ticks)
{
	Core.Init(nullptr, pCollision);
	Core.SetHookedPlayer(-1);
	float Worst = 1e9f;
	for(int i = 0; i < Ticks; ++i)
	{
		Core.m_Input = Input;
		const vec2 Prev = Core.m_Pos;
		Core.Tick(true);
		Core.Move();
		Core.Quantize();
		const int Steps = maximum(1, (int)(distance(Prev, Core.m_Pos) / 8.0f));
		for(int s = 1; s <= Steps; ++s)
		{
			const vec2 P = mix(Prev, Core.m_Pos, (float)s / (float)Steps);
			if(DangerAt(pCollision, P.x, P.y, Cfg))
				return (float)i; // frozen at tick i: the earlier, the worse
		}
		// How much open space is there around the tee at this moment? Sampled in eight directions, the
		// nearest danger wins — that is the "how far did the rocket actually throw me clear" measure.
		for(int d = 0; d < 8; ++d)
		{
			const vec2 Dir = direction((float)d / 8.0f * 2.0f * pi);
			for(float r = 16.0f; r <= ROCKET_CLEARANCE_MAX; r += 16.0f)
				if(DangerAt(pCollision, Core.m_Pos.x + Dir.x * r, Core.m_Pos.y + Dir.y * r, Cfg))
				{
					Worst = minimum(Worst, r);
					break;
				}
		}
	}
	// Survived the whole window: score above every "frozen at tick i" result, ranked by the room it kept.
	return (float)Ticks + minimum(Worst, ROCKET_CLEARANCE_MAX);
}

CRocketSaveAim CRocketSave::BestAim(CCollision *pCollision, const CCharacterCore &Core, const CNetObj_PlayerInput &Input, const CRocketSaveCfg &Cfg, vec2 Fallback)
{
	CRocketSaveAim Out;
	CCharacterCore Sim = Core;
	Sim.Init(nullptr, pCollision);
	Sim.SetHookedPlayer(-1);

	// A candidate is only worth anything when the grenade actually detonates against something solid close
	// enough to move us: a ceiling, a floor, a wall. That is the whole point of the rocket save — the blast
	// needs a surface to push off. The minimum kick is what the tee gains in one tick of air control, so a
	// shot that barely tickles it is not treated as a save.
	auto Try = [&](vec2 Dir, vec2 *pOutBlast, float *pOutKick) -> float {
		bool HitSolid = false;
		const vec2 Blast = BlastPos(pCollision, Sim.m_Pos, Dir, Cfg, &HitSolid);
		if(pOutBlast)
			*pOutBlast = Blast;
		if(pOutKick)
			*pOutKick = 0.0f;
		if(!HitSolid)
			return 0.0f;
		const vec2 Force = BlastForce(Sim.m_Pos, Blast, Cfg);
		const float Kick = length(Force);
		if(pOutKick)
			*pOutKick = Kick;
		if(Kick < ROCKET_MIN_KICK)
			return 0.0f;
		CCharacterCore Kicked = Sim;
		Kicked.m_Vel += Force;
		return Outcome(pCollision, Kicked, Input, Cfg, ms_Tuning.m_Horizon);
	};

	// What the plain "shoot at the danger" aim would achieve, as the baseline to beat.
	if(length(Fallback) > 0.001f)
	{
		vec2 Blast;
		float Kick = 0.0f;
		Out.m_Dir = normalize(Fallback);
		Out.m_PlainScore = Try(Out.m_Dir, &Blast, &Kick);
		Out.m_Score = Out.m_PlainScore;
		Out.m_Blast = Blast;
		Out.m_Kick = Kick;
		Out.m_Found = Out.m_PlainScore > 0.0f; // the plain aim only counts if it hits something solid
	}

	// Where are we actually going? The shot has to come from the direction of travel — that is what makes the
	// blast throw us BACK out of the danger we are flying into. Aiming behind us would only push us in
	// harder. The nearest solid surface within that arc is the one that hits hardest, and the tie-break
	// below picks it.
	const float Speed = length(Sim.m_Vel);
	const vec2 MoveDir = Speed > 0.001f ? Sim.m_Vel / Speed : vec2(0.0f, 0.0f);
	for(int i = 0; i < ms_Tuning.m_Rays; ++i)
	{
		const vec2 Dir = direction((float)i / (float)ms_Tuning.m_Rays * 2.0f * pi);
		if(Speed > 1.0f && dot(MoveDir, Dir) < ms_Tuning.m_InertiaDot)
			continue; // behind us: firing there would shove us further into what we are flying at
		vec2 Blast;
		float Kick = 0.0f;
		const float Score = Try(Dir, &Blast, &Kick);
		// Ties go to the harder kick, i.e. to the shot that detonates against the NEAREST solid surface:
		// blast force falls off with distance, so the closest wall, floor or ceiling throws us the furthest.
		if(Score > Out.m_Score || (Score > 0.0f && Score >= Out.m_Score - 0.5f && Kick > Out.m_Kick))
		{
			Out.m_Score = Score;
			Out.m_Dir = Dir;
			Out.m_Blast = Blast;
			Out.m_Kick = Kick;
			Out.m_Found = true;
		}
	}
	return Out;
}
