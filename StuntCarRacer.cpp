//--------------------------------------------------------------------------------------
// File: StuntCarRacer.cpp
//
// NOTE: This project builds with Microsoft Visual C++ 2008 Express Edition and requires
// Microsoft DirectX SDK (April 2007).  It is based on examples from that DirectX SDK.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "dxstdafx.h"

#include "resource.h"

#include "StuntCarRacer.h"
#include "3D_Engine.h"
#include "Backdrop.h"
#include "Track.h"
#include "Car.h"
#include "Car_Behaviour.h"
#include "Opponent_Behaviour.h"
#include "wavefunctions.h"
#include "Atlas.h"
#include "version.h"

#ifdef linux
#include <unistd.h>
#define STRING "%S"
#else
#define STRING L"%s"
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


//-----------------------------------------------------------------------------
// Defines, constants, and global variables
//-----------------------------------------------------------------------------

#ifdef SMOOTH
// Forward declaration for smooth shadow rendering
extern void UpdateInterpolatedShadow(float t);
#endif

#ifdef linux
#define DEFAULT_FRAME_GAP	(6)		// 4 Used to limit frame rate.  Amiga StuntCarRacer uses value of 6 (called MIN.FRAMES)
#else
#define DEFAULT_FRAME_GAP	(5)
#endif

#ifdef SMOOTH
#define DEFAULT_SMOOTH_PHYSICS_RATE (10.0f)	// Default physics rate for SMOOTH mode (Hz)
#endif

// Game timing notes:
// - Original Amiga PAL: 50Hz / 6 frame gap = 8.333 updates/sec (0.12 sec/tick)
// - NTSC standard: 60Hz / 8 frame gap = 7.5 updates/sec (0.1333 sec/tick)
// - Windows default: 60Hz / 4 frame gap = 15 game logic updates per second
// - Lap timing uses game tick counter (globalGameTicks) instead of wall clock time
// - Each game tick represents one game logic update (independent of frame rate)
// - Conversion: 1 tick = 0.1333 seconds (based on NTSC timing for better game physics)
// - This makes lap times more accurate and consistent regardless of frameGap setting
// - Users can adjust frameGap with F9/F10 keys to speed up or slow down gameplay

#define	HEIGHT_ABOVE_ROAD	(100)

#define	FURTHEST_Z (131072.0f)

GameModeType GameMode = TRACK_MENU;

// Both the following are used for keyboard input
UINT keyPress = '\0';
DWORD lastInput = 0;

static IDirectSound8 *ds;
IDirectSoundBuffer8 *WreckSoundBuffer = NULL;
IDirectSoundBuffer8 *HitCarSoundBuffer = NULL;
IDirectSoundBuffer8 *GroundedSoundBuffer = NULL;
IDirectSoundBuffer8 *CreakSoundBuffer = NULL;
IDirectSoundBuffer8 *SmashSoundBuffer = NULL;
IDirectSoundBuffer8 *OffRoadSoundBuffer = NULL;
IDirectSoundBuffer8 *EngineSoundBuffers[8] = {NULL};

IDirect3DTexture9 *g_pAtlas = NULL;

int wideScreen = 0;

static long frameGap = DEFAULT_FRAME_GAP;
#ifdef SMOOTH
static float smoothPhysicsRate = DEFAULT_SMOOTH_PHYSICS_RATE; // Physics rate for SMOOTH mode (Hz)
#endif
static bool bFrameMoved = FALSE;

bool bShowStats = FALSE;
bool bNewGame = FALSE;
#ifdef SMOOTH
static bool bNeedInterpolatorInit = FALSE;
#endif
bool bPaused = FALSE;
bool bPlayerPaused = FALSE;
bool bOpponentPaused = FALSE;
static bool bPrevPaused = FALSE;
long bTrackDrawMode = 0;
bool bOutsideView = FALSE;
long engineSoundPlaying = FALSE;
double gameStartTime, gameEndTime;
bool bSuperLeague = FALSE;

#if defined(DEBUG) || defined(_DEBUG)
FILE *out;
bool bTestKey = FALSE;
char OutputFile[] = "SCRlog.txt";
long VALUE1 = 1, VALUE2 = 2, VALUE3 = 3;
#endif

extern long TrackID;
extern long boostReserve, boostUnit, StandardBoost, SuperBoost;
extern long INITIALISE_PLAYER;
extern bool raceFinished, raceWon;
extern long lapNumber[];

// League / Super League variable
extern long damaged_limit;
extern long road_cushion_value;
extern long engine_power;
extern long boost_unit_value;
extern long opp_engine_power;

//-----------------------------------------------------------------------------
// Static variables
//-----------------------------------------------------------------------------
// Player 1 orientation
static long player1_x = 0,
			player1_y = 0,
			player1_z = 0;

static long player1_x_angle = (0<<6),
			player1_y_angle = (0<<6),
			player1_z_angle = (0<<6);

// Opponent orientation
static long opponent_x = 0,
			opponent_y = 0,
			opponent_z = 0;

static float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;

// Viewpoint 1 orientation
static long viewpoint1_x, viewpoint1_y, viewpoint1_z;
static long viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle;

// Target (lookat) point
static long target_x, target_y, target_z;

/**************************************************************************
  DSInit

  Description:
    Initialize all the DirectSound specific stuff
 **************************************************************************/

bool DSInit()
	{
    HRESULT err;

	//
	//	First create a DirectSound object

	err = DirectSoundCreate8(NULL, &ds, NULL);

    if (err != DS_OK)
        return FALSE;

	//
	//	Now set the cooperation level

    err = ds->SetCooperativeLevel(DXUTGetHWND(), DSSCL_NORMAL );

    if (err != DS_OK)
        return FALSE;
	
	return TRUE;
	}

/**************************************************************************
  DSSetMode

	Initialises all DirectSound samples etc

 **************************************************************************/

bool DSSetMode()
	{
	int i;

	// Amiga channels 1 and 2 are right side, channels 0 and 3 are left side

	if ((WreckSoundBuffer = MakeSoundBuffer(ds, L"WRECK")) == NULL)
		return FALSE;
	WreckSoundBuffer->SetPan(DSBPAN_RIGHT);
	WreckSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

	if ((HitCarSoundBuffer = MakeSoundBuffer(ds, L"HITCAR")) == NULL)
		return FALSE;
	HitCarSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
	HitCarSoundBuffer->SetPan(DSBPAN_RIGHT);
	HitCarSoundBuffer->SetVolume(AmigaVolumeToDirectX(56));

	if ((GroundedSoundBuffer = MakeSoundBuffer(ds, L"GROUNDED")) == NULL)
		return FALSE;
	GroundedSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 400);
	GroundedSoundBuffer->SetPan(DSBPAN_RIGHT);

	if ((CreakSoundBuffer = MakeSoundBuffer(ds, L"CREAK")) == NULL)
		return FALSE;
	CreakSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 238);
	CreakSoundBuffer->SetPan(DSBPAN_RIGHT);
	CreakSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

	if ((SmashSoundBuffer = MakeSoundBuffer(ds, L"SMASH")) == NULL)
		return FALSE;
	SmashSoundBuffer->SetFrequency(AMIGA_PAL_HZ / 280);
	SmashSoundBuffer->SetPan(DSBPAN_LEFT);
	SmashSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

	if ((OffRoadSoundBuffer = MakeSoundBuffer(ds, L"OFFROAD")) == NULL)
		return FALSE;
	OffRoadSoundBuffer->SetPan(DSBPAN_RIGHT);
	OffRoadSoundBuffer->SetVolume(AmigaVolumeToDirectX(64));

	if ((EngineSoundBuffers[0] = MakeSoundBuffer(ds, L"TICKOVER")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[1] = MakeSoundBuffer(ds, L"ENGINEPITCH2")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[2] = MakeSoundBuffer(ds, L"ENGINEPITCH3")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[3] = MakeSoundBuffer(ds, L"ENGINEPITCH4")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[4] = MakeSoundBuffer(ds, L"ENGINEPITCH5")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[5] = MakeSoundBuffer(ds, L"ENGINEPITCH6")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[6] = MakeSoundBuffer(ds, L"ENGINEPITCH7")) == NULL)
		return FALSE;
	if ((EngineSoundBuffers[7] = MakeSoundBuffer(ds, L"ENGINEPITCH8")) == NULL)
		return FALSE;

	for (i = 0; i < 8; i++)
	{
		EngineSoundBuffers[i]->SetPan(DSBPAN_LEFT);
		// Original Amiga volume was 48, but have reduced this for testing
		EngineSoundBuffers[i]->SetVolume(AmigaVolumeToDirectX(48/2));
	}

	return TRUE;
	}

/**************************************************************************
  DSTerm
 **************************************************************************/

void DSTerm()
	{
    if (WreckSoundBuffer)		WreckSoundBuffer->Release(),	WreckSoundBuffer = NULL;
    if (HitCarSoundBuffer)		HitCarSoundBuffer->Release(),	HitCarSoundBuffer = NULL;
    if (GroundedSoundBuffer)	GroundedSoundBuffer->Release(),	GroundedSoundBuffer = NULL;
    if (CreakSoundBuffer)		CreakSoundBuffer->Release(),	CreakSoundBuffer = NULL;
    if (SmashSoundBuffer)		SmashSoundBuffer->Release(),	SmashSoundBuffer = NULL;
    if (OffRoadSoundBuffer)		OffRoadSoundBuffer->Release(),	OffRoadSoundBuffer = NULL;

	for (int i = 0; i < 8; i++)
	{
		if (EngineSoundBuffers[i]) EngineSoundBuffers[i]->Release(), EngineSoundBuffers[i] = NULL;
	}

    if (ds) ds->Release(), ds = NULL;
	}

/*	======================================================================================= */
/*	Function:		InitialiseData															*/
/*																							*/
/*	Description:																			*/
/*	======================================================================================= */

static long InitialiseData( void )
	{
	long success = FALSE;
#if defined(DEBUG) || defined(_DEBUG)
	errno_t err;

	if ((err = fopen_s( &out, OutputFile, "w" )) != 0)
		return FALSE;
#endif

	CreateSinCosTable();

	ConvertAmigaTrack(LITTLE_RAMP);

	// Seed the random-number generator with current time so that
	// the numbers will be different every time we run
	srand( (unsigned)time( NULL ) );

	success = TRUE;

	return(success);
	}

/*	======================================================================================= */
/*	Function:		FreeData																*/
/*																							*/
/*	Description:																			*/
/*	======================================================================================= */

static void FreeData( void )
	{
	FreeTrackData();
	DSTerm();
#if defined(DEBUG) || defined(_DEBUG)
	fclose( out );
#endif
//	CloseAmigaRecording();
	return;
	}

/*	======================================================================================= */
/*	Function:		GetScreenDimensions														*/
/*																							*/
/*	Description:	Provide screen width and height											*/
/*	======================================================================================= */

/*	======================================================================================= */
/*	Function:		GetScreenDimensions															*/
/*																									*/
/*	Description:	Retrieve current screen/backbuffer width and height					*/
/*																									*/
/*	Parameters:		screen_width  - Output: current screen width in pixels				*/
/*					screen_height - Output: current screen height in pixels				*/
/*	======================================================================================= */

void GetScreenDimensions( long *screen_width,
						  long *screen_height )
	{
#ifdef linux
	/*const SDL_VideoInfo* info = SDL_GetVideoInfo();
	*screen_width = info->current_w;
	*screen_height = info->current_h; */
	*screen_width = (wideScreen)?800:640;
	*screen_height = 480;
#else
	const D3DSURFACE_DESC *desc;
	desc = DXUTGetBackBufferSurfaceDesc();

	*screen_width = desc->Width;
	*screen_height = desc->Height;
#endif
	}

//--------------------------------------------------------------------------------------
// Colours
//--------------------------------------------------------------------------------------

#define NUM_PALETTE_ENTRIES     (42+6)
//#define	PALETTE_COMPONENT_BITS	(8)		// bits per colour r/g/b component

static PALETTEENTRY SCPalette[NUM_PALETTE_ENTRIES] =
	{
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00},

		// car colours 1
		{0x00, 0x00, 0x00},
		{0x88, 0x00, 0x22},
		{0xaa, 0x00, 0x33},
		{0xcc, 0x00, 0x44},
		{0xee, 0x00, 0x55},
		{0x22, 0x22, 0x33},
		{0x44, 0x44, 0x44},
		{0x33, 0x33, 0x33},

		// car colours 2
		{0x00, 0x00, 0x00},
		{0x22, 0x00, 0x88},
		{0x33, 0x00, 0xaa},
		{0x44, 0x00, 0xcc},
		{0x55, 0x00, 0xee},
		{0x22, 0x22, 0x33},
		{0x44, 0x44, 0x44},
		{0x33, 0x33, 0x33},

		// track colours (i.e. Stunt Car Racer car colours)
		{0x00, 0x00, 0x00},
		{0x99, 0x99, 0x77},
		{0xbb, 0xbb, 0x99},
		{0xff, 0xff, 0x00},
		{0x99, 0xbb, 0x33},
		{0x55, 0x77, 0x77},
		{0x55, 0xbb, 0xff},
		{0x55, 0x99, 0xff},
		{0x33, 0x55, 0x77},
		{0x55, 0x00, 0x00}, // 9
		{0x77, 0x33, 0x33},	//10
		{0x99, 0x55, 0x55},
		{0xdd, 0x99, 0x99}, //12
		{0x77, 0x77, 0x55},
		{0xbb, 0xbb, 0xbb},
		{0xff, 0xff, 0xff},

		// extra track colours (altered super league)
		{ 51,   51,  119},	// SCR_BASE_COLOUR+16
		{119,  153,  119},
		{ 85,  153,   85},
		{0x00, 0x00, 0x55}, //19
		{0x33, 0x33, 0x77},	//20
		{0x99, 0x99, 0xdd}, //21
	};


