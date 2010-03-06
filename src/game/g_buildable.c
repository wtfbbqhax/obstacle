/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2000-2009 Darklegion Development

This file is part of Tremulous.

Tremulous is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Tremulous is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Tremulous; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "g_local.h"

/*
================
G_SetBuildableAnim

Triggers an animation client side
================
*/
void G_SetBuildableAnim( gentity_t *ent, buildableAnimNumber_t anim, qboolean force )
{
  int localAnim = anim | ( ent->s.legsAnim & ANIM_TOGGLEBIT );

  if( force )
    localAnim |= ANIM_FORCEBIT;

  // don't flip the togglebit more than once per frame
  if( ent->animTime != level.time )
  {
    ent->animTime = level.time;
    localAnim ^= ANIM_TOGGLEBIT;
  }

  ent->s.legsAnim = localAnim;
}

/*
================
G_SetIdleBuildableAnim

Set the animation to use whilst no other animations are running
================
*/
void G_SetIdleBuildableAnim( gentity_t *ent, buildableAnimNumber_t anim )
{
  ent->s.torsoAnim = anim;
}

/*
===============
G_CheckSpawnPoint

Check if a spawn at a specified point is valid
===============
*/
gentity_t *G_CheckSpawnPoint( int spawnNum, vec3_t origin, vec3_t normal,
    buildable_t spawn, vec3_t spawnOrigin )
{
  float   displacement;
  vec3_t  mins, maxs;
  vec3_t  cmins, cmaxs;
  vec3_t  localOrigin;
  trace_t tr;

  BG_BuildableBoundingBox( spawn, mins, maxs );

  if( spawn == BA_A_SPAWN )
  {
    VectorSet( cmins, -MAX_ALIEN_BBOX, -MAX_ALIEN_BBOX, -MAX_ALIEN_BBOX );
    VectorSet( cmaxs,  MAX_ALIEN_BBOX,  MAX_ALIEN_BBOX,  MAX_ALIEN_BBOX );

    displacement = ( maxs[ 2 ] + MAX_ALIEN_BBOX ) * M_ROOT3;
    VectorMA( origin, displacement, normal, localOrigin );
  }
  else if( spawn == BA_H_SPAWN || G_OC_CheckpointSpawnCheck() )
  {
    BG_ClassBoundingBox( PCL_HUMAN, cmins, cmaxs, NULL, NULL, NULL );

    VectorCopy( origin, localOrigin );
    localOrigin[ 2 ] += maxs[ 2 ] + fabs( cmins[ 2 ] ) + 1.0f;
  }
  else
    return NULL;

  trap_Trace( &tr, origin, NULL, NULL, localOrigin, spawnNum, MASK_SHOT );

  if( tr.entityNum != ENTITYNUM_NONE )
    return &g_entities[ tr.entityNum ];

  trap_Trace( &tr, localOrigin, cmins, cmaxs, localOrigin, -1, MASK_PLAYERSOLID );

  if( tr.entityNum != ENTITYNUM_NONE )
    return &g_entities[ tr.entityNum ];

  if( spawnOrigin != NULL )
    VectorCopy( localOrigin, spawnOrigin );

  return NULL;
}

#define POWER_REFRESH_TIME  2000

/*
================
G_FindProvider

Attempt to find power or creep for self; return qtrue and set self->parentNode if successful
================
*/
qboolean G_FindProvider( gentity_t *self )
{
  int       i, j;
  gentity_t *ent, *ent2;
  gentity_t *closestProvider = NULL;
  int       distance = 0;
  int       minDistance = INFINITE, requiredDistance = REACTOR_BASESIZE;
  vec3_t    temp_v;
  float     modifier;
  int       dps = G_DominationPoints();
  float     alienModifier;
  float     humanModifier;

  if( dps > 0 )
  {
    alienModifier = DOMINATION_SCALE * (0.5f + (float) level.dominationPoints[ TEAM_ALIENS ] / (float) dps);
    humanModifier = DOMINATION_SCALE * (0.5f + (float) level.dominationPoints[ TEAM_HUMANS ] / (float) dps);
  }
  else
  {
    alienModifier = 1.f;
    humanModifier = 1.f;
  }

  modifier = self->buildableTeam == TEAM_ALIENS ? alienModifier : humanModifier;

  // Core buildables are always powered
  if( G_IsCore( self->s.modelindex ) )
  {
    self->parentNode = self;

    return qtrue;
  }

  // Handle power buildables
  if( self->s.modelindex == BA_A_SPAWN || self->s.modelindex == BA_H_REPEATER || BG_IsDPoint( self->s.modelindex ) )
  {
    if( self->buildableTeam == TEAM_ALIENS )
      return ( self->parentNode = G_Overmind( ) ) != NULL;
    else
      return ( self->parentNode = G_Reactor( )  ) != NULL;
  }

  // Reset parent
  self->parentNode = NULL;

  // Iterate through entities
  for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( self->buildableTeam != ent->buildableTeam  && !BG_IsDPoint( ent->s.modelindex ) )
      continue;

    if( self->buildableTeam != ent->dominationTeam &&  BG_IsDPoint( ent->s.modelindex ) )
      continue;

    switch( ent->s.modelindex )
    {
      case BA_H_REACTOR:
        requiredDistance = REACTOR_BASESIZE;
        break;

      case BA_H_REPEATER:
        requiredDistance = REPEATER_BASESIZE;
        break;

      case BA_A_OVERMIND:
      case BA_A_SPAWN:
        requiredDistance = CREEP_BASESIZE;
        break;

      default:
        requiredDistance = DOMINATION_RANGE;
        break;
    }

    // If entity is a providing item calculate the distance to it
    if( ( G_IsCore( ent->s.modelindex ) || ent->s.modelindex == BA_H_REPEATER || ent->s.modelindex == BA_A_SPAWN || BG_IsDPoint( ent->s.modelindex ) ) &&
        ent->spawned && ent->powered && ent->health > 0 )
    {
      VectorSubtract( self->s.origin, ent->s.origin, temp_v );
      distance = VectorLength( temp_v );

      if( distance <= requiredDistance )
      {
        // Always prefer a non-buildable-zone power buildable if there is one in range
        if( !ent->usesBuildPointZone )
        {
          // Only power as much BP as the reactor can supply
          int buildPoints = ent->buildableTeam == TEAM_ALIENS ? g_alienBuildPoints.integer : g_humanBuildPoints.integer;

          buildPoints *= modifier;

          // Scan the buildables in the same zone and look at the BP remaining
          for( j = MAX_CLIENTS, ent2 = g_entities + j; j < level.num_entities; j++, ent2++ )
          {
            gentity_t *powerEntity;

            if( ent2->s.eType != ET_BUILDABLE )
              continue;

            if( ent2->buildableTeam != self->buildableTeam )
              continue;

            if( ent2 == self )
              continue;

            powerEntity = ent2->parentNode;

            if( powerEntity && powerEntity == ent )
            {
              buildPoints -= BG_Buildable( ent2->s.modelindex )->buildPoints;
            }
          }

          buildPoints -= ent->buildableTeam == TEAM_ALIENS ? level.alienBuildPointQueue : level.humanBuildPointQueue;

          buildPoints -= BG_Buildable( self->s.modelindex )->buildPoints;

          if( buildPoints >= 0 || ALWAYS_POWER )
          {
            // Return immediately
            self->parentNode = ent;

            return qtrue;
          }
          else
          {
            // A buildable can still be built if it shares BP from two zones

            // TODO: handle combined power zones here
          }
        }
        else if( ent->usesBuildPointZone && distance < minDistance )
        {
          // It's a build-point-zone buildable, so check that enough BP will be available to power
          // the buildable but only if self is a real buildable

          int buildPoints = g_zoneBuildPoints.integer;

          buildPoints *= modifier;

          // Scan the buildables in the same zone
          for( j = MAX_CLIENTS, ent2 = g_entities + j; j < level.num_entities; j++, ent2++ )
          {
            gentity_t *powerEntity;

            if( ent2->s.eType != ET_BUILDABLE )
              continue;

            if( ent2 == self )
              continue;

            powerEntity = ent2->parentNode;

            if( powerEntity && powerEntity == ent )
            {
              buildPoints -= BG_Buildable( ent2->s.modelindex )->buildPoints;
            }
          }

          if( self->usesBuildPointZone && level.buildPointZones[ ent->buildPointZone ].active )
            buildPoints -= level.buildPointZones[ ent->buildPointZone ].queuedBuildPoints;

          buildPoints -= BG_Buildable( self->s.modelindex )->buildPoints;

          if( buildPoints >= 0 || ALWAYS_POWER )
          {
            closestProvider = ent;
            minDistance = distance;
          }
          else
          {
            // a buildable can still be built if it shares BP from two zones

            // TODO: handle combined power zones here
          }
        }
      }
    }
  }

  // If there were no power items nearby give up
  if( closestProvider )
  {
    self->parentNode = closestProvider;

    return qtrue;
  }
  else
  {
    return qfalse;
  }
}

/*
================
G_ProvidingEntityForPoint

Simple wrapper to G_ProvidingEntityForEntity to find the entity providing
power for the specified point
================
*/
gentity_t *G_ProvidingEntityForPoint( const vec3_t origin, team_t team )
{
  gentity_t dummy;

  dummy.parentNode = NULL;
  dummy.buildableTeam = team;
  dummy.s.modelindex = BA_NONE;
  dummy.s.eType      = ET_BUILDABLE;
  VectorCopy( origin, dummy.s.origin );

  return G_ProvidingEntityForEntity( &dummy );
}

/*
================
G_ProvidingEntityForEntity

Simple wrapper to G_FindProvider to find the entity providing
power or creep for the specified entity
================
*/
gentity_t *G_ProvidingEntityForEntity( gentity_t *ent )
{
  if( G_FindProvider( ent ) )
    return ent->parentNode;
  return NULL;
}

/*
================
G_IsPowered

Check if a location has human power, returning the entity type
that is providing it
================
*/
buildable_t G_IsPowered( vec3_t origin )
{
  gentity_t *ent = G_ProvidingEntityForPoint( origin, TEAM_HUMANS );

  if( ent )
    return ent->s.modelindex;
  else
    return BA_NONE;
}


/*
==================
G_GetBuildPoints

Get the number of build points from a position
==================
*/
int G_GetBuildPoints( const vec3_t pos, team_t team, int extraDistance )
{
  gentity_t *powerPoint = G_ProvidingEntityForPoint( pos, team );

  if( G_TimeTilSuddenDeath( ) <= 0 )
  {
    return 0;
  }

  if( powerPoint && G_IsCore( powerPoint->s.modelindex ) )
  {
    if     ( team == TEAM_ALIENS )
    {
      return level.alienBuildPoints;
    }
    else if( team == TEAM_HUMANS )
    {
      return level.humanBuildPoints;
    }
  }

  if( powerPoint &&
      powerPoint->usesBuildPointZone && level.buildPointZones[ powerPoint->buildPointZone ].active )
  {
    return level.buildPointZones[ powerPoint->buildPointZone ].totalBuildPoints -
           level.buildPointZones[ powerPoint->buildPointZone ].queuedBuildPoints;
  }
  else
  {
    // Return the BP of the main zone by default

    if     ( team == TEAM_ALIENS )
    {
      return level.alienBuildPoints;
    }
    else if( team == TEAM_HUMANS )
    {
      return level.humanBuildPoints;
    }
  }

  return 0;
}

/*
==================
G_InPowerZone

See if a buildable is inside of another power zone
(This doesn't check if power zones overlap)
==================
*/
qboolean G_InPowerZone( gentity_t *self )
{
  int         i;
  gentity_t   *ent;
  int         distance;
  vec3_t      temp_v;

  for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( ent == self )
      continue;

    if( !ent->spawned )
      continue;

    if( ent->health <= 0 )
      continue;

    // if entity is a power item calculate the distance to it
    if( ( ent->s.modelindex == BA_H_REACTOR || ent->s.modelindex == BA_H_REPEATER || BG_IsDPoint( ent->s.modelindex ) ) &&
        ent->spawned && ent->powered )
    {
      VectorSubtract( self->s.origin, ent->s.origin, temp_v );
      distance = VectorLength( temp_v );

      if( ent->s.modelindex == BA_H_REACTOR && distance <= REACTOR_BASESIZE )
        return qtrue;
      else if( ent->s.modelindex == BA_H_REPEATER && distance <= REPEATER_BASESIZE )
        return qtrue;
      // allow repeaters to be built within range of domination points
      //else if( BG_IsDPoint( ent->s.modelindex ) && distance <= DOMINATION_RANGE )
        //return qtrue;
    }
  }

  return qfalse;
}

/*
================
G_FindDCC

attempt to find a controlling DCC for self, return number found
================
*/
int G_FindDCC( gentity_t *self )
{
  int       i;
  gentity_t *ent;
  int       distance = 0;
  vec3_t    temp_v;
  int       foundDCC = 0;

  if( self->buildableTeam != TEAM_HUMANS )
    return 0;

  //iterate through entities
  for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    //if entity is a dcc calculate the distance to it
    if( ent->s.modelindex == BA_H_DCC && ent->spawned )
    {
      VectorSubtract( self->s.origin, ent->s.origin, temp_v );
      distance = VectorLength( temp_v );
      if( distance < DC_RANGE && ent->powered )
      {
        foundDCC++; 
      }
    }
  }

  return foundDCC;
}

/*
================
G_IsDCCBuilt

See if any powered DCC exists
================
*/
qboolean G_IsDCCBuilt( void )
{
  int       i;
  gentity_t *ent;

  for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( ent->s.modelindex != BA_H_DCC )
      continue;

    if( !ent->spawned )
      continue;

    if( ent->health <= 0 )
      continue;

    return qtrue;
  }

  return qfalse;
}

/*
================
G_Reactor
G_Overmind

Since there's only one of these and we quite often want to find them, cache the
results, but check them for validity each time

The code here will break if more than one reactor or overmind is allowed, even
if one of them is dead/unspawned
================
*/
static gentity_t *G_FindBuildable( buildable_t buildable ); 

gentity_t *G_Reactor( void )
{
  static gentity_t *rc;

  // If cache becomes invalid renew it
  if( !rc || rc->s.eType != ET_BUILDABLE || rc->s.modelindex == BA_H_REACTOR )
    rc = G_FindBuildable( BA_H_REACTOR );

  // If we found it and it's alive, return it
  if( rc && rc->spawned && rc->health > 0 )
    return rc;

  return NULL;
}

gentity_t *G_Overmind( void )
{
  static gentity_t *om;

  // If cache becomes invalid renew it
  if( !om || om->s.eType != ET_BUILDABLE || om->s.modelindex == BA_A_OVERMIND )
    om = G_FindBuildable( BA_A_OVERMIND );

  // If we found it and it's alive, return it
  if( om && om->spawned && om->health > 0 )
    return om;

  return NULL;
}

/*
================
G_IsCreepHere

Check if a location has alien creep, returning the entity type
that is providing it
================
*/
buildable_t G_IsCreepHere( vec3_t origin )
{
  gentity_t *ent = G_ProvidingEntityForPoint( origin, TEAM_ALIENS );

  if( ent )
    return ent->s.modelindex;
  else
    return BA_NONE;
}

/*
================
G_IsCreepHereForPlayer

Special case for domination points
================
*/
buildable_t G_IsCreepHereForPlayer( vec3_t origin )
{
  gentity_t *ent = G_ProvidingEntityForPoint( origin, TEAM_ALIENS );

  if( ent && !BG_IsDPoint( ent->s.modelindex ) )
    return ent->s.modelindex;
  else
    return BA_NONE;
}

/*
================
G_CreepSlow

Set any nearby humans' SS_CREEPSLOWED flag
================
*/
static void G_CreepSlow( gentity_t *self )
{
  int         entityList[ MAX_GENTITIES ];
  vec3_t      range;
  vec3_t      mins, maxs;
  int         i, num;
  gentity_t   *enemy;
  buildable_t buildable = self->s.modelindex;
  float       creepSize = (float)BG_Buildable( buildable )->creepSize;

  if(G_OC_NeedNoCreep())
    G_OC_NoCreep();

  VectorSet( range, creepSize, creepSize, creepSize );

  VectorAdd( self->s.origin, range, maxs );
  VectorSubtract( self->s.origin, range, mins );

  //find humans
  num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    enemy = &g_entities[ entityList[ i ] ];

    if( enemy->flags & FL_NOTARGET )
      continue;

    if( enemy->client && enemy->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS &&
        enemy->client->ps.groundEntityNum != ENTITYNUM_NONE &&
	    ( G_Visible( self, enemy, CONTENTS_SOLID ) || G_OC_NoCreepThroughWalls() ) )
    {
      enemy->client->ps.stats[ STAT_STATE ] |= SS_CREEPSLOWED;
      enemy->client->lastCreepSlowTime = level.time;
    }
  }
}

/*
================
nullDieFunction

hack to prevent compilers complaining about function pointer -> NULL conversion
================
*/
static void nullDieFunction( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
}

//==================================================================================



/*
================
AGeneric_CreepRecede

Called when an alien buildable dies
================
*/
void AGeneric_CreepRecede( gentity_t *self )
{
  //if the creep just died begin the recession
  if( !( self->s.eFlags & EF_DEAD ) )
  {
    self->s.eFlags |= EF_DEAD;
    G_QueueBuildPoints( self );
    G_AddEvent( self, EV_BUILD_DESTROY, 0 );

    if( self->spawned )
      self->s.time = -level.time;
    else
      self->s.time = -( level.time -
          (int)( (float)CREEP_SCALEDOWN_TIME *
                 ( 1.0f - ( (float)( level.time - self->buildTime ) /
                            (float)BG_Buildable( self->s.modelindex )->buildTime ) ) ) );
  }

  //creep is still receeding
  if( ( self->timestamp + 10000 ) > level.time )
    self->nextthink = level.time + 500;
  else //creep has died
    G_FreeEntity( self );
}

/*
================
AGeneric_Blast

Called when an Alien buildable explodes after dead state
================
*/
void AGeneric_Blast( gentity_t *self )
{
  vec3_t dir;

  VectorCopy( self->s.origin2, dir );

  //do a bit of radius damage
  G_SelectiveRadiusDamage( self->s.pos.trBase, self, self->splashDamage,
                           self->splashRadius, self, self->splashMethodOfDeath,
                           TEAM_ALIENS );

  //pretty events and item cleanup
  self->s.eFlags |= EF_NODRAW; //don't draw the model once it's destroyed
  G_AddEvent( self, EV_ALIEN_BUILDABLE_EXPLOSION, DirToByte( dir ) );
  self->timestamp = level.time;
  self->think = AGeneric_CreepRecede;
  self->nextthink = level.time + 500;

  self->r.contents = 0;    //stop collisions...
  trap_LinkEntity( self ); //...requires a relink
}

