#include "GalaxyWnd.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstddef>
#include <iostream>

#include "MathHelper.hpp"
#include "specrend.h"
#include "Star.hpp"

double GalaxyWnd::_temp = 3700;

GalaxyWnd::GalaxyWnd()
	: SDLWindow()
	, _camOrient(0)
	, _starRenderType(1)
	, _flags((int)DisplayItem::STARS | (int)DisplayItem::AXIS | (int)DisplayItem::HELP | (int)DisplayItem::DUST | (int)DisplayItem::H2)
	, _galaxy()
	, _colNum(200)
	, _t0(1000)
	, _t1(10000)
	, _dt((_t1 - _t0) / _colNum)
	, _renderUpdateHint(ruhDENSITY_WAVES)
	, _vertDensityWaves(4)
{
	double x, y, z;
	for (int i = 0; i < _colNum; ++i)
	{
		Color& col = _col[i];
		colourSystem* cs = &SMPTEsystem;
		bbTemp = _t0 + _dt * i;
		spectrum_to_xyz(bb_spectrum, &x, &y, &z);
		xyz_to_rgb(cs, x, y, z, &col.r, &col.g, &col.b);
		norm_rgb(&col.r, &col.g, &col.b);
	}
}

GalaxyWnd::~GalaxyWnd()
{
	_vertDensityWaves.Release();
}

void GalaxyWnd::InitGL()
{
	// Font initialization
	TTF_Init();
	_pSmallFont = TTF_OpenFont("consola.ttf", 14);
	if (_pSmallFont == nullptr)
		throw std::runtime_error(TTF_GetError());

	_pFont = TTF_OpenFont("arial.ttf", 18);
	if (_pFont == nullptr)
		throw std::runtime_error(TTF_GetError());

	_pFontCaption = TTF_OpenFont("arial.ttf", 40);
	if (_pFontCaption == nullptr)
		throw std::runtime_error(TTF_GetError());

	// GL initialization
	glShadeModel(GL_SMOOTH);
	glViewport(0, 0, GetWidth(), GetHeight());

	SDL_Surface* tex;

	tex = SDL_LoadBMP("particle.bmp");
	if (!tex)
		throw std::runtime_error("Can't load star texture (particle.bmp).");

	// Check that the image's width is a power of 2
	if (tex->w & (tex->w - 1))
		throw std::runtime_error("texture width is not a power of 2.");

	// Also check if the height is a power of 2
	if (tex->h & (tex->h - 1))
		throw std::runtime_error("texture height is not a power of 2.");

	// get the number of channels in the SDL surface
	GLint  nOfColors = tex->format->BytesPerPixel;
	GLenum texture_format;
	if (nOfColors == 4)     // contains an alpha channel
	{
		texture_format = GL_RGBA;
	}
	else if (nOfColors == 3)     // no alpha channel
	{
		texture_format = GL_RGB;
	}
	else
		throw std::runtime_error("image is not truecolor");

	// Have OpenGL generate a texture object handle for us
	glGenTextures(1, &_texStar);

	// Bind the texture object
	glBindTexture(GL_TEXTURE_2D, _texStar);

	// Set the texture's stretching properties
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Edit the texture object's image data using the information SDL_Surface gives us
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		nOfColors,
		tex->w,
		tex->h,
		0,
		texture_format,
		GL_UNSIGNED_BYTE,
		tex->pixels);

	_vertDensityWaves.Initialize();

	glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);

	glDisable(GL_DEPTH_TEST);
	glClearColor(0.0f, .0f, 0.1f, 0.0f);

	SetCameraOrientation(Vec3D(0, 1, 0));

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void GalaxyWnd::InitSimulation()
{
	_galaxy.Reset(
		13000,    // radius of the galaxy
		4000,     // radius of the core
		0.0004,   // angluar offset of the density wave per parsec of radius
		0.85,     // excentricity at the edge of the core
		0.95,      // excentricity at the edge of the disk
		30000,    // total number of stars
		true,     // has dark matter
		2,        // Perturbations per full ellipse
		40,       // Amplitude damping factor of perturbation
		100);      // dust render size in pixel

	_roi = _galaxy.GetFarFieldRad() * 1.3;
}

