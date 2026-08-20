//=============================================================================
// UnrealVideoMenu
//=============================================================================
class UnrealVideoMenu expands UnrealMenu
	localized;

var float brightness;
var string[32] CurrentRes;
var string[255] AvailableRes;
var string[64] MenuValues[20];
var string[32] Resolutions[16];
var int resNum;
var int SoundVol, MusicVol;
var bool bLowTextureDetail;
var bool bSoftwareRenderer;

function ReadRenderer()
{
	local string[128] RendererClass;

	RendererClass = PlayerOwner.ConsoleCommandResult("GetRenderDevice");
	bSoftwareRenderer = InStr(RendererClass, "SoftDrv") >= 0;
}

function SelectRenderer(bool bSoftware)
{
	bSoftwareRenderer = bSoftware;
	if ( bSoftwareRenderer )
	{
		PlayerOwner.ConsoleCommand("SetRenderDevice Software");
	}
	else
	{
		PlayerOwner.ConsoleCommand("SetRenderDevice OpenGL");
	}
}

function bool ProcessLeft()
{
	if ( Selection == 1 )
	{
		Brightness = FMax(0.2, Brightness - 0.1);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager Brightness "$Brightness);
		PlayerOwner.ConsoleCommand("FLUSH");
		return true;
	}
	else if ( Selection == 3 )
	{
		ResNum--;
		if ( ResNum < 0 )
		{
			ResNum = ArrayCount(Resolutions);
			While ( Resolutions[ResNum] == "" )
				ResNum--;
		}
		MenuValues[3] = Resolutions[ResNum];
		return true;
	}	
	else if ( Selection == 4 )
	{
		SelectRenderer(!bSoftwareRenderer);
		return true;
	}
	else if ( Selection == 6 )
	{
		MusicVol = Max(0, MusicVol - 32);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.AudioDevice MusicVolume "$MusicVol);
		return true;
	}
	else if ( Selection == 7 )
	{
		SoundVol = Max(0, SoundVol - 32);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.AudioDevice SoundVolume "$SoundVol);
		return true;
	}	
	else if ( Selection == 5 )
	{
		bLowTextureDetail = !bLowTextureDetail;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager LowDetailTextures "$bLowTextureDetail);
		return true;
	}
	return false;
}

function bool ProcessRight()
{
	local string[255] ParseString;
	local string[32] FirstString;
	local int p;

	if ( Selection == 1 )
	{
		Brightness = FMin(1, Brightness + 0.1);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager Brightness "$Brightness);
		PlayerOwner.ConsoleCommand("FLUSH");
		return true;
	}
	else if ( Selection == 3 )
	{
		ResNum++;
		if ( (ResNum >= ArrayCount(Resolutions)) || (Resolutions[ResNum] == "") )
			ResNum = 0;
		MenuValues[3] = Resolutions[ResNum];
		return true;
	}	
	else if ( Selection == 4 )
	{
		SelectRenderer(!bSoftwareRenderer);
		return true;
	}
	else if ( Selection == 6 )
	{
		MusicVol = Min(255, MusicVol + 32);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.AudioDevice MusicVolume "$MusicVol);
		return true;
	}
	else if ( Selection == 7 )
	{
		SoundVol = Min(255, SoundVol + 32);
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.AudioDevice SoundVolume "$SoundVol);
		return true;
	}
	else if ( Selection == 5 )
	{
		bLowTextureDetail = !bLowTextureDetail;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager LowDetailTextures "$bLowTextureDetail);
		return true;
	}
	return false;
}		

function bool ProcessSelection()
{
	local Menu ChildMenu;

	if ( Selection == 2 )
	{
		PlayerOwner.ConsoleCommand("TOGGLEFULLSCREEN");
		CurrentRes = PlayerOwner.ConsoleCommandResult("GetCurrentRes");
		GetAvailableRes();
		return true;
	}
	else if ( Selection == 3 )
	{
		PlayerOwner.ConsoleCommand("SetRes "$MenuValues[3]);
		CurrentRes = PlayerOwner.ConsoleCommandResult("GetCurrentRes");
		GetAvailableRes();
		return true;
	}
	else if ( Selection == 4 )
	{
		SelectRenderer(!bSoftwareRenderer);
		return true;
	}
	else if ( Selection == 5 )
	{
		bLowTextureDetail = !bLowTextureDetail;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager LowDetailTextures "$bLowTextureDetail);
		return true;
	}
	else if ( Selection == 8 )
	{
		ChildMenu = spawn(class'UnrealPerformanceMenu', owner);
		if ( ChildMenu != None )
		{
			HUD(Owner).MainMenu = ChildMenu;
			ChildMenu.ParentMenu = self;
			ChildMenu.PlayerOwner = PlayerOwner;
		}
		return true;
	}
		
	return false;
}