/*
================
AGeneric_Die

Called when an Alien buildable is killed and enters a brief dead state prior to
exploding.
================
*/
void AGeneric_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  G_RewardAttackers( self );
  G_SetBuildableAnim( self, BANIM_DESTROY1, qtrue );
  G_SetIdleBuildableAnim( self, BANIM_DESTROYED );

  self->die = nullDieFunction;
  self->think = AGeneric_Blast;
  self->s.eFlags &= ~EF_FIRING; //prevent any firing effects
  self->powered = qfalse;

  if( self->spawned )
    self->nextthink = level.time + 5000;
  else
    self->nextthink = level.time; //blast immediately

  G_LogDestruction( self, attacker, mod );
}

/*
================
AGeneric_CreepCheck

Tests for creep and kills the buildable if there is none
================
*/
void AGeneric_CreepCheck( gentity_t *self )
{
  gentity_t *spawn;

  G_CreepSlow( self );

  if( G_OC_NeedRepeaterBlast() )
    return;

  spawn = self->parentNode;
  if( !G_FindProvider( self ) )
  {
    if( spawn && self->killedBy != ENTITYNUM_NONE )
      G_Damage( self, NULL, g_entities + self->killedBy, NULL, NULL,
                self->health, 0, MOD_NOCREEP );
    else
      G_Damage( self, NULL, NULL, NULL, NULL, self->health, 0, MOD_NOCREEP );
    return;
  }
}

/*
================
AGeneric_Think

A generic think function for Alien buildables
================
*/
void AGeneric_Think( gentity_t *self )
{
  self->powered = G_FindProvider( self ) && G_Overmind( );
  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink;
  AGeneric_CreepCheck( self );
  G_OC_DefaultAlienPowered();
}

/*
================
AGeneric_Pain

A generic pain function for Alien buildables
================
*/
void AGeneric_Pain( gentity_t *self, gentity_t *attacker, int damage )
{
  if( self->health <= 0 )
    return;
    
  // Alien buildables only have the first pain animation defined
  G_SetBuildableAnim( self, BANIM_PAIN1, qfalse );
}




//==================================================================================




/*
================
ASpawn_Die

Called when an alien spawn dies
================
*/
void ASpawn_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  int i;

  AGeneric_Die( self, inflictor, attacker, damage, mod );
  
  // All supported structures that no longer have creep will have been killed
  // by whoever killed this structure
  for( i = MAX_CLIENTS; i < level.num_entities; i++ )
  {
    gentity_t *ent = g_entities + i;
    
    if( !ent->inuse || ent->health <= 0 || ent->s.eType != ET_BUILDABLE ||
        ent->parentNode != self )
      continue;
    ent->killedBy = attacker - g_entities;
  }
}

/*
================
ASpawn_Think

think function for Alien Spawn
================
*/
void ASpawn_Think( gentity_t *self )
{
  gentity_t *ent;

  G_OC_DefaultAlienPowered();

  if( self->spawned )
  {
    //only suicide if at rest
    if( self->s.groundEntityNum )
    {
      if( ( ent = G_CheckSpawnPoint( self->s.number, self->s.origin,
              self->s.origin2, BA_A_SPAWN, NULL ) ) != NULL && !level.buildablesMoving )
      {
        // If the thing blocking the spawn is a buildable, kill it. 
        // If it's part of the map, kill self. 
        if( ent->s.eType == ET_BUILDABLE )
        {
          G_Damage( ent, NULL, NULL, NULL, NULL, 10000, 0, MOD_SUICIDE );
          G_SetBuildableAnim( self, BANIM_SPAWN1, qtrue );
        }
        else if( ent->s.number == ENTITYNUM_WORLD || ent->s.eType == ET_MOVER )
        {
          G_Damage( self, NULL, NULL, NULL, NULL, 10000, 0, MOD_SUICIDE );
          return;
        }

        if( ent->s.eType == ET_CORPSE )
          G_FreeEntity( ent ); //quietly remove
      }
    }
  }

  G_CreepSlow( self );

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink;
}





//==================================================================================





#define OVERMIND_ATTACK_PERIOD 10000
#define OVERMIND_DYING_PERIOD  5000
#define OVERMIND_SPAWNS_PERIOD 30000

/*
================
AOvermind_Think

Think function for Alien Overmind
================
*/
void AOvermind_Think( gentity_t *self )
{
  vec3_t range = { OVERMIND_ATTACK_RANGE, OVERMIND_ATTACK_RANGE, OVERMIND_ATTACK_RANGE };
  vec3_t mins, maxs;
  int    i;

  self->powered = qtrue;

  G_OC_OvermindPowered();

  VectorAdd( self->s.origin, range, maxs );
  VectorSubtract( self->s.origin, range, mins );

  if( self->spawned && ( self->health > 0 ) && self->powered )
  {
    //do some damage
    if( G_SelectiveRadiusDamage( self->s.pos.trBase, self, self->splashDamage,
          self->splashRadius, self, MOD_OVERMIND, TEAM_ALIENS ) )
    {
      self->timestamp = level.time;
      G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
    }
    
    // just in case an egg finishes building after we tell overmind to stfu
    if( level.numAlienSpawns > 0 )
      level.overmindMuted = qfalse;

    // shut up during intermission
    if( level.intermissiontime )
      level.overmindMuted = qtrue;

    //low on spawns
    if( !level.overmindMuted && level.numAlienSpawns <= 0 &&
        level.time > self->overmindSpawnsTimer )
    {
      qboolean haveBuilder = qfalse;
      gentity_t *builder;

      self->overmindSpawnsTimer = level.time + OVERMIND_SPAWNS_PERIOD;
      G_BroadcastEvent( EV_OVERMIND_SPAWNS, 0 );

      for( i = 0; i < level.numConnectedClients; i++ )
      {
        builder = &g_entities[ level.sortedClients[ i ] ];
        if( builder->health > 0 &&
          ( builder->client->pers.classSelection == PCL_ALIEN_BUILDER0 ||
            builder->client->pers.classSelection == PCL_ALIEN_BUILDER0_UPG ) )
        {
          haveBuilder = qtrue;
          break;
        }
      }
      // aliens now know they have no eggs, but they're screwed, so stfu
      if( ( !haveBuilder || G_TimeTilSuddenDeath( ) <= 0 ) && !G_OC_NoSuddenDeath() )
        level.overmindMuted = qtrue;
    }

    //overmind dying
    if( self->health < ( OVERMIND_HEALTH / 10.0f ) && level.time > self->overmindDyingTimer )
    {
      self->overmindDyingTimer = level.time + OVERMIND_DYING_PERIOD;
      G_BroadcastEvent( EV_OVERMIND_DYING, 0 );
    }

    //overmind under attack
    if( self->health < self->lastHealth && level.time > self->overmindAttackTimer )
    {
      self->overmindAttackTimer = level.time + OVERMIND_ATTACK_PERIOD;
      G_BroadcastEvent( EV_OVERMIND_ATTACK, 0 );
    }

    self->lastHealth = self->health;
  }
  else
    self->overmindSpawnsTimer = level.time + OVERMIND_SPAWNS_PERIOD;

  G_CreepSlow( self );

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink;
}





//==================================================================================





/*
================
ABarricade_Pain

Barricade pain animation depends on shrunk state
================
*/
void ABarricade_Pain( gentity_t *self, gentity_t *attacker, int damage )
{
  if( self->health <= 0 )
    return;

  if( !self->shrunkTime )
    G_SetBuildableAnim( self, BANIM_PAIN1, qfalse );
  else
    G_SetBuildableAnim( self, BANIM_PAIN2, qfalse );
}

/*
================
ABarricade_Shrink

Set shrink state for a barricade. When unshrinking, checks to make sure there
is enough room.
================
*/
void ABarricade_Shrink( gentity_t *self, qboolean shrink )
{
  if ( G_OC_NeedUnshrink() )
    G_OC_Unshrink();
  if ( !self->spawned || self->health <= 0 )
    shrink = qtrue;
  if ( shrink && self->shrunkTime )
  {
    int anim;

    // We need to make sure that the animation has been set to shrunk mode
    // because we start out shrunk but with the construct animation when built
    self->shrunkTime = level.time;
    anim = self->s.torsoAnim & ~( ANIM_FORCEBIT | ANIM_TOGGLEBIT );
    if ( self->spawned && self->health > 0 && anim != BANIM_DESTROYED )
    {
      G_SetIdleBuildableAnim( self, BANIM_DESTROYED );
      G_SetBuildableAnim( self, BANIM_ATTACK1, qtrue );
    }
    return;
  }

  if ( !shrink && ( !self->shrunkTime ||
       level.time < self->shrunkTime + BARRICADE_SHRINKTIMEOUT ) )
    return;

  BG_BuildableBoundingBox( BA_A_BARRICADE, self->r.mins, self->r.maxs );

  if ( shrink )
  {
    self->r.maxs[ 2 ] = (int)( self->r.maxs[ 2 ] * BARRICADE_SHRINKPROP );
    self->shrunkTime = level.time;

    // shrink animation, the destroy animation is used
    if ( self->spawned && self->health > 0 )
    {
      G_SetBuildableAnim( self, BANIM_ATTACK1, qtrue );
      G_SetIdleBuildableAnim( self, BANIM_DESTROYED );
    }
  }
  else
  {
    trace_t tr;
    int anim;

    trap_Trace( &tr, self->s.origin, self->r.mins, self->r.maxs,
                self->s.origin, self->s.number, MASK_PLAYERSOLID );
    if ( tr.startsolid || tr.fraction < 1.0f )
    {
      self->r.maxs[ 2 ] = (int)( self->r.maxs[ 2 ] * BARRICADE_SHRINKPROP );
      return;
    }
    self->shrunkTime = 0;

    // unshrink animation, IDLE2 has been hijacked for this
    anim = self->s.legsAnim & ~( ANIM_FORCEBIT | ANIM_TOGGLEBIT );
    if ( self->spawned && self->health > 0 &&
         anim != BANIM_CONSTRUCT1 && anim != BANIM_CONSTRUCT2 )
    {
      G_SetIdleBuildableAnim( self, BG_Buildable( BA_A_BARRICADE )->idleAnim );
      G_SetBuildableAnim( self, BANIM_ATTACK2, qtrue );
    }
  }

  // a change in size requires a relink
  if ( self->spawned )
    trap_LinkEntity( self );
}

/*
================
ABarricade_Die

Called when an alien barricade dies
================
*/
void ABarricade_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  AGeneric_Die( self, inflictor, attacker, damage, mod );
  ABarricade_Shrink( self, qtrue );
}

/*
================
ABarricade_Think

Think function for Alien Barricade
================
*/
void ABarricade_Think( gentity_t *self )
{
  AGeneric_Think( self );
  G_OC_DefaultAlienPowered();

  // Shrink if unpowered
  ABarricade_Shrink( self, !self->powered );

  self->nextthink += G_OC_AlienBuildableOptimizedThinkTime();
}

/*
================
ABarricade_Touch

Barricades shrink when they are come into contact with an Alien that can
pass through
================
*/

void ABarricade_Touch( gentity_t *self, gentity_t *other, trace_t *trace )
{
  gclient_t *client = other->client;
  int client_z, min_z;

  if( !client || client->pers.teamSelection != TEAM_ALIENS )
    return;

  G_OC_BarricadeShrink();

  // Client must be high enough to pass over. Note that STEPSIZE (18) is
  // hardcoded here because we don't include bg_local.h!
  client_z = other->s.origin[ 2 ] + other->r.mins[ 2 ];
  min_z = self->s.origin[ 2 ] - 18 +
          (int)( self->r.maxs[ 2 ] * BARRICADE_SHRINKPROP );
  if( client_z < min_z )
    return;
  ABarricade_Shrink( self, qtrue );
}

//==================================================================================




/*
================
AAcidTube_Think

Think function for Alien Acid Tube
================
*/
void AAcidTube_Think( gentity_t *self )
{
  int       entityList[ MAX_GENTITIES ];
  vec3_t    range = { ACIDTUBE_RANGE, ACIDTUBE_RANGE, ACIDTUBE_RANGE };
  vec3_t    mins, maxs;
  int       i, num;
  gentity_t *enemy;

  AGeneric_Think( self );

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink + G_OC_AlienBuildableOptimizedThinkTime();

  G_OC_DefaultAlienPowered();

  VectorAdd( self->s.origin, range, maxs );
  VectorSubtract( self->s.origin, range, mins );

  // attack nearby humans
  if( self->spawned && self->health > 0 && self->powered )
  {
    num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
    for( i = 0; i < num; i++ )
    {
      enemy = &g_entities[ entityList[ i ] ];

      if( !G_Visible( self, enemy, CONTENTS_SOLID ) )
        continue;

      if( enemy->flags & FL_NOTARGET )
        continue;

      if( enemy->client && enemy->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS )
      {
        // start the attack animation
        if( level.time >= self->timestamp + ACIDTUBE_REPEAT_ANIM )
        {
          self->timestamp = level.time;
          G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
          G_AddEvent( self, EV_ALIEN_ACIDTUBE, DirToByte( self->s.origin2 ) );
        }
        
        G_SelectiveRadiusDamage( self->s.pos.trBase, self, ACIDTUBE_DAMAGE,
                                 ACIDTUBE_RANGE, self, MOD_ATUBE, TEAM_ALIENS );                           
        self->nextthink = level.time + ACIDTUBE_REPEAT;
        return;
      }
    }
  }
}




//==================================================================================

/*
================
AHive_CheckTarget

Returns true and fires the hive missile if the target is valid
================
*/
static qboolean AHive_CheckTarget( gentity_t *self, gentity_t *enemy )
{
  trace_t trace;
  vec3_t tip_origin, dirToTarget;

  // Check if this is a valid target
  if( enemy->health <= 0 || !enemy->client ||
      enemy->client->ps.stats[ STAT_TEAM ] != TEAM_HUMANS )
    return qfalse;

  // Check if the tip of the hive can see the target
  VectorMA( self->s.pos.trBase, self->r.maxs[ 2 ], self->s.origin2,
            tip_origin );
  trap_Trace( &trace, tip_origin, NULL, NULL, enemy->s.pos.trBase,
              self->s.number, MASK_SHOT );
  if( trace.fraction < 1.0f && trace.entityNum != enemy->s.number )
    return qfalse;

  self->active = qtrue;
  self->target_ent = enemy;
  self->timestamp = level.time + HIVE_REPEAT;

  VectorSubtract( enemy->s.pos.trBase, self->s.pos.trBase, dirToTarget );
  VectorNormalize( dirToTarget );
  vectoangles( dirToTarget, self->turretAim );

  // Fire at target
  FireWeapon( self );
  G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
  return qtrue;
}

/*
================
AHive_Think

Think function for Alien Hive
================
*/
void AHive_Think( gentity_t *self )
{
  int       start;

  AGeneric_Think( self );

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink + G_OC_AlienBuildableOptimizedThinkTime();

  G_OC_DefaultAlienPowered();

  // Hive missile hasn't returned in HIVE_REPEAT seconds, forget about it
  if( self->timestamp < level.time )
    self->active = qfalse;

  // Find a target to attack
  if( self->spawned && !self->active && self->powered )
  {
    int i, num, entityList[ MAX_GENTITIES ];
    vec3_t mins, maxs,
           range = { HIVE_SENSE_RANGE, HIVE_SENSE_RANGE, HIVE_SENSE_RANGE };

    VectorAdd( self->s.origin, range, maxs );
    VectorSubtract( self->s.origin, range, mins );

    num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

    if( num == 0 )
      return;

    start = rand( ) % num;
    for( i = start; i < num + start; i++ )
    {
      if( AHive_CheckTarget( self, g_entities + entityList[ i % num ] ) )
        return;
    }
  }
}

/*
================
AHive_Pain

pain function for Alien Hive
================
*/
void AHive_Pain( gentity_t *self, gentity_t *attacker, int damage )
{
  if( self->spawned && self->powered && !self->active )
    AHive_CheckTarget( self, attacker );

  G_SetBuildableAnim( self, BANIM_PAIN1, qfalse );
}


//==================================================================================




#define HOVEL_TRACE_DEPTH 128.0f

/*
================
AHovel_Blocked

Is this hovel entrance blocked?
================
*/
qboolean AHovel_Blocked( gentity_t *hovel, gentity_t *player, qboolean provideExit )
{
  vec3_t    forward, normal, origin, start, end, angles, hovelMaxs;
  vec3_t    mins, maxs;
  float     displacement;
  trace_t   tr;

  BG_BuildableBoundingBox( BA_A_HOVEL, NULL, hovelMaxs );
  BG_ClassBoundingBox( player->client->ps.stats[ STAT_CLASS ],
                       mins, maxs, NULL, NULL, NULL );

  VectorCopy( hovel->s.origin2, normal );
  AngleVectors( hovel->s.angles, forward, NULL, NULL );
  VectorInverse( forward );

  displacement = VectorMaxComponent( maxs ) * M_ROOT3 +
                 VectorMaxComponent( hovelMaxs ) * M_ROOT3 + 1.0f;

  VectorMA( hovel->s.origin, displacement, forward, origin );

  VectorCopy( hovel->s.origin, start );
  VectorCopy( origin, end );

  // see if there's something between the hovel and its exit 
  // (eg built right up against a wall)
  trap_Trace( &tr, start, NULL, NULL, end, player->s.number, MASK_PLAYERSOLID );
  if( tr.fraction < 1.0f )
    return qtrue;

  vectoangles( forward, angles );

  VectorMA( origin, HOVEL_TRACE_DEPTH, normal, start );

  //compute a place up in the air to start the real trace
  trap_Trace( &tr, origin, mins, maxs, start, player->s.number, MASK_PLAYERSOLID );

  VectorMA( origin, ( HOVEL_TRACE_DEPTH * tr.fraction ) - 1.0f, normal, start );
  VectorMA( origin, -HOVEL_TRACE_DEPTH, normal, end );

  trap_Trace( &tr, start, mins, maxs, end, player->s.number, MASK_PLAYERSOLID );

  VectorCopy( tr.endpos, origin );

  trap_Trace( &tr, origin, mins, maxs, origin, player->s.number, MASK_PLAYERSOLID );

  if( tr.fraction < 1.0f )
    return qtrue;

  if( provideExit )
  {
    G_SetOrigin( player, origin );
    VectorCopy( origin, player->client->ps.origin );
    // nudge
    VectorMA( normal, 200.0f, forward, player->client->ps.velocity );
    G_SetClientViewAngle( player, angles );
  }

  return qfalse;
}