/** \brief Update the density wave vertex buffers
*/
void GalaxyWnd::UpdateDensityWaves()
{
	std::cout << "Density render update request received" << std::endl;

	//
	// First three ellipsis are the boundaries of core, galaxy and galactic medium
	//

	std::vector<VertexColor> vert;
	std::vector<int> idx;

	int pertNum = 0; // _galaxy.GetPertN();
	double pertAmp = 0; //// _galaxy.GetPertAmp();
	auto r = _galaxy.GetCoreRad();
	AddEllipsisVertices(vert, idx, r, r, 0, pertNum, pertAmp, 1, 1, 0);

	r = _galaxy.GetRad();
	AddEllipsisVertices(vert, idx, r, r, 0, pertNum, pertAmp, 0, 1, 0);

	r = _galaxy.GetFarFieldRad();
	AddEllipsisVertices(vert, idx, r, r, 0, pertNum, pertAmp, 1, 0, 0);

	_vertDensityWaves.Update(vert, idx);
	_renderUpdateHint &= ~ruhDENSITY_WAVES;
}

void GalaxyWnd::Update()
{
	if ((_renderUpdateHint & ruhDENSITY_WAVES) != 0)
	{
		UpdateDensityWaves();
	}
}

void GalaxyWnd::Render()
{
	static long ct = 0;
	if (!(_flags & (int)DisplayItem::PAUSE))
	{
		++ct;
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	Vec3D orient;
	switch (_camOrient)
	{
		// Default orientation
	case 0:
		orient.x = 0;
		orient.y = 1;
		orient.z = 0;
		break;

		// Rotate with galaxy core
	case 1:
	{
		auto& p = _galaxy.GetStarPos(1);
		orient.x = p.x;
		orient.y = p.y;
		orient.z = 0;
	}
	break;

	// Rotate with edge of disk
	case 2:
	{
		auto& p = _galaxy.GetStarPos(2);
		orient.x = p.x;
		orient.y = p.y;
		orient.z = 0;
	}
	break;
	}

	Vec3D lookAt(0, 0, 0);
	Vec3D pos(0, 0, 5000);

	SetCamera(pos, lookAt, orient);

	if (!(_flags & (int)DisplayItem::PAUSE))
		_galaxy.SingleTimeStep(100000); // time in years

	if (_flags & (int)DisplayItem::AXIS)
		DrawAxis(Vec2D(0, 0));


	if (_flags & (int)DisplayItem::DUST)
		DrawDust();

	if (_flags & (int)DisplayItem::H2)
		DrawH2();

	if (_flags & (int)DisplayItem::STARS)
		DrawStars();

	if (_flags & (int)DisplayItem::DENSITY_WAVES)
	{
		DrawDensityWaves(50, _galaxy.GetFarFieldRad());
		DrawGalaxyRadii();
	}

	if (_flags & (int)DisplayItem::VELOCITY)
		DrawVelocity();

	if (_flags & (int)DisplayItem::HELP)
	{
		DrawHelp();
	}

	SDL_GL_SwapWindow(_pSdlWnd);
	SDL_Delay(1);
}

void GalaxyWnd::AddEllipsisVertices(
	std::vector<VertexColor>& vert,
	std::vector<int>& vertIdx,
	double a,
	double b,
	double angle,
	uint32_t pertNum,
	double pertAmp,
	float red,
	float green,
	float blue) const
{
	const int steps = 100;
	const double x = 0;
	const double y = 0;

	// Angle is given by Degree Value
	double beta = -angle * MathHelper::DEG_TO_RAD;
	double sinbeta = sin(beta);
	double cosbeta = cos(beta);

	Vec2D pos;
	Vec2D vecNull;

	int firstPointIdx = vert.size();
	for (int i = 0; i < 360; i += 360 / steps)
	{
		double alpha = i * MathHelper::DEG_TO_RAD;
		double sinalpha = sin(alpha);
		double cosalpha = cos(alpha);

		GLfloat fx = x + (a * cosalpha * cosbeta - b * sinalpha * sinbeta);
		GLfloat fy = y + (a * cosalpha * sinbeta + b * sinalpha * cosbeta);

		if (pertNum > 0)
		{
			fx += (a / pertAmp) * sin(alpha * 2 * pertNum);
			fy += (a / pertAmp) * cos(alpha * 2 * pertNum);
		}

		vertIdx.push_back(vert.size());

		VertexColor vc = { fx, fy, 0, red, green, blue };
		vert.push_back(vc);
	}

	// Close the loop and reset the element index array
	vertIdx.push_back(firstPointIdx);
	vertIdx.push_back(0xFFFF);
}

void GalaxyWnd::DrawEllipsis(double a, double b, double angle, GLfloat width)
{
	const int steps = 100;
	const double x = 0;
	const double y = 0;

	// Angle is given by Degree Value
	double beta = -angle * MathHelper::DEG_TO_RAD; //(Math.PI/180) converts Degree Value into Radians
	double sinbeta = sin(beta);
	double cosbeta = cos(beta);

	glEnable(GL_BLEND);            // soft blending of point sprites
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(width);
	glBegin(GL_LINE_STRIP);

	Vec2D pos;
	Vec2D vecNull;
	for (int i = 0; i < 361; i += 360 / steps)
	{
		double alpha = i * MathHelper::DEG_TO_RAD;
		double sinalpha = sin(alpha);
		double cosalpha = cos(alpha);

		GLfloat fx = x + (a * cosalpha * cosbeta - b * sinalpha * sinbeta);
		GLfloat fy = y + (a * cosalpha * sinbeta + b * sinalpha * cosbeta);

		fx += (a / _galaxy.GetPertAmp()) * sin(alpha * 2 * _galaxy.GetPertN());
		fy += (a / _galaxy.GetPertAmp()) * cos(alpha * 2 * _galaxy.GetPertN());

		glVertex3f(fx, fy, 0);
	}

	glEnd();
	glDisable(GL_BLEND);
}

void GalaxyWnd::DrawVelocity()
{
	Star* pStars = _galaxy.GetStars();

	double dt_in_sec = _galaxy.GetTimeStep() * MathHelper::SEC_PER_YEAR;
	glPointSize(1);
	glColor3f(0.5, 0.7, 0.5);
	glBegin(GL_POINTS);
	for (int i = 0; i < _galaxy.GetNumStars(); ++i)
	{
		const Vec2D& vel = pStars[i].m_vel;
		double r = pStars[i].m_a; //(pStars[i].m_a + pStars[i].m_b)/2;

		// umrechnen in km/s
		double v = sqrt(vel.x * vel.x + vel.y * vel.y);   // pc / timestep
		v /= dt_in_sec;          // v in pc/sec
		v *= MathHelper::PC_TO_KM; // v in km/s

		glVertex3f(r, v * 10, 0.0f);
	}
	glEnd();
}

void GalaxyWnd::DrawDensityWaves(int num, double rad)
{
	double dr = rad / num;

	for (int i = 0; i <= num; ++i)
	{
		double r = dr * (i + 1);
		glColor4f(0.8, 0.8, 0.8, 0.5);
		DrawEllipsis(
			r,
			r * _galaxy.GetExcentricity(r),
			MathHelper::RAD_TO_DEG * _galaxy.GetAngularOffset(r),
			1);
	}
}

void GalaxyWnd::DrawStars()
{
	glBindTexture(GL_TEXTURE_2D, _texStar);

	float maxSize = 0.0f;
	glGetFloatv(GL_POINT_SIZE_MAX, &maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MAX_ARB, maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MIN_ARB, 1.0f);
	glTexEnvf(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);

	glEnable(GL_POINT_SPRITE_ARB);
	glEnable(GL_TEXTURE_2D);       // point sprite texture support
	glEnable(GL_BLEND);            // soft blending of point sprites
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	int num = _galaxy.GetNumStars();
	Star* pStars = _galaxy.GetStars();


	glPointSize(3); //4
	glBegin(GL_POINTS);

	if (_starRenderType == 2)
		glColor3f(1, 1, 1);

	// Render all Stars from the stars array
	for (int i = 1; i < num; ++i)
	{
		const Vec2D& pos = pStars[i].m_pos;
		const Color& col = ColorFromTemperature(pStars[i].m_temp);
		if (_starRenderType == 1)
		{
			glColor3f(
				(GLfloat)col.r * pStars[i].m_mag,
				(GLfloat)col.g * pStars[i].m_mag,
				(GLfloat)col.b * pStars[i].m_mag);
		}
		glVertex3f(pos.x, pos.y, 0.0f);

	}
	glEnd();

	// Render a portion of the stars as bright distinct stars
	glPointSize(6); //4
	glBegin(GL_POINTS);


	for (int i = 1; i < num / 30; ++i)
	{
		const Vec2D& pos = pStars[i].m_pos;
		const Color& col = ColorFromTemperature(pStars[i].m_temp);
		if (_starRenderType == 1)
		{
			glColor3f(
				0.2f + col.r * pStars[i].m_mag,
				0.2f + col.g * pStars[i].m_mag,
				0.2f + col.b * pStars[i].m_mag);
		}
		glVertex3f(pos.x, pos.y, 0.0f);

	}
	glEnd();

	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
}

void GalaxyWnd::DrawDust()
{
	glBindTexture(GL_TEXTURE_2D, _texStar);

	float maxSize = 0.0f;
	glGetFloatv(GL_POINT_SIZE_MAX_ARB, &maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MAX_ARB, maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MIN_ARB, 1.0f);
	glTexEnvf(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);

	glEnable(GL_POINT_SPRITE_ARB);
	glEnable(GL_TEXTURE_2D);       // point sprite texture support
	glEnable(GL_BLEND);            // soft blending of point sprites
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	const Star* pDust = _galaxy.GetDust();
	int num = _galaxy.GetNumDust();

	// size 70 looks ok when the fov is 28174
	glPointSize(std::min((float)(_galaxy.GetDustRenderSize() * 28174 / _fov), maxSize));
	glBegin(GL_POINTS);

	for (int i = 0; i < num; ++i)
	{
		const Vec2D& pos = pDust[i].m_pos;
		const Color& col = ColorFromTemperature(pDust[i].m_temp);
		glColor3f(
			col.r * pDust[i].m_mag,
			col.g * pDust[i].m_mag,
			col.b * pDust[i].m_mag);
		glVertex3f(pos.x, pos.y, 0.0f);

	}
	glEnd();

	glDisable(GL_POINT_SPRITE_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
}

void GalaxyWnd::DrawH2()
{
	glBindTexture(GL_TEXTURE_2D, _texStar);

	float maxSize = 0.0f;
	glGetFloatv(GL_POINT_SIZE_MAX_ARB, &maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MAX_ARB, maxSize);
	glPointParameterfARB(GL_POINT_SIZE_MIN_ARB, 1.0f);
	glTexEnvf(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);

	glEnable(GL_POINT_SPRITE_ARB);
	glEnable(GL_TEXTURE_2D);       // point sprite texture support
	glEnable(GL_BLEND);            // soft blending of point sprites
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);


	Star* pH2 = _galaxy.GetH2();
	int num = _galaxy.GetNumH2();

	for (int i = 0; i < num; ++i)
	{
		int k1 = 2 * i;
		int k2 = 2 * i + 1;

		const Vec2D& p1 = pH2[k1].m_pos;
		const Vec2D& p2 = pH2[k2].m_pos;

		double dst = sqrt((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
		double size = ((1000 - dst) / 10) - 50;
		if (size < 1)
			continue;

		glPointSize(2 * size);
		glBegin(GL_POINTS);
		const Color& col = ColorFromTemperature(pH2[k1].m_temp);
		glColor3f(
			col.r * pH2[i].m_mag * 2,
			col.g * pH2[i].m_mag * 0.5,
			col.b * pH2[i].m_mag * 0.5);
		glVertex3f(p1.x, p1.y, 0.0f);
		glEnd();

		glPointSize(size / 6);
		glBegin(GL_POINTS);
		glColor3f(1, 1, 1);
		glVertex3f(p1.x, p1.y, 0.0f);
		glEnd();
	}


	glDisable(GL_POINT_SPRITE_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
}

void GalaxyWnd::DrawGalaxyRadii()
{
//#define USE_VERTEX_BUFFER
#if defined USE_VERTEX_BUFFER
	_vertDensityWaves.Draw();

	// Captions (immer noch im immediate mode!)
	glColor3f(1, 1, 0);
	DrawText(_pFont, TextCoords::Model, 0, _galaxy.GetCoreRad() + 500, "Core");
	glColor3f(0, 1, 0);
	DrawText(_pFont, TextCoords::Model, 0, _galaxy.GetRad() + 500, "Disk");
	glColor3f(1, 0, 0);
	DrawText(_pFont, TextCoords::Model, 0, _galaxy.GetFarFieldRad() + 500, "Intergalactic medium");
#else
	//  code for immediate drawing
	glColor4f(1, 1, 0, 0.6);
	double r = _galaxy.GetCoreRad();
	if (r > 0)
	{
		DrawEllipsis(r, r, 0, 3);
		DrawText(_pFont, TextCoords::Model, 0, r + 500, "Core");
	}

	glColor4f(0, 1, 0, 0.6);
	r = _galaxy.GetRad();
	DrawEllipsis(r, r, 0, 3);
	DrawText(_pFont, TextCoords::Model, 0, r + 500, "Disk");

	glColor4f(1, 0, 0, 0.6);
	r = _galaxy.GetFarFieldRad();
	DrawEllipsis(r, r, 0, 3);
	DrawText(_pFont, TextCoords::Model, 0, r + 500, "Intergalactic medium");
#endif
}

void GalaxyWnd::DrawAxis(const Vec2D& origin)
{
	glColor3f((GLfloat)0.3, (GLfloat)0.3, (GLfloat)0.3);
	glLineWidth(1);

	GLfloat s = (GLfloat)std::pow(10, (int)(std::log10(_fov / 2)));
	GLfloat l = _fov / 100, p = 0;

	glPushMatrix();
	glTranslated(origin.x, origin.y, 0);

	for (int i = 0; p < _fov; ++i)
	{
		p += s;

		if (i % 2 == 0)
		{
			DrawText(_pFont, TextCoords::Model, p - l, -4 * l, "%2.0f", p);
		}
		else
		{
			glRasterPos2f(p - l, 2 * l);
			DrawText(_pFont, TextCoords::Model, p - l, 2 * l, "%2.0f", p);
		}

		glBegin(GL_LINES);
		glVertex3f(p, -l, 0);
		glVertex3f(p, l, 0);
		glVertex3f(-p, -l, 0);
		glVertex3f(-p, 0, 0);
		glVertex3f(-l, p, 0);
		glVertex3f(0, p, 0);
		glVertex3f(-l, -p, 0);
		glVertex3f(0, -p, 0);
		glEnd();

	}

	glBegin(GL_LINES);
	glVertex3f((GLfloat)-_fov, 0, 0);
	glVertex3f((GLfloat)_fov, 0, 0);
	glVertex3f(0, (GLfloat)-_fov, 0);
	glVertex3f(0, (GLfloat)_fov, 0);
	glEnd();

	glPopMatrix();
}

void GalaxyWnd::DrawHelp()
{
	double x0 = 10, y0 = 60, dy = 20;
	int line = 0;
	Vec3D p;

	glColor3f(0.8, 0.8, 1);
	double y = y0 - 60;
	DrawText(_pFontCaption, TextCoords::Window, x0, y0 - 60, "Spiral Galaxy Simulator");

	y0 = 60; // _height - 540;
	double dy1 = TTF_FontHeight(_pFont) + 8;
	double dy2 = TTF_FontHeight(_pSmallFont) + 6;

	glColor4f(1, 1, 1, 0.6);
	y = y0;	DrawText(_pFont, TextCoords::Window, x0, y, "Simulation Controls:");
	y += dy1;	DrawText(_pSmallFont, TextCoords::Window, x0, y, "FPS:  %d", GetFPS());
	y += dy2;	DrawText(_pSmallFont, TextCoords::Window, x0, y, "Time: %2.2e y", _galaxy.GetTime());

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Geometry:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[r],[f] RadCore:     %d pc", (int)_galaxy.GetCoreRad());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[t],[g] RadGalaxy:   %d pc", (int)_galaxy.GetRad());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "        RadFarField: %d pc", (int)_galaxy.GetFarFieldRad());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[q],[a] ExInner:     %2.2f", _galaxy.GetExInner());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[w],[s] ExOuter:     %2.2f", _galaxy.GetExOuter());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[e],[d] AngOff:      %1.4g deg/pc", _galaxy.GetAngularOffset());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[+],[-] FoV:         %1.2f pc", _fov);

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Spiral Arms:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[Home],[End]    Num pert:  %d", _galaxy.GetPertN());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[PG_UP],[PG_DN] pertDamp:  %1.2f", _galaxy.GetPertAmp());

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Display Features:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[b],[n]  Dust render size:  %2.2lf", _galaxy.GetDustRenderSize());
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F1] Help Screen");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F2] Toggle Axis");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F3] Toggle Stars");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F4] Toggle Dust");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F5] Toggle H2 Regions");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[F6] Toggle Density Waves");

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Physics:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[z],[h] Base Temp.:  %2.2lf K", _temp);
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[m] Toggle Dark Matter: %s", _galaxy.HasDarkMatter() ? "ON":"OFF");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[v] Display Velocity Curve: %s", ((_flags & (int)DisplayItem::VELOCITY) != 0) ? "ON":"OFF");

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Predefined Galaxies:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[KP1] - [KP8] Predefined Galaxies");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[Pause]       Halt simulation");

	y += dy1; DrawText(_pFont, TextCoords::Window, x0, y, "Camera Control:");
	y += dy1; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[1] fixed");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[2] rotating with core");
	y += dy2; DrawText(_pSmallFont, TextCoords::Window, x0, y, "[3] rotating with outer disc");

	glColor4f(0.8, 0.8, 0.8, 0.3);
	DrawText(_pFont, TextCoords::Window, _width - 180, _height - 30, " (C) 2020 Ingo Berg");
}

