// Copyright (c) 2010-2011 Zipline Games, Inc. All Rights Reserved.
// http://getmoai.com

#include "pch.h"
#include <moaicore/MOAIGfxDevice.h>
#include <moaicore/MOAIPlatformerBody2D.h>
#include <moaicore/MOAIPlatformerFsm2D.h>
#include <moaicore/MOAISurfaceSampler2D.h>

#ifndef FP_EPSILON
	#define FP_EPSILON 0.0001f
#endif

#define FP_EQUAL(f0,f1) \
	((( f0 + FP_EPSILON ) > f1 ) && (( f1 - FP_EPSILON ) < f0 ))

#define IS_CEILING(surface) \
	( surface.mNorm.mY <= this->mCeilCos )

#define IS_FLOOR(surface) \
	( surface.mNorm.mY >= this->mFloorCos )

#define IS_WALL(surface) \
	(( surface.mNorm.mY < this->mFloorCos ) && ( surface.mNorm.mY > this->mCeilCos ))

#define FLOOR_PAD_LENGTH 0.0001f

//================================================================//
// MOAIPlatformerFsm2D
//================================================================//

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::ApplyMoveOnFloor ( const MOAISurface2D* hit, float time ) {

	if ( !hit ) {
		this->mLoc.Add ( this->mProjectedMove );
		this->mFoot.Add ( this->mProjectedMove );
		this->mState = STATE_DONE;
		return;
	}
		
	//printf ( "hit: %p (%f, %f) %f %f\n", hit, hit->mNorm.mX, hit->mNorm.mY, bestTime, bestDot );
	
	if ( time > 0.0f ) {
		
		this->mProjectedMove.Scale ( time );
		this->mLoc.Add ( this->mProjectedMove );
		this->mFoot.Add ( this->mProjectedMove );
	
		// calculate amount of move satisfied and apply remainder to original move
		float originalMoveDist = this->mProjectedMoveDist;
		float actualMoveDist = this->mProjectedMove.Dot ( this->mFloorTangent );
		
		if ( actualMoveDist < originalMoveDist ) {
			float remainder = ( originalMoveDist - actualMoveDist ) / originalMoveDist;
			this->mMove.Scale ( remainder );
		}
		else {
			this->mMove.Init ( 0.0f, 0.0f );
			this->mState = STATE_DONE;
			return;
		}
	}
	
	this->SetFloor ( *hit );
	
	// move may have been altered, shoved, truncated, etc.
	float prevMoveDist = this->mProjectedMoveDist;
	this->ProjectMove ();
	
	// if next floor reverses sign of last projected move then abort
	if (( prevMoveDist >= 0.0f ) != ( this->mProjectedMoveDist >= 0.0f )) {
		this->mState = STATE_DONE;
		return;
	}
	
	if ( this->mMove.LengthSquared () < 0.00001f ) {
		this->mState = STATE_DONE;
	}
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::ApplyWallShoveOnFloor ( bool wallToLeft, bool wallToRight, float leftWallDepth, float rightWallDepth ) {
	
	bool leftShove = ( leftWallDepth < 0.0f );
	bool rightShove = ( rightWallDepth < 0.0f );
	
	float shoveScale = 0.0f;
	bool blockMove = false;
	
	if ( leftShove && rightShove ) {
		shoveScale = ( rightWallDepth - leftWallDepth ) * 0.5f;
		blockMove = true;
	}
	else if ( leftShove ) {
		
		blockMove = ( this->mProjectedMove.mX <= 0.0f );
	
		shoveScale = -leftWallDepth;
		if ( wallToRight && ( shoveScale >= rightWallDepth )) {
			shoveScale = rightWallDepth + (( shoveScale - rightWallDepth ) * 0.5f );
			blockMove = true;
		}
	}
	else if ( rightShove ) {
		
		blockMove = ( this->mProjectedMove.mX >= 0.0f );
	
		shoveScale = rightWallDepth;
		if ( wallToLeft && ( shoveScale <= -leftWallDepth )) {
			shoveScale =  (( shoveScale + leftWallDepth ) * 0.5f ) - leftWallDepth;
			blockMove = true;
		}
	}
	
	USVec2D shoveVec = this->mFloorTangent;
	shoveVec.Scale ( shoveScale );
	
	if ( blockMove ) {
		this->mProjectedMove = shoveVec;
	}
	else {
		
		float floorDist = this->mProjectedMove.Dot ( this->mFloorTangent );
		
		if ( this->mProjectedMove.mX > 0.0f ) {
			if ( shoveVec.mX > this->mProjectedMove.mX ) {
				this->mProjectedMove = shoveVec;
			}
			else if ( wallToRight && ( floorDist >= rightWallDepth )) {
				this->mProjectedMove = this->mFloorTangent;
				this->mProjectedMove.Scale ( rightWallDepth );
			}
		}
	
		if ( this->mProjectedMove.mX < 0.0f ) {
			if ( shoveVec.mX < this->mProjectedMove.mX ) {
				this->mProjectedMove = shoveVec;
			}
			else if ( wallToLeft && ( -floorDist >= leftWallDepth )) {
				this->mProjectedMove = this->mFloorTangent;
				this->mProjectedMove.Scale ( -leftWallDepth );
			}
		}
	}
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::CalculateWallShoveOnFloor () {

	bool wallToLeft = false;
	bool wallToRight = false;

	float leftWallDepth = 0.0f;
	float rightWallDepth = 0.0f;
	
	for ( u32 i = 0; i < this->mSurfaceBuffer.mTop; ++i ) {
		const MOAISurface2D& surface = this->mSurfaceBuffer.mSurfaces [ i ];
		if ( IS_FLOOR ( surface )) continue;
		
		// ignore walls facing away from loc
		float dist = USDist::PointToPlane2D ( this->mLoc, surface );
		if ( dist < 0.0f ) continue;
		
		// this gets the distance from the edge of the circle to the closest feature on the surface
		// if the feature is inside the circle a negative value will be returned
		float wallDepth = surface.GetCircleDepthAlongRay ( this->mLoc, this->mFloorTangent ); // TODO: return bool; ignore walls behind circle

		// sort left and right wall info
		// we always want the closest wall
		if ( surface.mNorm.mX > 0.0f ) {
			if ( !wallToLeft || ( wallDepth < leftWallDepth )) {
				leftWallDepth = wallDepth;
			}
			wallToLeft = true;
		}
		else {
			if ( !wallToRight || ( wallDepth < rightWallDepth )) {
				rightWallDepth = wallDepth;
			}
			wallToRight = true;
		}
	}
	
	if ( wallToLeft || wallToRight ) {
		this->ApplyWallShoveOnFloor ( wallToLeft, wallToRight, leftWallDepth, rightWallDepth );
	}
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::DoFloorStep () {
	
	this->CalculateWallShoveOnFloor ();
	
	USVec2D move = this->mProjectedMove;
	
	if ( move.LengthSquared () < 0.00001f ) {
		this->mState = STATE_DONE;
		return;
	}
	
	float bestTime = 1.0f;
	float bestDot = 0.0f;
	float bridgeTime = 0.0f;
	const MOAISurface2D* hit = 0;
	
	for ( u32 i = 0; i < this->mSurfaceBuffer.mTop; ++i ) {
		
		const MOAISurface2D& surface = this->mSurfaceBuffer.mSurfaces [ i ];
		//if (( &surface == this->mFloor ) || ( !IS_FLOOR ( surface ))) continue;
		if ( !IS_FLOOR ( surface )) continue;
		
		float dot = surface.mNorm.Dot ( move ); // cos between move and surface
		float time = 0.0f;
			
		if (( bestDot < 0.0f ) && ( dot > 0.0f )) continue; // if we've hit a valley, ignore all peaks
		if ( surface.IsLeaving ( this->mFoot, move, -0.0001f )) continue; // ignore any surface we're leaving
		
		// floor is parallel to move, so process as bridge
		if (( dot < 0.000001f ) && ( dot > -0.000001f )) {
			
			// if we're on a bridge, take bridge as new floor (and bridge time) if end is farther than current bridge
			if ( surface.IsBridge ( this->mFoot, move, 0.0001f, time )) {
				if ( time > bridgeTime ) {
					bridgeTime = time;
					this->mFloor = &surface;
				}
			}

			if (( bestDot > 0.0f ) && ( bestTime < ( bridgeTime - 0.0001f ))) {
				bestTime = 1.0f;
				bestDot = 0.0f;
				hit = 0;
			}
		}
		else {
		
			if ( !surface.GetRayHit ( this->mFoot, move, 0.0001f, time )) continue; // ignore surface w/ no valid hit
			
			//printf ( "RAY HIT: %f %f %p (%f, %f)\n", time, dot, &surface, surface.mNorm.mX, surface.mNorm.mY );
			
			if ( time > 1.0f ) continue; // won't hit this move, so ignore
			if ( time < -0.0001f ) continue; // floor is behind us
			
			// skip if surface is a peak and peak is closer than end of current bridge
			if (( dot > 0.0f ) && ( time < ( bridgeTime - 0.0001f ))) continue;
			
			if (( time < ( bestTime + 0.0001f )) && ( time > ( bestTime - 0.0001f ))) {

				// ignore collision if we're on its edge
				if ( surface.IsOnEdge ( this->mFoot, 0.00001f )) continue;
				
				// break ties by always taking the steepest valley
				if ( dot > bestDot ) continue;
			}
			else {
				
				USVec2D hitLoc = move;
				hitLoc.Scale ( time );
				hitLoc.Add ( this->mFoot );
				
				if ( surface.IsLeaving ( hitLoc, move, -0.0001f )) continue;
				
				if ( dot > 0.0f ) {
					// surface is a peak
					// last found a peak, so only take farther hits UNLESS they are valleys
					if (( bestDot > 0.0f ) && ( dot > 0.0f ) && ( time < bestTime )) continue; 
				}
				else {
					// surface is a valley
					// last found a valley, so only take nearer hits
					// remember: after finding any valley, all peaks will be ignored
					if (( bestDot < 0.0f ) && ( time > bestTime )) continue; 
				}
			}
			
			bestTime = time;
			bestDot = dot;
			hit = &surface;
		}
	}
	
	this->ApplyMoveOnFloor ( hit, bestTime );
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::Move ( MOAIPlatformerBody2D& body ) {

	// gather surfaces for move
	// if snap - process snap
	//		gap filling - when snapping, use snap span to place dummy platform
	// process wall overlaps - build up 'push' vector
	// determing if move possible - cannot move into 'push' vector
	// resolve move/push
	//		idea of push is to get platformer out of wall overlap
	//		push gets added to move
	//		move is processed until another wall is hit
	//		when wall is hit, move is nilled out and remainder of push is handled
	//		push is 'soft' - if body is overlapped on boths sides, body should resolve to center between walls

	this->mCeilCos = body.mCeilCos;
	this->mFloorCos = body.mFloorCos;
	
	this->mLoc.Init ( 0.0f, 0.0f );
	this->mFoot.Init ( 0.0f, -(( body.mSkirt / body.mVRad ) + 1.0f ));
	this->mUp.Init ( 0.0f, 1.0f );
	
	this->mMove = body.mMove;
	this->mMove.Scale ( 1.0f / body.mHRad, 1.0f / body.mVRad );
	this->mProjectedMove.Init ( this->mMove.mX, 0.0f );
	this->mProjectedMoveDist = this->mMove.mX;
	
	this->mFloorNorm.Init ( 0.0f, 1.0f );
	this->mFloorTangent.Init ( 1.0f, 0.0f );
	
	USBox bounds;
	body.GetPropBounds ( bounds );
	bounds.Transform ( body.mLocalToWorldMtx );
	bounds.Inflate ( bounds.Width ());
	body.GatherSurfacesForBounds ( this->mSurfaceBuffer, bounds );

	this->SnapUp ();

	u32 steps = 0;
	for ( ; steps < MAX_STEPS; ++steps ) {
		
		//if ( this->mMove.LengthSquared () > 0.00001f ) {
		//	if ( i == 0 ) printf ( "\n" );
		//	printf ( "step %d (%f, %f)\n", i, this->mMove.mX, this->mMove.mY );
		//}
		
		this->DoFloorStep ();
		
		if ( this->mState == STATE_DONE ) break;
		
		//switch ( this->mState ) {
		//
		//	// slide along floor
		//	case STATE_ON_FLOOR:
		//		this->DoFloorStep ();
		//	break;
		//	
		//	// ignore in-air for now
		//	case STATE_IN_AIR:
		//	case STATE_DONE:
		//	break;
		//}
	}
	
	body.mLoc.mX += this->mLoc.mX * body.mHRad;
	body.mLoc.mY += this->mLoc.mY * body.mVRad;
	body.mMove.Init ( 0.0f, 0.0f );
	
	body.mSteps = steps + 1;
	body.mCompleted = this->mState == STATE_DONE;
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::ProjectMove () {

	this->mProjectedMove = this->mMove;
	this->mProjectedMove.PerpProject ( this->mFloorNorm );
	this->mProjectedMoveDist = this->mProjectedMove.Dot ( this->mFloorTangent );
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::SetFloor ( const MOAISurface2D& floor ) {

	this->mState = STATE_ON_FLOOR;	
		
	this->mFloor = &floor;
	this->mFloorNorm = floor.mNorm;
	this->mFloorTangent = floor.mTangent;
}

//----------------------------------------------------------------//
void MOAIPlatformerFsm2D::SnapUp () {

	const MOAISurface2D* snapSurface = 0;
	float snapDist = -0.001f;
	float stem = this->mLoc.mY - this->mFoot.mY;

	for ( u32 i = 0; i < this->mSurfaceBuffer.mTop; ++i ) {
		
		const MOAISurface2D& surface = this->mSurfaceBuffer.mSurfaces [ i ];
		
		if ( !IS_FLOOR ( surface )) continue; // not a floor (TODO: pass in floor cos)
		if (( this->mFoot.mX < surface.mXMin ) || ( this->mFoot.mX > surface.mXMax )) continue; // loc not over surface
		if ( surface.IsLeaving ( this->mFoot, this->mMove, -0.0001f )) continue;
		
		float dist;
		if ( surface.GetRayHit ( this->mFoot, this->mUp, dist ))  {
			
			// bail if snap is above us or lower than last best snap
			if (( dist > stem ) || ( dist < snapDist )) continue;
			
			// 'snap' is true if we already have a valid snap
			if ( snapSurface ) {
				
				// looks like we have multiple floors to choose from...
				if ( dist > snapDist ) {
				
					// we have a clear winner
					snapSurface = &surface;
					snapDist = dist;
				}
				else {
					
					// snap is the same as the last snap...
					
					// break the tie
					// if there's a this->mMove, choose the surface with the steepest angle
					// (against the this->mMove)
					if ( this->mMove.mX > 0.0f ) {
						if ( surface.mNorm.mX < snapSurface->mNorm.mX ) {
							snapSurface = &surface;
						}
					}
					else if ( this->mMove.mX < 0.0f ) {
						if ( surface.mNorm.mX > snapSurface->mNorm.mX ) {
							snapSurface = &surface;
						}
					}
				}
			}
			else {
			
				// first floor, so go with it
				snapSurface = &surface;
				snapDist = dist;
			}
		}
	}
		
	if ( snapSurface ) {
	
		snapDist *= stem;
		this->mLoc.mY += snapDist;
		this->mFoot.mY += snapDist;
		
		this->SetFloor ( *snapSurface );
		this->ProjectMove ();
	}
	else {
		this->mFloor = 0;
		this->mState = STATE_IN_AIR;
	}
}