DWORD SCRGB (long colour_index)		// return full RGB value
	{
	return(D3DCOLOR_XRGB(SCPalette[colour_index].peRed,
					SCPalette[colour_index].peGreen,
					SCPalette[colour_index].peBlue));
	}

DWORD Fill_Colour, Line_Colour;

void SetSolidColour (long colour_index)
	{
/*
    static DWORD reducedSCPalette[NUM_PALETTE_ENTRIES];
    static long first_time = TRUE;

    // make all reduced palette values on first call
    if (first_time)
        {
        long i;
        for (i = 0; i < NUM_PALETTE_ENTRIES; i++)
            {
            // reduce R/G/B to 5/8 of original
            reducedSCPalette[i] = D3DCOLOR_XRGB((5*SCPalette[i].peRed)/8,
			                               (5*SCPalette[i].peGreen)/8,
			                               (5*SCPalette[i].peBlue)/8);
            }

        first_time = FALSE;
        }

	Fill_Colour = reducedSCPalette[colour_index];
*/
	Fill_Colour = SCRGB(colour_index);
	}


void SetLineColour (long colour_index)
	{
	Line_Colour = SCRGB(colour_index);
	}


void SetTextureColour (long colour_index)
	{
	Fill_Colour = SCRGB(colour_index);
	}

#ifdef NOT_USED
/*	======================================================================================= */
/*	Function:		EnforceConstantFrameRate												*/
/*																							*/
/*	Description:	Attempt to keep frame rate close to MAX_FRAME_RATE						*/
/*	======================================================================================= */

static void EnforceConstantFrameRate( long max_frame_rate )
	{
	static long first_time = TRUE;

	static DWORD last_time_ms;
	DWORD this_time_ms, frame_time_ms;
	DWORD min_frame_time_ms = (1000/max_frame_rate);
	long remaining_ms;	// use long because it is signed (DWORD isn't)


	if (first_time)
		{
		first_time = FALSE;
		last_time_ms = timeGetTime();
		}
	else
	{
	this_time_ms = timeGetTime();
	frame_time_ms = this_time_ms - last_time_ms;

	remaining_ms = static_cast<long>(min_frame_time_ms) - static_cast<long>(frame_time_ms);
	last_time_ms = this_time_ms;	if (remaining_ms > 0)
		{
		Sleep(remaining_ms);
		last_time_ms += static_cast<DWORD>(remaining_ms);
		}
	}	return;
	}
#endif

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
#ifdef linux
TTF_Font *g_pFont = NULL;
TTF_Font *g_pFontLarge = NULL;
GLuint   g_pSprite = 0;	// Texture for batching text calls
#else
ID3DXFont *g_pFont = NULL;         // Font for drawing text
ID3DXFont *g_pFontLarge = NULL;    // Font for drawing large text

/*	======================================================================================= */
/*	Function:		GetTextScale															*/
/*																									*/
/*	Description:	Calculate text scaling factor based on current vs base resolution		*/
/*					Used to scale font sizes and text positions for different window sizes	*/
/*																									*/
/*	Returns:		Scaling factor (1.0 = base resolution, 2.0 = double size, etc.)		*/
/*	======================================================================================= */

// Helper function to get text scale based on current resolution
float GetTextScale()
{
	long current_width, current_height;
	GetScreenDimensions(&current_width, &current_height);
	float base_width = wideScreen ? static_cast<float>(BASE_WIDTH_WIDESCREEN) : static_cast<float>(BASE_WIDTH_STANDARD);
	return static_cast<float>(current_width) / base_width;
}

/*	======================================================================================= */
/*	Function:		FormatLapTime																	*/
/*																										*/
/*	Description:	Format lap time in seconds to mm:ss.ss string							*/
/*																										*/
/*	Returns:		Formatted time string in provided buffer									*/
/*	======================================================================================= */

void FormatLapTime(double timeInSeconds, WCHAR* buffer, size_t bufferSize)
{
	if (timeInSeconds <= 0.0)
	{
		StringCchPrintf(buffer, bufferSize, L"--:--.--");
		return;
	}
	
	int totalCentiseconds = static_cast<int>(timeInSeconds * 100.0);
	int minutes = totalCentiseconds / 6000;
	int seconds = (totalCentiseconds % 6000) / 100;
	int centiseconds = totalCentiseconds % 100;
	
	StringCchPrintf(buffer, bufferSize, L"%02d:%02d.%02d", minutes, seconds, centiseconds);
}

ID3DXSprite *g_pSprite = NULL;       // Sprite for batching draw text calls
#endif

#ifndef linux
//--------------------------------------------------------------------------------------
// Rejects any devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsDeviceAcceptable( D3DCAPS9 *pCaps, D3DFORMAT AdapterFormat, 
                                  D3DFORMAT BackBufferFormat, bool bWindowed, void *pUserContext )
{
    // Typically want to skip backbuffer formats that don't support alpha blending
    IDirect3D9 *pD3D = DXUTGetD3DObject(); 
    if ( FAILED( pD3D->CheckDeviceFormat( pCaps->AdapterOrdinal, pCaps->DeviceType,
                    AdapterFormat, D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, 
                    D3DRTYPE_TEXTURE, BackBufferFormat ) ) )
        return false;

    return true;
}


//--------------------------------------------------------------------------------------
// Before a device is created, modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings *pDeviceSettings, const D3DCAPS9 *pCaps, void *pUserContext )
{
    // For the first device created if its a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if (s_bFirstTime)
    {
        s_bFirstTime = false;
        if ( pDeviceSettings->DeviceType == D3DDEVTYPE_REF )
            DXUTDisplaySwitchingToREFWarning();
    }

    return true;
}


//--------------------------------------------------------------------------------------
// Create any D3DPOOL_MANAGED resources here 
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnCreateDevice( IDirect3DDevice9 *pd3dDevice, const D3DSURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext )
{
    HRESULT hr;

//    V_RETURN( g_DialogResourceManager.OnCreateDevice( pd3dDevice ) );
//    V_RETURN( g_SettingsDlg.OnCreateDevice( pd3dDevice ) );

    // Initialize the fonts with scaled sizes
	float textScale = GetTextScale();
    V_RETURN( D3DXCreateFont( pd3dDevice, static_cast<int>(15 * textScale), 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, 
                              OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
                              L"Arial", &g_pFont ) );

    V_RETURN( D3DXCreateFont( pd3dDevice, static_cast<int>(25 * textScale), 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, 
                              OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
                              L"Arial", &g_pFontLarge ) );

	// Set the initial projection transform (will be updated in OnResetDevice and when resizing)
	D3DXMATRIX matProj;
	FLOAT fAspect = pBackBufferSurfaceDesc->Width / static_cast<FLOAT>(pBackBufferSurfaceDesc->Height);
	D3DXMatrixPerspectiveFovLH( &matProj, D3DX_PI/4, fAspect, 0.5f, FURTHEST_Z );
	pd3dDevice->SetTransform( D3DTS_PROJECTION, &matProj );

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Create any D3DPOOL_DEFAULT resources here 
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnResetDevice( IDirect3DDevice9 *pd3dDevice, 
                                const D3DSURFACE_DESC *pBackBufferSurfaceDesc, void *pUserContext )
{
    HRESULT hr;

//    V_RETURN( g_DialogResourceManager.OnResetDevice() );
//    V_RETURN( g_SettingsDlg.OnResetDevice() );

    // Recreate fonts with proper scaling for new resolution
	if (g_pFont)
	{
		g_pFont->Release();
		g_pFont = NULL;
	}
	if (g_pFontLarge)
	{
		g_pFontLarge->Release();
		g_pFontLarge = NULL;
	}
	
	float textScale = GetTextScale();
	V_RETURN( D3DXCreateFont( pd3dDevice, static_cast<int>(15 * textScale), 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, 
	                          OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
	                          L"Arial", &g_pFont ) );
	
	V_RETURN( D3DXCreateFont( pd3dDevice, static_cast<int>(25 * textScale), 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, 
	                          OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, 
	                          L"Arial", &g_pFontLarge ) );

    // Create a sprite to help batch calls when drawing many lines of text
    V_RETURN( D3DXCreateSprite( pd3dDevice, &g_pSprite ) );

	if (FAILED(D3DXCreateTextureFromResource(pd3dDevice, NULL, L"ATLAS", &g_pAtlas)))
	{
		OutputDebugStringW(L"ERROR: Failed to create texture from ATLAS resource\n");
		return E_FAIL;
	}

	InitAtlasCoord();

	if ((hr = CreatePolygonVertexBuffer(pd3dDevice)) != S_OK)
	{
		OutputDebugStringW(L"ERROR: Failed to create polygon vertex buffer\n");
		return hr;
	}
	if ((hr = CreateTrackVertexBuffer(pd3dDevice)) != S_OK)
	{
		OutputDebugStringW(L"ERROR: Failed to create track vertex buffer\n");
		return hr;
	}
	if ((hr = CreateShadowVertexBuffer(pd3dDevice)) != S_OK)
	{
		OutputDebugStringW(L"ERROR: Failed to create shadow vertex buffer\n");
		return hr;
	}
	if ((hr = CreateCarVertexBuffer(pd3dDevice)) != S_OK)
	{
		OutputDebugStringW(L"ERROR: Failed to create car vertex buffer\n");
		return hr;
	}
	if ((hr = CreateCockpitVertexBuffer(pd3dDevice)) != S_OK)
	{
		OutputDebugStringW(L"ERROR: Failed to create cockpit vertex buffer\n");
		return hr;
	}

	// Set the projection transform (view and world are updated per frame)
    D3DXMATRIX matProj;
	FLOAT fAspect = pBackBufferSurfaceDesc->Width / static_cast<FLOAT>(pBackBufferSurfaceDesc->Height);
    D3DXMatrixPerspectiveFovLH( &matProj, D3DX_PI/4, fAspect, 0.5f, FURTHEST_Z );
    pd3dDevice->SetTransform( D3DTS_PROJECTION, &matProj );

    pd3dDevice->SetRenderState( D3DRS_ZENABLE,      TRUE );
    pd3dDevice->SetRenderState( D3DRS_SHADEMODE,    D3DSHADE_FLAT );
    pd3dDevice->SetRenderState( D3DRS_LIGHTING,     FALSE );

	// Disable texture mapping by default (only DrawTrack() enables it)
	pd3dDevice->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );

	return S_OK;
}
#else
// some helper functions....
void CreateFonts()
{
	if(!TTF_WasInit() && TTF_Init()==-1) {
		printf("TTF_Init: %s\n", TTF_GetError());
		exit(1);
	}

	if (g_pFont==NULL)
	{
		g_pFont = TTF_OpenFont("DejaVuSans-Bold.ttf", 15);
	}
	if (g_pFontLarge==NULL)
	{
		g_pFontLarge = TTF_OpenFont("DejaVuSans-Bold.ttf", 25);
	}
	printf("Font created (%p / %p)\n", g_pFont, g_pFontLarge);
}
void CloseFonts()
{
	if (g_pFont!=NULL)
	{
		TTF_CloseFont(g_pFont);
		g_pFont = 0;
	}
	if (g_pFontLarge!=NULL)
	{
		TTF_CloseFont(g_pFontLarge);
		g_pFontLarge = NULL;
	}
}
void LoadTextures()
{
	if (!g_pAtlas) g_pAtlas = new IDirect3DTexture9();
	g_pAtlas->LoadTexture("Bitmap/atlas.png");
	InitAtlasCoord();
	printf("Texture loaded\n");
}
void CreateBuffers(IDirect3DDevice9 *pd3dDevice)
{
	if (CreatePolygonVertexBuffer(pd3dDevice) != S_OK)
		printf("Error creating PolygonVertexBuffer\n");
	if (CreateTrackVertexBuffer(pd3dDevice) != S_OK)
		printf("Error creating TrackVertexBuffer\n");
	if (CreateShadowVertexBuffer(pd3dDevice) != S_OK)
		printf("Error creating ShadowVertexBuffer\n");
	if (CreateCarVertexBuffer(pd3dDevice) != S_OK)
		printf("Error creating CarVertexBuffer\n");
	if (CreateCockpitVertexBuffer(pd3dDevice) != S_OK)
		printf("Error creating CarVertexBuffer\n");

}
#endif	//!linux
/*	======================================================================================= */
/*	Function:		CalcTrackMenuViewpoint													*/
/*																							*/
/*	Description:	*/
/*	======================================================================================= */