GalaxyWnd::Color GalaxyWnd::ColorFromTemperature(double temp) const
{
	int idx = (temp - _t0) / (_t1 - _t0) * _colNum;
	idx = std::min(_colNum - 1, idx);
	idx = std::max(0, idx);
	return _col[idx];
}

void GalaxyWnd::OnProcessEvents(Uint32 type)
{
	switch (type)
	{
	case SDL_MOUSEBUTTONDOWN:
		break;

	case SDL_KEYDOWN:
		switch (m_event.key.keysym.sym)
		{
		case SDLK_END:
			_galaxy.SetPertN(std::max(_galaxy.GetPertN() - 1, 0));
			break;

		case SDLK_HOME:
			_galaxy.SetPertN(std::min(_galaxy.GetPertN() + 1, 5));
			break;

		case SDLK_PAGEDOWN:
			_galaxy.SetPertAmp(_galaxy.GetPertAmp() - 2);
			break;

		case SDLK_PAGEUP:
			_galaxy.SetPertAmp(_galaxy.GetPertAmp() + 2);
			break;

		case SDLK_1:
			_camOrient = 0;
			break;

		case SDLK_2:
			_camOrient = 1;
			break;

		case SDLK_3:
			_camOrient = 2;
			break;

		case SDLK_q:
			_galaxy.SetExInner(_galaxy.GetExInner() + 0.05);
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_a:
			_galaxy.SetExInner(std::max(_galaxy.GetExInner() - 0.05, 0.0));
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_w:
			_galaxy.SetExOuter(_galaxy.GetExOuter() + 0.05);
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_s:
			_galaxy.SetExOuter(std::max(_galaxy.GetExOuter() - 0.05, 0.0));
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_e:
			_galaxy.SetAngularOffset(_galaxy.GetAngularOffset() + 0.00002);
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_d:
			_galaxy.SetAngularOffset(std::max(_galaxy.GetAngularOffset() - 0.00002, 0.0));
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_r:
			if (_galaxy.GetRad() > _galaxy.GetCoreRad() + 500)
			{
				_galaxy.SetCoreRad(_galaxy.GetCoreRad() + 500);
			}
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_m:
			_galaxy.ToggleDarkMatter();
			break;

		case SDLK_f:
			_galaxy.SetCoreRad(std::max(_galaxy.GetCoreRad() - 500, 0.0));
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_t:
			_galaxy.SetRad(_galaxy.GetRad() + 1000);
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_g:
			_galaxy.SetRad(std::max(_galaxy.GetRad() - 1000, 0.0));
			_renderUpdateHint |= ruhDENSITY_WAVES;
			break;

		case SDLK_z:
			_temp += 100;
			_temp = std::min(_temp, 15000.0);
			std::cout << "Color is " << _temp << std::endl;
			_galaxy.SetColorFunction([](double r) { return _temp + r / 4.5; });
			break;

		case SDLK_h:
			_temp -= 100;
			_temp = std::max(_temp, 500.0);
			std::cout << "Color is " << _temp << std::endl;
			_galaxy.SetColorFunction([](double r) { return _temp + r / 4.5; });
			break;

		case SDLK_b:
			_galaxy.SetDustRenderSize(_galaxy.GetDustRenderSize() - 5);
			break;

		case SDLK_n:
			_galaxy.SetDustRenderSize(_galaxy.GetDustRenderSize() + 5);
			break;

		case  SDLK_F1:
			_flags ^= (int)DisplayItem::HELP;
			break;

		case SDLK_F2:
			_flags ^= (int)DisplayItem::AXIS;
			break;

		case  SDLK_F3:
			if (_starRenderType == 2)
			{
				_starRenderType = 0;
				_flags &= ~(int)DisplayItem::STARS;
			}
			else
			{
				_starRenderType++;
				_flags |= (int)DisplayItem::STARS;
			}
			break;

		case  SDLK_F4:
			_flags ^= (int)DisplayItem::DUST;
			break;

		case  SDLK_F5:
			_flags ^= (int)DisplayItem::H2;
			break;

		case SDLK_F6:
			_flags ^= (int)DisplayItem::DENSITY_WAVES;
			break;

		case  SDLK_v:
			_flags ^= (int)DisplayItem::VELOCITY;
			break;

		case  SDLK_PAUSE:
			_flags ^= (int)DisplayItem::PAUSE;
			break;

		case SDLK_KP_0:
			_temp = 4000;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				4000,     // radius of the core
				0.0004,   // angluar offset of the density wave per parsec of radius
				0.85,     // excentricity at the edge of the core
				0.95,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				2,        // Perturbations per full ellipse
				40,       // Amplitude damping factor of perturbation
				90,
				[](double r) { return _temp + r / 5; });      // dust render size in pixel
			_fov = 33960;
			break;

		case SDLK_KP_1:
			_temp = 4500;
			_galaxy.Reset(
				16000,    // radius of the galaxy
				4000,     // radius of the core
				0.0003,   // angluar offset of the density wave per parsec of radius
				0.8,     // excentricity at the edge of the core
				0.85,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				0,        // Perturbations per full ellipse
				40,       // Amplitude damping factor of perturbation
				100,
				[](double r) { return _temp + r / 4.5; });
			_fov = 46585;
			break;

		case SDLK_KP_2:
			_temp = 4200;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				4000,     // radius of the core
				0.0004,   // angluar offset of the density wave per parsec of radius
				0.9,      // excentricity at the edge of the core
				0.9,      // excentricity at the edge of the disk
				30000,
				true,
				0,
				0,
				70,   // total number of stars
				[](double r) { return _temp + r / 4.5; });
			break;
		case SDLK_KP_3:
			_temp = 4500;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				4000,     // radius of the core
				0.0004,   // angluar offset of the density wave per parsec of radius
				1.35,      // excentricity at the edge of the core
				1.05,      // excentricity at the edge of the disk
				40000,
				true,
				0,
				0,
				70,   // total number of stars
				[](double r) { return _temp + r / 4.5; });
			break;

		case SDLK_KP_4:
			_temp = 4000;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				4500,     // radius of the core
				0.0002,   // angluar offset of the density wave per parsec of radius
				0.65,     // excentricity at the edge of the core
				0.95,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				3,        // Perturbations per full ellipse
				72,       // Amplitude damping factor of perturbation
				90,      // dust render size in pixel
				[](double r) { return _temp + r / 4.5; });
			_fov = 35000;
			break;

			// Typ SBb
		case SDLK_KP_5:
			_temp = 4500;
			_galaxy.Reset(
				15000,    // radius of the galaxy
				4000,     // radius of the core
				0.0003,   // angluar offset of the density wave per parsec of radius
				1.45,     // excentricity at the edge of the core
				1.0,      // excentricity at the edge of the disk
				40000,
				true,
				0,
				0,
				70,   // total number of stars
				[](double r) { return _temp + r / 5; });
			break;

		case SDLK_KP_6:
			_temp = 2200;
			_galaxy.Reset(
				14000,    // radius of the galaxy
				12500,     // radius of the core
				0.0002,   // angluar offset of the density wave per parsec of radius
				0.65,     // excentricity at the edge of the core
				0.95,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				3,        // Perturbations per full ellipse
				72,       // Amplitude damping factor of perturbation
				85,        // dust render size in pixel
				[](double r) { return _temp + r / 5; });
			_fov = 36982;
			break;


		case SDLK_KP_7:
			_temp = 2800;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				1500,     // radius of the core
				0.0004,   // angluar offset of the density wave per parsec of radius
				1.1,     // excentricity at the edge of the core
				1.0,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				1,        // Perturbations per full ellipse
				20,       // Amplitude damping factor of perturbation
				80,
				[](double r) { return _temp + r / 5; });      // dust render size in pixel
			_fov = 41091;
			break;


		case SDLK_KP_8:
			_temp = 4500;
			_galaxy.Reset(
				13000,    // radius of the galaxy
				4000,     // radius of the core
				0.0004,   // angluar offset of the density wave per parsec of radius
				0.85,     // excentricity at the edge of the core
				0.95,      // excentricity at the edge of the disk
				40000,    // total number of stars
				true,     // has dark matter
				1,        // Perturbations per full ellipse
				20,       // Amplitude damping factor of perturbation
				80,      // dust render size in pixel
				[](double r) { return _temp + r / 5; });
			_fov = 41091;
			break;

		case SDLK_PLUS:
		case SDLK_KP_PLUS:
			ScaleAxis(0.9);
			SetCameraOrientation(Vec3D(0, 1, 0));
			break;


		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			ScaleAxis(1.1);
			SetCameraOrientation(Vec3D(0, 1, 0));
			break;

		default:
			break;

		}

		break;
	}
}
