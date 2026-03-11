
#ifndef	_STUNT_CAR_RACER
#define	_STUNT_CAR_RACER

/*	========= */
/*	Constants */
/*	========= */
#define SCR_BASE_COLOUR	26

// Game timing constant
#define SECONDS_PER_GAME_TICK (0.1333)  // Based on NTSC timing: 60Hz / 8 frames = 7.5 updates/sec, so 1/7.5 = 0.1333 sec/tick

// Screen resolution constants
#define BASE_WIDTH_STANDARD		640		// Standard 4:3 base width
#define BASE_WIDTH_WIDESCREEN	800		// Widescreen 16:10 base width
#define BASE_HEIGHT				480		// Base height for both modes

// Dashboard text position constants (in base 640x480/800x480 coordinate space)
#define DASHBOARD_TEXT_X_BASE		88		// Base X position for lap/boost text
#define DASHBOARD_TEXT_X_DISTANCE	84		// Base X position for distance text
#define DASHBOARD_TEXT_X_TIMERS		480		// Base X position for lap timers
#define DASHBOARD_TEXT_Y_TOP		50.0f	// Y offset from bottom for top row (lap/boost/current time)
#define DASHBOARD_TEXT_Y_BOTTOM		25.0f	// Y offset from bottom for bottom row (distance/last lap)

typedef enum
	{
	TITLE_SCREEN = 0,
	TRACK_MENU,
	TRACK_PREVIEW,
	GAME_IN_PROGRESS,
	GAME_OVER
	} GameModeType;

/*
// Untransformed coloured vertex
#define D3DFVF_UTVERTEX (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE)
*/
// Untransformed coloured textured vertex
#define D3DFVF_UTVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)

/*	===================== */
/*	Structure definitions */
/*	===================== */
/*
// Untransformed coloured vertex
struct UTVERTEX
{
    D3DXVECTOR3 pos;	// The untransformed position for the vertex
	D3DXVECTOR3 normal;	// The surface normal for the vertex
    DWORD color;		// The vertex diffuse color value
};
*/
#ifndef linux
// Untransformed coloured textured vertex
struct UTVERTEX
{
    D3DXVECTOR3 pos;	// The untransformed position for the vertex
    DWORD color;		// The vertex diffuse color value
	FLOAT tu,tv;		// The texture co-ordinates
};
#endif

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void GetScreenDimensions( long *screen_width,
								 long *screen_height );

extern DWORD SCRGB (long colour_index);
extern DWORD SCColour (long colour_index);

extern void SetSolidColour (long colour_index);
extern void SetLineColour (long colour_index);
extern void SetTextureColour (long colour_index);

// Debug
extern long VALUE1, VALUE2, VALUE3;

#endif	/* _STUNT_CAR_RACER */