static void CalcTrackMenuViewpoint( void )
{
static long circle_y_angle = 0;

short sin, cos;
long centre = (NUM_TRACK_CUBES * CUBE_SIZE)/2;
long radius = ((NUM_TRACK_CUBES - 2) * CUBE_SIZE)/PRECISION;

	// Target orientation - centre of world
	target_x = (NUM_TRACK_CUBES * CUBE_SIZE)/2;
	target_y = 0;
	target_z = (NUM_TRACK_CUBES * CUBE_SIZE)/2;

	// camera moves in a circle around the track
	if (!bPaused) circle_y_angle += 128;
	circle_y_angle &= (MAX_ANGLE - 1);

	GetSinCos(circle_y_angle, &sin, &cos);

	viewpoint1_x = centre + (sin * radius);
	viewpoint1_y = -CUBE_SIZE * 3;
	viewpoint1_z = centre + (cos * radius);

	LockViewpointToTarget(viewpoint1_x,
						  viewpoint1_y,
						  viewpoint1_z,
						  target_x,
						  target_y,
						  target_z,
						  &viewpoint1_x_angle,
						  &viewpoint1_y_angle);
	viewpoint1_z_angle = 0;
}

/*	======================================================================================= */
/*	Function:		CalcTrackPreviewViewpoint												*/
/*																							*/
/*	Description:	*/
/*	======================================================================================= */

#define NUM_PREVIEW_CAMERAS (9)

static void CalcTrackPreviewViewpoint( void )
{
	// Target orientation - opponent
	target_x = opponent_x,
	target_y = opponent_y,
	target_z = opponent_z;

#ifndef  PREVIEW_METHOD1
	long centre = (NUM_TRACK_CUBES * CUBE_SIZE)/2;

	viewpoint1_x = centre;

	if (TrackID == DRAW_BRIDGE)
		viewpoint1_y = opponent_y - (CUBE_SIZE*5)/2;	// Draw Bridge requires a higher viewpoint
	else
		viewpoint1_y = opponent_y - CUBE_SIZE/2;

	viewpoint1_z = centre;

    viewpoint1_x += (target_x-viewpoint1_x)/2;
    viewpoint1_z += (target_z-viewpoint1_z)/2;

	// lock viewpoint y angle to target
	LockViewpointToTarget(viewpoint1_x,
						  viewpoint1_y,
						  viewpoint1_z,
						  target_x,
						  target_y,
						  target_z,
						  &viewpoint1_x_angle,
						  &viewpoint1_y_angle);
#else
    // cameras - four at corners, four half way along, one at centre
    long camera_x[NUM_PREVIEW_CAMERAS] =
                                        {CUBE_SIZE,
                                         CUBE_SIZE,
                                         (NUM_TRACK_CUBES-1) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES-1) * CUBE_SIZE,
                                         //
                                         0,
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE,
                                         //
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE
                                        };
    long camera_z[NUM_PREVIEW_CAMERAS] =
                                        {CUBE_SIZE,
                                         (NUM_TRACK_CUBES-1) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES-1) * CUBE_SIZE,
                                         CUBE_SIZE,
                                         //
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES) * CUBE_SIZE,
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE,
                                         0,
                                         //
                                         (NUM_TRACK_CUBES/2) * CUBE_SIZE
                                        };

    // calculate nearest camera
    long camera, distance, shortest_distance = 0, nearest = 0;
    double o, a;
    for (camera = 0; camera < NUM_PREVIEW_CAMERAS; camera++)
        {
        o = double(camera_x[camera] - target_x);
        a = double(camera_z[camera] - target_z);
        distance = static_cast<long>(sqrt((o*o) + (a*a)));

        if (camera == 0)
            {
            shortest_distance = distance;
            nearest = camera;
            }
        else if (distance < shortest_distance)
            {
            shortest_distance = distance;
            nearest = camera;
            }
        }

	viewpoint1_x = camera_x[nearest];
	viewpoint1_y = player1_y - CUBE_SIZE/2;
	viewpoint1_z = camera_z[nearest];

	LockViewpointToTarget(viewpoint1_x,
						  viewpoint1_y,
						  viewpoint1_z,
						  target_x,
						  target_y,
						  target_z,
						  &viewpoint1_x_angle,
						  &viewpoint1_y_angle);
#endif

	viewpoint1_z_angle = 0;
}

/*	======================================================================================= */
/*	Backdrop angle smoothing															*/
/*	======================================================================================= */

#ifdef SMOOTH
// Backdrop angle smoothing
struct AngleInterpolator
{
	long old_x_angle, old_y_angle, old_z_angle;
	long new_x_angle, new_y_angle, new_z_angle;

	void UpdateAngles(long x_angle, long y_angle, long z_angle)
	{
		old_x_angle = new_x_angle;
		old_y_angle = new_y_angle;
		old_z_angle = new_z_angle;

		new_x_angle = x_angle;
		new_y_angle = y_angle;
		new_z_angle = z_angle;
	}

	// Helper to compute shortest angle difference considering wrapping
	long AngleDiff(long from, long to)
	{
		long diff = to - from;
		// Normalize to [-MAX_ANGLE/2, MAX_ANGLE/2] to take shortest path
		if (diff > MAX_ANGLE / 2)
			diff -= MAX_ANGLE;
		else if (diff < -MAX_ANGLE / 2)
			diff += MAX_ANGLE;
		return diff;
	}

	void GetInterpolatedAngles(float f, long& out_x, long& out_y, long& out_z)
	{
		// Use shortest path for angle interpolation
		out_x = old_x_angle + static_cast<long>(f * AngleDiff(old_x_angle, new_x_angle));
		out_y = old_y_angle + static_cast<long>(f * AngleDiff(old_y_angle, new_y_angle));
		out_z = old_z_angle + static_cast<long>(f * AngleDiff(old_z_angle, new_z_angle));
		
		// Ensure angles stay in valid range [0, MAX_ANGLE)
		out_x = (out_x + MAX_ANGLE) % MAX_ANGLE;
		out_y = (out_y + MAX_ANGLE) % MAX_ANGLE;
		out_z = (out_z + MAX_ANGLE) % MAX_ANGLE;
	}
};

AngleInterpolator BackdropAngleInterpolator;
#endif

/*	======================================================================================= */
/*	Function:		CalcGameViewpoint														*/
/*																							*/
/*	Description:	*/
/*	======================================================================================= */