/*
================
APropHovel_Blocked

Wrapper to test a hovel placement for validity
================
*/
static qboolean APropHovel_Blocked( vec3_t origin, vec3_t angles, vec3_t normal,
                                    gentity_t *player )
{
  gentity_t hovel;

  VectorCopy( origin, hovel.s.origin );
  VectorCopy( angles, hovel.s.angles );
  VectorCopy( normal, hovel.s.origin2 );

  return AHovel_Blocked( &hovel, player, qfalse );
}

/*
================
AHovel_Use

Called when an alien uses a hovel
================
*/
void AHovel_Use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
  vec3_t  hovelOrigin, hovelAngles, inverseNormal;

  if( self->spawned && self->powered )
  {
    if( self->active && !G_OC_OCHovelNeverOccupied() )
    {
      //this hovel is in use
      G_TriggerMenu( activator->client->ps.clientNum, MN_A_HOVEL_OCCUPIED );
    }
    else if( ( ( activator->client->ps.stats[ STAT_CLASS ] == PCL_ALIEN_BUILDER0 ) ||
               ( activator->client->ps.stats[ STAT_CLASS ] == PCL_ALIEN_BUILDER0_UPG ) ) &&
             activator->health > 0 && self->health > 0 )
    {
      if( AHovel_Blocked( self, activator, qfalse ) )
      {
        //you can get in, but you can't get out
        G_TriggerMenu( activator->client->ps.clientNum, MN_A_HOVEL_BLOCKED );
        return;
      }

      self->active = qtrue;
      G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );

      //prevent lerping
      activator->client->ps.eFlags ^= EF_TELEPORT_BIT;
      activator->client->ps.eFlags |= EF_NODRAW;
      G_UnlaggedClear( activator );

      // Cancel pending suicides
      activator->suicideTime = 0;

      activator->client->ps.stats[ STAT_STATE ] |= SS_HOVELING;
      activator->client->hovel = self;
      self->builder = activator;

      VectorCopy( self->s.pos.trBase, hovelOrigin );
      VectorMA( hovelOrigin, 128.0f, self->s.origin2, hovelOrigin );

      VectorCopy( self->s.origin2, inverseNormal );
      VectorInverse( inverseNormal );
      vectoangles( inverseNormal, hovelAngles );

      VectorCopy( activator->s.pos.trBase, activator->client->hovelOrigin );

      G_SetOrigin( activator, hovelOrigin );
      VectorCopy( hovelOrigin, activator->client->ps.origin );
      G_SetClientViewAngle( activator, hovelAngles );
    }
  }
}


/*
================
AHovel_Think

Think for alien hovel
================
*/
void AHovel_Think( gentity_t *self )
{
  AGeneric_CreepCheck( self );

  self->nextthink = level.time + 200 + G_OC_AlienBuildableOptimizedThinkTime();

  G_OC_DefaultAlienPowered();

  if( self->spawned )
  {
    if( self->active )
      G_SetIdleBuildableAnim( self, BANIM_IDLE2 );
    else
      G_SetIdleBuildableAnim( self, BANIM_IDLE1 );
  }
}

/*
================
AHovel_Die

Die for alien hovel
================
*/
void AHovel_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  //if the hovel is occupied free the occupant
  if( self->active )
  {
    gentity_t *builder = self->builder;
    vec3_t    newOrigin;
    vec3_t    newAngles;

    VectorCopy( self->s.angles, newAngles );
    newAngles[ ROLL ] = 0;

    VectorCopy( self->s.origin, newOrigin );
    VectorMA( newOrigin, 1.0f, self->s.origin2, newOrigin );

    //prevent lerping
    builder->client->ps.eFlags ^= EF_TELEPORT_BIT;
    builder->client->ps.eFlags &= ~EF_NODRAW;
    G_UnlaggedClear( builder );

    G_SetOrigin( builder, newOrigin );
    VectorCopy( newOrigin, builder->client->ps.origin );
    G_SetClientViewAngle( builder, newAngles );

    //client leaves hovel
    builder->client->ps.stats[ STAT_STATE ] &= ~SS_HOVELING;
  }
  
  AGeneric_Die( self, inflictor, attacker, damage, mod );
  self->nextthink = level.time + 100;
}





//==================================================================================




/*
================
ABooster_Touch

Called when an alien touches a booster
================
*/
void ABooster_Touch( gentity_t *self, gentity_t *other, trace_t *trace )
{
  gclient_t *client = other->client;

  if( !self->spawned || !self->powered || self->health <= 0 )
    return;

  if( !client )
    return;

  if( client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS )
    return;

  client->ps.stats[ STAT_STATE ] |= SS_BOOSTED;
  client->boostedTime = level.time;
}




//==================================================================================

#define TRAPPER_ACCURACY 10 // lower is better

/*
================
ATrapper_FireOnEnemy

Used by ATrapper_Think to fire at enemy
================
*/
void ATrapper_FireOnEnemy( gentity_t *self, int firespeed, float range )
{
  gentity_t *enemy = self->enemy;
  vec3_t    dirToTarget;
  vec3_t    halfAcceleration, thirdJerk;
  float     distanceToTarget = BG_Buildable( self->s.modelindex )->turretRange;
  int       lowMsec = 0;
  int       highMsec = (int)( (
    ( ( distanceToTarget * LOCKBLOB_SPEED ) +
      ( distanceToTarget * BG_Class( enemy->client->ps.stats[ STAT_CLASS ] )->speed ) ) /
    ( LOCKBLOB_SPEED * LOCKBLOB_SPEED ) ) * 1000.0f );

  VectorScale( enemy->acceleration, 1.0f / 2.0f, halfAcceleration );
  VectorScale( enemy->jerk, 1.0f / 3.0f, thirdJerk );

  // highMsec and lowMsec can only move toward
  // one another, so the loop must terminate
  while( highMsec - lowMsec > TRAPPER_ACCURACY )
  {
    int   partitionMsec = ( highMsec + lowMsec ) / 2;
    float time = (float)partitionMsec / 1000.0f;
    float projectileDistance = LOCKBLOB_SPEED * time;

    VectorMA( enemy->s.pos.trBase, time, enemy->s.pos.trDelta, dirToTarget );
    VectorMA( dirToTarget, time * time, halfAcceleration, dirToTarget );
    VectorMA( dirToTarget, time * time * time, thirdJerk, dirToTarget );
    VectorSubtract( dirToTarget, self->s.pos.trBase, dirToTarget );
    distanceToTarget = VectorLength( dirToTarget );

    if( projectileDistance < distanceToTarget )
      lowMsec = partitionMsec;
    else if( projectileDistance > distanceToTarget )
      highMsec = partitionMsec;
    else if( projectileDistance == distanceToTarget )
      break; // unlikely to happen
  }

  VectorNormalize( dirToTarget );
  vectoangles( dirToTarget, self->turretAim );

  //fire at target
  FireWeapon( self );
  G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
  self->count = level.time + firespeed;
}

/*
================
ATrapper_CheckTarget

Used by ATrapper_Think to check enemies for validity
================
*/
qboolean ATrapper_CheckTarget( gentity_t *self, gentity_t *target, int range )
{
  vec3_t    distance;
  trace_t   trace;

  if( !target ) // Do we have a target?
    return qfalse;
  if( !target->inuse ) // Does the target still exist?
    return qfalse;
  if( target == self ) // is the target us?
    return qfalse;
  if( !target->client ) // is the target a bot or player?
    return qfalse;
  if( target->client->ps.stats[ STAT_TEAM ] == TEAM_ALIENS ) // one of us?
    return qfalse;
  if( target->client->sess.spectatorState != SPECTATOR_NOT ) // is the target alive?
    return qfalse;
  if( target->health <= 0 ) // is the target still alive?
    return qfalse;
  if( target->client->ps.stats[ STAT_STATE ] & SS_BLOBLOCKED ) // locked?
    return qfalse;
  if( target->flags & FL_NOTARGET ) // Does the target have notarget enabled?
    return qfalse;

  VectorSubtract( target->r.currentOrigin, self->r.currentOrigin, distance );
  if( VectorLength( distance ) > range ) // is the target within range?
    return qfalse;

  //only allow a narrow field of "vision"
  VectorNormalize( distance ); //is now direction of target
  if( DotProduct( distance, self->s.origin2 ) < LOCKBLOB_DOT )
    return qfalse;

  trap_Trace( &trace, self->s.pos.trBase, NULL, NULL, target->s.pos.trBase, self->s.number, MASK_SHOT );
  if ( trace.contents & CONTENTS_SOLID ) // can we see the target?
    return qfalse;

  return qtrue;
}

/*
================
ATrapper_FindEnemy

Used by ATrapper_Think to locate enemy gentities
================
*/
void ATrapper_FindEnemy( gentity_t *ent, int range )
{
  gentity_t *target;
  int       i;
  int       start;

  // iterate through entities
  // note that if we exist then level.num_entities != 0
  start = rand( ) % level.num_entities;
  for( i = start; i < level.num_entities + start; i++ )
  {
    target = g_entities + ( i % level.num_entities );
    //if target is not valid keep searching
    if( !ATrapper_CheckTarget( ent, target, range ) )
      continue;

    if( target->flags & FL_NOTARGET )
      continue;

    //we found a target
    ent->enemy = target;
    return;
  }

  //couldn't find a target
  ent->enemy = NULL;
}

/*
================
ATrapper_Think

think function for Alien Defense
================
*/
void ATrapper_Think( gentity_t *self )
{
  int range =     BG_Buildable( self->s.modelindex )->turretRange;
  int firespeed = BG_Buildable( self->s.modelindex )->turretFireSpeed;

  AGeneric_Think( self );

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink + G_OC_AlienBuildableOptimizedThinkTime();

  G_OC_DefaultAlienPowered();

  if( self->spawned && self->powered )
  {
    //if the current target is not valid find a new one
    if( !ATrapper_CheckTarget( self, self->enemy, range ) )
      ATrapper_FindEnemy( self, range );

    //if a new target cannot be found don't do anything
    if( !self->enemy )
      return;

    //if we are pointing at our target and we can fire shoot it
    if( self->count < level.time )
      ATrapper_FireOnEnemy( self, firespeed, range );
  }
}




//==================================================================================




/*
================
G_SuicideIfNoPower

Destroy human structures that have been unpowered too long
================
*/
static void G_SuicideIfNoPower( gentity_t *self )
{
  if( self->buildableTeam != TEAM_HUMANS )
    return;

  if( G_Reactor( ) && !self->parentNode )
  {
    // If no parent for x seconds then disappear
    if( self->count < 0 )
      self->count = level.time;
    else if( self->count > 0 && ( ( level.time - self->count ) > HUMAN_BUILDABLE_INACTIVE_TIME ) )
      G_Damage( self, NULL, NULL, NULL, NULL, self->health, 0, MOD_NOCREEP );
  }
  else
  {
    self->count = -1;
  }
}

/*
================
G_IdlePowerState

Set buildable idle animation to match power state
================
*/
static void G_IdlePowerState( gentity_t *self )
{
  if( self->powered )
  {
    if( self->s.torsoAnim == BANIM_IDLE3 )
      G_SetIdleBuildableAnim( self, BG_Buildable( self->s.modelindex )->idleAnim );
  }
  else
  {
    if( self->s.torsoAnim != BANIM_IDLE3 )
      G_SetIdleBuildableAnim( self, BANIM_IDLE3 );
  }
}




//==================================================================================




/*
================
HSpawn_Disappear

Called when a human spawn is destroyed before it is spawned
think function
================
*/
void HSpawn_Disappear( gentity_t *self )
{
  self->s.eFlags |= EF_NODRAW; //don't draw the model once its destroyed
  self->timestamp = level.time;
  G_QueueBuildPoints( self );

  G_FreeEntity( self );
}


/*
================
HSpawn_blast

Called when a human spawn explodes
think function
================
*/
void HSpawn_Blast( gentity_t *self )
{
  vec3_t  dir;

  // we don't have a valid direction, so just point straight up
  dir[ 0 ] = dir[ 1 ] = 0;
  dir[ 2 ] = 1;

  self->timestamp = level.time;

  //do some radius damage
  G_RadiusDamage( self->s.pos.trBase, self, self->splashDamage,
    self->splashRadius, self, self->splashMethodOfDeath );

  // begin freeing build points
  G_QueueBuildPoints( self );
  // turn into an explosion
  self->s.eType = ET_EVENTS + EV_HUMAN_BUILDABLE_EXPLOSION;
  self->freeAfterEvent = qtrue;
  G_AddEvent( self, EV_HUMAN_BUILDABLE_EXPLOSION, DirToByte( dir ) );
}


/*
================
HSpawn_die

Called when a human spawn dies
================
*/
void HSpawn_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  G_RewardAttackers( self );
  G_SetBuildableAnim( self, BANIM_DESTROY1, qtrue );
  G_SetIdleBuildableAnim( self, BANIM_DESTROYED );

  self->die = nullDieFunction;
  self->powered = qfalse; //free up power
  self->s.eFlags &= ~EF_FIRING; //prevent any firing effects

  if( self->spawned )
  {
    self->think = HSpawn_Blast;
    self->nextthink = level.time + HUMAN_DETONATION_DELAY;
  }
  else
  {
    self->think = HSpawn_Disappear;
    self->nextthink = level.time; //blast immediately
  }

  G_LogDestruction( self, attacker, mod );
}

/*
================
HSpawn_Think

Think for human spawn
================
*/
void HSpawn_Think( gentity_t *self )
{
  gentity_t *ent;

  G_SuicideIfNoPower( self );

  // set parentNode
  self->powered = G_FindProvider( self ) && G_Reactor( );

  if( self->spawned )
  {
    //only suicide if at rest
    if( self->s.groundEntityNum )
    {
      if( ( ent = G_CheckSpawnPoint( self->s.number, self->s.origin,
              self->s.origin2, BA_H_SPAWN, NULL ) ) != NULL && !level.buildablesMoving )
      {
        // If the thing blocking the spawn is a buildable, kill it. 
        // If it's part of the map, kill self. 
        if( ent->s.eType == ET_BUILDABLE )
        {
          G_Damage( ent, NULL, NULL, NULL, NULL, self->health, 0, MOD_SUICIDE );
          G_SetBuildableAnim( self, BANIM_SPAWN1, qtrue );
        }
        else if( ent->s.number == ENTITYNUM_WORLD || ent->s.eType == ET_MOVER )
        {
          G_Damage( self, NULL, NULL, NULL, NULL, self->health, 0, MOD_SUICIDE );
          return;
        }

        if( ent->s.eType == ET_CORPSE )
          G_FreeEntity( ent ); //quietly remove
      }
    }
  }

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink;
}




//==================================================================================




/*
================
HRepeater_Die

Called when a repeater dies
================
*/
static void HRepeater_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  G_RewardAttackers( self );
  G_SetBuildableAnim( self, BANIM_DESTROY1, qtrue );
  G_SetIdleBuildableAnim( self, BANIM_DESTROYED );

  self->die = nullDieFunction;
  self->powered = qfalse; //free up power
  self->s.eFlags &= ~EF_FIRING; //prevent any firing effects

  if( self->spawned )
  {
    self->think = HSpawn_Blast;
    self->nextthink = level.time + HUMAN_DETONATION_DELAY;
  }
  else
  {
    self->think = HSpawn_Disappear;
    self->nextthink = level.time; //blast immediately
  }

  G_LogDestruction( self, attacker, mod );
}

/*
================
HRepeater_Think

Think for human power repeater
================
*/
void HRepeater_Think( gentity_t *self )
{
  int               i;
  qboolean          reactor = qfalse;
  gentity_t         *ent;

  if( self->spawned )
  {
    // Iterate through entities
    for ( i = 1, ent = g_entities + i; i < level.num_entities; i++, ent++ )
    {
      if( ent->s.eType != ET_BUILDABLE )
        continue;

      if( ent->s.modelindex == BA_H_REACTOR && ent->spawned && ent->health > 0 )
        reactor = qtrue;
    }
  }

  if( G_InPowerZone( self ) )
  {
    // if the repeater is inside of another power zone then disappear
    G_Damage( self, NULL, NULL, NULL, NULL, self->health, 0, MOD_SUICIDE );
  }

  G_IdlePowerState( self );

  self->powered = reactor;

  G_OC_DefaultHumanPowered();

  self->nextthink = level.time + POWER_REFRESH_TIME;
}

/*
================
HRepeater_Use

Use for human power repeater
================
*/
void HRepeater_Use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
  if( self->health <= 0 || !self->spawned  )
    return;

  if( other && other->client )
    G_GiveClientMaxAmmo( other, qtrue );
}

/*
================
HSpawn_Use

Use for human spawn
================
*/
void HSpawn_Use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
  gentity_t *dest;
  vec3_t spawn_origin, spawn_angles;

  if( !G_OC_Teleport() )
    return;

  if( self->health <= 0 )
    return;

  if( other->flags & FL_NOTARGET )
    return;

  if( !self->spawned )
    return;

  //if( !self->powered )
    //return;

  if( other && ( dest = G_SelectHumanSpawnPoint( self->s.origin, 0, self->groupID, self ) ) )
  {
    VectorCopy( dest->s.origin, spawn_origin );
    if( !other->client->pers.autoAngleDisabled )
      VectorCopy( dest->s.angles, spawn_angles );
    else
      VectorCopy( other->s.angles, spawn_angles );
    if( G_CheckSpawnPoint( dest->s.number, dest->s.origin, dest->s.origin2, BA_H_SPAWN, spawn_origin ) == NULL )
    {
      TeleportPlayer( other, spawn_origin, spawn_angles );
      VectorScale( other->client->ps.velocity, 0.0, other->client->ps.velocity );
    }
  }
}

