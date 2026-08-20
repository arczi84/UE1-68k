//=============================================================================
// TimeDemoIntroNullHud.
// Original TimeDemo measurement HUD, with only Escape abort handling added.
//=============================================================================
class TimeDemoIntroNullHud expands IntroNullHud;

var float StartTime;
var float LastSecTime;
var float LastCycleTime;
var int FrameNum;
var int FrameLastSecond;
var int FrameLastCycle;
var string[100] CycleMessage;
var float LastSec;
var float MinFPS;
var float MaxFPS;
var bool bFinished;

event PostBeginPlay()
{
	FrameNum = 0;
	FrameLastSecond = 0;
	FrameLastCycle = 0;
	LastSec = 0;
	LastCycleTime = 0;
	CycleMessage = "";
	MinFPS = 0;
	MaxFPS = 0;
	bFinished = false;
}

function PreRender( canvas Canvas )
{
	if ( FrameNum == 0 )
	{
		StartTime = Level.TimeSeconds;
		LastSecTime = Level.TimeSeconds;
	}
	Super.PreRender(Canvas);
}

function PostRender( canvas Canvas )
{
	local float Avg;
	local TimeDemo T;

	// Never call IntroNullHud.PostRender without its PlayerPawn owner.  That
	// routine dereferences PlayerPawn(Owner) and would log once per frame.
	if ( PlayerPawn(Owner) == None )
		return;

	// ShowMenu has already paused the standalone game.  Destroying TimeDemo
	// restores its saved HUD; that HUD draws the normal menu on the next frame.
	if ( PlayerPawn(Owner) != None && PlayerPawn(Owner).bShowMenu )
	{
		foreach AllActors( class'TimeDemo', T )
		{
			T.Destroy();
			return;
		}
	}

	if ( !bFinished )
	{
		FrameNum++;
		FrameLastSecond++;
		FrameLastCycle++;
		Avg = FrameNum / (Level.TimeSeconds - StartTime);

		if ( Level.TimeSeconds - LastSecTime > 1 )
		{
			LastSec = FrameLastSecond / (Level.TimeSeconds - LastSecTime);
			FrameLastSecond = 0;
			LastSecTime = Level.TimeSeconds;
		}

		if ( LastSec < MinFPS || MinFPS == 0 )
			MinFPS = LastSec;
		if ( LastSec > MaxFPS )
			MaxFPS = LastSec;
	}
	else
		Avg = FrameNum / (Level.TimeSeconds - StartTime);

	Canvas.Font = Canvas.MedFont;
	Canvas.SetPos(0, 24);
	Canvas.DrawText("Average: "$Avg$" FPS.");
	Canvas.SetPos(0, 36);
	Canvas.DrawText("Last Second: "$LastSec$" FPS.", false);
	Canvas.SetPos(0, 48);
	Canvas.DrawText("Minimum: "$MinFPS$" FPS.", false);
	Canvas.SetPos(0, 60);
	Canvas.DrawText("Maximum: "$MaxFPS$" FPS.", false);
	Canvas.SetPos(0, 72);
	Canvas.DrawText(CycleMessage, false);
	Super.PostRender(Canvas);
}

function StartCycle()
{
	LastCycleTime = Level.TimeSeconds;
	FrameLastCycle = 0;
	CycleMessage = "Timing one flyby cycle...";
}

function FinishCycle()
{
	local float CycleSeconds;
	local float CycleFPS;

	CycleSeconds = Level.TimeSeconds - LastCycleTime;
	if ( CycleSeconds > 0 )
		CycleFPS = FrameLastCycle / CycleSeconds;
	else
		CycleFPS = 0;
	CycleMessage = "Result: "$CycleFPS$" FPS ("$FrameLastCycle$
		" frames, "$CycleSeconds$" seconds)";
	bFinished = true;
	Level.BroadcastMessage(CycleMessage, true);
}

event Destroyed()
{
	local float Avg;

	Avg = FrameNum / (Level.TimeSeconds - StartTime);
	Level.BroadcastMessage(FrameNum$" frames rendered in "$
		(Level.TimeSeconds - StartTime)$" seconds. "$Avg$" FPS average.", true);
	Super.Destroyed();
}