static void CalcGameViewpoint( void )
{
long x_offset, y_offset, z_offset;

	if (bOutsideView)
	{
		// set Viewpoint 1 to behind Player 1
		// 04/11/1998 - would probably need to do a final rotation (i.e. of the trig. coefficients)
		//			    to allow a viewpoint with e.g. a different X angle to that of the player.
		//				For the car this would mean the following rotations: Y,X,Z, Y,X,Z, X
		//				For the viewpoint this would mean the following rotations: Y,X,Z, X (possibly!)
		CalcYXZTrigCoefficients(player1_x_angle,
								player1_y_angle,
								player1_z_angle);

		// vector from centre of car
		x_offset = 0;
		y_offset = 0xc0;
		z_offset = 0x300;
		WorldOffset(&x_offset, &y_offset, &z_offset);
		viewpoint1_x = (player1_x - x_offset);
		viewpoint1_y = (player1_y - y_offset);
		viewpoint1_z = (player1_z - z_offset);

		viewpoint1_x_angle = player1_x_angle;
		//viewpoint1_x_angle = (player1_x_angle + (48<<6)) & (MAX_ANGLE-1);
		viewpoint1_y_angle = player1_y_angle;
		//viewpoint1_y_angle = (player1_y_angle - (64<<6)) & (MAX_ANGLE-1);
		viewpoint1_z_angle = player1_z_angle;
		//viewpoint1_x_angle = 0;
		//viewpoint1_z_angle = 0;
	}
	else
	{
		viewpoint1_x = player1_x;
		viewpoint1_y = player1_y - (HEIGHT_ABOVE_ROAD << LOG_PRECISION);
//		viewpoint1_y = player1_y - (90 << LOG_PRECISION);
		viewpoint1_z = player1_z;

		viewpoint1_x_angle = player1_x_angle;
		viewpoint1_y_angle = player1_y_angle;
		viewpoint1_z_angle = player1_z_angle;
	}

#ifdef SMOOTH
	// Update backdrop angle interpolator
	BackdropAngleInterpolator.UpdateAngles(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
#endif
}

//--------------------------------------------------------------------------------------
// Handle updates to the scene
//--------------------------------------------------------------------------------------
#ifdef SMOOTH
struct MTXInterpolator
{
	D3DXMATRIX oldMatTrans, newMatTrans;
	D3DXQUATERNION oldQuatRot, newQuatRot;

	void UpdateMatrices(const D3DMATRIX& newTrans, const D3DMATRIX& newRot)
	{
		oldMatTrans = newMatTrans;
		oldQuatRot = newQuatRot;

		newMatTrans = newTrans;
		D3DXMATRIX newRotX = newRot;
		D3DXQuaternionRotationMatrix(&newQuatRot, &newRotX);
	}

	D3DXMATRIX CreateInterpolatedMtx(float f, bool view = false)
	{
		D3DXMATRIX result;
		D3DXMATRIX trans = oldMatTrans + f * (newMatTrans - oldMatTrans);

		D3DXQUATERNION rotQ;
		D3DXQuaternionSlerp(&rotQ, &oldQuatRot, &newQuatRot, f);
		D3DXMATRIX rot;
		D3DXMatrixRotationQuaternion(&rot, &rotQ);

		if (view)
			D3DXMatrixMultiply(&result, &trans, &rot);
		else
			D3DXMatrixMultiply(&result, &rot, &trans);
		return result;
	}
};


MTXInterpolator InterpolatorView;
MTXInterpolator InterpolatorCarOwn;
MTXInterpolator InterpolatorCarOpponent;

// Camera position interpolator for track preview
struct Vec3Interpolator
{
	long old_x, old_y, old_z;
	long new_x, new_y, new_z;

	void Update(long x, long y, long z)
	{
		old_x = new_x;
		old_y = new_y;
		old_z = new_z;
		new_x = x;
		new_y = y;
		new_z = z;
	}

	void GetInterpolated(float t, long& out_x, long& out_y, long& out_z)
	{
		out_x = old_x + static_cast<long>((new_x - old_x) * t);
		out_y = old_y + static_cast<long>((new_y - old_y) * t);
		out_z = old_z + static_cast<long>((new_z - old_z) * t);
	}
};

Vec3Interpolator CameraPositionInterpolator;

static D3DXMATRIX matWorldTrack;
#else
static D3DXMATRIX matWorldTrack, matWorldCar, matWorldOpponentsCar;
#endif

static void SetCarWorldTransform( void )
{
D3DXMATRIX matRot, matTemp, matTrans;

	D3DXMatrixIdentity(&matRot);
	float xa = ((static_cast<float>(player1_x_angle) * 2 * D3DX_PI) / 65536.0f);
	float ya = ((static_cast<float>(player1_y_angle) * 2 * D3DX_PI) / 65536.0f);
	float za = ((static_cast<float>(player1_z_angle) * 2 * D3DX_PI) / 65536.0f);
	// Produce and combine the rotation matrices
	D3DXMatrixRotationZ(&matTemp, za);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationX(&matTemp, xa);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationY(&matTemp, ya);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	// Produce the translation matrix
	// Position car slightly higher than wheel height (VCAR_HEIGHT/4) so wheels are fully visible
	D3DXMatrixTranslation( &matTrans, static_cast<float>(player1_x>>LOG_PRECISION), static_cast<float>(-player1_y>>LOG_PRECISION)+VCAR_HEIGHT/3, static_cast<float>(player1_z>>LOG_PRECISION) );
	// Combine the rotation and translation matrices to complete the world matrix
#ifdef SMOOTH
	InterpolatorCarOwn.UpdateMatrices(matTrans, matRot);
#else
	D3DXMatrixMultiply(&matWorldCar, &matRot, &matTrans);
#endif
	
}

static void SetOpponentsCarWorldTransform( void )
{
D3DXMATRIX matRot, matTemp, matTrans;

	D3DXMatrixIdentity(&matRot);
//	float xa = (((float)opponent_x_angle * 2 * D3DX_PI) / 65536.0f);
//	float ya = (((float)opponent_y_angle * 2 * D3DX_PI) / 65536.0f);
//	float za = (((float)opponent_z_angle * 2 * D3DX_PI) / 65536.0f);
	// Produce and combine the rotation matrices
	D3DXMatrixRotationZ(&matTemp, opponent_z_angle);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationX(&matTemp, opponent_x_angle);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationY(&matTemp, opponent_y_angle);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	// Produce the translation matrix
	// Position car at wheel height (VCAR_HEIGHT/4)
	D3DXMatrixTranslation( &matTrans, static_cast<float>(opponent_x>>LOG_PRECISION), static_cast<float>(-opponent_y>>LOG_PRECISION)+VCAR_HEIGHT/4, static_cast<float>(opponent_z>>LOG_PRECISION) );
	// Combine the rotation and translation matrices to complete the world matrix
#ifdef SMOOTH
	InterpolatorCarOpponent.UpdateMatrices(matTrans, matRot);
#else
	D3DXMatrixMultiply(&matWorldOpponentsCar, &matRot, &matTrans);
#endif
}


static void StopEngineSound( void )
{
	if (engineSoundPlaying)
	{
		for (int i = 0; i < 8; i++)
			EngineSoundBuffers[i]->Stop();

		engineSoundPlaying = FALSE;
	}
}


// Global game tick counter (increments once per game logic update)
static long globalGameTicks = 0;

// Helper function to access game tick counter from other modules
long GetCurrentGameTick()
{
	return globalGameTicks;
}

#ifdef SMOOTH
// Helper functions for interpolator initialization (call twice to set old=new)
static void InitializeBackdropInterpolator(long x_angle, long y_angle, long z_angle)
{
	BackdropAngleInterpolator.UpdateAngles(x_angle, y_angle, z_angle);
	BackdropAngleInterpolator.UpdateAngles(x_angle, y_angle, z_angle);
}

static void InitializeCameraPositionInterpolator(long x, long y, long z)
{
	CameraPositionInterpolator.Update(x, y, z);
	CameraPositionInterpolator.Update(x, y, z);
}

static void InitializeViewMatrixInterpolator()
{
	D3DXMATRIX matTrans, matRot, matTemp;
	D3DXMatrixTranslation(&matTrans, static_cast<float>(-viewpoint1_x), static_cast<float>(viewpoint1_y>>LOG_PRECISION), static_cast<float>(-viewpoint1_z));
	D3DXMatrixIdentity(&matRot);
	float xa = ((static_cast<float>(-viewpoint1_x_angle) * 2 * D3DX_PI) / 65536.0f);
	float ya = ((static_cast<float>(-viewpoint1_y_angle) * 2 * D3DX_PI) / 65536.0f);
	float za = ((static_cast<float>(-viewpoint1_z_angle) * 2 * D3DX_PI) / 65536.0f);
	D3DXMatrixRotationY(&matTemp, ya);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationX(&matTemp, xa);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	D3DXMatrixRotationZ(&matTemp, za);
	D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
	InterpolatorView.UpdateMatrices(matTrans, matRot);
	InterpolatorView.UpdateMatrices(matTrans, matRot);
}

static void InitializeOpponentCarInterpolator()
{
	SetOpponentsCarWorldTransform();
	SetOpponentsCarWorldTransform();
}

static void ResetOpponentCarInterpolator()
{
	D3DXMATRIX identity;
	D3DXMatrixIdentity(&identity);
	InterpolatorCarOpponent.UpdateMatrices(identity, identity);
	InterpolatorCarOpponent.UpdateMatrices(identity, identity);
}
#endif

#ifdef SMOOTH
struct Ticker
{
	Ticker(float newFPS)
	{
		TickFraction = 0.0f;
		TickPercent = 0.0f;
		TicksAccumulated = 0;
		DoFrame = true;
		SetTargetFPS(newFPS);
	}

	void SetTargetFPS(float newFPS)
	{
		TargetFPS = newFPS;
		TickDuration = 1.0f / TargetFPS;
		// Preserve the existing fraction so changing rate doesn't cause a skip,
		// but clamp it to just under one tick so we don't fire immediately.
		if (TickFraction >= TickDuration)
			TickFraction = TickDuration - 0.0001f;
		TickPercent = TickFraction / TickDuration;
		DoFrame = false;
	}

	void Update(float elapsedSeconds)
	{
		// Cap elapsed time to avoid a spiral of catch-up ticks after pauses or stutter.
		// At most allow catching up 4 ticks worth of time; anything beyond is discarded.
		const float maxAccumulation = TickDuration * 4.0f;
		if (elapsedSeconds > maxAccumulation)
			elapsedSeconds = maxAccumulation;

		TickFraction += elapsedSeconds;

		// Count how many full ticks are due (drain the accumulator fully)
		TicksAccumulated = 0;
		while (TickFraction >= TickDuration)
		{
			TickFraction -= TickDuration;
			++TicksAccumulated;
		}
		DoFrame = TicksAccumulated > 0;

		TickPercent = TickFraction / TickDuration;
	}

	float TargetFPS;
	float TickDuration;
	float TickFraction;
	float TickPercent;
	bool  DoFrame;
	int   TicksAccumulated;  // number of physics ticks due this frame (normally 0 or 1)
};


// Initialize with smoothPhysicsRate so F9/F10 can adjust it
Ticker GameTicker(smoothPhysicsRate);
Ticker SoundTicker(50.0f);
#endif

void CALLBACK OnFrameMove( IDirect3DDevice9 *pd3dDevice, double fTime, float fElapsedTime, void *pUserContext )
{
	static D3DXVECTOR3 vUpVec( 0.0f, 1.0f, 0.0f );
	static long frameCount = 0;
	DWORD input = lastInput;	// take copy of user input
	D3DXMATRIX matRot, matTemp, matTrans, matView;

#ifdef SMOOTH
	// Initialize interpolators on first frame to avoid flash
	static bool firstFrame = true;
	if (firstFrame)
	{
		firstFrame = false;
		// Calculate initial viewpoint for track menu
		CalcTrackMenuViewpoint();
		InitializeBackdropInterpolator(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
		
		// Initialize camera position interpolator with scaled track menu camera position
		long scaled_x = viewpoint1_x >> LOG_PRECISION;
		long scaled_y = viewpoint1_y >> LOG_PRECISION;
		long scaled_z = viewpoint1_z >> LOG_PRECISION;
		InitializeCameraPositionInterpolator(scaled_x, scaled_y, scaled_z);
		
		// Initialize opponent car interpolator to identity
		ResetOpponentCarInterpolator();
	}
#endif

#ifndef linux
	// crude 60fps cap method...
	static float lastFrame = 0.0f;
#define FPSMAX (1.0f/60.f)
	lastFrame += fElapsedTime;
	if (lastFrame < FPSMAX)
		return;
	lastFrame -= FPSMAX;
	// After FPS cap, use fixed timestep for frame logic to ensure consistent timing
	// regardless of display refresh rate (fixes slow motion bug on 165Hz/240Hz displays)
	fElapsedTime = FPSMAX;
#endif
	bFrameMoved = FALSE;
//	VALUE3 = frameGap;

	if (GameMode == GAME_OVER)
	{
		StopEngineSound();
		return;
	}

	// Handle pause state changes
	if (bPaused && !bPrevPaused)
	{
		// Just paused - nothing to do with tick-based timing
		StopEngineSound();
	}
	else if (!bPaused && bPrevPaused)
	{
		// Just unpaused - nothing to do with tick-based timing
	}
	else if (bPaused)
	{
		// Still paused - keep engine stopped
		StopEngineSound();
	}
	
	// Update previous pause state
	bPrevPaused = bPaused;

	if (TrackID == NO_TRACK)
		return;

	// Track preview and game mode run at reduced frame rate
	if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS))
	{
#ifdef SMOOTH
		GameTicker.Update(fElapsedTime);
		SoundTicker.Update(fElapsedTime);
#endif
		if (GameMode == GAME_IN_PROGRESS)
		{
			// Following function should run at 50Hz
#ifdef SMOOTH
			if (!bPaused && SoundTicker.DoFrame) FramesWheelsEngine(EngineSoundBuffers);
#else
			if (!bPaused) FramesWheelsEngine(EngineSoundBuffers);
#endif
		}

#ifdef SMOOTH
		if (!GameTicker.DoFrame)
		{
			if (GameMode == GAME_IN_PROGRESS)
			{
				D3DMATRIX viewMtx = InterpolatorView.CreateInterpolatedMtx(GameTicker.TickPercent, true);
				pd3dDevice->SetTransform(D3DTS_VIEW, &viewMtx);
			}
			else if (GameMode == TRACK_PREVIEW)
			{
				// Set view matrix for track preview on interpolation frames
				// Interpolate camera position (already scaled)
				long cam_x, cam_y, cam_z;
				CameraPositionInterpolator.GetInterpolated(GameTicker.TickPercent, cam_x, cam_y, cam_z);
				D3DXVECTOR3 vEyePt(static_cast<float>(cam_x), static_cast<float>(-cam_y), static_cast<float>(cam_z));
				// Lookat the interpolated car position
				D3DXMATRIX carMtx = InterpolatorCarOpponent.CreateInterpolatedMtx(GameTicker.TickPercent);
				D3DXVECTOR3 vLookatPt(carMtx._41, carMtx._42, carMtx._43);
				D3DXMatrixLookAtLH(&matView, &vEyePt, &vLookatPt, &vUpVec);
				pd3dDevice->SetTransform(D3DTS_VIEW, &matView);
			}
			return;  // Early return for interpolation frames (no game logic)
		}

		// Increment game tick counter when game logic runs (every GameTicker.DoFrame)
		if (!bPaused)
		globalGameTicks++;

#else
		if (frameCount > 0)
			--frameCount;

		if (frameCount == 0)
		{
			frameCount = frameGap;

			// Increment game tick counter (game logic updates once per frameGap frames)
			if (!bPaused)
				globalGameTicks++;
		}
		else
		{
			return;
		}
#endif
	}
	else if (GameMode == TRACK_MENU)
	{
		// Stop engine sound if at track menu or if game has finished
		StopEngineSound();
	}

	if ((GameMode == GAME_IN_PROGRESS) && (keyPress == 'R'))
	{
		// point car in opposite direction
		player1_y_angle += _180_DEGREES;
		player1_y_angle &= (MAX_ANGLE-1);
		INITIALISE_PLAYER = TRUE;
		keyPress = '\0';
	}

	if (!bPaused)
		MoveDrawBridge();

	// Car behaviour
	if ((GameMode == TRACK_PREVIEW) || (GameMode == GAME_IN_PROGRESS))
	{
		if (!bPaused)
		{
			if ((GameMode == GAME_IN_PROGRESS) && (!bPlayerPaused))
				CarBehaviour(input,
							 &player1_x,
							 &player1_y,
							 &player1_z,
							 &player1_x_angle,
							 &player1_y_angle,
							 &player1_z_angle);

			OpponentBehaviour(&opponent_x,
							  &opponent_y,
							  &opponent_z,
							  &opponent_x_angle,
							  &opponent_y_angle,
							  &opponent_z_angle,
							  bOpponentPaused);
		}

		LimitViewpointY(&player1_y);
	}

#ifdef SMOOTH
	// Initialize interpolators after opponent/car positioning on first frame
	if (bNeedInterpolatorInit)
	{
		if (GameMode == GAME_IN_PROGRESS)
		{
			bNeedInterpolatorInit = FALSE;
			CalcGameViewpoint();
			InitializeBackdropInterpolator(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
			InitializeViewMatrixInterpolator();
		}
		else if (GameMode == TRACK_PREVIEW)
		{
			bNeedInterpolatorInit = FALSE;
			
			// Calculate track preview viewpoint based on opponent position
			CalcTrackPreviewViewpoint();
			InitializeOpponentCarInterpolator();
			
			// Initialize camera position interpolator with scaled viewpoint
			long scaled_x = viewpoint1_x >> LOG_PRECISION;
			long scaled_y = viewpoint1_y >> LOG_PRECISION;
			long scaled_z = viewpoint1_z >> LOG_PRECISION;
			InitializeCameraPositionInterpolator(scaled_x, scaled_y, scaled_z);
			InitializeBackdropInterpolator(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
		}
	}
#endif

	if ((GameMode == TRACK_MENU) || (GameMode == TRACK_PREVIEW))
	{
		if (GameMode == TRACK_MENU)
			CalcTrackMenuViewpoint();
		else
		{
			CalcTrackPreviewViewpoint();

			// Set the car's world transform matrix
			SetOpponentsCarWorldTransform();
		}

#ifdef SMOOTH
		// Update backdrop angle interpolator
		BackdropAngleInterpolator.UpdateAngles(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
#endif

		// Set Direct3D transforms, ready for OnFrameRender
		viewpoint1_x >>= LOG_PRECISION;
		// NOTE: viewpoint1_y must be preserved for use by DrawBackdrop
		viewpoint1_z >>= LOG_PRECISION;

#ifdef SMOOTH
		// Update camera position interpolator for track preview AFTER scaling
		// Note: viewpoint1_y is NOT scaled (preserved for DrawBackdrop), so scale it here
		if (GameMode == TRACK_PREVIEW)
		{
			CameraPositionInterpolator.Update(viewpoint1_x, viewpoint1_y >> LOG_PRECISION, viewpoint1_z);
		}
#endif

		target_x >>= LOG_PRECISION;
		target_y = -target_y;
		target_y >>= LOG_PRECISION;
		target_z >>= LOG_PRECISION;

		// Set the track's world transform matrix
		D3DXMatrixIdentity( &matWorldTrack );

		//
		// Set the view transform matrix
		//
		// Set the eye point
		D3DXVECTOR3 vEyePt( static_cast<float>(viewpoint1_x), static_cast<float>(-viewpoint1_y>>LOG_PRECISION), static_cast<float>(viewpoint1_z) );
		// Set the lookat point
#ifdef SMOOTH
		D3DXVECTOR3 vLookatPt;
		if (GameMode == TRACK_MENU)
		{
			vLookatPt.x = (float)target_x;
			vLookatPt.y = (float)target_y;
			vLookatPt.z = (float)target_z;
		}
		else
		{
			// Track preview: interpolate both camera position and lookat (camera positions already scaled)
			// Interpolate camera position
			long cam_x, cam_y, cam_z;
			CameraPositionInterpolator.GetInterpolated(GameTicker.TickPercent, cam_x, cam_y, cam_z);
			vEyePt.x = static_cast<float>(cam_x);
			vEyePt.y = static_cast<float>(-cam_y);
			vEyePt.z = static_cast<float>(cam_z);
			// Interpolate lookat position
			D3DXMATRIX carMtx = InterpolatorCarOpponent.CreateInterpolatedMtx(GameTicker.TickPercent);
			vLookatPt.x = carMtx._41;
			vLookatPt.y = carMtx._42;
			vLookatPt.z = carMtx._43;
		}
#else
		D3DXVECTOR3 vLookatPt( static_cast<float>(target_x), static_cast<float>(target_y), static_cast<float>(target_z) );
#endif
		D3DXMatrixLookAtLH( &matView, &vEyePt, &vLookatPt, &vUpVec );
		pd3dDevice->SetTransform( D3DTS_VIEW, &matView );
	}
	else if (GameMode == GAME_IN_PROGRESS)
	{
		CalcGameViewpoint();

		// Set Direct3D transforms, ready for OnFrameRender
		viewpoint1_x >>= LOG_PRECISION;
		// NOTE: viewpoint1_y must be preserved for use by DrawBackdrop
		viewpoint1_z >>= LOG_PRECISION;

		// Set the track's world transform matrix
		D3DXMatrixIdentity( &matWorldTrack );

		// Set the opponent's car world transform matrix
		/*
		// temp set opponent's position to same as player
		if ((opponent_x == 0) && (opponent_y == 0) && (opponent_z == 0))
		{
			opponent_x = player1_x;
			opponent_y = player1_y + (0xc00 * 256 * 4);	// Subtract amount above road, added by PositionCarAbovePiece()
			opponent_z = player1_z;
			opponent_x_angle = player1_x_angle;
			opponent_y_angle = player1_y_angle;
			opponent_z_angle = player1_z_angle;
		}
		*/
		SetOpponentsCarWorldTransform();

		if (bOutsideView)
		{
			// Set the car's world transform matrix
			SetCarWorldTransform();
		}

		//
		// Set the view transform matrix
		//
		// Produce the translation matrix
		D3DXMatrixTranslation( &matTrans, static_cast<float>(-viewpoint1_x), static_cast<float>(viewpoint1_y>>LOG_PRECISION), static_cast<float>(-viewpoint1_z) );
		D3DXMatrixIdentity(&matRot);
		float xa = ((static_cast<float>(-viewpoint1_x_angle) * 2 * D3DX_PI) / 65536.0f);
		float ya = ((static_cast<float>(-viewpoint1_y_angle) * 2 * D3DX_PI) / 65536.0f);
		float za = ((static_cast<float>(-viewpoint1_z_angle) * 2 * D3DX_PI) / 65536.0f);
		// Produce and combine the rotation matrices
#ifdef linux
		D3DXMatrixRotationY(&matTemp, ya + D3DX_PI);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
		D3DXMatrixRotationX(&matTemp, -xa);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
		D3DXMatrixRotationZ(&matTemp, -za);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
#else
		D3DXMatrixRotationY(&matTemp, ya);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
		D3DXMatrixRotationX(&matTemp, xa);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
		D3DXMatrixRotationZ(&matTemp, za);
		D3DXMatrixMultiply(&matRot, &matRot, &matTemp);
#endif
		// Combine the rotation and translation matrices to complete the world matrix
#ifdef SMOOTH
		// update matrices for interpolation
		InterpolatorView.UpdateMatrices(matTrans, matRot);

		//D3DXMatrixMultiply(&matView, &matTrans, &matRot);
		D3DMATRIX viewMtx = InterpolatorView.CreateInterpolatedMtx(GameTicker.TickPercent, true);
#else
		D3DXMatrixMultiply(&matView, &matTrans, &matRot);
#endif
#ifdef linux
		D3DXMatrixScaling(&matTrans, +1, -1, +1);
		D3DXMatrixMultiply(&matView, &matView, &matTrans);
#endif
#ifdef SMOOTH
		pd3dDevice->SetTransform(D3DTS_VIEW, &viewMtx);
#else
		pd3dDevice->SetTransform( D3DTS_VIEW, &matView );
#endif
	}

	if (!bPaused)
		bFrameMoved = TRUE;
}


/*	======================================================================================= */
/*	Function:		HandleTrackMenu															*/
/*																							*/
/*	Description:	Output track menu text													*/
/*	======================================================================================= */
#ifdef linux
#define FIRSTMENU SDLK_1
#define STARTMENU SDLK_s
#define LEAGUEMENU SDLK_l
#else
#define FIRSTMENU '1'
#define STARTMENU 'S'
#define LEAGUEMENU 'L'
#endif

static void HandleTrackMenu( CDXUTTextHelper &txtHelper )
{
	long i, track_number;
	UINT firstMenuOption, lastMenuOption;
	float textScale = GetTextScale();
	txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?10:0)) * textScale), static_cast<int>(15*8*textScale) );
	txtHelper.DrawTextLine( L"Choose track :-" );

	for (i = 0, firstMenuOption = FIRSTMENU; i < NUM_TRACKS; i++)
	{
		txtHelper.DrawFormattedTextLine( L"'%d' -  " STRING, (i+1), GetTrackName(i) );
	}
	lastMenuOption = i + FIRSTMENU - 1;

	// output instructions
	const D3DSURFACE_DESC *pd3dsdBackBuffer = DXUTGetBackBufferSurfaceDesc();
	txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?10:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-15*8*textScale) );
	txtHelper.DrawFormattedTextLine( L"Current track - " STRING L".  Press 'S' to select, Escape to quit", (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID)));
	txtHelper.DrawTextLine( L"'L' to switch Super League On/Off");

	if (((keyPress >= firstMenuOption) && (keyPress <= lastMenuOption)) || (keyPress == LEAGUEMENU))
	{
		if (keyPress == LEAGUEMENU) {
			bSuperLeague = !bSuperLeague;
			track_number = TrackID;
			CreateCarVertexBuffer(DXUTGetD3DDevice());	// recreate car
		} else 
			track_number = keyPress - firstMenuOption;	// start at 0

		if (!ConvertAmigaTrack(track_number))
		{
#if defined(DEBUG) || defined(_DEBUG)
			fprintf(out, "Failed to convert track %d\n", track_number);
#endif
			MessageBox(NULL, L"Failed to convert track", L"Error", MB_OK);	//temp
			return;
		}

		if (CreateTrackVertexBuffer(DXUTGetD3DDevice()) != S_OK)
		{
#if defined(DEBUG) || defined(_DEBUG)
			fprintf(out, "Failed to create track vertex buffer %d\n", track_number);
#endif
			MessageBox(NULL, L"Failed to create track vertex buffer", L"Error", MB_OK);	//temp
			return;
			}

			keyPress = '\0';
		}

	if ((keyPress == STARTMENU) && (TrackID != NO_TRACK))
	{
		bNewGame = TRUE;	// Used here just to reset the opponent's car, which is then shown during the track preview
		ResetPlayer();		// Also reset player to clear values if there was a previous game (CarBehaviour normally does this, but isn't called for track preview)
        GameMode = TRACK_PREVIEW;
		bPlayerPaused = bOpponentPaused = FALSE;
#ifdef SMOOTH
		// Defer interpolator initialization until after opponent is positioned
		bNeedInterpolatorInit = TRUE;
#endif
		keyPress = '\0';
	}
	

	return;
}