/*
================
HReactor_Think

Think function for Human Reactor
================
*/
void HReactor_Think( gentity_t *self )
{
  int       entityList[ MAX_GENTITIES ];
  vec3_t    range = { REACTOR_ATTACK_RANGE,
                      REACTOR_ATTACK_RANGE,
                      REACTOR_ATTACK_RANGE };
  vec3_t    dccrange = { REACTOR_ATTACK_DCC_RANGE,
                         REACTOR_ATTACK_DCC_RANGE,
                         REACTOR_ATTACK_DCC_RANGE };
  vec3_t    mins, maxs;
  int       i, num;
  gentity_t *enemy, *tent;

  self->powered = qtrue;

  G_OC_ReactorPowered();

  if( self->dcc )
  {
    VectorAdd( self->s.origin, dccrange, maxs );
    VectorSubtract( self->s.origin, dccrange, mins );
  }
  else
  {
    VectorAdd( self->s.origin, range, maxs );
    VectorSubtract( self->s.origin, range, mins );
  }

  if( self->spawned && ( self->health > 0 ) && self->powered )
  {
    qboolean fired = qfalse;

    // Creates a tesla trail for every target
    num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
    for( i = 0; i < num; i++ )
    {
      enemy = &g_entities[ entityList[ i ] ];
      if( !enemy->client ||
          enemy->client->ps.stats[ STAT_TEAM ] != TEAM_ALIENS )
        continue;

      if( enemy->flags & FL_NOTARGET )
        continue;

      tent = G_TempEntity( enemy->s.pos.trBase, EV_TESLATRAIL );
      tent->s.generic1 = self->s.number; //src
      tent->s.clientNum = enemy->s.number; //dest
      VectorCopy( self->s.pos.trBase, tent->s.origin2 );
      fired = qtrue;
    }

    // Actual damage is done by radius
    if( fired )
    {
      self->timestamp = level.time;
      if( self->dcc )
        G_SelectiveRadiusDamage( self->s.pos.trBase, self,
                                 REACTOR_ATTACK_DCC_DAMAGE,
                                 REACTOR_ATTACK_DCC_RANGE, self,
                                 MOD_REACTOR, TEAM_HUMANS );
      else
        G_SelectiveRadiusDamage( self->s.pos.trBase, self,
                                 REACTOR_ATTACK_DAMAGE,
                                 REACTOR_ATTACK_RANGE, self,
                                 MOD_REACTOR, TEAM_HUMANS );
    }
  }

  if( self->dcc )
    self->nextthink = level.time + REACTOR_ATTACK_DCC_REPEAT;
  else
    self->nextthink = level.time + REACTOR_ATTACK_REPEAT;
}

//==================================================================================



/*
================
HArmoury_Activate

Called when a human activates an Armoury
================
*/
void HArmoury_Activate( gentity_t *self, gentity_t *other, gentity_t *activator )
{
  if( self->spawned )
  {
    G_OC_ArmouryUsed(activator, self);

    //only humans can activate this
    if( activator->client->ps.stats[ STAT_TEAM ] != TEAM_HUMANS )
      return;

    //if this is powered then call the armoury menu
    if( self->powered )
      G_TriggerMenu( activator->client->ps.clientNum, MN_H_ARMOURY );
    else
      G_TriggerMenu( activator->client->ps.clientNum, MN_H_NOTPOWERED );
  }
}

/*
================
HArmoury_Think

Think for armoury
================
*/
void HArmoury_Think( gentity_t *self )
{
  //make sure we have power
  self->nextthink = level.time + POWER_REFRESH_TIME + G_OC_HumanBuildableOptimizedThinkTime();

  self->powered = G_FindProvider( self ) && G_Reactor( );
  G_OC_DefaultHumanPowered();

  G_OC_BONUS_BUILDABLE_THINK();  // before return but after OC power functions

  G_SuicideIfNoPower( self );
}




//==================================================================================





/*
================
HDCC_Think

Think for dcc
================
*/
void HDCC_Think( gentity_t *self )
{
  //make sure we have power
  self->nextthink = level.time + POWER_REFRESH_TIME + G_OC_HumanBuildableOptimizedThinkTime();

  self->powered = G_FindProvider( self ) && G_Reactor( );
  G_OC_DefaultHumanPowered();

  G_SuicideIfNoPower( self );
}


//==================================================================================


/*
================
Domination_Think

Domination points broadcast all changes.
The powered and mark bits are hijacked to represent captured state and capture
team respectively.
================
*/
void Domination_Think( gentity_t *self )
{
  vec3_t range = { DOMINATION_RANGE, DOMINATION_RANGE, DOMINATION_RANGE },
         mins, maxs, dir;
  int i, num, think_interval, players[ NUM_TEAMS ],
      client[ NUM_TEAMS ], entityList[ MAX_GENTITIES ];
  gentity_t *ent;
  float balance, distance;

  players[ TEAM_ALIENS ] = 0;
  players[ TEAM_HUMANS ] = 0;
  client[ TEAM_ALIENS ] = -1;
  client[ TEAM_HUMANS ] = -1;

  think_interval = BG_Buildable( self->s.modelindex )->nextthink;
  self->nextthink = level.time + think_interval;

  VectorAdd( self->s.origin, range, maxs );
  VectorSubtract( self->s.origin, range, mins );

  // Count all players and buildables in domination range
  balance = 0;
  num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    float  weight = 0.f;
    team_t team;

    ent = &g_entities[ entityList[ i ] ];

    // Must be alive and visible to the point
    if( ent->health <= 0 || !G_Visible( self, ent, CONTENTS_SOLID ) )
      continue;

    // Count eligible entities and record the first player for each team
    if( ent->s.eType == ET_BUILDABLE )
    {
      if( ent->spawned && ent->powered && ent->health > 0 )
      {
        team = ent->buildableTeam;
        weight = DOMINATION_TIME_BUILDABLE;
        //weight = BG_Buildable( ent->s.modelindex )->buildTime /
                 //DOMINATION_WS_BUILDABLE;
        if( team == TEAM_ALIENS )
          weight = -weight;
        else if( team != TEAM_HUMANS )
          weight = 0;
      }
      else
      {
        continue;
      }
    }
    else if( ent->s.eType == ET_PLAYER )
    {
      team = ent->client->pers.teamSelection;
      if( team == TEAM_HUMANS )
        weight = DOMINATION_TIME_HUMAN;
        //weight = BG_GetValueOfPlayer( &ent->client->ps ) /
                 //DOMINATION_WS_HUMAN;
      else if( team == TEAM_ALIENS )
        weight = -DOMINATION_TIME_ALIEN;
        //weight = -BG_Buildable( ent->client->ps.stats[ STAT_CLASS ] )->value /
                 //DOMINATION_WS_ALIEN;
      if( client[ team ] < 0 )
        client[ team ] = entityList[ i ];
    }
    else
    {
      continue;
    }

    // Square fall-off with distance
    VectorSubtract( self->s.origin, ent->s.origin, dir );
    distance = VectorLength( dir );
    if( distance >= DOMINATION_RANGE )
      continue;
    weight *= DOMINATION_RANGE_SQRT / sqrt( DOMINATION_RANGE - distance );
    if( weight < 0.1f && weight > -0.9f )
      continue;

    balance += think_interval / weight;

    players[ team ]++;
  }

  // Launch an attack
  if( self->dominationAttacking == TEAM_NONE && level.time > self->timestamp )
  {
    // More aliens than humans = alien attack
    if( self->dominationTeam != TEAM_ALIENS && balance < 0 )
    {
      self->dominationAttacking = TEAM_ALIENS;
      self->timestamp = level.time + DOMINATION_COOLDOWN;
      //trap_SendServerCommand( -1, va( "print \"^1Aliens^7 attacking %s^7!\n\"",
                              //self->dominationName ) );
      if( self->dominationTeam == TEAM_NONE )
        self->deconstruct = qfalse;
      self->dominationClient = client[ TEAM_ALIENS ];
    }

    // More humans than aliens = human attack
    else if( self->dominationTeam != TEAM_HUMANS && balance > 0 )
    {
      self->dominationAttacking = TEAM_HUMANS;
      self->timestamp = level.time + DOMINATION_COOLDOWN;
      //trap_SendServerCommand( -1, va( "print \"^5Humans^7 attacking %s^7!\n\"",
                              //self->dominationName ) );
      if( self->dominationTeam == TEAM_NONE )
        self->deconstruct = qtrue;
      self->dominationClient = client[ TEAM_HUMANS ];
    }
  }

  // Neutral and not under attack; decrement the domination time
  if( self->dominationTeam == TEAM_NONE && !players[ TEAM_HUMANS ] &&
      !players[ TEAM_ALIENS ] )
    self->dominationTime -= 100.0 * think_interval / DOMINATION_TIME_CLEAR;

  // Claimed and not under attack; increment the domination time
  else if( ( self->dominationTeam == TEAM_HUMANS && !players[ TEAM_ALIENS ] ) ||
           ( self->dominationTeam == TEAM_ALIENS && !players[ TEAM_HUMANS ] ) )
    self->dominationTime += 100.0 * think_interval / DOMINATION_TIME_CLEAR;

  // Increment the domination timer according to the balance shift
  else if( self->dominationTeam == TEAM_HUMANS ||
           ( self->dominationTeam == TEAM_NONE &&
             self->dominationAttacking == TEAM_HUMANS ) )
      self->dominationTime += 100.0 * ((float) balance);
  else if( self->dominationTeam == TEAM_ALIENS ||
           ( self->dominationTeam == TEAM_NONE &&
             self->dominationAttacking == TEAM_ALIENS ) )
      self->dominationTime += 100.0 * ((float) -balance);

  // Domination cleared
  if( self->dominationTime <= 0 )
  {
    gentity_t *ent;

    if( self->dominationTeam != TEAM_NONE )
    {
      level.dominationPoints[ self->dominationTeam ]--;
      level.dominationPoints[ TEAM_NONE ]++;
      self->dominationTeam = TEAM_NONE;
      self->powered = qfalse;

      // We need to update all buildings depending on us for power/creep
      for( i = 0, ent = g_entities + i; i < level.num_entities; i++, ent++ )
        if( ent->parentNode == self )
          ent->parentNode = NULL;
    }
    if( !players[ self->dominationAttacking ] )
      self->dominationAttacking = TEAM_NONE;
    else
      self->deconstruct = self->dominationAttacking == TEAM_HUMANS;
    self->dominationTime = 0;

    // free build point zone
    if( self->usesBuildPointZone )
    {
      buildPointZone_t *zone = &level.buildPointZones[self->buildPointZone];

      zone->active = qfalse;
      self->usesBuildPointZone = qfalse;
    }
  }

  // Complete domination
  else if( self->dominationTime >= 100.0 )
  {
    self->dominationTime = 100.0;
    if( self->dominationTeam == TEAM_NONE ) {
      self->powered = qtrue;
      if( self->dominationAttacking == TEAM_ALIENS )
      {
        self->deconstruct = qfalse;
        //trap_SendServerCommand( -1, va( "print \"^1Aliens^7 dominate %s^7!\n\"",
                                //self->dominationName ) );
        if( self->dominationClient >= 0 )
          G_AddCreditToClient( g_entities[ self->dominationClient ].client,
                               DOMINATION_FREEKILL_ALIEN, qtrue );
      }
      else if( self->dominationAttacking == TEAM_HUMANS )
      {
        self->deconstruct = qtrue;
        //trap_SendServerCommand( -1, va( "print \"^5Humans^7 dominate %s^7!\n\"",
                              //self->dominationName ) );
        if( self->dominationClient >= 0 )
          G_AddCreditToClient( g_entities[ self->dominationClient ].client,
                               DOMINATION_FREEKILL_HUMAN, qtrue );
      }
      level.dominationPoints[ self->dominationTeam ]--;
      level.dominationPoints[ self->dominationAttacking ]++;
      self->dominationTeam = self->dominationAttacking;
    }
    self->dominationAttacking = TEAM_NONE;
  }

  // Use health to transmit domination progress
  self->health = DOMINATION_HEALTH * self->dominationTime / 100.0;
}

void Domination_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod )
{
  level.dominationPoints[ self->dominationTeam ]--;
}


//==================================================================================




/*
================
HMedistat_Die

Die function for Human Medistation
================
*/
void HMedistat_Die( gentity_t *self, gentity_t *inflictor,
                    gentity_t *attacker, int damage, int mod )
{
  //clear target's healing flag
  if( self->enemy && self->enemy->client )
    self->enemy->client->ps.stats[ STAT_STATE ] &= ~SS_HEALING_ACTIVE;

  HSpawn_Die( self, inflictor, attacker, damage, mod );
}

/*
================
HMedistat_Think

think function for Human Medistation
================
*/
void HMedistat_Think( gentity_t *self )
{
  int       entityList[ MAX_GENTITIES ];
  vec3_t    mins, maxs;
  int       i, num;
  gentity_t *player;
  qboolean  occupied = qfalse;

  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink;

  self->powered = G_FindProvider( self ) && G_Reactor( );
  G_OC_DefaultHumanPowered();
  G_OC_BONUS_BUILDABLE_THINK();  // before return but after OC power functions

  G_SuicideIfNoPower( self );
  G_IdlePowerState( self );

  //clear target's healing flag
  if( self->enemy && self->enemy->client )
    self->enemy->client->ps.stats[ STAT_STATE ] &= ~SS_HEALING_ACTIVE;

  //make sure we have power
  if( !self->powered )
  {
    if( self->active )
    {
      G_SetBuildableAnim( self, BANIM_CONSTRUCT2, qtrue );
      G_SetIdleBuildableAnim( self, BANIM_IDLE1 );
      self->active = qfalse;
      self->enemy = NULL;
    }

    self->nextthink = level.time + POWER_REFRESH_TIME;
    return;
  }

  if( self->spawned )
  {
    VectorAdd( self->s.origin, self->r.maxs, maxs );
    VectorAdd( self->s.origin, self->r.mins, mins );

    mins[ 2 ] += fabs( self->r.mins[ 2 ] ) + self->r.maxs[ 2 ];
    maxs[ 2 ] += 60; //player height

    //if active use the healing idle
    if( self->active )
      G_SetIdleBuildableAnim( self, BANIM_IDLE2 );
      
    //check if a previous occupier is still here
    num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
    for( i = 0; i < num; i++ )
    {
      player = &g_entities[ entityList[ i ] ];

      if( player->flags & FL_NOTARGET )
        continue;

      //remove poison from everyone, not just the healed player
      if( player->client && player->client->ps.stats[ STAT_STATE ] & SS_POISONED )
        player->client->ps.stats[ STAT_STATE ] &= ~SS_POISONED;
      if( player->client )
        G_OC_MediUsed( player, self );

      if( self->enemy == player && player->client &&
          player->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS &&
          player->health < player->client->ps.stats[ STAT_MAX_HEALTH ] &&
          PM_Live( player->client->ps.pm_type ) )
      {
        occupied = qtrue;
        player->client->ps.stats[ STAT_STATE ] |= SS_HEALING_ACTIVE;
      }
    }

    if( !occupied )
    {
      self->enemy = NULL;

      //look for something to heal
      for( i = 0; i < num; i++ )
      {
        player = &g_entities[ entityList[ i ] ];

        if( player->flags & FL_NOTARGET )
          continue;

        if( player->client && player->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS )
        {
          if( ( player->health < player->client->ps.stats[ STAT_MAX_HEALTH ] ||
                player->client->ps.stats[ STAT_STAMINA ] < STAMINA_MAX ) &&
              PM_Live( player->client->ps.pm_type ) )
          {
            self->enemy = player;

            //start the heal anim
            if( !self->active )
            {
              G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
              self->active = qtrue;
              player->client->ps.stats[ STAT_STATE ] |= SS_HEALING_ACTIVE;
            }
          }
          else if( !BG_InventoryContainsUpgrade( UP_MEDKIT, player->client->ps.stats ) )
            BG_AddUpgradeToInventory( UP_MEDKIT, player->client->ps.stats );
        }
      }
    }

    //nothing left to heal so go back to idling
    if( !self->enemy && self->active )
    {
      G_SetBuildableAnim( self, BANIM_CONSTRUCT2, qtrue );
      G_SetIdleBuildableAnim( self, BANIM_IDLE1 );

      self->active = qfalse;
    }
    else if( self->enemy && self->enemy->client ) //heal!
    {
      if( self->enemy->client->ps.stats[ STAT_STAMINA ] <  STAMINA_MAX )
        self->enemy->client->ps.stats[ STAT_STAMINA ] += STAMINA_MEDISTAT_RESTORE;

      if( self->enemy->client->ps.stats[ STAT_STAMINA ] > STAMINA_MAX )
        self->enemy->client->ps.stats[ STAT_STAMINA ] = STAMINA_MAX;

      self->enemy->health++;

      //if they're completely healed, give them a medkit
      if( self->enemy->health >= self->enemy->client->ps.stats[ STAT_MAX_HEALTH ] )
      {
        self->enemy->health =  self->enemy->client->ps.stats[ STAT_MAX_HEALTH ];
        if( !BG_InventoryContainsUpgrade( UP_MEDKIT, self->enemy->client->ps.stats ) )
          BG_AddUpgradeToInventory( UP_MEDKIT, self->enemy->client->ps.stats );
      }
    }
  }
}




//==================================================================================




/*
================
HMGTurret_CheckTarget

Used by HMGTurret_Think to check enemies for validity
================
*/
qboolean HMGTurret_CheckTarget( gentity_t *self, gentity_t *target,
                                qboolean los_check )
{
  trace_t   tr;
  vec3_t    dir, end;

  if( !target || target->health <= 0 || !target->client ||
      target->client->pers.teamSelection != TEAM_ALIENS ||
      ( target->client->ps.stats[ STAT_STATE ] & SS_HOVELING ) )
    return qfalse;
    
  if( !los_check )
    return qtrue;

  // Accept target if we can line-trace to it
  VectorSubtract( target->s.pos.trBase, self->s.pos.trBase, dir );
  VectorNormalize( dir );
  VectorMA( self->s.pos.trBase, MGTURRET_RANGE, dir, end );
  trap_Trace( &tr, self->s.pos.trBase, NULL, NULL, end,
              self->s.number, BG_OC_SHOTMASK );
  return tr.entityNum == target - g_entities;
}


