
#ifndef	_OPPONENT_BEHAVIOUR
#define	_OPPONENT_BEHAVIOUR

/*	========= */
/*	Constants */
/*	========= */
#define	NO_OPPONENT	(-1)

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void OpponentBehaviour (long *x,
							   long *y,
							   long *z,
							   float *x_angle,
							   float *y_angle,
							   float *z_angle,
							   bool bOpponentPaused);

extern void CarToCarCollision( void );

extern long CalculateIfWinning( long start_finish_piece );

extern long CalculateOpponentsDistance (void);

#ifdef SMOOTH
#include <d3dx9.h>
extern bool GetInterpolatedShadowVertices(float t, D3DXVECTOR3& v1, D3DXVECTOR3& v2, D3DXVECTOR3& v3, D3DXVECTOR3& v4);
#endif

#endif	/* _OPPONENT_BEHAVIOUR */