function DrawMenu(canvas Canvas)
{
	local int StartX, StartY, Spacing, i, HelpPanelX;

	DrawBackGround(Canvas, (Canvas.ClipY < 250));
	HelpPanelX = 228;

	Spacing = Clamp(0.04 * Canvas.ClipY, 16, 32);
	StartX = Max(40, 0.5 * Canvas.ClipX - 120);

	DrawTitle(Canvas);
	StartY = Max(36, 0.5 * (Canvas.ClipY - MenuLength * Spacing - 128));

	// draw text
	DrawList(Canvas, false, Spacing, StartX, StartY);  

	// draw icons
	Brightness = float(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.ViewportManager Brightness"));
	DrawSlider(Canvas, StartX + 155, StartY + 1, (10 * Brightness - 2), 0, 1);

	SoundVol = int(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.AudioDevice SoundVolume"));
	MusicVol = int(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.AudioDevice MusicVolume"));
	DrawSlider(Canvas, StartX + 155, StartY + 5*Spacing + 1, MusicVol, 0, 32);
	DrawSlider(Canvas, StartX + 155, StartY + 6*Spacing + 1, SoundVol, 0, 32);

	if ( CurrentRes == "" )
		GetAvailableRes();
	else if ( AvailableRes == "" )
		GetAvailableRes();

	SetFontBrightness( Canvas, (Selection == 3) );
	Canvas.SetPos(StartX + 152, StartY + Spacing * 2);
	if ( MenuValues[3] ~= CurrentRes )
		Canvas.DrawText("["$MenuValues[3]$"]", false);
	else
		Canvas.DrawText(" "$MenuValues[3], false);
	Canvas.DrawColor = Canvas.Default.DrawColor;

	ReadRenderer();
	SetFontBrightness( Canvas, (Selection == 4) );
	Canvas.SetPos(StartX + 152, StartY + Spacing * 3);
	if ( bSoftwareRenderer )
		Canvas.DrawText("Software", false);
	else
		Canvas.DrawText("OpenGL", false);
	Canvas.DrawColor = Canvas.Default.DrawColor;

	bLowTextureDetail = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.ViewportManager LowDetailTextures"));
	SetFontBrightness( Canvas, (Selection == 5) );
	Canvas.SetPos(StartX + 152, StartY + Spacing * 4);
	if ( bLowTextureDetail )
		Canvas.DrawText("Low", false);
	else
		Canvas.DrawText("High", false);
	Canvas.DrawColor = Canvas.Default.DrawColor;

	// Draw help panel
	DrawHelpPanel(Canvas, StartY + MenuLength * Spacing, HelpPanelX);
}

function GetAvailableRes()
{
	local int p,i;
	local string[255] ParseString;

	AvailableRes = PlayerOwner.ConsoleCommandResult("GetRes");
	resNum = 0;
	ParseString = AvailableRes;
	p = InStr(ParseString, " ");
	while ( (ResNum < ArrayCount(Resolutions)) && (p != -1) ) 
	{
		Resolutions[ResNum] = Left(ParseString, p);
		ParseString = Right(ParseString, Len(ParseString) - p - 1);
		p = InStr(ParseString, " ");
		ResNum++;
	}

	Resolutions[ResNum] = ParseString;
	for ( i=ResNum+1; i< ArrayCount(Resolutions); i++ )
		Resolutions[i] = "";

	CurrentRes = PlayerOwner.ConsoleCommandResult("GetCurrentRes");
	MenuValues[3] = CurrentRes;
	for ( i=0; i< ResNum+1; i++ )
		if ( MenuValues[3] ~= Resolutions[i] )
		{
			ResNum = i;
			return;
		}

	ResNum = 0;
	MenuValues[3] = Resolutions[0];
}

defaultproperties
{
	 MenuTitle="AUDIO/VIDEO"
	 MenuList(1)="Brightness"
	 MenuList(2)="Toggle Fullscreen Mode"
	 MenuList(3)="Select Resolution"
	 MenuList(4)="Renderer"
	 MenuList(5)="Texture Detail"
	 MenuList(6)="Music Volume"
	 MenuList(7)="Sound Volume"
	 MenuList(8)="Performance"
     HelpMessage(1)="Adjust display brightness using the left and right arrow keys."
     HelpMessage(2)="Display Unreal in a window. Note that going to a software display mode may remove high detail actors that were visible with hardware acceleration."
	 HelpMessage(3)="Use the left and right arrows to select a resolution, and press enter to select this resolution."
	 HelpMessage(4)="Choose OpenGL or Software rendering. Restart Unreal to apply the change."
	 HelpMessage(5)="Use the low texture detail option to improve performance. Changes take effect on the next level change."
     HelpMessage(6)="Adjust the volume of the music using the left and right arrow keys."
     HelpMessage(7)="Adjust the volume of sound effects in the game using the left and right arrow keys."
	 HelpMessage(8)="Configure additional graphics options for better performance."
     MenuLength=8
}