/*
================
HMGTurret_TrackEnemy

Used by HMGTurret_Think to track enemy location
================
*/
qboolean HMGTurret_TrackEnemy( gentity_t *self )
{
  vec3_t  dirToTarget, dttAdjusted, angleToTarget, angularDiff, xNormal;
  vec3_t  refNormal = { 0.0f, 0.0f, 1.0f };
  float   temp, rotAngle;

  VectorSubtract( self->enemy->s.pos.trBase, self->s.pos.trBase, dirToTarget );
  VectorNormalize( dirToTarget );

  CrossProduct( self->s.origin2, refNormal, xNormal );
  VectorNormalize( xNormal );
  rotAngle = RAD2DEG( acos( DotProduct( self->s.origin2, refNormal ) ) );
  RotatePointAroundVector( dttAdjusted, xNormal, dirToTarget, rotAngle );

  vectoangles( dttAdjusted, angleToTarget );

  angularDiff[ PITCH ] = AngleSubtract( self->s.angles2[ PITCH ], angleToTarget[ PITCH ] );
  angularDiff[ YAW ] = AngleSubtract( self->s.angles2[ YAW ], angleToTarget[ YAW ] );

  //if not pointing at our target then move accordingly
  if( angularDiff[ PITCH ] < 0 && angularDiff[ PITCH ] < (-MGTURRET_ANGULARSPEED) )
    self->s.angles2[ PITCH ] += MGTURRET_ANGULARSPEED;
  else if( angularDiff[ PITCH ] > 0 && angularDiff[ PITCH ] > MGTURRET_ANGULARSPEED )
    self->s.angles2[ PITCH ] -= MGTURRET_ANGULARSPEED;
  else
    self->s.angles2[ PITCH ] = angleToTarget[ PITCH ];

  //disallow vertical movement past a certain limit
  temp = fabs( self->s.angles2[ PITCH ] );
  if( temp > 180 )
    temp -= 360;

  if( temp < -MGTURRET_VERTICALCAP )
    self->s.angles2[ PITCH ] = (-360) + MGTURRET_VERTICALCAP;

  //if not pointing at our target then move accordingly
  if( angularDiff[ YAW ] < 0 && angularDiff[ YAW ] < ( -MGTURRET_ANGULARSPEED ) )
    self->s.angles2[ YAW ] += MGTURRET_ANGULARSPEED;
  else if( angularDiff[ YAW ] > 0 && angularDiff[ YAW ] > MGTURRET_ANGULARSPEED )
    self->s.angles2[ YAW ] -= MGTURRET_ANGULARSPEED;
  else
    self->s.angles2[ YAW ] = angleToTarget[ YAW ];

  AngleVectors( self->s.angles2, dttAdjusted, NULL, NULL );
  RotatePointAroundVector( dirToTarget, xNormal, dttAdjusted, -rotAngle );
  vectoangles( dirToTarget, self->turretAim );

  //fire if target is within accuracy
  return ( abs( angularDiff[ YAW ] ) - MGTURRET_ANGULARSPEED <=
           MGTURRET_ACCURACY_TO_FIRE ) &&
         ( abs( angularDiff[ PITCH ] ) - MGTURRET_ANGULARSPEED <=
           MGTURRET_ACCURACY_TO_FIRE );
}


/*
================
HMGTurret_FindEnemy

Used by HMGTurret_Think to locate enemy gentities
================
*/
void HMGTurret_FindEnemy( gentity_t *self )
{
  int       entityList[ MAX_GENTITIES ];
  vec3_t    range;
  vec3_t    mins, maxs;
  int       i, num;
  gentity_t *target;
  int       start;

  if( self->enemy )
    self->enemy->targeted = NULL;

  self->enemy = NULL;
    
  // Look for targets in a box around the turret
  VectorSet( range, MGTURRET_RANGE, MGTURRET_RANGE, MGTURRET_RANGE );
  VectorAdd( self->s.origin, range, maxs );
  VectorSubtract( self->s.origin, range, mins );
  num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

  if( num == 0 )
    return;

  start = rand( ) % num;
  for( i = start; i < num + start ; i++ )
  {
    target = &g_entities[ entityList[ i % num ] ];
    if( !HMGTurret_CheckTarget( self, target, qtrue ) )
      continue;
    if( target->flags & FL_NOTARGET )
      continue;

    self->enemy = target;
    self->enemy->targeted = self;
    return;
  }
}

/*
================
HMGTurret_State

Raise or lower MG turret towards desired state
================
*/
enum {
  MGT_STATE_INACTIVE,
  MGT_STATE_DROP,
  MGT_STATE_RISE,
  MGT_STATE_ACTIVE
};

static qboolean HMGTurret_State( gentity_t *self, int state )
{
  float angle;

  if( self->waterlevel == state )
    return qfalse;

  angle = AngleNormalize180( self->s.angles2[ PITCH ] );

  if( state == MGT_STATE_INACTIVE )
  {
    if( angle < MGTURRET_VERTICALCAP )
    {
      if( self->waterlevel != MGT_STATE_DROP )
      {
        self->speed = 0.25f;
        self->waterlevel = MGT_STATE_DROP;
      }
      else
        self->speed *= 1.25f;

      self->s.angles2[ PITCH ] = 
        MIN( MGTURRET_VERTICALCAP, angle + self->speed );
      return qtrue;
    }
    else
      self->waterlevel = MGT_STATE_INACTIVE;
  }
  else if( state == MGT_STATE_ACTIVE )
  {
    if( !self->enemy && angle > 0.0f )
    {
      self->waterlevel = MGT_STATE_RISE;
      self->s.angles2[ PITCH ] =
        MAX( 0.0f, angle - MGTURRET_ANGULARSPEED * 0.5f );
    }
    else
      self->waterlevel = MGT_STATE_ACTIVE;
  }

  return qfalse;
}

/*
================
HMGTurret_Think

Think function for MG turret
================
*/
void HMGTurret_Think( gentity_t *self )
{
  self->nextthink = level.time + 
                    BG_Buildable( self->s.modelindex )->nextthink + G_OC_HumanBuildableOptimizedThinkTime();

  // Turn off client side muzzle flashes
  self->s.eFlags &= ~EF_FIRING;

  self->powered = G_FindProvider( self ) && G_Reactor( );
  G_OC_DefaultHumanPowered();

  G_SuicideIfNoPower( self );
  G_IdlePowerState( self );

  // If not powered or spawned don't do anything
  if( !self->powered )
  {
    // if power loss drop turret
    if( self->spawned &&
        HMGTurret_State( self, MGT_STATE_INACTIVE ) && !G_OC_NoTurretDroop() )
      return;

    self->nextthink = level.time + POWER_REFRESH_TIME;
    return;
  }
  if( !self->spawned )
    return;
    
  // If the current target is not valid find a new enemy
  if( !HMGTurret_CheckTarget( self, self->enemy, qtrue ) )
  {
    self->active = qfalse;
    self->turretSpinupTime = -1;
    HMGTurret_FindEnemy( self );
  }
  // if newly powered raise turret
  HMGTurret_State( self, MGT_STATE_ACTIVE );
  if( !self->enemy )
    return;

  // Track until we can hit the target
  if( !HMGTurret_TrackEnemy( self ) )
  {
    self->active = qfalse;
    self->turretSpinupTime = -1;
    return;
  }

  // Update spin state
  if( !self->active && self->timestamp < level.time )
  {
    self->active = qtrue;

    self->turretSpinupTime = level.time + MGTURRET_SPINUP_TIME;
    G_AddEvent( self, EV_MGTURRET_SPINUP, 0 );
  }

  // Not firing or haven't spun up yet
  if( !self->active || self->turretSpinupTime > level.time )
    return;
    
  // Fire repeat delay
  if( self->timestamp > level.time )
    return;

  FireWeapon( self );
  self->s.eFlags |= EF_FIRING;
  self->timestamp = level.time + BG_Buildable( self->s.modelindex )->turretFireSpeed;
  G_AddEvent( self, EV_FIRE_WEAPON, 0 );
  G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );
}




//==================================================================================




/*
================
HTeslaGen_Think

Think function for Tesla Generator
================
*/
void HTeslaGen_Think( gentity_t *self )
{
  self->nextthink = level.time + BG_Buildable( self->s.modelindex )->nextthink + G_OC_HumanBuildableOptimizedThinkTime();

  G_SuicideIfNoPower( self );
  G_IdlePowerState( self );

  //if not powered don't do anything and check again for power next think
  self->powered = G_FindProvider( self ) && G_Reactor( );
  G_OC_DefaultHumanPowered();
  if( !self->powered )
  {
    self->s.eFlags &= ~EF_FIRING;
    self->nextthink = level.time + POWER_REFRESH_TIME;
    return;
  }

  if( self->spawned && self->timestamp < level.time )
  {
    vec3_t range, mins, maxs;
    int entityList[ MAX_GENTITIES ], i, num;

    // Communicates firing state to client
    self->s.eFlags &= ~EF_FIRING;

    VectorSet( range, TESLAGEN_RANGE, TESLAGEN_RANGE, TESLAGEN_RANGE );
    VectorAdd( self->s.origin, range, maxs );
    VectorSubtract( self->s.origin, range, mins );

    // Attack nearby Aliens
    num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
    for( i = 0; i < num; i++ )
    {
      self->enemy = &g_entities[ entityList[ i ] ];
      if( self->enemy->flags & FL_NOTARGET )
        continue;
      if( self->enemy->client && self->enemy->health > 0 &&
          self->enemy->client->ps.stats[ STAT_TEAM ] == TEAM_ALIENS &&
          Distance( self->enemy->s.pos.trBase,
                    self->s.pos.trBase ) <= TESLAGEN_RANGE )
        FireWeapon( self );
    }
    self->enemy = NULL;

    if( self->s.eFlags & EF_FIRING )
    {
      G_AddEvent( self, EV_FIRE_WEAPON, 0 );

      //doesn't really need an anim
      //G_SetBuildableAnim( self, BANIM_ATTACK1, qfalse );

      self->timestamp = level.time + TESLAGEN_REPEAT;
    }
  }
}




//==================================================================================




/*
============
G_QueueBuildPoints
============
*/
void G_QueueBuildPoints( gentity_t *self )
{
  gentity_t *killer = NULL;
  gentity_t *powerEntity;

  if( self->killedBy != ENTITYNUM_NONE )
    killer = &g_entities[ self->killedBy ];

  if( killer && killer->client &&
      killer->client->ps.stats[ STAT_TEAM ] == self->buildableTeam )
  {
    // Don't take away build points if killed by a teammate
    // deconstructing an egg/overmind (MOD_NOCREEP)
    return;
  }
      
  switch( self->buildableTeam )
  {
    default:
    case TEAM_NONE:
      return;

    case TEAM_ALIENS:
      if( !level.alienBuildPointQueue )
        level.alienNextQueueTime = level.time + g_alienBuildQueueTime.integer;

      level.alienBuildPointQueue +=
          BG_Buildable( self->s.modelindex )->buildPoints;
      break;

    case TEAM_HUMANS:
      powerEntity = G_ProvidingEntityForEntity( self );

      if( powerEntity )
      {
        int nqt;
        int testBuildable = powerEntity->s.modelindex;

        if( BG_IsDPoint( testBuildable ) )
          testBuildable = BA_H_REPEATER;

        switch( testBuildable )
        {
          case BA_H_REACTOR:
            nqt = G_NextQueueTime( level.humanBuildPointQueue,
                                   g_humanBuildPoints.integer,
                                   g_humanBuildQueueTime.integer );
            if( !level.humanBuildPointQueue ||
                level.time + nqt < level.humanNextQueueTime )
              level.humanNextQueueTime = level.time + nqt;

            level.humanBuildPointQueue +=
                BG_Buildable( self->s.modelindex )->buildPoints;
            break;

          case BA_H_REPEATER:
            if( powerEntity->usesBuildPointZone &&
                level.buildPointZones[ powerEntity->buildPointZone ].active )
            {
              buildPointZone_t *zone = &level.buildPointZones[ powerEntity->buildPointZone ];

              nqt = G_NextQueueTime( zone->queuedBuildPoints,
                                     zone->totalBuildPoints,
                                     g_zoneBuildQueueTime.integer );

              if( !zone->queuedBuildPoints ||
                  level.time + nqt < zone->nextQueueTime )
                zone->nextQueueTime = level.time + nqt;

              zone->queuedBuildPoints +=
                BG_Buildable( self->s.modelindex )->buildPoints;
            }
            break;

          default:
            break;
        }
      }
  }
}

/*
============
G_NextQueueTime
============
*/
int G_NextQueueTime( int queuedBP, int totalBP, int queueBaseRate )
{
  float fractionQueued;

  if( totalBP == 0 )
    return 0;

  fractionQueued = queuedBP / (float)totalBP;
  return ( 1.0f - fractionQueued ) * queueBaseRate;
}

/*
============
G_BuildableTouchTriggers

Find all trigger entities that a buildable touches.
============
*/
void G_BuildableTouchTriggers( gentity_t *ent )
{
  int       i, num;
  int       touch[ MAX_GENTITIES ];
  gentity_t *hit;
  trace_t   trace;
  vec3_t    mins, maxs;
  vec3_t    bmins, bmaxs;
  static    vec3_t range = { 10, 10, 10 };

  // dead buildables don't activate triggers!
  if( ent->health <= 0 )
    return;

  BG_BuildableBoundingBox( ent->s.modelindex, bmins, bmaxs );

  VectorAdd( ent->s.origin, bmins, mins );
  VectorAdd( ent->s.origin, bmaxs, maxs );

  VectorSubtract( mins, range, mins );
  VectorAdd( maxs, range, maxs );

  num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

  VectorAdd( ent->s.origin, bmins, mins );
  VectorAdd( ent->s.origin, bmaxs, maxs );

  for( i = 0; i < num; i++ )
  {
    hit = &g_entities[ touch[ i ] ];

    if( !hit->touch )
      continue;

    if( !( hit->r.contents & CONTENTS_TRIGGER ) )
      continue;

    //ignore buildables not yet spawned
    if( !ent->spawned )
      continue;

    if( !trap_EntityContact( mins, maxs, hit ) )
      continue;

    memset( &trace, 0, sizeof( trace ) );

    if( hit->touch )
      hit->touch( hit, ent, &trace );
  }
}


/*
===============
G_BuildableThink

General think function for buildables
===============
*/
void G_BuildableThink( gentity_t *ent, int msec )
{
  int i;
  int maxHealth = BG_Buildable( ent->s.modelindex )->health;
  int regenRate = BG_Buildable( ent->s.modelindex )->regenRate;
  int buildTime = BG_Buildable( ent->s.modelindex )->buildTime;
  buildPointZone_t *zone;

  //toggle spawned flag for buildables
  if( !ent->spawned && ent->health > 0 )
  {
    if( ent->buildTime + buildTime < level.time )
      ent->spawned = qtrue;
  }

  // Timer actions
  ent->time1000 += msec;
  if( ent->time1000 >= 1000 )
  {
    ent->time1000 -= 1000;

    if( !ent->spawned && ent->health > 0 )
      ent->health += (int)( ceil( (float)maxHealth / (float)( buildTime * 0.001f ) ) );
    else if( ent->health > 0 && ent->health < maxHealth )
    {
      if( ent->buildableTeam == TEAM_ALIENS && regenRate &&
        ( ent->lastDamageTime + ALIEN_REGEN_DAMAGE_TIME ) < level.time )
      {
        ent->health += regenRate;
      }
      else if( ent->buildableTeam == TEAM_HUMANS && ent->dcc &&
        ( ent->lastDamageTime + HUMAN_REGEN_DAMAGE_TIME ) < level.time )
      {
        ent->health += DC_HEALRATE * ent->dcc;
      }
    }

    if( ent->health >= maxHealth )
    {
      int i;
      ent->health = maxHealth;
      for( i = 0; i < MAX_CLIENTS; i++ )
        ent->credits[ i ] = 0;
    }
  }

  if( ent->clientSpawnTime > 0 )
    ent->clientSpawnTime -= msec;

  if( ent->clientSpawnTime < 0 )
    ent->clientSpawnTime = 0;

  ent->dcc = ( ent->buildableTeam != TEAM_HUMANS ) ? 0 : G_FindDCC( ent );

  // Set health
  ent->s.generic1 = MAX( ent->health, 0 );

  // Set flags
  ent->s.eFlags &= ~( EF_B_POWERED | EF_B_SPAWNED | EF_B_MARKED );
  if( ent->powered )
    ent->s.eFlags |= EF_B_POWERED;

  if( ent->spawned )
    ent->s.eFlags |= EF_B_SPAWNED;

  if( ent->deconstruct )
    ent->s.eFlags |= EF_B_MARKED;

  // Check if this buildable is touching any triggers
  G_BuildableTouchTriggers( ent );

  // Fall back on normal physics routines
  G_Physics( ent, msec );

  // Initialise zone once spawned
  if( BG_Buildable( ent->s.modelindex )->zone && ent->spawned && ( !ent->usesBuildPointZone || !level.buildPointZones[ ent->buildPointZone ].active ) )
  {
    // See if a free zone exists
    for( i = 0; i < g_zoneMax.integer; i++ )
    {
      zone = &level.buildPointZones[ i ];

      if( !zone->active && ( !BG_IsDPoint( ent->s.modelindex ) || ent->dominationTeam != TEAM_NONE ) )
      {
        // Initialise the BP queue with all BP queued
        zone->queuedBuildPoints = zone->totalBuildPoints = g_zoneBuildPoints.integer;
        zone->nextQueueTime = level.time;
        zone->team = ent->buildableTeam;
        // special case for domination points
        if( BG_IsDPoint( ent->s.modelindex ) )
          zone->team = ent->dominationTeam;
        zone->active = qtrue;

        ent->buildPointZone = zone - level.buildPointZones;
        ent->usesBuildPointZone = qtrue;

        break;
      }
    }
  }
}


/*
===============
G_BuildableDie

Generic die function for buildables.  This is called for buildables before their die function.
===============
*/
void G_BuildableDie( gentity_t *ent )
{
  // free build point zone
  if( ent->usesBuildPointZone )
  {
    buildPointZone_t *zone = &level.buildPointZones[ent->buildPointZone];

    zone->active = qfalse;
    ent->usesBuildPointZone = qfalse;
  }
}


/*
===============
G_BuildableRange

Check whether a point is within some range of a type of buildable
===============
*/
gentity_t *G_BuildableRange( vec3_t origin, float r, buildable_t buildable )
{
  int       entityList[ MAX_GENTITIES ];
  vec3_t    range;
  vec3_t    mins, maxs;
  int       i, num;
  gentity_t *ent;

  VectorSet( range, r, r, r );
  VectorAdd( origin, range, maxs );
  VectorSubtract( origin, range, mins );

  num = trap_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    ent = &g_entities[ entityList[ i ] ];

    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( ent->buildableTeam == TEAM_HUMANS && !ent->powered )
      continue;

    if( ent->s.modelindex == buildable && ent->spawned )
      return ent;
  }

  return qfalse;
}