/*	======================================================================================= */
/*	Function:		HandleTrackPreview														*/
/*																							*/
/*	Description:	Output track preview text												*/
/*	======================================================================================= */

static void HandleTrackPreview( CDXUTTextHelper &txtHelper )
{
	// output instructions
	const D3DSURFACE_DESC *pd3dsdBackBuffer = DXUTGetBackBufferSurfaceDesc();
	float textScale = GetTextScale();
	txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?10:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-15*9*textScale) );
	txtHelper.DrawFormattedTextLine( L"Selected track - " STRING L".  Press 'S' to start game", (TrackID == NO_TRACK ? L"None" : GetTrackName(TrackID)));
	txtHelper.DrawTextLine( L"'M' for track menu, Escape to quit");
	txtHelper.DrawTextLine( L"(Press F4 to change scenery, F9 / F10 to adjust frame rate)" );

	txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?10:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-15*6*textScale) );
	txtHelper.DrawTextLine( L"Keyboard controls during game :-" );
	#if defined(PANDORA) || defined(PYRA)
	txtHelper.DrawTextLine( L"  DPad = Steer, (X) = Accelerate, (B) = Brake, (R) = Nitro" );
	#else
	txtHelper.DrawTextLine( L"  Arrow left = Steer left, Arrow right = Steer right, Space = Accelerate, Arrow down = Brake" );
	#endif
	txtHelper.DrawTextLine( L"  R = Point car in opposite direction, P = Pause, O = Unpause" );
	txtHelper.DrawTextLine( L"  M = Back to track menu, Escape = Quit" );

	if (keyPress == STARTMENU)
	{
		bNewGame = TRUE;
        GameMode = GAME_IN_PROGRESS;

		// Trigger a resize message to ensure proper text positioning from the start
		HWND hWnd = DXUTGetHWND();
		if (hWnd)
		{
			RECT rect;
			GetWindowRect(hWnd, &rect);
			SendMessage(hWnd, WM_SIZING, WMSZ_BOTTOMRIGHT, (LPARAM)&rect);
		}
		// initialise game data
		ResetLapData(OPPONENT);
		ResetLapData(PLAYER);
		gameStartTime = DXUTGetTime();
		gameEndTime = 0;
		if (bSuperLeague) {
			boostReserve = SuperBoost;
			road_cushion_value = 1;
			engine_power = 320;
			boost_unit_value = 12;
			opp_engine_power = 314;
		} else {
			boostReserve = StandardBoost;	// SuperBoost for super league
			road_cushion_value = 0;
			engine_power = 240;
			boost_unit_value = 16;
			opp_engine_power = 236;
		}
		boostUnit = 0;
		bPlayerPaused = bOpponentPaused = FALSE;
#ifdef SMOOTH
		// Set flag to initialize interpolators after CarBehaviour positions the car
		bNeedInterpolatorInit = TRUE;
#endif
		keyPress = '\0';
	}

	return;
}


//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for 
// efficient text rendering.  Also render text specific to GameMode.
//--------------------------------------------------------------------------------------
extern long new_damage;
extern long opponentsID;
extern WCHAR *opponentNames[];

void RenderText( double fTime )
{
    // The helper object simply helps keep track of text position, and color
    // and then it calls pFont->DrawText( m_pSprite, strMsg, -1, &rc, DT_NOCLIP, m_clr );
    // If NULL is passed in as the sprite object, then it will work fine however the 
    // pFont->DrawText() will not be batched together.  Batching calls will improve perf.
	float textScale = GetTextScale();
#ifdef linux
	static
#endif
    CDXUTTextHelper txtHelper( g_pFont, g_pSprite, static_cast<int>(15 * textScale) );

    // Output statistics
    txtHelper.Begin();
	txtHelper.SetForegroundColor( D3DXCOLOR( 1.0f, 1.0f, 0.0f, 1.0f ) );
	if (bShowStats)
	{
		txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?10:0)) * textScale), 0 );
#ifndef linux
		txtHelper.DrawTextLine( DXUTGetFrameStats(true) );
		txtHelper.DrawTextLine( DXUTGetDeviceStats() );
#else
		
		txtHelper.DrawFormattedTextLine( L"fTime: %0.1f  sin(fTime): %0.4f", fTime, sin(fTime) );
#endif

#if defined(DEBUG) || defined(_DEBUG)
		// Output VALUE1, VALUE, VALUE3
		txtHelper.DrawFormattedTextLine( L"V1: %08x, V2: %08x, V3: %08x", VALUE1, VALUE2, VALUE3 );
#else
		// Output version
		txtHelper.DrawTextLine( L"Version 1.0" );
