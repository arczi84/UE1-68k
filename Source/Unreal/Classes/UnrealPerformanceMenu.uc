//=============================================================================
// UnrealPerformanceMenu
// Amiga-oriented renderer performance controls.
//=============================================================================
class UnrealPerformanceMenu expands UnrealMenu
	localized;

var bool bDetailTextures;
var bool bTextureFiltering;
var bool bVolumetricLighting;
var bool bShinySurfaces;
var bool bCoronas;
var bool bHighDetailActors;
var bool bNoLighting;

function ReadSettings()
{
	bDetailTextures      = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.RenderDevice DetailTextures"));
	bTextureFiltering    = bool(PlayerOwner.ConsoleCommandResult("GetTextureFiltering"));
	bVolumetricLighting  = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.RenderDevice VolumetricLighting"));
	bShinySurfaces       = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.RenderDevice ShinySurfaces"));
	bCoronas             = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.RenderDevice Coronas"));
	bHighDetailActors    = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.RenderDevice HighDetailActors"));
	bNoLighting          = bool(PlayerOwner.ConsoleCommandResult("get ini:Engine.Engine.ViewportManager NoLighting"));
}

function bool ToggleSelection()
{
	ReadSettings();

	if ( Selection == 1 )
	{
		bDetailTextures = !bDetailTextures;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.RenderDevice DetailTextures "$bDetailTextures);
	}
	else if ( Selection == 2 )
	{
		bTextureFiltering = !bTextureFiltering;
		if ( bTextureFiltering )
			PlayerOwner.ConsoleCommand("SetTextureFiltering On");
		else
			PlayerOwner.ConsoleCommand("SetTextureFiltering Off");
	}
	else if ( Selection == 3 )
	{
		bVolumetricLighting = !bVolumetricLighting;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.RenderDevice VolumetricLighting "$bVolumetricLighting);
	}
	else if ( Selection == 4 )
	{
		bShinySurfaces = !bShinySurfaces;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.RenderDevice ShinySurfaces "$bShinySurfaces);
	}
	else if ( Selection == 5 )
	{
		bCoronas = !bCoronas;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.RenderDevice Coronas "$bCoronas);
	}
	else if ( Selection == 6 )
	{
		bHighDetailActors = !bHighDetailActors;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.RenderDevice HighDetailActors "$bHighDetailActors);
	}
	else if ( Selection == 7 )
	{
		bNoLighting = !bNoLighting;
		PlayerOwner.ConsoleCommand("set ini:Engine.Engine.ViewportManager NoLighting "$bNoLighting);
	}
	else
		return false;

	return true;
}

function bool ProcessLeft()
{
	return ToggleSelection();
}

function bool ProcessRight()
{
	return ToggleSelection();
}

function bool ProcessSelection()
{
	return ToggleSelection();
}

function DrawValue(canvas Canvas, int X, int Y, bool bEnabled, string[16] EnabledText, string[16] DisabledText)
{
	Canvas.SetPos(X, Y);
	if ( bEnabled )
		Canvas.DrawText(EnabledText, false);
	else
		Canvas.DrawText(DisabledText, false);
}

function DrawMenu(canvas Canvas)
{
	local int StartX, StartY, Spacing, HelpPanelX;

	DrawBackGround(Canvas, (Canvas.ClipY < 250));
	HelpPanelX = 228;
	Spacing = Clamp(0.04 * Canvas.ClipY, 16, 32);
	StartX = Max(40, 0.5 * Canvas.ClipX - 120);

	DrawTitle(Canvas);
	StartY = Max(36, 0.5 * (Canvas.ClipY - MenuLength * Spacing - 128));
	DrawList(Canvas, false, Spacing, StartX, StartY);

	ReadSettings();

	SetFontBrightness(Canvas, (Selection == 1));
	DrawValue(Canvas, StartX + 152, StartY, bDetailTextures, "On", "Off");
	SetFontBrightness(Canvas, (Selection == 2));
	DrawValue(Canvas, StartX + 152, StartY + Spacing, bTextureFiltering, "Bilinear", "Nearest");
	SetFontBrightness(Canvas, (Selection == 3));
	DrawValue(Canvas, StartX + 152, StartY + 2 * Spacing, bVolumetricLighting, "On", "Off");
	SetFontBrightness(Canvas, (Selection == 4));
	DrawValue(Canvas, StartX + 152, StartY + 3 * Spacing, bShinySurfaces, "On", "Off");
	SetFontBrightness(Canvas, (Selection == 5));
	DrawValue(Canvas, StartX + 152, StartY + 4 * Spacing, bCoronas, "On", "Off");
	SetFontBrightness(Canvas, (Selection == 6));
	DrawValue(Canvas, StartX + 152, StartY + 5 * Spacing, bHighDetailActors, "On", "Off");
	SetFontBrightness(Canvas, (Selection == 7));
	DrawValue(Canvas, StartX + 152, StartY + 6 * Spacing, !bNoLighting, "On", "Off");
	Canvas.DrawColor = Canvas.Default.DrawColor;

	DrawHelpPanel(Canvas, StartY + MenuLength * Spacing, HelpPanelX);
}

defaultproperties
{
	MenuTitle="PERFORMANCE"
	MenuList(1)="Detail Textures"
	MenuList(2)="Texture Filtering"
	MenuList(3)="Volumetric Lighting"
	MenuList(4)="Reflections"
	MenuList(5)="Coronas"
	MenuList(6)="High Detail Actors"
	MenuList(7)="Lighting"
	HelpMessage(1)="Disable the additional detail texture pass for a substantial speed increase."
	HelpMessage(2)="Use nearest filtering for a possible speed increase at the cost of pixelated textures."
	HelpMessage(3)="Disable volumetric lighting effects for better performance."
	HelpMessage(4)="Disable shiny and reflective surfaces for better performance."
	HelpMessage(5)="Disable light coronas for a small speed increase."
	HelpMessage(6)="Disable high detail actors. The change takes full effect after loading a level."
	HelpMessage(7)="Disable lighting for maximum speed. This greatly changes the appearance of the game."
	MenuLength=7
}