/*
================
G_FindBuildable

Finds a buildable of the specified type
================
*/
static gentity_t *G_FindBuildable( buildable_t buildable )
{
  int       i;
  gentity_t *ent;

  for( i = MAX_CLIENTS, ent = g_entities + i;
       i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( ent->s.modelindex == buildable && !( ent->s.eFlags & EF_DEAD ) )
      return ent;
  }

  return NULL;
}

/*
===============
G_BuildablesIntersect

Test if two buildables intersect each other
===============
*/
static qboolean G_BuildablesIntersect( buildable_t a, vec3_t originA,
                                       buildable_t b, vec3_t originB )
{
  vec3_t minsA, maxsA;
  vec3_t minsB, maxsB;

  BG_BuildableBoundingBox( a, minsA, maxsA );
  VectorAdd( minsA, originA, minsA );
  VectorAdd( maxsA, originA, maxsA );

  BG_BuildableBoundingBox( b, minsB, maxsB );
  VectorAdd( minsB, originB, minsB );
  VectorAdd( maxsB, originB, maxsB );

  return BoundsIntersect( minsA, maxsA, minsB, maxsB );
}

/*
===============
G_CompareBuildablesForRemoval

qsort comparison function for a buildable removal list
===============
*/
static buildable_t  cmpBuildable;
static vec3_t       cmpOrigin;
static int G_CompareBuildablesForRemoval( const void *a, const void *b )
{
  int       precedence[ ] =
  {
    BA_NONE,

    BA_A_BARRICADE,
    BA_A_ACIDTUBE,
    BA_A_TRAPPER,
    BA_A_HIVE,
    BA_A_BOOSTER,
    BA_A_HOVEL,
    BA_A_SPAWN,
    BA_A_OVERMIND,

    BA_H_MGTURRET,
    BA_H_TESLAGEN,
    BA_H_DCC,
    BA_H_MEDISTAT,
    BA_H_ARMOURY,
    BA_H_SPAWN,
    BA_H_REPEATER,
    BA_H_REACTOR
  };

  gentity_t *buildableA, *buildableB;
  int       i;
  int       aPrecedence = 0, bPrecedence = 0;
  qboolean  aMatches = qfalse, bMatches = qfalse;

  buildableA = *(gentity_t **)a;
  buildableB = *(gentity_t **)b;

  if( buildableA->s.eType != ET_BUILDABLE )
    return 1;
  if( buildableB->s.eType != ET_BUILDABLE )
    return -1;

  // Prefer the one that collides with the thing we're building
  aMatches = G_BuildablesIntersect( cmpBuildable, cmpOrigin,
      buildableA->s.modelindex, buildableA->s.origin );
  bMatches = G_BuildablesIntersect( cmpBuildable, cmpOrigin,
      buildableB->s.modelindex, buildableB->s.origin );
  if( aMatches && !bMatches )
    return -1;
  else if( !aMatches && bMatches )
    return 1;

  // If one matches the thing we're building, prefer it
  aMatches = ( buildableA->s.modelindex == cmpBuildable );
  bMatches = ( buildableB->s.modelindex == cmpBuildable );
  if( aMatches && !bMatches )
    return -1;
  else if( !aMatches && bMatches )
    return 1;

  // They're the same type
  if( buildableA->s.modelindex == buildableB->s.modelindex )
  {
    gentity_t *powerEntity = G_ProvidingEntityForPoint( cmpOrigin, buildableA->buildableTeam );

    // Prefer the entity that is providing power for this point
    aMatches = ( powerEntity == buildableA );
    bMatches = ( powerEntity == buildableB );
    if( aMatches && !bMatches )
      return -1;
    else if( !aMatches && bMatches )
      return 1;

    // Pick the one marked earliest
    return buildableA->deconstructTime - buildableB->deconstructTime;
  }

  // Resort to preference list
  for( i = 0; i < sizeof( precedence ) / sizeof( precedence[ 0 ] ); i++ )
  {
    if( buildableA->s.modelindex == precedence[ i ] )
      aPrecedence = i;

    if( buildableB->s.modelindex == precedence[ i ] )
      bPrecedence = i;
  }

  return aPrecedence - bPrecedence;
}

/*
===============
G_ClearDeconMarks

Remove decon mark from all buildables
===============
*/
void G_ClearDeconMarks( void )
{
  int       i;
  gentity_t *ent;

  for( i = MAX_CLIENTS, ent = g_entities + i ; i < level.num_entities ; i++, ent++ )
  {
    if( !ent->inuse )
      continue;

    if( ent->s.eType != ET_BUILDABLE )
      continue;

    // Domination points use deconstruct flag to pass team; don't mess with it
    if( BG_IsDPoint( ent->s.modelindex ) )
      continue;

    ent->deconstruct = qfalse;
  }
}

/*
===============
G_FreeMarkedBuildables

Free up build points for a team by deconstructing marked buildables
===============
*/
void G_FreeMarkedBuildables( gentity_t *deconner, char *readable, int rsize,
  char *nums, int nsize )
{
  int       i;
  int       bNum;
  int       listItems = 0;
  int       totalListItems = 0;
  gentity_t *ent;
  int       removalCounts[ BA_NUM_BUILDABLES ] = {0};

  if( readable && rsize )
    readable[ 0 ] = '\0';
  if( nums && nsize )
    nums[ 0 ] = '\0';

  if( !g_markDeconstruct.integer || G_OC_NoMarkDeconstruct() )
    return; // Not enabled, can't deconstruct anything

  for( i = 0; i < level.numBuildablesForRemoval; i++ )
  {
    ent = level.markedBuildables[ i ];
    bNum = BG_Buildable( ent->s.modelindex )->number;

    if( removalCounts[ bNum ] == 0 )
      totalListItems++;

    G_Damage( ent, NULL, NULL, NULL, NULL, ent->health, 0, MOD_DECONSTRUCT );

    removalCounts[ bNum ]++;

    if( nums )
      Q_strcat( nums, nsize, va( " %d", ent - g_entities ) );

    G_FreeEntity( ent );
  }

  if( !readable )
    return;

  for( i = 0; i < BA_NUM_BUILDABLES; i++ )
  {
    if( removalCounts[ i ] )
    {
      if( listItems )
      {
        if( listItems == ( totalListItems - 1 ) )
          Q_strcat( readable, rsize,  va( "%s and ",
            ( totalListItems > 2 ) ? "," : "" ) );
        else
          Q_strcat( readable, rsize, ", " );
      }
      Q_strcat( readable, rsize, va( "%s", BG_Buildable( i )->humanName ) );
      if( removalCounts[ i ] > 1 )
        Q_strcat( readable, rsize, va( " (%dx)", removalCounts[ i ] ) );
      listItems++;
    }
  }
}

/*
===============
G_SufficientBPAvailable

Determine if enough build points can be released for the buildable
and list the buildables that must be destroyed if this is the case
===============
*/
static itemBuildError_t G_SufficientBPAvailable( buildable_t     buildable,
                                                 vec3_t          origin )
{
  int               i;
  int               numBuildables = 0;
  int               pointsYielded = 0;
  gentity_t         *ent;
  team_t            team = BG_Buildable( buildable )->team;
  int               buildPoints = BG_Buildable( buildable )->buildPoints;
  int               remainingBP, remainingSpawns;
  qboolean          collision = qfalse;
  int               collisionCount = 0;
  qboolean          repeaterInRange = qfalse;
  int               repeaterInRangeCount = 0;
  itemBuildError_t  bpError;
  buildable_t       spawn;
  buildable_t       core;
  int               spawnCount = 0;

  level.numBuildablesForRemoval = 0;

  if( team == TEAM_ALIENS )
  {
    remainingBP     = G_GetBuildPoints( origin, team, 0 );
    remainingSpawns = level.numAlienSpawns;
    bpError         = IBE_NOALIENBP;
    spawn           = BA_A_SPAWN;
    core            = BA_A_OVERMIND;
  }
  else if( team == TEAM_HUMANS )
  {
    if( buildable == BA_H_REACTOR || buildable == BA_H_REPEATER )
      remainingBP   = level.humanBuildPoints;
    else
      remainingBP   = G_GetBuildPoints( origin, team, 0 );

    remainingSpawns = level.numHumanSpawns;
    bpError         = IBE_NOHUMANBP;
    spawn           = BA_H_SPAWN;
    core            = BA_H_REACTOR;
  }
  else if( team == TEAM_NONE )
  {
  }
  else
  {
    Com_Error( ERR_FATAL, "team is %d\n", team );
    return IBE_NONE;
  }

  // Simple non-marking case
  if( !g_markDeconstruct.integer || G_OC_NoMarkDeconstruct() )
  {
    if( remainingBP - buildPoints < 0 )
      return bpError;

    // Check for buildable<->buildable collisions
    for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
    {
      if( ent->s.eType != ET_BUILDABLE )
        continue;

      if( G_BuildablesIntersect( buildable, origin, ent->s.modelindex, ent->s.origin ) )
        return IBE_NOROOM;
    }

    return IBE_NONE;
  }

  // Set buildPoints to the number extra that are required
  buildPoints -= remainingBP;

  // Build a list of buildable entities
  for( i = MAX_CLIENTS, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    collision = G_BuildablesIntersect( buildable, origin, ent->s.modelindex, ent->s.origin );

    if( collision )
    {
      // Don't allow replacements at all
      if( g_markDeconstruct.integer == 1 && !G_OC_NoMarkDeconstruct() )
        return IBE_NOROOM;

      // Only allow replacements of the same type
      if( g_markDeconstruct.integer == 2 && ent->s.modelindex != buildable && !G_OC_NoMarkDeconstruct() )
        return IBE_NOROOM;

      // Any other setting means anything goes

      collisionCount++;
    }

    // Check if this is a repeater and it's in range
    if( buildable == BA_H_REPEATER &&
        buildable == ent->s.modelindex &&
        Distance( ent->s.origin, origin ) < REPEATER_BASESIZE )
    {
      repeaterInRange = qtrue;
      repeaterInRangeCount++;
    }
    else
      repeaterInRange = qfalse;

    // Don't allow marked buildables to be replaced in another zone,
    // unless the marked buildable isn't in a zone (and thus unpowered)
    if( team == TEAM_HUMANS &&
        buildable != BA_H_REACTOR &&
        buildable != BA_H_REPEATER &&
        G_ProvidingEntityForPoint( ent->s.origin, TEAM_HUMANS ) != G_ProvidingEntityForPoint( origin, TEAM_HUMANS ) )
      continue;

    if( !ent->inuse )
      continue;

    if( ent->health <= 0 )
      continue;

    if( ent->buildableTeam != team )
      continue;

    // Don't allow destruction of hovel with granger inside
    if( ent->s.modelindex == BA_A_HOVEL && ent->active )
      continue;

    // Explicitly disallow replacement of the core buildable with anything
    // other than the core buildable
    if( ent->s.modelindex == core && buildable != core )
      continue;

    // Don't allow a power source to be replaced by a dependant
    if( team == TEAM_HUMANS &&
        G_ProvidingEntityForPoint( origin, TEAM_HUMANS ) == ent &&
        buildable != BA_H_REPEATER &&
        buildable != core )
      continue;

    if( ent->deconstruct )
    {
      level.markedBuildables[ numBuildables++ ] = ent;

      // Buildables that are marked here will always end up at the front of the
      // removal list, so just incrementing numBuildablesForRemoval is sufficient
      if( collision || repeaterInRange )
      {
        // Collided with something, so we definitely have to remove it or
        // it's a repeater that intersects the new repeater's power area,
        // so it must be removed

        if( collision )
          collisionCount--;

        if( repeaterInRange )
          repeaterInRangeCount--;

        pointsYielded += BG_Buildable( ent->s.modelindex )->buildPoints;
        level.numBuildablesForRemoval++;
      }
      else if( BG_Buildable( ent->s.modelindex )->uniqueTest &&
               ent->s.modelindex == buildable )
      {
        // If it's a unique buildable, it must be replaced by the same type
        pointsYielded += BG_Buildable( ent->s.modelindex )->buildPoints;
        level.numBuildablesForRemoval++;
      }
    }
  }

  // We still need build points, but have no candidates for removal
  if( buildPoints > 0 && numBuildables == 0 )
    return bpError;

  // Collided with something we can't remove
  if( collisionCount > 0 )
    return IBE_NOROOM;

  // There are one or more repeaters we can't remove
  if( repeaterInRangeCount > 0 )
    return IBE_RPTPOWERHERE;

  // Sort the list
  cmpBuildable = buildable;
  VectorCopy( origin, cmpOrigin );
  qsort( level.markedBuildables, numBuildables, sizeof( level.markedBuildables[ 0 ] ),
         G_CompareBuildablesForRemoval );

  // Determine if there are enough markees to yield the required BP
  for( ; pointsYielded < buildPoints && level.numBuildablesForRemoval < numBuildables;
       level.numBuildablesForRemoval++ )
  {
    ent = level.markedBuildables[ level.numBuildablesForRemoval ];
    pointsYielded += BG_Buildable( ent->s.modelindex )->buildPoints;
  }

  for( i = 0; i < level.numBuildablesForRemoval; i++ )
  {
    if( level.markedBuildables[ i ]->s.modelindex == spawn )
      spawnCount++;
  }

  // Make sure we're not removing the last spawn
  if( !g_cheats.integer && remainingSpawns > 0 && ( remainingSpawns - spawnCount ) < 1 && !G_OC_NeedNoDestroyLastSpawn() )
    return IBE_LASTSPAWN;

  // Not enough points yielded
  if( pointsYielded < buildPoints )
    return bpError;
  else
    return IBE_NONE;
}

/*
================
G_SetBuildableLinkState

Links or unlinks all the buildable entities
================
*/
static void G_SetBuildableLinkState( qboolean link )
{
  int       i;
  gentity_t *ent;

  for ( i = 1, ent = g_entities + i; i < level.num_entities; i++, ent++ )
  {
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    if( link )
      trap_LinkEntity( ent );
    else
      trap_UnlinkEntity( ent );
  }
}

static void G_SetBuildableMarkedLinkState( qboolean link )
{
  int       i;
  char      map[ MAX_STRING_CHARS ];
  gentity_t *ent;

  trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );

  for( i = 0; i < level.numBuildablesForRemoval; i++ )
  {
    ent = level.markedBuildables[ i ];
    if( link )
      trap_LinkEntity( ent );
    else
      trap_UnlinkEntity( ent );
  }

  if( trap_FS_FOpenFile( va( "layouts/%s/%s.cfg", map, level.layout ), NULL, FS_READ ) )
  {
    trap_SendConsoleCommand( EXEC_APPEND,
        va( "exec layouts/%s/%s.cfg", map, level.layout ) );

    trap_Cvar_Set( "g_LayoutConfigsLoaded", "1" );
  }
}