#endif
	}

	switch (GameMode)
	{
		case TRACK_MENU:
			HandleTrackMenu(txtHelper);
			txtHelper.End();
			break;

		case TRACK_PREVIEW:
			HandleTrackPreview(txtHelper);
			txtHelper.End();
			break;

		case GAME_IN_PROGRESS:
		case GAME_OVER:
			// Show car speed, damage and race details
			const D3DSURFACE_DESC *pd3dsdBackBuffer = DXUTGetBackBufferSurfaceDesc();
			WCHAR lapText[3] = L"  ";
			// Output opponent's name for four seconds at race start
			if (((DXUTGetTime() - gameStartTime) < 4.0) && (opponentsID != NO_OPPONENT))
			{
				txtHelper.SetInsertionPos( static_cast<int>((250+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-15*20*textScale) );
				txtHelper.DrawFormattedTextLine( L"Opponent: " STRING, opponentNames[opponentsID] );
			}
			txtHelper.SetInsertionPos( static_cast<int>((2+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-15*2*textScale) );
			if (lapNumber[PLAYER] > 0)
				StringCchPrintf( lapText, 3, L"%d", lapNumber[PLAYER] );
			txtHelper.SetForegroundColor( D3DXCOLOR( 0.0f, 0.0f, 0.0f, 1.0f ) );
			
			// Position text using base 800x480 coordinates, then scale
			float base_height = static_cast<float>(BASE_HEIGHT);
			float scaleY = static_cast<float>(pd3dsdBackBuffer->Height) / base_height;
		
			// Boost text - positioned in top dashboard box
			txtHelper.SetInsertionPos( static_cast<int>((DASHBOARD_TEXT_X_BASE+(wideScreen?80:0)) * textScale), static_cast<int>((BASE_HEIGHT - DASHBOARD_TEXT_Y_TOP) * scaleY) );
			txtHelper.DrawFormattedTextLine( L"L" STRING L"       B%02d", lapText, boostReserve );			// Distance text - positioned in bottom dashboard box
			txtHelper.SetInsertionPos( static_cast<int>((DASHBOARD_TEXT_X_DISTANCE+(wideScreen?80:0)) * textScale), static_cast<int>((BASE_HEIGHT - DASHBOARD_TEXT_Y_BOTTOM) * scaleY) );
			txtHelper.DrawFormattedTextLine( L"        %+05d", CalculateOpponentsDistance() );

			// Lap timers - positioned in right and bottom gray cockpit areas
			WCHAR currentLapStr[16], lastLapStr[16];
			
			// Calculate current lap time using game ticks
			double currentLapElapsed = 0.0;
			if (raceFinished)
			{
				// Show total race time when race is finished
				currentLapElapsed = totalRaceTime[PLAYER];
			}
			else if (lapStartTick[PLAYER] > 0)
			{
				// Convert current lap ticks to seconds
				long currentLapTicks = globalGameTicks - lapStartTick[PLAYER];
				currentLapElapsed = currentLapTicks * SECONDS_PER_GAME_TICK;
			}
			
			// Format the times
			FormatLapTime(currentLapElapsed, currentLapStr, 16);
			FormatLapTime(lastLapTime[PLAYER], lastLapStr, 16);
			
			// Current lap/race time - right gray area
			txtHelper.SetInsertionPos( static_cast<int>((DASHBOARD_TEXT_X_TIMERS+(wideScreen?80:0)) * textScale), static_cast<int>((BASE_HEIGHT - DASHBOARD_TEXT_Y_TOP) * scaleY) );
			if (raceFinished)
				txtHelper.DrawFormattedTextLine( L"R %s", currentLapStr );
			else
				txtHelper.DrawFormattedTextLine( L"C %s", currentLapStr );
			
			// Last lap time - bottom gray area (positioned to right of distance display)
			txtHelper.SetInsertionPos( static_cast<int>((DASHBOARD_TEXT_X_TIMERS+(wideScreen?80:0)) * textScale), static_cast<int>((BASE_HEIGHT - DASHBOARD_TEXT_Y_BOTTOM) * scaleY) );
			txtHelper.DrawFormattedTextLine( L"L %s", lastLapStr );
			
#if defined(DEBUG) || defined(_DEBUG)
			// Physics rate indicator - top right corner
			txtHelper.SetInsertionPos( static_cast<int>((pd3dsdBackBuffer->Width - 80 * textScale)), static_cast<int>(10 * textScale) );
#ifdef SMOOTH
			txtHelper.DrawFormattedTextLine( L"Hz:%.1f", smoothPhysicsRate );
#else
			txtHelper.DrawFormattedTextLine( L"FG:%d", frameGap );
#endif
#endif

			txtHelper.End();

			if (raceFinished)
			{
				#ifdef linux
				static
				#endif
				CDXUTTextHelper txtHelperLarge( g_pFontLarge, g_pSprite, static_cast<int>(25 * textScale) );

				txtHelperLarge.Begin();

				double currentTime = DXUTGetTime(), diffTime;
				if (gameEndTime == 0.0)
					gameEndTime = currentTime;

				// Show race finished text for six seconds, then end the game
				diffTime = currentTime - gameEndTime;
				if (diffTime > 6.0)
				{
					GameMode = GAME_OVER;
				}

				if (GameMode == GAME_OVER)
				{
#ifdef 	linux
					txtHelperLarge.SetInsertionPos( static_cast<int>((250+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-25*13*textScale) );
					txtHelperLarge.DrawTextLine( L"GAME OVER" );
					txtHelperLarge.SetInsertionPos( static_cast<int>((132+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-25*11*textScale) );
					txtHelperLarge.DrawTextLine( L"Press 'M' for track menu" );
#else
					txtHelperLarge.SetInsertionPos( static_cast<int>((124+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-25*12*textScale) );
					txtHelperLarge.DrawTextLine( L"GAME OVER: Press 'M' for track menu" );
#endif
				}
				else
				{
					long intTime = static_cast<long>(diffTime);
					// Text flashes white/black, changing every half second
					if ((diffTime - (double)intTime) < 0.5)
						txtHelperLarge.SetForegroundColor( D3DXCOLOR( 1.0f, 1.0f, 1.0f, 1.0f ) );
					else
						txtHelperLarge.SetForegroundColor( D3DXCOLOR( 0.0f, 0.0f, 0.0f, 1.0f ) );

					txtHelperLarge.SetInsertionPos( static_cast<int>((250+(wideScreen?80:0)) * textScale), static_cast<int>(pd3dsdBackBuffer->Height-25*12*textScale) );

					if (raceWon)
						txtHelperLarge.DrawTextLine( L"RACE WON" );
					else
						txtHelperLarge.DrawTextLine( L"RACE LOST" );
				}

				txtHelperLarge.End();
			}
			break;
	}
	//	VALUE2 = raceFinished ? 1 : 0;
	//	VALUE3 = (long)gameEndTime;
}


#ifdef NOT_USED
//-----------------------------------------------------------------------------
// Name: SetupLights()
// Desc: Sets up the lights and materials for the scene.
//-----------------------------------------------------------------------------
void SetupLights( IDirect3DDevice9 *pd3dDevice )
{
D3DXVECTOR3 vecDir;
D3DLIGHT9 light;

    // Set up a material. The material here just has the diffuse and ambient
    // colors set to white. Note that only one material can be used at a time.
    D3DMATERIAL9 mtrl;
    ZeroMemory( &mtrl, sizeof(D3DMATERIAL9) );
    mtrl.Diffuse.r = mtrl.Ambient.r = 1.0f;
    mtrl.Diffuse.g = mtrl.Ambient.g = 1.0f;
    mtrl.Diffuse.b = mtrl.Ambient.b = 1.0f;
    mtrl.Diffuse.a = mtrl.Ambient.a = 1.0f;
    pd3dDevice->SetMaterial( &mtrl );

	/*
    // Set up a white spotlight
    ZeroMemory( &light, sizeof(D3DLIGHT9) );
    light.Type       = D3DLIGHT_SPOT;
    light.Diffuse.r  = 1.0f;
    light.Diffuse.g  = 1.0f;
    light.Diffuse.b  = 1.0f;
	// Set position vector
//	light.Position = D3DXVECTOR3(32768.0f, 1000.0f, 32768.0f);
	if (GameMode == TRACK_MENU)
	{
		light.Position.x = 32768.0f;
		light.Position.y = 16384.0f;
		light.Position.z = 32768.0f;
	}
	else
	{
		light.Position.x = (player1_x>>LOG_PRECISION);
		light.Position.y = 16384.0f;
		light.Position.z = (player1_z>>LOG_PRECISION);
	}
	// Set direction vector to simulate sunlight
    vecDir = D3DXVECTOR3(0.0f, -1.0f, 0.0f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    light.Range       = 32768;//((float)sqrt(FLT_MAX));
	light.Falloff = 1.0f;
	light.Attenuation0 = 1.0f;
	light.Attenuation1 = 0.0f;
	light.Attenuation2 = 0.0f;
	light.Theta = PI/3;
	light.Phi = PI/2;
	pd3dDevice->SetLight( 0, &light );
    pd3dDevice->LightEnable( 0, TRUE );
	*/

	/**/
    // Set up four white, directional lights
    ZeroMemory( &light, sizeof(D3DLIGHT9) );
    light.Type       = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r  = 0.33f;
    light.Diffuse.g  = 0.33f;
    light.Diffuse.b  = 0.33f;
	// Set direction vector to simulate sunlight
    vecDir = D3DXVECTOR3(0.2f, -0.7f, 0.5f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    light.Range       = 10000.0f;
    pd3dDevice->SetLight( 1, &light );
    pd3dDevice->LightEnable( 1, TRUE );
	/**/
    vecDir = D3DXVECTOR3(0.2f, -0.7f, -0.5f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    pd3dDevice->SetLight( 2, &light );
    pd3dDevice->LightEnable( 2, TRUE );
	/**/
    vecDir = D3DXVECTOR3(-0.2f, -0.7f, 0.5f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    pd3dDevice->SetLight( 3, &light );
    pd3dDevice->LightEnable( 3, TRUE );
	/**/
    vecDir = D3DXVECTOR3(-0.2f, -0.7f, -0.5f);
    D3DXVec3Normalize( (D3DXVECTOR3*)&light.Direction, &vecDir );
    pd3dDevice->SetLight( 4, &light );
    pd3dDevice->LightEnable( 4, TRUE );
	/**/

    // Finally, turn on some ambient light and turn lighting on
    pd3dDevice->SetRenderState( D3DRS_AMBIENT, 0x00303030 );
	pd3dDevice->SetRenderState( D3DRS_LIGHTING, TRUE );
}
#endif


//--------------------------------------------------------------------------------------
// Render the scene 
//--------------------------------------------------------------------------------------

void CALLBACK OnFrameRender( IDirect3DDevice9 *pd3dDevice, double fTime, float fElapsedTime, void *pUserContext )
{
	HRESULT hr;

//    // Clear the render target and the zbuffer
//    V( pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 45, 50, 170), 1.0f, 0) );

    // Clear the zbuffer
#ifdef SMOOTH
	V(pd3dDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, 0xff444488, 1.0f, 0));
#else
    V( pd3dDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0) );
#endif
    // Render the scene
    if ( SUCCEEDED( pd3dDevice->BeginScene() ) )
    {
		// Disable Z buffer and polygon culling, ready for DrawBackdrop()
		pd3dDevice->SetRenderState( D3DRS_ZENABLE, FALSE );
		pd3dDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );

		// Draw Backdrop
#ifdef SMOOTH
		long smooth_x_angle, smooth_y_angle, smooth_z_angle;
		BackdropAngleInterpolator.GetInterpolatedAngles(GameTicker.TickPercent, smooth_x_angle, smooth_y_angle, smooth_z_angle);
		DrawBackdrop(viewpoint1_y, smooth_x_angle, smooth_y_angle, smooth_z_angle);
#else
		DrawBackdrop(viewpoint1_y, viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
#endif

//		SetupLights(pd3dDevice);

#ifdef SMOOTH
		// Update interpolated shadow before drawing track
		if (GameMode == GAME_IN_PROGRESS)
		{
			UpdateInterpolatedShadow(GameTicker.TickPercent);
		}
#endif

		// Draw Track
		pd3dDevice->SetTransform( D3DTS_WORLD, &matWorldTrack );
		DrawTrack(pd3dDevice);

		switch (GameMode)
		{
			case TRACK_MENU:
				break;

		case TRACK_PREVIEW:
		{
			// Draw Opponent's Car
#ifdef SMOOTH
			D3DMATRIX mtx = InterpolatorCarOpponent.CreateInterpolatedMtx(GameTicker.TickPercent);
			pd3dDevice->SetTransform(D3DTS_WORLD, &mtx);
#else
			pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldOpponentsCar);
#endif
			DrawCar(pd3dDevice);
		}
			break;			case GAME_IN_PROGRESS:
			case GAME_OVER:
			{
				// Draw Opponent's Car
#ifdef SMOOTH
				D3DMATRIX mtx = InterpolatorCarOpponent.CreateInterpolatedMtx(GameTicker.TickPercent);
				pd3dDevice->SetTransform(D3DTS_WORLD, &mtx);
#else
				pd3dDevice->SetTransform(D3DTS_WORLD, &matWorldOpponentsCar);
#endif
				DrawCar(pd3dDevice);
			}
				if (bOutsideView)
				{
					// Draw Player1's Car
#ifdef SMOOTH
					D3DMATRIX mtx = InterpolatorCarOwn.CreateInterpolatedMtx(GameTicker.TickPercent);
					pd3dDevice->SetTransform(D3DTS_WORLD, &mtx);
#else
					pd3dDevice->SetTransform( D3DTS_WORLD, &matWorldCar );
#endif
					DrawCar(pd3dDevice);
				}
				else
				{
					// draw cockpit...
					DrawCockpit(pd3dDevice);
				}
				break;
		}

		if (GameMode == GAME_IN_PROGRESS)
		{
			DrawOtherGraphics();

			//jsr	display.speed.bar
			if (bFrameMoved) UpdateDamage();

			UpdateLapData();
			//jsr	display.opponents.distance
		}

		RenderText( fTime );

		// End the scene
		pd3dDevice->EndScene();
	}
}

#ifndef linux
//--------------------------------------------------------------------------------------
// Handle messages to the application 
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, 
                          bool *pbNoFurtherProcessing, void *pUserContext )
{
	// Handle window resizing to preserve aspect ratio
	if (uMsg == WM_SIZING)
	{
		RECT* pRect = (RECT*)lParam;
		
		// Get window chrome size (borders and title bar)
		RECT windowRect = *pRect;
		RECT clientRect;
		HWND hwnd = DXUTGetHWND();
		if (hwnd)
		{
			GetClientRect(hwnd, &clientRect);
			RECT currentWindowRect;
			GetWindowRect(hwnd, &currentWindowRect);
			
			int chromeWidth = (currentWindowRect.right - currentWindowRect.left) - clientRect.right;
			int chromeHeight = (currentWindowRect.bottom - currentWindowRect.top) - clientRect.bottom;
			
			// Calculate client area dimensions from the window rect being resized
			int clientWidth = (pRect->right - pRect->left) - chromeWidth;
			int clientHeight = (pRect->bottom - pRect->top) - chromeHeight;
			
			// Preserve aspect ratio based on wideScreen setting (5:3 for 800:480)
			float targetAspect = wideScreen ? (5.0f / 3.0f) : (4.0f / 3.0f);
			float currentAspect = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
			
			// Adjust based on which edge is being dragged
			if (currentAspect > targetAspect)
			{
				// Too wide, adjust width based on client area
				int newClientWidth = static_cast<int>(clientHeight * targetAspect);
				int newWidth = newClientWidth + chromeWidth;
				if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
					pRect->left = pRect->right - newWidth;
				else
					pRect->right = pRect->left + newWidth;
			}
			else if (currentAspect < targetAspect)
			{
				// Too tall, adjust height based on client area
				int newClientHeight = static_cast<int>(clientWidth / targetAspect);
				int newHeight = newClientHeight + chromeHeight;
				if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
					pRect->top = pRect->bottom - newHeight;
				else
					pRect->bottom = pRect->top + newHeight;
			}
		}
		
		*pbNoFurtherProcessing = true;
		return 0;
	}
	
	return 0;
}

//--------------------------------------------------------------------------------------
// As a convenience, DXUT inspects the incoming windows messages for
// keystroke messages and decodes the message parameters to pass relevant keyboard
// messages to the application.  The framework does not remove the underlying keystroke 
// messages, which are still passed to the application's MsgProc callback.
//--------------------------------------------------------------------------------------
void CALLBACK KeyboardProc( UINT nChar, bool bKeyDown, bool bAltDown, void *pUserContext )
{
    if (bKeyDown)
    {
		keyPress = nChar;
        switch(nChar)
        {
#if defined(DEBUG) || defined(_DEBUG)
			case VK_F1:
				bTestKey = !bTestKey;
				break;
#endif
			case VK_F2:
				++bTrackDrawMode;
				if (bTrackDrawMode > 1) bTrackDrawMode = 0;
				DXUTReset3DEnvironment();
				break;

			case VK_F4:
				NextSceneryType();
				break;

			case VK_F5:
				bShowStats = !bShowStats;
				break;

			case VK_F6:
				bPlayerPaused = !bPlayerPaused;
				break;

			case VK_F7:
				bOpponentPaused = !bOpponentPaused;
				break;

			case VK_F9:
#ifdef SMOOTH
				// In SMOOTH mode: increase physics rate (faster gameplay)
				if (smoothPhysicsRate < 15.0f) {
					smoothPhysicsRate += 1.0f;
					GameTicker.SetTargetFPS(smoothPhysicsRate);
				}
#else
			// In classic mode: decrease frameGap (faster gameplay)
			if (frameGap > 1) {
				frameGap--;
			}
#endif
			break;

			case VK_F10:
#ifdef SMOOTH
				// In SMOOTH mode: decrease physics rate (slower gameplay)
				if (smoothPhysicsRate > 5.0f) {
					smoothPhysicsRate -= 1.0f;
					GameTicker.SetTargetFPS(smoothPhysicsRate);
				}
#else
				// In classic mode: increase frameGap (slower gameplay)
				frameGap++;
#endif
				break;

#if defined(DEBUG) || defined(_DEBUG)
			case VK_BACK:
				bOutsideView = !bOutsideView;
				break;
#endif

			case 'M':
				if (GameMode != TRACK_MENU)
				{
					GameMode = TRACK_MENU;
					opponentsID = NO_OPPONENT;

					// Reset pause state when returning to menu
					bPaused = FALSE;
					bPlayerPaused = FALSE;
					bOpponentPaused = FALSE;

					// reset all animated objects
					ResetDrawBridge();

#ifdef SMOOTH
					// Reset interpolators when returning to menu to prevent flash on next game start
					CalcTrackMenuViewpoint();
					InitializeBackdropInterpolator(viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);
					InitializeViewMatrixInterpolator();
					ResetOpponentCarInterpolator();
#endif
				}
				break;

			case 'O':
				bPaused = FALSE;
				break;

			case 'P':
				bPaused = TRUE;
				break;

			case 'Z':
				bNewGame = TRUE;		// for testing to try stopping car positioning bug
				break;

			// controls for Car Behaviour, Player 1
			case VK_LEFT:
				lastInput |= KEY_P1_LEFT;
				break;

			case VK_RIGHT:
				lastInput |= KEY_P1_RIGHT;
				break;

			case VK_SPACE:
				lastInput |= KEY_P1_BOOST;
				break;

			case VK_DOWN:
				lastInput |= KEY_P1_BRAKE;
				break;

			case VK_UP:
				lastInput |= KEY_P1_ACCEL;
				break;
        }

#ifdef NOT_USED
        switch( nChar )
        {
            case VK_LEFT: fEyeX -= 1000.0f; break;
            case VK_RIGHT: fEyeX += 1000.0f; break;
            case VK_UP: fEyeY -= 1000.0f; break;
            case VK_DOWN: fEyeY += 1000.0f; break;
            case VK_PRIOR: fEyeZ -= 1000.0f; break;	// pgup
            case VK_NEXT: fEyeZ += 1000.0f; break;	// pgdn
        }
#endif
    }
	else
	{
		keyPress = '\0';
        switch(nChar)
        {
			// controls for Car Behaviour, Player 1
			case VK_LEFT:
				lastInput &= ~KEY_P1_LEFT;
				break;

			case VK_RIGHT:
				lastInput &= ~KEY_P1_RIGHT;
				break;

			case VK_SPACE:	// couldn't find VK_ definition for HASH key
				lastInput &= ~KEY_P1_BOOST;
				break;

			case VK_DOWN:
				lastInput &= ~KEY_P1_BRAKE;
				break;

			case VK_UP:
				lastInput &= ~KEY_P1_ACCEL;
				break;
		}
	}
}


//--------------------------------------------------------------------------------------
// Release resources created in the OnResetDevice callback here 
//--------------------------------------------------------------------------------------
void CALLBACK OnLostDevice( void *pUserContext )
{
//    g_DialogResourceManager.OnLostDevice();
//    g_SettingsDlg.OnLostDevice();
//    CDXUTDirectionWidget::StaticOnLostDevice();
    if (g_pFont)
        g_pFont->OnLostDevice();
    if (g_pFontLarge)
        g_pFontLarge->OnLostDevice();
    SAFE_RELEASE(g_pSprite);

	FreePolygonVertexBuffer();
	FreeTrackVertexBuffer();
	FreeShadowVertexBuffer();
	FreeCarVertexBuffer();
	FreeCockpitVertexBuffer();

	if (g_pAtlas) g_pAtlas->Release(), g_pAtlas = NULL;
}


//--------------------------------------------------------------------------------------
// Release resources created in the OnCreateDevice callback here
//--------------------------------------------------------------------------------------
void CALLBACK OnDestroyDevice( void *pUserContext )
{
//    g_DialogResourceManager.OnDestroyDevice();
//    g_SettingsDlg.OnDestroyDevice();
    SAFE_RELEASE(g_pFont);
    SAFE_RELEASE(g_pFontLarge);

	FreePolygonVertexBuffer();
	FreeTrackVertexBuffer();
	FreeShadowVertexBuffer();
	FreeCarVertexBuffer();
	FreeCockpitVertexBuffer();
}


//--------------------------------------------------------------------------------------
// Initialize everything and go into a render loop
//--------------------------------------------------------------------------------------
INT WINAPI WinMain( HINSTANCE, HINSTANCE, LPSTR, int )
{
    // Enable run-time memory check for debug builds.
	wchar_t maintitle[50] = {0};
	wsprintf(maintitle, L"StuntCarRemake v%d.%02d.%02d", V_MAJOR, V_MINOR, V_PATCH);
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // Set the callback functions
    DXUTSetCallbackDeviceCreated( OnCreateDevice );
    DXUTSetCallbackDeviceReset( OnResetDevice );
    DXUTSetCallbackDeviceLost( OnLostDevice );
    DXUTSetCallbackDeviceDestroyed( OnDestroyDevice );
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackKeyboard( KeyboardProc );
    DXUTSetCallbackFrameRender( OnFrameRender );
    DXUTSetCallbackFrameMove( OnFrameMove );
   
    // Perform any application-level initialization here
	if (!InitialiseData())
	    return DXUTGetExitCode();

    // Initialize DXUT and create the desired Win32 window and Direct3D device for the application
    DXUTInit( true, true, true, false ); // Parse the command line, handle the default hotkeys, show msgboxes, don't handle Alt-Enter
    DXUTSetCursorSettings( true, true ); // Show the cursor and clip it when in full screen
    DXUTCreateWindow( maintitle );
	
	// Get screen resolution and set initial window size to roughly half
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	int initialWidth = screenWidth / 2;
	int initialHeight = screenHeight / 2;
	// Maintain 5:3 aspect ratio (same as 800:480)
	if (initialWidth * 3 != initialHeight * 5) {
		initialHeight = (initialWidth * 3) / 5;
	}
	
    DXUTCreateDevice( D3DADAPTER_DEFAULT, true, initialWidth, initialHeight, IsDeviceAcceptable, ModifyDeviceSettings );
	wideScreen = 1;

//	DXUTSetConstantFrameTime( true, 0.033f );	// Doesn't seem to work

	//
	//	Initialise sound objects
	//
	if (!DSInit())
		return FALSE;

	if (!DSSetMode())
		return FALSE;

    // Start the render loop
    DXUTMainLoop();

    // Perform any application-level cleanup here
	FreeData();

    return DXUTGetExitCode();
}

#else

bool process_events()
{
    SDL_Event event;
    while( SDL_PollEvent( &event ) ) {
        switch( event.type ) {
        case SDL_KEYDOWN:
			keyPress = event.key.keysym.sym;
			// some special cases for French keyboards
			if((event.key.keysym.mod & KMOD_SHIFT) == 0)
			switch(event.key.keysym.sym) {
				case SDLK_AMPERSAND:	keyPress = SDLK_1; break;
				case 233:				keyPress = SDLK_2; break;
				case SDLK_QUOTEDBL:		keyPress = SDLK_3; break;
				case SDLK_QUOTE:		keyPress = SDLK_4; break;
				case SDLK_LEFTPAREN:	keyPress = SDLK_5; break;
				case SDLK_MINUS:		keyPress = SDLK_6; break;
				case 232:				keyPress = SDLK_7; break;
				case SDLK_UNDERSCORE:	keyPress = SDLK_8; break;
				case 231:				keyPress = SDLK_9; break;
				case 224:				keyPress = SDLK_0; break;
			}
            switch( keyPress ) {
#if defined(DEBUG) || defined(_DEBUG)
				case SDLK_F1:
					bTestKey = !bTestKey;
					break;
#endif
				case SDLK_F2:
					++bTrackDrawMode;
					if (bTrackDrawMode > 1) bTrackDrawMode = 0;
					DXUTReset3DEnvironment();
					break;

				case SDLK_F4:
					NextSceneryType();
					break;

				case SDLK_F5:
					bShowStats = !bShowStats;
					break;

				case SDLK_F6:
					bPlayerPaused = !bPlayerPaused;
					break;

				case SDLK_F7:
					bOpponentPaused = !bOpponentPaused;
					break;

				case SDLK_F9:
					if (frameGap > 1) frameGap--;
					break;

				case SDLK_F10:
					frameGap++;
					break;

#if defined(DEBUG) || defined(_DEBUG)
				case SDLK_BACK:
					bOutsideView = !bOutsideView;
					break;
#endif
				case SDLK_m:
					if (GameMode != TRACK_MENU)
					{
						GameMode = TRACK_MENU;

						opponentsID = NO_OPPONENT;

						// reset all animated objects
						ResetDrawBridge();
					}
					break;

				case SDLK_o:
					bPaused = FALSE;
					break;

				case SDLK_p:
					bPaused = TRUE;
					break;

				case SDLK_z:
					bNewGame = TRUE;		// for testing to try stopping car positioning bug
					break;

				// controls for Car Behaviour, Player 1
				case SDLK_LEFT:
					lastInput |= KEY_P1_LEFT;
					break;

				case SDLK_RIGHT:
					lastInput |= KEY_P1_RIGHT;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_RCTRL:
#else
				case SDLK_SPACE:
				case SDLK_RSHIFT:
				case SDLK_LSHIFT:
#endif
					lastInput |= KEY_P1_BOOST;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_END:
#else
				case SDLK_DOWN:
#endif
					lastInput |= KEY_P1_BRAKE;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_PAGEDOWN:
#else
				case SDLK_UP:
#endif
					lastInput |= KEY_P1_ACCEL;
					break;

				case SDLK_ESCAPE:
					return false;
				}
            break;
        case SDL_KEYUP:
			keyPress = 0;
            switch( event.key.keysym.sym ) {
				// controls for Car Behaviour, Player 1
				case SDLK_LEFT:
					lastInput &= ~KEY_P1_LEFT;
					break;

				case SDLK_RIGHT:
					lastInput &= ~KEY_P1_RIGHT;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_RCTRL:
#else
				case SDLK_SPACE:
				case SDLK_RSHIFT:
				case SDLK_LSHIFT:
#endif
					lastInput &= ~KEY_P1_BOOST;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_END:
#else
				case SDLK_DOWN:
#endif
					lastInput &= ~KEY_P1_BRAKE;
					break;

#if defined(PANDORA) || defined(PYRA)
				case SDLK_PAGEDOWN:
#else
				case SDLK_UP:
#endif
					lastInput &= ~KEY_P1_ACCEL;
					break;
				}
			break;
        case SDL_QUIT:
            return false;
        }
    }
	return true;
}

IDirect3DDevice9 pd3dDevice;
#ifdef USE_SDL2
SDL_Window *window = NULL;
#endif

#ifdef __EMSCRIPTEN__
extern "C"
void initialize_gl4es();
double fLastTime;
void em_main_loop()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	double fTime = DXUTGetTime();
	process_events();	// no quit...
	OnFrameMove( &pd3dDevice, fTime, fTime - fLastTime, NULL );
	OnFrameRender( &pd3dDevice, fTime, fTime - fLastTime, NULL );
#ifdef USE_SDL2
	SDL_GL_SwapWindow(window);
#else
	SDL_GL_SwapBuffers();
#endif

	int32_t timetowait = (1.0f/50.0f - (fTime-fLastTime))*1000;
	//int32_t timetowait = (1.0f/60.0f - (fTime-fLastTime))*1000;
	if (timetowait>0)
		SDL_Delay(timetowait);

	fLastTime = fTime;
}
#endif

int GL_MSAA = 0;
int main(int argc, const char** argv)
{
#ifdef __EMSCRIPTEN__
	initialize_gl4es();
#endif
	char maintitle[50] = {0};
	sprintf(maintitle, "StuntCarRemake v%d.%02d.%02d", V_MAJOR, V_MINOR, V_PATCH);
	printf("%s\n", maintitle);
	// get executable folder and cd into it...
	// this is linux only, will not work on BSD or macOS
	char buf[500];
	ssize_t bufsized = readlink("/proc/self/exe", buf, sizeof(buf));
	if(bufsized>0) {
		char* p = strrchr(buf, '/');
		if(*p) {
			*p=0;
			chdir(buf);
			printf("chdir(\"%s\")\n", buf);
		}
	}
#ifdef USE_SDL2
	SDL_GLContext context = NULL;
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK)==-1) {
		printf("Could not initialise SDL2: %s\n", SDL_GetError());
		exit(-1);
	}
#else
	SDL_Surface *screen = NULL;
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTTHREAD)==-1) {
		printf("Could not initialise SDL: %s\n", SDL_GetError());
		exit(-1);
	}
