//=============================================================================
// TimeDemoInterpolationPoint.
// Original measurement path, stopped after one complete measured cycle.
//=============================================================================
class TimeDemoInterpolationPoint expands InterpolationPoint;

var TimeDemoIntroNullHud H;

function InterpolateEnd( actor Other )
{
	if ( H.LastCycleTime != 0 )
	{
		// Let the original HUD calculate and retain the completed-cycle result.
		H.StartCycle();
		Other.SetPhysics(PHYS_None);
		return;
	}

	H.StartCycle();
	Super.InterpolateEnd(Other);
}