/*
================
G_CanBuild

Checks to see if a buildable can be built
================
*/
itemBuildError_t G_CanBuild( gentity_t *ent, buildable_t buildable, int distance, vec3_t origin )
{
  vec3_t            angles;
  vec3_t            entity_origin, normal;
  vec3_t            mins, maxs;
  trace_t           tr1, tr2, tr3;
  itemBuildError_t  reason = IBE_NONE, tempReason;
  gentity_t         *tempent;
  float             minNormal;
  qboolean          invert;
  int               contents;
  playerState_t     *ps = &ent->client->ps;
  int               buildPoints;
  int               i;

  if( G_OC_NeedAlternateCanBuild() )
  {
    G_OC_AlternateCanBuild();
  }

  // Stop all buildables from interacting with traces
  G_SetBuildableLinkState( qfalse );

  BG_BuildableBoundingBox( buildable, mins, maxs );

  BG_PositionBuildableRelativeToPlayer( ps, mins, maxs, trap_Trace, entity_origin, angles, &tr1 );
  trap_Trace( &tr2, entity_origin, mins, maxs, entity_origin, ent->s.number, MASK_PLAYERSOLID );
  trap_Trace( &tr3, ps->origin, NULL, NULL, entity_origin, ent->s.number, MASK_PLAYERSOLID );

  VectorCopy( entity_origin, origin );

  VectorCopy( tr1.plane.normal, normal );
  minNormal = BG_Buildable( buildable )->minNormal;
  invert = BG_Buildable( buildable )->invertNormal;

  //can we build at this angle?
  if( !( normal[ 2 ] >= minNormal || ( invert && normal[ 2 ] <= -minNormal ) ) )
    reason = IBE_NORMAL;

  if( tr1.entityNum != ENTITYNUM_WORLD )
    reason = IBE_NORMAL;

  contents = trap_PointContents( entity_origin, -1 );
  buildPoints = BG_Buildable( buildable )->buildPoints;

  if( ( tempReason = G_SufficientBPAvailable( buildable, origin ) ) != IBE_NONE )
    reason = tempReason;

  if ( BG_Buildable( buildable )->team        == TEAM_NONE )
  {
    // team-less buildables can only be built with cheats on, but otherwise
    // have no restrictions
    if( !g_cheats.integer )
      return IBE_PERMISSION;

    return IBE_NONE;
  }
  else if( ent->client->ps.stats[ STAT_TEAM ] == TEAM_ALIENS )
  {
    //alien criteria

    // Check there is an Overmind
    if( buildable != BA_A_OVERMIND )
    {
      if( !G_Overmind( ) )
        reason = IBE_NOOVERMIND;
    }

    //check there is creep near by for building on
    if( BG_Buildable( buildable )->creepTest )
    {
      if( !G_IsCreepHere( entity_origin ) )
        reason = IBE_NOCREEP;
    }

    if( buildable == BA_A_HOVEL )
    {
      vec3_t    builderMins, builderMaxs;

      //this assumes the adv builder is the biggest thing that'll use the hovel
      BG_ClassBoundingBox( PCL_ALIEN_BUILDER0_UPG, builderMins, builderMaxs, NULL, NULL, NULL );

      if( APropHovel_Blocked( origin, angles, normal, ent ) )
        reason = IBE_HOVELEXIT;
    }

    // Check permission to build here
    if( tr1.surfaceFlags & SURF_NOALIENBUILD || contents & CONTENTS_NOALIENBUILD )
      reason = IBE_PERMISSION;
  }
  else if( ent->client->ps.stats[ STAT_TEAM ] == TEAM_HUMANS )
  {
    //human criteria

    // Check for power
    if( G_IsPowered( entity_origin ) == BA_NONE || !G_Reactor( ) )
    {
      //tell player to build a repeater to provide power
      if( buildable != BA_H_REACTOR && buildable != BA_H_REPEATER )
        reason = IBE_NOPOWERHERE;
    }

    //this buildable requires a DCC
    if( BG_Buildable( buildable )->dccTest && !G_IsDCCBuilt( ) )
      reason = IBE_NODCC;

    //check that there is a parent reactor when building a repeater
    if( buildable == BA_H_REPEATER )
    {
      tempent = G_Reactor( );

      if( tempent == NULL ) // No reactor
        reason = IBE_RPTNOREAC;   
      else if( g_markDeconstruct.integer && G_IsPowered( entity_origin ) == BA_H_REACTOR )
        reason = IBE_RPTPOWERHERE;
      else if( !g_markDeconstruct.integer && G_IsPowered( entity_origin ) )
        reason = IBE_RPTPOWERHERE;
    }

    // Check permission to build here
    if( tr1.surfaceFlags & SURF_NOHUMANBUILD || contents & CONTENTS_NOHUMANBUILD )
      reason = IBE_PERMISSION;
  }

  // Check permission to build here
  if( tr1.surfaceFlags & SURF_NOBUILD || contents & CONTENTS_NOBUILD )
    reason = IBE_PERMISSION;

  // Can we only have one of these?
  if( BG_Buildable( buildable )->uniqueTest )
  {
    tempent = G_FindBuildable( buildable );
    if( tempent && !tempent->deconstruct )
    {
      switch( buildable )
      {
        case BA_A_OVERMIND:
          reason = IBE_ONEOVERMIND;
          break;

        case BA_A_HOVEL:
          reason = IBE_ONEHOVEL;
          break;

        case BA_H_REACTOR:
          reason = IBE_ONEREACTOR;
          break;

        default:
          Com_Error( ERR_FATAL, "No reason for denying build of %d\n", buildable );
          break;
      }
    }
  }

  // Relink buildables
  G_SetBuildableLinkState( qtrue );

  //check there is enough room to spawn from (presuming this is a spawn)
  if( reason == IBE_NONE )
  {
    G_SetBuildableMarkedLinkState( qfalse );
    if( G_CheckSpawnPoint( ENTITYNUM_NONE, origin, normal, buildable, NULL ) != NULL )
      reason = IBE_NORMAL;
    G_SetBuildableMarkedLinkState( qtrue );
  }

  //this item does not fit here
  if( reason == IBE_NONE && ( tr2.fraction < 1.0f || tr3.fraction < 1.0f ) )
    reason = IBE_NOROOM;

  if( reason != IBE_NONE )
    level.numBuildablesForRemoval = 0;

  // Cannot build a reactor or an overmind within range of a domination point.
  // Moving completely to a domination point is prevented by this.
  if( buildable == BA_H_REACTOR || buildable == BA_A_OVERMIND )
  {
      for ( i = 1, tempent = g_entities + i; i < level.num_entities; i++, tempent++ )
      {
        if( tempent->s.eType != ET_BUILDABLE )
          continue;

        if( BG_IsDPoint( tempent->s.modelindex ) )
        {
          vec3_t dir;
          float distance;

          VectorSubtract( origin, tempent->s.origin, dir );
          distance = VectorLength( dir );
          if( distance < DOMINATION_RANGE )
            return IBE_NEARDP;
        }
      }
  }

  return reason;
}

/*
================
G_Build

Spawns a buildable
================
*/
static gentity_t *G_Build( gentity_t *builder, buildable_t buildable, vec3_t origin, vec3_t angles )
{
  gentity_t *built;
  vec3_t    normal;
  char      readable[ MAX_STRING_CHARS ];
  char      buildnums[ MAX_STRING_CHARS ];
  buildLog_t *log = NULL;

  // add build log so that next function can find it
  if( builder && builder->client )
    log = G_BuildLogNew( builder, BF_BUILT, qfalse );

  // Free existing buildables
  G_FreeMarkedBuildables( builder, readable, sizeof( readable ),
    buildnums, sizeof( buildnums ) );

  // Spawn the buildable
  built = G_Spawn();
  built->s.eType = ET_BUILDABLE;
  built->killedBy = ENTITYNUM_NONE;
  built->classname = BG_Buildable( buildable )->entityName;
  built->s.modelindex = buildable;
  built->buildableTeam = built->s.modelindex2 = BG_Buildable( buildable )->team;
  BG_BuildableBoundingBox( buildable, built->r.mins, built->r.maxs );

  // detect the buildable's normal vector
  if( !builder->client )
  {
    // initial base layout created by server

    if( builder->s.origin2[ 0 ] ||
        builder->s.origin2[ 1 ] ||
        builder->s.origin2[ 2 ] )
    {
      VectorCopy( builder->s.origin2, normal );
    }
    else if( BG_Buildable( buildable )->traj == TR_BUOYANCY )
      VectorSet( normal, 0.0f, 0.0f, -1.0f );
    else
      VectorSet( normal, 0.0f, 0.0f, 1.0f );
  }
  else
  {
    // in-game building by a player
    BG_GetClientNormal( &builder->client->ps, normal );

    if( built->s.modelindex == BA_H_SPAWN )//|| built->s.modelindex == BA_H_SPAWN )
      builder->groupID = 0;
    else
      builder->groupID = 2;  // FIXME: magic numbers
  }

  // when building the initial layout, spawn the entity slightly off its
  // target surface so that it can be "dropped" onto it
  if( !builder->client )
    VectorMA( origin, 1.0f, normal, origin );

  built->health = 1;

  built->splashDamage = BG_Buildable( buildable )->splashDamage;
  built->splashRadius = BG_Buildable( buildable )->splashRadius;
  built->splashMethodOfDeath = BG_Buildable( buildable )->meansOfDeath;

  built->nextthink = BG_Buildable( buildable )->nextthink;

  built->takedamage = qtrue;
  built->spawned = qfalse;
  built->buildTime = built->s.time = level.time;

  // build instantly in cheat mode
  if( builder->client && g_cheats.integer )
  {
    built->health = BG_Buildable( buildable )->health;
    built->buildTime = built->s.time =
      level.time - BG_Buildable( buildable )->buildTime;
  }

  // extended buildable stuff
  built->groupID = builder->groupID;
  built->reserved = builder->reserved;
  built->reserved2 = builder->reserved2;

  // Setup domination point
  if( buildable >= BA_DPOINT_FIRST && buildable <= BA_DPOINT_LAST )
  {
    char name[ MAX_STRING_CHARS ];
    gentity_t *ent;

    built->takedamage = qfalse;
    built->dominationTeam = TEAM_NONE;
    built->dominationAttacking = TEAM_NONE;
    built->timestamp = level.time;
    built->think = Domination_Think;
    built->die = Domination_Die;
    built->r.svFlags |= SVF_BROADCAST; // broadcast changes to everyone
    built->flags |= FL_GODMODE;
    level.dominationPoints[ TEAM_NONE ]++;
    if( ( ent = Team_GetLocation( built ) ) )
    {
      trap_GetConfigstring( CS_LOCATIONS + ent->s.generic1, name, sizeof( name ) );
      Com_sprintf( built->dominationName, sizeof( built->dominationName ),
          "%s^7 (%c)", name, 'A' + buildable - BA_DPOINT_FIRST );
    }
    else
    {
      Com_sprintf( built->dominationName, sizeof( built->dominationName ),
          "point %c", 'A' + buildable - BA_DPOINT_FIRST );
    }
  }

  //things that vary for each buildable that aren't in the dbase
  switch( buildable )
  {
    case BA_A_SPAWN:
      built->die = ASpawn_Die;
      built->think = ASpawn_Think;
      built->pain = AGeneric_Pain;
      break;

    case BA_A_BARRICADE:
      built->die = ABarricade_Die;
      built->think = ABarricade_Think;
      built->pain = ABarricade_Pain;
      built->touch = ABarricade_Touch;
      built->shrunkTime = 0;
      ABarricade_Shrink( built, qtrue );
      break;

    case BA_A_BOOSTER:
      built->die = AGeneric_Die;
      built->think = AGeneric_Think;
      built->pain = AGeneric_Pain;
      built->touch = ABooster_Touch;
      break;

    case BA_A_ACIDTUBE:
      built->die = AGeneric_Die;
      built->think = AAcidTube_Think;
      built->pain = AGeneric_Pain;
      break;

    case BA_A_HIVE:
      built->die = AGeneric_Die;
      built->think = AHive_Think;
      built->pain = AHive_Pain;
      break;

    case BA_A_TRAPPER:
      built->die = AGeneric_Die;
      built->think = ATrapper_Think;
      built->pain = AGeneric_Pain;
      break;

    case BA_A_OVERMIND:
      built->die = ASpawn_Die;
      built->think = AOvermind_Think;
      built->pain = AGeneric_Pain;
      break;

    case BA_A_HOVEL:
      built->die = AHovel_Die;
      built->use = AHovel_Use;
      built->think = AHovel_Think;
      built->pain = AGeneric_Pain;
      break;

    case BA_H_SPAWN:
      built->die = HSpawn_Die;
      built->use = HSpawn_Use;
      built->think = HSpawn_Think;
      break;

    case BA_H_MGTURRET:
      built->die = HSpawn_Die;
      built->think = HMGTurret_Think;
      break;

    case BA_H_TESLAGEN:
      built->die = HSpawn_Die;
      built->think = HTeslaGen_Think;
      break;

    case BA_H_ARMOURY:
      built->think = HArmoury_Think;
      built->die = HSpawn_Die;
      built->use = HArmoury_Activate;
      break;

    case BA_H_DCC:
      built->think = HDCC_Think;
      built->die = HSpawn_Die;
      break;

    case BA_H_MEDISTAT:
      built->think = HMedistat_Think;
      built->die = HMedistat_Die;
      break;

    case BA_H_REACTOR:
      built->think = HReactor_Think;
      built->die = HSpawn_Die;
      built->use = HRepeater_Use;
      built->powered = built->active = qtrue;
      break;

    case BA_H_REPEATER:
      built->think = HRepeater_Think;
      built->die = HRepeater_Die;
      built->use = HRepeater_Use;
      built->count = -1;
      break;

    default:
      //erk
      break;
  }

  built->s.number = built - g_entities;
  built->r.contents = CONTENTS_BODY;
  built->clipmask = MASK_PLAYERSOLID;
  built->enemy = NULL;
  built->s.weapon = BG_Buildable( buildable )->turretProjType;

  if( builder->client )
    built->builtBy = builder->client->ps.clientNum;
  else
    built->builtBy = -1;

  G_SetOrigin( built, origin );

  // gently nudge the buildable onto the surface :)
  VectorScale( normal, -50.0f, built->s.pos.trDelta );

  // set turret angles
  VectorCopy( builder->s.angles2, built->s.angles2 );

  VectorCopy( angles, built->s.angles );
  built->s.angles[ PITCH ] = 0.0f;
  built->s.angles2[ YAW ] = angles[ YAW ];
  built->s.angles2[ PITCH ] = MGTURRET_VERTICALCAP;
  built->s.pos.trType = BG_Buildable( buildable )->traj;
  built->s.pos.trTime = level.time;
  built->physicsBounce = BG_Buildable( buildable )->bounce;
  built->s.groundEntityNum = -1;

  built->s.generic1 = MAX( built->health, 0 );

  if( BG_Buildable( buildable )->team == TEAM_ALIENS )
  {
    built->powered = qtrue;
    built->s.eFlags |= EF_B_POWERED;
  }
  else if( BG_Buildable( buildable )->team == TEAM_HUMANS && ( built->powered = G_FindProvider( built ) ) )
    built->s.eFlags |= EF_B_POWERED;

  built->s.eFlags &= ~EF_B_SPAWNED;

  if( buildable >= BA_DPOINT_FIRST && buildable <= BA_DPOINT_LAST )
  {
    // reset hijacked bits
    built->powered = built->deconstruct = qfalse;  
    built->s.eFlags &= ~EF_B_POWERED & ~EF_B_MARKED;
  }

  VectorCopy( normal, built->s.origin2 );

  G_AddEvent( built, EV_BUILD_CONSTRUCT, 0 );

  G_SetIdleBuildableAnim( built, BG_Buildable( buildable )->idleAnim );

  if( built->builtBy >= 0 )
    G_SetBuildableAnim( built, BANIM_CONSTRUCT1, qtrue );

  trap_LinkEntity( built );

  G_OC_BUILDABLEBUILT( built );

  if( builder && builder->client )
  {
    G_TeamCommand( builder->client->ps.stats[ STAT_TEAM ],
      va( "print \"%s ^2built^7 by %s%s%s\n\"",
        BG_Buildable( built->s.modelindex )->humanName,
        builder->client->pers.netname,
        ( readable[ 0 ] ) ? "^7, ^3replacing^7 " : "",
        readable ) );
    G_LogPrintf( "Construct: %d %d %s%s: %s" S_COLOR_WHITE " is building "
      "%s%s%s\n",
      builder - g_entities,
      built - g_entities,
      BG_Buildable( built->s.modelindex )->name,
      buildnums,
      builder->client->pers.netname,
      BG_Buildable( built->s.modelindex )->humanName,
      readable[ 0 ] ? ", replacing " : "",
      readable );
  }

  if( log )
    G_BuildLogSet( log, built );

  return built;
}

/*
=================
G_BuildIfValid
=================
*/
qboolean G_BuildIfValid( gentity_t *ent, buildable_t buildable )
{
  float         dist;
  vec3_t        origin;

  dist = BG_Class( ent->client->ps.stats[ STAT_CLASS ] )->buildDist;

  switch( G_CanBuild( ent, buildable, dist, origin ) )
  {
    case IBE_NONE:
      G_Build( ent, buildable, origin, ent->s.apos.trBase );
      return qtrue;

    case IBE_NOALIENBP:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_NOBP );
      return qfalse;

    case IBE_NOOVERMIND:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_NOOVMND );
      return qfalse;

    case IBE_NOCREEP:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_NOCREEP );
      return qfalse;

    case IBE_ONEOVERMIND:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_ONEOVERMIND );
      return qfalse;

    case IBE_ONEHOVEL:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_ONEHOVEL );
      return qfalse;

    case IBE_HOVELEXIT:
      G_TriggerMenu( ent->client->ps.clientNum, MN_A_HOVEL_EXIT );
      return qfalse;

    case IBE_NORMAL:
      G_TriggerMenu( ent->client->ps.clientNum, MN_B_NORMAL );
      return qfalse;

    case IBE_PERMISSION:
      G_TriggerMenu( ent->client->ps.clientNum, MN_B_NORMAL );
      return qfalse;

    case IBE_ONEREACTOR:
      G_TriggerMenu( ent->client->ps.clientNum, MN_H_ONEREACTOR );
      return qfalse;

    case IBE_NOPOWERHERE:
      G_TriggerMenu( ent->client->ps.clientNum, MN_H_NOPOWERHERE );
      return qfalse;

    case IBE_NOROOM:
      G_TriggerMenu( ent->client->ps.clientNum, MN_B_NOROOM );
      return qfalse;

    case IBE_NOHUMANBP:
      G_TriggerMenu( ent->client->ps.clientNum, MN_H_NOBP);
      return qfalse;

    case IBE_NODCC:
      G_TriggerMenu( ent->client->ps.clientNum, MN_H_NODCC );
      return qfalse;

    case IBE_RPTPOWERHERE:
      G_TriggerMenu( ent->client->ps.clientNum, MN_H_RPTPOWERHERE );
      return qfalse;

    case IBE_LASTSPAWN:
      G_TriggerMenu( ent->client->ps.clientNum, MN_B_LASTSPAWN );
      return qfalse;

    case IBE_NEARDP:
      G_TriggerMenu( ent->client->ps.clientNum, MN_NEARDP );
      return qfalse;

    default:
      break;
  }

  return qfalse;
}

/*
================
G_FinishSpawningBuildable

Traces down to find where an item should rest, instead of letting them
free fall from their spawn points
================
*/
static void G_FinishSpawningBuildable( gentity_t *ent )
{
  trace_t     tr;
  vec3_t      dest;
  gentity_t   *built;
  buildable_t buildable = ent->s.modelindex;

  built = G_Build( ent, buildable, ent->s.pos.trBase, ent->s.angles );
  built->deconstruct = ent->deconstruct;
  G_FreeEntity( ent );

  built->takedamage = qtrue;
  built->spawned = qtrue; //map entities are already spawned
  built->health = BG_Buildable( buildable )->health;
  built->s.eFlags |= EF_B_SPAWNED;

  // drop towards normal surface
  VectorScale( built->s.origin2, -4096.0f, dest );
  VectorAdd( dest, built->s.origin, dest );

  trap_Trace( &tr, built->s.origin, built->r.mins, built->r.maxs, dest, built->s.number, built->clipmask );

  if( G_OC_NeedStartSolid() && tr.startsolid )
  {
    G_Printf( S_COLOR_YELLOW "G_FinishSpawningBuildable: %s startsolid at %s\n",
              built->classname, vtos( built->s.origin ) );
    G_FreeEntity( built );
    return;
  }

  //point items in the correct direction
  VectorCopy( tr.plane.normal, built->s.origin2 );

  // allow to ride movers
  built->s.groundEntityNum = tr.entityNum;

  G_SetOrigin( built, tr.endpos );

  trap_LinkEntity( built );

  // qsort entities so that the less important buildables will be unpowered first

  G_OC_BUILDABLEBUILT( built );
}

/*
============
Build Log
============
*/
void G_BuildLogFree( buildLog_t *log )
{
  buildLog_t *tmp;

  while( ( tmp = log ) )
  {
    log = log->marked;
    BG_Free( tmp );
  }
}

void G_BuildLogCleanup( void )
{
  buildLog_t *tmp;

  while( ( tmp = level.buildLog ) )
  {
    level.buildLog = level.buildLog->next;
    G_BuildLogFree( tmp );
  }
}

static int G_BuildLogId( void )
{
  static int id = 0;
  buildLog_t *ptr, *tmp;
  int        i;

  // keep the log trimmed
  for( tmp = level.buildLog, i = 0; tmp && i < 64 - 2; tmp = tmp->next, i++ );
  if( tmp )
  {
    ptr = tmp->next;
    tmp->next = NULL;

    while( ( tmp = ptr ) )
    {
      ptr = ptr->next;
      G_BuildLogFree( tmp );
    }
  }

  id++;
  return id;
}