#endif
	atexit(SDL_Quit);

	TTF_Init();

	// crude command line parameter reading
	int nomsaa = 0;
	int fullscreen = 0;
	int desktop = 0;
	int givehelp = 0;

	for (int i=1; i<argc; i++) {
		if(!strcmp(argv[i], "-f"))
			fullscreen = 1;
		else if(!strcmp(argv[i], "--fullscreen"))
			fullscreen = 1;
		else if(!strcmp(argv[i], "-d"))
			desktop = 1;
		else if(!strcmp(argv[i], "--desktop"))
			desktop = 1;
		else if(!strcmp(argv[i], "-n"))
			nomsaa = 1;
		else if(!strcmp(argv[i], "--nomsaa"))
			nomsaa = 1;
		else givehelp = 1;
	}
	if(givehelp) {
		printf("Unrecognized parameter.\nOptions are:\n\t-f|--fullscreen\tUse fullscreen\n\t-n|--nomsaa\tDisable MSAA\n");
		exit(0);
	}

    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

#if defined(PANDORA)
	int revision = 5;
	FILE *f = fopen("/etc/powervr-esrev", "r");
	if (f) {
		fscanf(f, "%d", &revision);
		fclose(f);
		printf("Pandora Model detected = %d\n", revision);
	}
	if(revision==5 && !nomsaa) {
		// only do MSAA for Gigahertz model
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 2);
		GL_MSAA=1;
	}
