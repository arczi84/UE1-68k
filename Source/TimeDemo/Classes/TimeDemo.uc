//=============================================================================
// TimeDemo.
// Version 0.1 - by Mek.
//=============================================================================
class TimeDemo expands Info;

var HUD OldHUD;
var TimeDemoIntroNullHud NewHUD;
var InterpolationPoint OldPoint;
var TimeDemoInterpolationPoint NewPoint;

event PostBeginPlay()
{
	local PlayerPawn P;
	local TimeDemo T;
	local InterpolationPoint I;

	foreach AllActors( class'TimeDemo', T )
		if ( T != Self )
			T.Destroy();

	foreach AllActors( class'PlayerPawn', P )
	{
		OldHUD = P.myHUD;
		// The intro can legitimately start without an existing HUD.  Using
		// OldHUD.Owner in that case creates the benchmark HUD with Owner=None,
		// which makes IntroNullHud.PostRender raise Accessed None every frame.
		NewHUD = spawn( class'TimeDemoIntroNullHud', P );
		P.myHUD = NewHUD;
	}

	OldPoint = None;
	foreach AllActors( class'InterpolationPoint', I, 'Path' )
	{
		if ( I.Position == 0 )
		{
			OldPoint = I;
			break;
		}
	}

	if ( OldPoint != None )
	{
		OldPoint.Tag = 'Stolen';
		NewPoint = Spawn( class'TimeDemoInterpolationPoint', OldPoint.Owner );
		NewPoint.SetLocation( OldPoint.Location );
		NewPoint.SetRotation( OldPoint.Rotation );
		NewPoint.Position = 0;
		NewPoint.RateModifier = OldPoint.RateModifier;
		NewPoint.bEndOfPath = OldPoint.bEndOfPath;
		NewPoint.Tag = 'Path';
		NewPoint.Next = OldPoint.Next;
		NewPoint.Prev = OldPoint.Prev;
		NewPoint.Prev.Next = NewPoint;
		NewPoint.Next.Prev = NewPoint;
		NewPoint.H = NewHUD;
	}

	Super.PostBeginPlay();
}

event Destroyed()
{
	local PlayerPawn P;

	foreach AllActors( class'PlayerPawn', P )
	{
		if ( P.myHUD != None )
			P.myHUD.Destroy();
		P.myHUD = OldHUD;
	}

	if ( OldPoint != None )
	{
		if ( NewPoint != None )
			NewPoint.Destroy();
		OldPoint.Tag = 'Path';
		OldPoint.Prev.Next = OldPoint;
		OldPoint.Next.Prev = OldPoint;
	}

	Super.Destroyed();
}