buildLog_t *G_BuildLogNew( gentity_t *attacker, buildFate_t fate, qboolean marked )
{
  buildLog_t *log;

  log = BG_Alloc( sizeof( buildLog_t ) );
  log->time = level.time;
  if( attacker && attacker->client )
    Q_strncpyz( log->guid, attacker->client->pers.guid, sizeof( log->guid ) );
  log->fate = fate;

  if( marked && level.buildLog )
  {
    log->id = level.buildLog->id;
    log->marked = level.buildLog->marked;
    level.buildLog->marked = log;
  }
  else
  {
    log->id = G_BuildLogId( );
    log->next = level.buildLog;
    level.buildLog = log;
  }

  return log;
}

void G_BuildLogSet( buildLog_t *log, gentity_t *buildable )
{
  log->buildable = buildable->s.modelindex;
  VectorCopy( buildable->s.pos.trBase, log->origin );
  VectorCopy( buildable->s.angles, log->angles );
  VectorCopy( buildable->s.origin2, log->origin2 );
  VectorCopy( buildable->s.angles2, log->angles2 );

  if( buildable->parentNode )
    log->parent = buildable->parentNode->s.modelindex;
  else
    log->parent = BA_NONE;
}

static qboolean G_RevertCanFit( buildLog_t * log )
{
  trace_t   tr;
  vec3_t    mins, maxs;
  vec3_t    start, end;
  int       blockers[ MAX_GENTITIES ];
  int       num;
  gentity_t *ghosts[ MAX_GENTITIES ];
  int       ghostNum = 0;
  gentity_t *targ;
  int       i;

  BG_BuildableBoundingBox( log->buildable, mins, maxs );
  VectorAdd( log->origin, mins, mins );
  VectorAdd( log->origin, maxs, maxs );

  num = trap_EntitiesInBox( mins, maxs, blockers, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    targ = g_entities + blockers[ i ];
    if( targ->s.eType == ET_PLAYER ||
        ( targ->s.eType == ET_BUILDABLE && targ->health <= 0 ) )
    {
      // ignore players and dead buildables
      trap_UnlinkEntity( targ );
      ghosts[ ghostNum++ ] = targ;
    }
  }

  BG_BuildableBoundingBox( log->buildable, mins, maxs );
  // trace the same as when placing
  VectorScale( log->origin2, 1.0f, start );
  VectorAdd( log->origin, start, start );

  VectorScale( log->origin2, -1.0f, end );
  VectorAdd( log->origin, end, end );

  trap_Trace( &tr, start, mins, maxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );

  // restore ignored entities
  for( i = 0; i < ghostNum; i++ )
    trap_LinkEntity( ghosts[ i ] );

  return ( !tr.startsolid );
}

static void G_RevertThink( gentity_t *self )
{
  vec3_t mins, maxs;
  int    blockers[ MAX_GENTITIES ];
  int    num;
  int    victims = 0;
  int    i;

  BG_BuildableBoundingBox( self->s.modelindex, mins, maxs );
  VectorAdd( self->s.pos.trBase, mins, mins );
  VectorAdd( self->s.pos.trBase, maxs, maxs );
  num = trap_EntitiesInBox( mins, maxs, blockers, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    gentity_t *targ;
    vec3_t    shove;

    targ = g_entities + blockers[ i ];
    if( targ->client )
    {
      VectorSet( shove, crandom() * 150, crandom() * 150, random() * 150 );
      VectorAdd( targ->client->ps.velocity, shove, targ->client->ps.velocity );
      victims++;
    }
  }

  if( victims )
  {
    self->nextthink = level.time + ( FRAMETIME * 2 );
    return;
  }

  level.numBuildablesForRemoval = 0;
  G_FinishSpawningBuildable( self );
}

static void G_RevertSpawn( buildLog_t *log, qboolean marked )
{
  gentity_t *built;
  gentity_t *targ;
  vec3_t    mins, maxs;
  int       blockers[ MAX_GENTITIES ];
  int       num;
  int       i;

  BG_BuildableBoundingBox( log->buildable, mins, maxs );
  VectorAdd( log->origin, mins, mins );
  VectorAdd( log->origin, maxs, maxs );
  num = trap_EntitiesInBox( mins, maxs, blockers, MAX_GENTITIES );
  for( i = 0; i < num; i++ )
  {
    targ = g_entities + blockers[ i ];
    if( targ->s.eType == ET_BUILDABLE && targ->health <= 0 )
    {
      // old dead entity
      G_FreeEntity( targ );
    }
  }

  built = G_Spawn( );
  built->r.contents = 0;
  built->s.modelindex = log->buildable;
  built->deconstruct = marked;

  VectorCopy( log->origin, built->s.pos.trBase );
  VectorCopy( log->angles, built->s.angles );
  VectorCopy( log->origin2, built->s.origin2 );
  VectorCopy( log->angles2, built->s.angles2 );

  built->think = G_RevertThink;
  built->nextthink = level.time + 50;
}

const char *G_RevertBuild( buildLog_t *log )
{
  gentity_t *targ;
  vec3_t    dist;
  int       i;

  // revert a build
  if( log->fate == BF_BUILT )
  {
    buildLog_t *mark;

    for( i = MAX_CLIENTS; i < level.num_entities; i++ )
    {
      targ = g_entities + i;

      if( targ->s.eType != ET_BUILDABLE ||
          targ->s.modelindex != log->buildable )
        continue;

      VectorSubtract( targ->s.pos.trBase, log->origin, dist );
      if( VectorLength( dist ) > 5 )
        continue;

      trap_UnlinkEntity( targ );
      for( mark = log->marked; mark; mark = mark->marked )
      {
        if( !G_RevertCanFit( mark ) )
        {
          trap_LinkEntity( targ );
          return "conflict with an existing buildable or map feature";
        }
      }

      G_FreeEntity( targ );
      for( mark = log->marked; mark; mark = mark->marked )
        G_RevertSpawn( mark, qtrue );
      return NULL;
    }

    return "unable to find buildable";
  }

  // revert a removal
  if( G_RevertCanFit( log ) )
  {
    G_RevertSpawn( log, qfalse );

    // give back queued build points
    if( ( log->fate == BF_DESTROYED || log->fate == BF_TEAMKILLED ) &&
        log->parent != BA_NONE )
    {
      int              value = BG_Buildable( log->buildable )->buildPoints;
      team_t           team  = BG_Buildable( log->buildable )->team;
      gentity_t        *power;
      buildPointZone_t *zone;

      power = G_ProvidingEntityForPoint( log->origin, team );
      if( power && power->usesBuildPointZone )
      {
        zone = &level.buildPointZones[ power->buildPointZone ];
        zone->queuedBuildPoints = MAX( 0, zone->queuedBuildPoints - value );
      }
      else
      {
        if     ( team == TEAM_ALIENS )
        {
          level.alienBuildPointQueue = MAX( 0, level.alienBuildPointQueue - value );
        }
        else if( team == TEAM_HUMANS )
        {
          level.humanBuildPointQueue = MAX( 0, level.alienBuildPointQueue - value );
        }
        else
        {
        }
      }
    }

    return NULL;
  }
  return "conflict with an existing buildable or map feature";
}


/*
============
G_SpawnBuildable

Sets the clipping size and plants the object on the floor.

Items can't be immediately dropped to floor, because they might
be on an entity that hasn't spawned yet.
============
*/
void G_SpawnBuildable( gentity_t *ent, buildable_t buildable, int groupID, int reserved, float reserved2 )
{
  ent->s.modelindex = buildable;

  // some movers spawn on the second frame, so delay item
  // spawns until the third frame so they can ride trains
  ent->nextthink = level.time + FRAMETIME * 2;
  ent->think = G_FinishSpawningBuildable;

  ent->groupID = groupID;
  ent->reserved = reserved;
  ent->reserved2 = reserved2;
}

/*
============
G_LayoutSave
============
*/
void G_LayoutSave( char *name )
{
  char map[ MAX_QPATH ];
  char fileName[ MAX_OSPATH ];
  fileHandle_t f;
  int len;
  int i;
  gentity_t *ent;
  char *s;

  trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );
  if( !map[ 0 ] )
  {
    G_Printf( "LayoutSave( ): no map is loaded\n" );
    return;
  }
  Com_sprintf( fileName, sizeof( fileName ), "layouts/%s/%s.dat", map, name );

  len = trap_FS_FOpenFile( fileName, &f, FS_WRITE );
  if( len < 0 )
  {
    G_Printf( "layoutsave: could not open %s\n", fileName );
    return;
  }

  G_Printf( "layoutsave: saving layout to %s\n", fileName );

  for( i = MAX_CLIENTS; i < level.num_entities; i++ )
  {
    ent = &level.gentities[ i ];
    if( ent->s.eType != ET_BUILDABLE )
      continue;

    s = va( "%s %f %f %f %f %f %f %f %f %f %f %f %f %d %d %f\n",
      BG_Buildable( ent->s.modelindex )->name,
      ent->s.pos.trBase[ 0 ],
      ent->s.pos.trBase[ 1 ],
      ent->s.pos.trBase[ 2 ],
      ent->s.angles[ 0 ],
      ent->s.angles[ 1 ],
      ent->s.angles[ 2 ],
      ent->s.origin2[ 0 ],
      ent->s.origin2[ 1 ],
      ent->s.origin2[ 2 ],
      ent->s.angles2[ 0 ],
      ent->s.angles2[ 1 ],
      ent->s.angles2[ 2 ],
	  ent->groupID,
	  ent->reserved,
	  ent->reserved2 );
    trap_FS_Write( s, strlen( s ), f );
  }
  trap_FS_FCloseFile( f );
}

/*
============
G_LayoutList
============
*/
int G_LayoutList( const char *map, char *list, int len )
{
  // up to 128 single character layout names could fit in layouts
  char fileList[ ( MAX_CVAR_VALUE_STRING / 2 ) * 5 ] = {""};
  char layouts[ MAX_CVAR_VALUE_STRING ] = {""};
  int numFiles, i, fileLen = 0, listLen;
  int  count = 0;
  char *filePtr;

  Q_strcat( layouts, sizeof( layouts ), "*BUILTIN* " );
  numFiles = trap_FS_GetFileList( va( "layouts/%s", map ), ".dat",
    fileList, sizeof( fileList ) );
  filePtr = fileList;
  for( i = 0; i < numFiles; i++, filePtr += fileLen + 1 )
  {
    fileLen = strlen( filePtr );
    listLen = strlen( layouts );
    if( fileLen < 5 )
      continue;

    // list is full, stop trying to add to it
    if( ( listLen + fileLen ) >= sizeof( layouts ) )
      break;

    Q_strcat( layouts,  sizeof( layouts ), filePtr );
    listLen = strlen( layouts );

    // strip extension and add space delimiter
    layouts[ listLen - 4 ] = ' ';
    layouts[ listLen - 3 ] = '\0';
    count++;
  }
  if( count != numFiles )
  {
    G_Printf( S_COLOR_YELLOW "WARNING: layout list was truncated to %d "
      "layouts, but %d layout files exist in layouts/%s/.\n",
      count, numFiles, map );
  }
  Q_strncpyz( list, layouts, len );
  return count + 1;
}

/*
============
G_LayoutSelect

set level.layout based on g_layouts or g_layoutAuto
============
*/
void G_LayoutSelect( void )
{
  char fileName[ MAX_OSPATH ];
  char layouts[ MAX_CVAR_VALUE_STRING ];
  char layouts2[ MAX_CVAR_VALUE_STRING ];
  char *l;
  char map[ MAX_QPATH ];
  char *s;
  int cnt = 0;
  int layoutNum;

  Q_strncpyz( layouts, g_layouts.string, sizeof( layouts ) );
  trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );

  // one time use cvar
  trap_Cvar_Set( "g_layouts", "" );

  // pick an included layout at random if no list has been provided
  if( !layouts[ 0 ] && g_layoutAuto.integer )
  {
    G_LayoutList( map, layouts, sizeof( layouts ) );
  }

  if( !layouts[ 0 ] )
    return;

  Q_strncpyz( layouts2, layouts, sizeof( layouts2 ) );
  l = &layouts2[ 0 ];
  layouts[ 0 ] = '\0';
  while( 1 )
  {
    s = COM_ParseExt( &l, qfalse );
    if( !*s )
      break;

    if( !Q_stricmp( s, "*BUILTIN*" ) )
    {
      Q_strcat( layouts, sizeof( layouts ), s );
      Q_strcat( layouts, sizeof( layouts ), " " );
      cnt++;
      continue;
    }

    Com_sprintf( fileName, sizeof( fileName ), "layouts/%s/%s.dat", map, s );
    if( trap_FS_FOpenFile( fileName, NULL, FS_READ ) > 0 )
    {
      Q_strcat( layouts, sizeof( layouts ), s );
      Q_strcat( layouts, sizeof( layouts ), " " );
      cnt++;
    }
    else
      G_Printf( S_COLOR_YELLOW "WARNING: layout \"%s\" does not exist\n", s );
  }
  if( !cnt )
  {
      G_Printf( S_COLOR_RED "ERROR: none of the specified layouts could be "
        "found, using map default\n" );
      return;
  }
  layoutNum = ( rand( ) % cnt ) + 1;
  cnt = 0;

  Q_strncpyz( layouts2, layouts, sizeof( layouts2 ) );
  l = &layouts2[ 0 ];
  while( 1 )
  {
    s = COM_ParseExt( &l, qfalse );
    if( !*s )
      break;

    Q_strncpyz( level.layout, s, sizeof( level.layout ) );
    cnt++;
    if( cnt >= layoutNum )
      break;
  }
  G_Printf( "using layout \"%s\" from list (%s)\n", level.layout, layouts );
}

/*
============
G_LayoutBuildItem
============
*/
void G_LayoutBuildItem( buildable_t buildable, vec3_t origin,
  vec3_t angles, vec3_t origin2, vec3_t angles2, int groupID, int reserved, float reserved2 )
{
  gentity_t *builder;

  builder = G_Spawn( );
  builder->client = 0;
  VectorCopy( origin, builder->s.pos.trBase );
  VectorCopy( angles, builder->s.angles );
  VectorCopy( origin2, builder->s.origin2 );
  VectorCopy( angles2, builder->s.angles2 );
  G_SpawnBuildable( builder, buildable, groupID, reserved, reserved2 );
}

/*
============
G_LayoutLoad

load the layout .dat file indicated by level.layout and spawn buildables
as if a builder was creating them
============
*/
void G_LayoutLoad( void )
{
  fileHandle_t f;
  int len;
  char *layout, *layoutHead;
  char map[ MAX_QPATH ];
  char buildName[ MAX_TOKEN_CHARS ];
  int buildable;
  vec3_t origin = { 0.0f, 0.0f, 0.0f };
  vec3_t angles = { 0.0f, 0.0f, 0.0f };
  vec3_t origin2 = { 0.0f, 0.0f, 0.0f };
  vec3_t angles2 = { 0.0f, 0.0f, 0.0f };
  int groupID = 0;
  int reserved = 0;
  float reserved2 = 0.0f;
  char line[ MAX_STRING_CHARS ];
  int i = 0;

  if( !level.layout[ 0 ] || !Q_stricmp( level.layout, "*BUILTIN*" ) )
    return;

  trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );
  len = trap_FS_FOpenFile( va( "layouts/%s/%s.dat", map, level.layout ),
    &f, FS_READ );
  if( len < 0 )
  {
    G_Printf( "ERROR: layout %s could not be opened\n", level.layout );
    return;
  }
  layoutHead = layout = BG_Alloc( len + 1 );
  trap_FS_Read( layout, len, f );
  layout[ len ] = '\0';
  trap_FS_FCloseFile( f );
  while( *layout )
  {
    if( i >= sizeof( line ) - 1 )
    {
      G_Printf( S_COLOR_RED "ERROR: line overflow in %s before \"%s\"\n",
       va( "layouts/%s/%s.dat", map, level.layout ), line );
      break;
    }
    line[ i++ ] = *layout;
    line[ i ] = '\0';
    if( *layout == '\n' )
    {
      i = 0;
      sscanf( line, "%s %f %f %f %f %f %f %f %f %f %f %f %f %d %d %f\n",
        buildName,
        &origin[ 0 ], &origin[ 1 ], &origin[ 2 ],
        &angles[ 0 ], &angles[ 1 ], &angles[ 2 ],
        &origin2[ 0 ], &origin2[ 1 ], &origin2[ 2 ],
        &angles2[ 0 ], &angles2[ 1 ], &angles2[ 2 ],
        &groupID, &reserved, &reserved2 );
      buildable = atoi( buildName );
      if( buildable > BA_NONE && buildable < BA_NUM_BUILDABLES )
      {
        if( buildable > BA_NONE && buildable < BA_NUM_BUILDABLES )
          G_LayoutBuildItem( buildable, origin, angles, origin2, angles2, groupID, reserved, reserved2 );
        else
          G_Printf( S_COLOR_YELLOW "WARNING: bad buildable number (%d) in "
            " layout.  skipping\n", buildable );
      }
      else
      {
        buildable = BG_BuildableByName( buildName )->number;

        if( buildable > BA_NONE && buildable < BA_NUM_BUILDABLES )
          G_LayoutBuildItem( buildable, origin, angles, origin2, angles2, groupID, reserved, reserved2 );
        else
          G_Printf( S_COLOR_YELLOW "WARNING: bad buildable name (%s) in "
            " layout.  skipping\n", buildName );
      }
    }
    layout++;
  }
  BG_Free( layoutHead );
}

/*
============
G_BaseSelfDestruct
============
*/
void G_BaseSelfDestruct( team_t team )
{
  int       i;
  gentity_t *ent;

  for( i = MAX_CLIENTS; i < level.num_entities; i++ )
  {
    ent = &level.gentities[ i ];
    if( ent->health <= 0 )
      continue;
    if( ent->s.eType != ET_BUILDABLE )
      continue;
    if( ent->buildableTeam != team )
      continue;
    G_Damage( ent, NULL, NULL, NULL, NULL, 10000, 0, MOD_SUICIDE );
  }
}

/*
============
G_IsCore
============
*/
qboolean G_IsCore( buildable_t buildable )
{
  return buildable == BA_A_OVERMIND || buildable == BA_H_REACTOR;
}