#else
	if(!nomsaa) {
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 4);
		GL_MSAA=1;
	}
#endif
	int flags = 0;
	wideScreen = 0;
	int screenH, screenW, screenX, screenY;
#ifdef USE_SDL2
	flags = SDL_WINDOW_OPENGL;
#else
	flags = SDL_OPENGL | SDL_DOUBLEBUF;
#endif
	if(fullscreen)
#ifdef USE_SDL2
		flags |= SDL_WINDOW_FULLSCREEN;
#else
		flags |= SDL_FULLSCREEN;
#endif
#ifdef PANDORA
#ifdef USE_SDL2
		flags |= SDL_WINDOW_FULLSCREEN;
#else
	flags |= SDL_FULLSCREEN;
#endif
	screenW = 800; screenH = 480;
#elif defined(CHIP)
#ifdef USE_SDL2
		flags |= SDL_WINDOW_FULLSCREEN;
#else
	flags |= SDL_FULLSCREEN;
#endif
	screenW = 480; screenH = 272;
#else
	if(desktop || fullscreen) {
#ifdef USE_SDL2
		flags |= (desktop)?SDL_WINDOW_FULLSCREEN_DESKTOP:SDL_WINDOW_FULLSCREEN;
#else
		flags |= SDL_FULLSCREEN;
#endif
		if(desktop) {
#ifdef USE_SDL2
			screenW = 640;
			screenH = 480;
#else
			const SDL_VideoInfo* infos = SDL_GetVideoInfo();
			screenW = infos->current_w;
			screenH = infos->current_h;
#endif
		} else {
			screenW = 640;
			screenH = 480;
		}
	} else {
		screenW = 800;
		screenH = 480;
	}
#endif
#ifdef USE_SDL2
	window = SDL_CreateWindow(maintitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenW, screenH, flags);
	if(window==NULL && GL_MSAA) {
		// fallback to no MSAA
		GL_MSAA=0;
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0);
		window = SDL_CreateWindow(maintitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screenW, screenH, flags);
	}
	if(window==NULL) {
		printf("Couldn't create Window (%dx%d): %s\n", screenW, screenH, SDL_GetError());
		exit(-2);
	}
	context = SDL_GL_CreateContext(window);
	if(context==NULL) {
			printf("Couldn't create OpenGL Context: %s\n", SDL_GetError());
			exit(-3);
	}
	SDL_GetWindowSize(window, &screenW, &screenH);
	SDL_SetWindowTitle(window, maintitle);
#endif
	{
		// icon...
		int x,y,n;
		unsigned char *img = stbi_load("Bitmap/icon.png", &x, &y, &n, STBI_rgb_alpha);
		if(img) {
			SDL_Surface *icon = 
			#ifdef USE_SDL2
				SDL_CreateRGBSurfaceWithFormatFrom(img, x, y, 32, x*4, SDL_PIXELFORMAT_RGBA32);
			#else
				SDL_CreateRGBSurfaceFrom(img, x, y, 32, x*4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			#endif
			if(icon) {
				#ifdef USE_SDL2
				SDL_SetWindowIcon(window, icon);
				SDL_FreeSurface(icon);
				#else
				SDL_WM_SetIcon(icon, NULL);
				#endif
			}
			free(img);
		}
	}
#ifndef USE_SDL2
	screen = SDL_SetVideoMode( screenW, screenH, 32, flags );
    if ( screen == NULL ) {
		// fallback to no MSAA
		GL_MSAA=0;
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0);
		screen = SDL_SetVideoMode( screenW, screenH, 32, flags );
    	if ( screen == NULL ) {
#ifdef PANDORA
			printf("Couldn't set 800x480x16 video mode: %s\n", SDL_GetError());
#else
			printf("Couldn't set %dx%dx32 video mode: %s\n", screenW, screenH, SDL_GetError());
#endif
        	exit(-2);
		}
    } else {
		glEnable(GL_MULTISAMPLE);
	}
	SDL_WM_SetCaption(maintitle, NULL);
#endif
	// automatic guess the scale
	float screenScale = 1.;
	if(screenW/640. < screenH/480.)
		screenScale = screenW/640.;
	else
		screenScale = screenH/480.;
	// is it a Wide screen ration?
	if((screenW/screenScale - 640)>=80)
		wideScreen=1;
	screenX = (screenW-(wideScreen?800.:640.)*screenScale)/2.;
	screenY = (screenH-480.*screenScale)/2.;
	screenW = (wideScreen?800:640)*screenScale;
	screenH = 480*screenScale;
#ifdef USE_SDL2
	if(flags&SDL_WINDOW_FULLSCREEN || flags&SDL_WINDOW_FULLSCREEN_DESKTOP)
#else
	if(flags&SDL_FULLSCREEN)
#endif
		SDL_ShowCursor(SDL_DISABLE);
	glViewport(screenX, screenY, screenW, screenH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	screenH = 480;
	screenW = wideScreen?800:640;
	glOrtho(0, screenW, screenH, 0, 0, FURTHEST_Z);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    D3DXMATRIX matProj;
	FLOAT fAspect = screenW / 480.0f;
    D3DXMatrixPerspectiveFovLH( &matProj, D3DX_PI/4, fAspect, 0.5f, FURTHEST_Z );
    pd3dDevice.SetTransform( D3DTS_PROJECTION, &matProj );

	glEnable(GL_DEPTH_TEST);
	glAlphaFunc(GL_NOTEQUAL, 0);
//	glEnable(GL_ALPHA_TEST);
//	glShadeModel(GL_FLAT);
	glDisable(GL_LIGHTING);
	// Disable texture mapping by default (only DrawTrack() enables it)
	pd3dDevice.SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );

	sound_init();


	CreateFonts();
	LoadTextures();

	if (!InitialiseData()) {
		printf("Error initialising data\n");
		exit(-3);
	}

	CreateBuffers(&pd3dDevice);

	DSInit();
	DSSetMode();

	glClearColor(0,0,0,1);
#ifdef __EMSCRIPTEN__
	fLastTime = DXUTGetTime();
	emscripten_set_main_loop(em_main_loop, 0, 1);
#else
	bool run = true;
	double fLastTime = DXUTGetTime();
    while( run ) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		double fTime = DXUTGetTime();
		run = process_events();
		OnFrameMove( &pd3dDevice, fTime, fTime - fLastTime, NULL );
        OnFrameRender( &pd3dDevice, fTime, fTime - fLastTime, NULL );
#ifdef USE_SDL2
		SDL_GL_SwapWindow(window);
#else
		SDL_GL_SwapBuffers();
#endif

		int32_t timetowait = (1.0f/50.0f - (fTime-fLastTime))*1000;
		//int32_t timetowait = (1.0f/60.0f - (fTime-fLastTime))*1000;
		if (timetowait>0)
			SDL_Delay(timetowait);

		fLastTime = fTime;
    }
#endif
	FreeData();

	CloseFonts();

	sound_destroy();
	TTF_Quit();
	SDL_Quit();
	
	exit(0);
}
#endif
