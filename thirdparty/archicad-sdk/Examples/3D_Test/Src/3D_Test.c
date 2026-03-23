// *****************************************************************************
// Source code for the 3D Test Add-On
// *****************************************************************************

#include "APIEnvir.h"


// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Location.hpp"

#include	"ResourceIDs.hpp"


// -----------------------------------------------------------------------------
// Get a material with index
// -----------------------------------------------------------------------------

API_MaterialType	GetUMaterial (Int32 iumat)
{
	static GS::HashTable<Int32, API_MaterialType> umatCache;

	API_Component3D	mat = {};

	mat.header.typeID = API_UmatID;
	mat.header.index = iumat;
	if (umatCache.ContainsKey (iumat))
		return umatCache.Get (iumat);

	ACAPI_ModelAccess_GetComponent (&mat);
	delete mat.umat.mater.texture.fileLoc;
	mat.umat.mater.texture.fileLoc = nullptr;

	umatCache.Add (iumat, mat.umat.mater);
	return mat.umat.mater;
}


// -----------------------------------------------------------------------------
// Coordinate helper functions
// -----------------------------------------------------------------------------

API_Coord3D		operator- (const API_Coord3D &c1, const API_Coord3D &c2)

{	API_Coord3D c;

	c.x = c1.x - c2.x;
	c.y = c1.y - c2.y;
	c.z = c1.z - c2.z;
	return c;
}

API_Coord3D		operator+ (const API_Coord3D &c1, const API_Coord3D &c2)

{	API_Coord3D c;

	c.x = c1.x + c2.x;
	c.y = c1.y + c2.y;
	c.z = c1.z + c2.z;
	return c;
}

API_Coord		operator- (API_Coord c1, API_Coord c2)

{	API_Coord c;

	c.x = c1.x - c2.x;
	c.y = c1.y - c2.y;
	return c;
}

API_Coord		operator+ (API_Coord c1, API_Coord c2)

{	API_Coord c;

	c.x = c1.x + c2.x;
	c.y = c1.y + c2.y;
	return c;
}
API_Coord3D		Normalize (const API_Coord3D &v)

{
	API_Coord3D ret = v;
	double len = v.x * v.x + v.y * v.y + v.z * v.z;
	if (len > 1e-10) {
		double lambda = 1.0 / sqrt (len);
		ret.x = ret.x * lambda;
		ret.y = ret.y * lambda;
		ret.z = ret.z * lambda;
	}
	return ret;
}

API_Vector3D	VectProd (const API_Vector3D &v1, const API_Vector3D &v2)

{
	API_Vector3D v;

	v.x = v1.y * v2.z - v1.z * v2.y;
	v.y = v1.z * v2.x - v1.x * v2.z;
	v.z = v1.x * v2.y - v1.y * v2.x;

	return v;
}

double		operator* (const API_Coord3D &a, const API_Coord3D &b)

{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

API_Coord3D		operator* (const double &a, const API_Coord3D &b)

{
	return {a*b.x, a*b.y, a*b.z};
}

// -----------------------------------------------------------------------------
// Dump a body (BODY, VERT, EDGE)
// -----------------------------------------------------------------------------

static void		DumpBody (Int32 ibody, bool detailed, const API_Coord& dbOffset)
{
	API_Component3D 	component = {};
	Int32				j, nVert, nEdge, nPgon, kk;
	API_Tranmat			tm;
	API_UVCoord			resultUV;
	API_Coord3D			normVect;
	Int32				elemIdx, bodyIdx, ivect, fpedg, lpedg;
	GSErrCode			err;

	WriteReport ("BODY #%d", ibody);

	component.header.typeID = API_BodyID;
	component.header.index  = ibody;
	err = ACAPI_ModelAccess_GetComponent (&component);
	if (err != NoError) {
		WriteReport ("    ERROR");
		return;
	}

	if (!detailed)
		return;

	nVert = component.body.nVert;
	nEdge = component.body.nEdge;
	nPgon = component.body.nPgon;
	tm = component.body.tranmat;
	elemIdx = component.body.head.elemIndex - 1;
	bodyIdx = component.body.head.bodyIndex - 1;

	WriteReport ("  VERTs");
	for (j = 1; j <= nVert; j++) {
		component.header.typeID = API_VertID;
		component.header.index  = j;
		err = ACAPI_ModelAccess_GetComponent (&component);
		if (err == NoError) {
			API_Coord3D	trCoord;	// world coordinates
			trCoord.x = tm.tmx[0]*component.vert.x + tm.tmx[1]*component.vert.y + tm.tmx[2]*component.vert.z + tm.tmx[3];
			trCoord.y = tm.tmx[4]*component.vert.x + tm.tmx[5]*component.vert.y + tm.tmx[6]*component.vert.z + tm.tmx[7];
			trCoord.z = tm.tmx[8]*component.vert.x + tm.tmx[9]*component.vert.y + tm.tmx[10]*component.vert.z + tm.tmx[11];
			trCoord.x += dbOffset.x;
			trCoord.y += dbOffset.y;
			WriteReport ("    [%2d]  (%lf, %lf, %lf)",
						 j,
						 trCoord.x, trCoord.y, trCoord.z);
		}
	}

	WriteReport ("  EDGEs");
	for (j = 1; j <= nEdge; j++) {
		component.header.typeID = API_EdgeID;
		component.header.index  = j;
		err = ACAPI_ModelAccess_GetComponent (&component);
		if (err == NoError)
			WriteReport ("    [%2d]  (%3d, %3d) - (%3d, %3d)",
						 j,
						 component.edge.vert1, component.edge.vert2,
						 component.edge.pgon1, component.edge.pgon2);
	}

	WriteReport ("  PGONs %ld %ld %ld", nPgon, elemIdx, bodyIdx);
	for (j = 1; j <= nPgon; j++) {
		component.header.typeID = API_PgonID;
		component.header.index  = j;
		err = ACAPI_ModelAccess_GetComponent (&component);
		if (err == NoError)
			WriteReport ("    [%2d]  (first edge: %3d, last edge: %3d, ivect: %3d)",
						 j,
						 component.pgon.fpedg, component.pgon.lpedg, component.pgon.ivect);

		API_MaterialType mat = GetUMaterial (component.pgon.iumat);
		WriteReport ("    MATERIAL %s [index: %d], guid: %s", mat.head.name, mat.head.index, APIGuidToString (mat.head.guid).ToCStr ().Get ());

		if (elemIdx < 0 || bodyIdx < 0) {
			WriteReport ("  No TEXTURE info");
			continue;
		}

		ivect = component.pgon.ivect;
		fpedg = component.pgon.fpedg;
		lpedg = component.pgon.lpedg;

		WriteReport ("    TEXTURE");

		API_TexCoordPars	pars = {0};
		pars.elemIdx = elemIdx;
		pars.bodyIdx = bodyIdx;
		pars.pgonIndex = j;
		//pars.filler_1;
		// get a coordinate for surface point
		component.header.typeID = API_PedgID;
		component.header.index  = component.pgon.fpedg;
		err = ACAPI_ModelAccess_GetComponent (&component);
		if (err == NoError) {
			component.header.typeID = API_EdgeID;
			component.header.index  = ::abs (component.pedg.pedg);
			err = ACAPI_ModelAccess_GetComponent (&component);
			if (err == NoError) {
				component.header.typeID = API_VertID;
				component.header.index  = ::abs (component.edge.vert1);
				err = ACAPI_ModelAccess_GetComponent (&component);
				if (err == NoError) {
					pars.surfacePoint.x = component.vert.x;
					pars.surfacePoint.y = component.vert.y;
					pars.surfacePoint.z = component.vert.z;
					err = ACAPI_ModelAccess_GetTextureCoord (&pars, &resultUV);
					if (err == NoError)
						WriteReport ("          (texture: (u,v) : (%5.2lf, %5.2lf))",
									 resultUV.u, resultUV.v);
				}
			}
		}

		WriteReport ("    NORMALVECTOR");
		component.header.typeID = API_VectID;
		component.header.index  = ::abs (ivect);
		err = ACAPI_ModelAccess_GetComponent (&component);
		if (err == NoError) {
			if (ivect < 0) {
				component.vect.x = -component.vect.x;
				component.vect.y = -component.vect.y;
				component.vect.z = -component.vect.z;
			}
			normVect = {component.vect.x, component.vect.y, component.vect.z};
			API_Coord3D	trVect;	// world coordinates
			trVect.x = tm.tmx[0]*component.vect.x + tm.tmx[1]*component.vect.y + tm.tmx[2]*component.vect.z;
			trVect.y = tm.tmx[4]*component.vect.x + tm.tmx[5]*component.vect.y + tm.tmx[6]*component.vect.z;
			trVect.z = tm.tmx[8]*component.vect.x + tm.tmx[9]*component.vect.y + tm.tmx[10]*component.vect.z;
			WriteReport ("          [%2d]  (%lf, %lf, %lf)",
						 ivect,
						 trVect.x, trVect.y, trVect.z);
			normVect = trVect;
		}

		GS::Array<GS::Array<API_Coord3D>>  conts;
		UIndex		contInd = 0;

		conts.Push (GS::Array<API_Coord3D>());

		for (kk = fpedg; kk <= lpedg; ++kk) {
			component.header.typeID = API_PedgID;
			component.header.index  = kk;
			err = ACAPI_ModelAccess_GetComponent (&component);
			if (err == NoError) {
				component.header.typeID = API_EdgeID;
				component.header.index  = ::abs (component.pedg.pedg);
				bool isPositive = component.pedg.pedg > 0;
				if (component.pedg.pedg != 0) {
					err = ACAPI_ModelAccess_GetComponent (&component);
					if (err == NoError) {
						component.header.typeID = API_VertID;
						component.header.index  = isPositive ? component.edge.vert1 : component.edge.vert2;
						err = ACAPI_ModelAccess_GetComponent (&component);
						if (err == NoError) {
							conts[contInd].Push ({component.vert.x, component.vert.y, component.vert.z});
						}
					}
				} else {
					conts.Push (GS::Array<API_Coord3D>());
					++contInd;
				}
			}
		}

		API_Coord3D vx = Normalize (conts[0][1] - conts[0][0]);
		API_Coord3D vy = Normalize (VectProd (normVect, vx));
		conts.Enumerate ([&] (GS::Array<API_Coord3D>& subCont) {
			subCont.Enumerate ([&] (API_Coord3D& vert) {
				API_Coord3D vv (vert);
				vert.x = (vv - conts[0][0]) * vx;
				vert.y = (vv - conts[0][0]) * vy;
				vert.z = 0.0;
			});
		});

		USize	nConts = conts.GetSize (), nCoords (0);
		conts.Enumerate ([&] (const GS::Array<API_Coord3D>& subCont) {
			subCont.Enumerate ([&] (const API_Coord3D& ) {
				++nCoords;
			});
		});
		DBPrintf ("\t\tNumber of contours is: %lu, number of coordinates is: %lu\n", nConts, nCoords);

		API_ElementMemo		memo = {};
		memo.coords = reinterpret_cast<API_Coord**>(BMhAllClear ((1 + nConts + nCoords) * sizeof (API_Coord)));
		memo.pends  = reinterpret_cast<Int32**>(BMhAllClear ((1 + nConts) * sizeof (Int32)));
		Int32 ci = 0, pi = 0;
		conts.Enumerate ([&] (const GS::Array<API_Coord3D>& subCont) {
			subCont.Enumerate ([&] (const API_Coord3D& vert) {
				API_Coord& cc = (*memo.coords)[++ci];
				cc.x = vert.x;
				cc.y = vert.y;
				WriteReport ("          [%2d]  (%lf, %lf)",
							 ci,
							 cc.x, cc.y);
			});
			// duplicate first coordinate
			API_Coord& cc = (*memo.coords)[++ci];
			cc.x = subCont[0].x;
			cc.y = subCont[0].y;
			WriteReport ("          [%2d]  (%lf, %lf)",
						 ci,
						 cc.x, cc.y);

			// store the polygon end
			(*memo.pends)[++pi] = ci;
			WriteReport ("      [%2d]  (%2d)",
						 pi,
						 ci);
		});


		if (nConts > 1) {
			API_Coord		**coords = nullptr;
			GSErrCode err1 = ACAPI_Polygon_TriangulatePoly (&memo, &coords);
			if (err1 == NoError) {
				UInt32 nn = BMGetHandleSize ((GSHandle) coords) / (3 * sizeof (API_Coord));
				DBPrintf ("Success!!! Number of triangles is: %lu\n", nn);

				for (UInt32 ii = 0; ii < nn; ++ii) {
					UInt32 numFound = 0;
					for (UInt32 jj = 0; jj < 3; ++jj) {
						API_Coord3D	backCo = conts[0][0] + (*coords)[3 * ii + jj].x * vx + (*coords)[3 * ii + jj].y * vy;
						conts.Enumerate ([&] (const GS::Array<API_Coord3D>& subCont) {
							subCont.Enumerate ([&] (const API_Coord3D& vert) {
								if (fabs (backCo.x - vert.x) < EPS && fabs (backCo.y - vert.y) < EPS && fabs (backCo.z - vert.z) < EPS)
									++numFound;
							});
							switch (numFound) {
								case 2:
									DBPrintf ("triangle id: %lu has an edge on this contour\n", ii);
									break;
								default:
									DBPrintf ("triangle id: %lu bad contour\n", ii);
									break;
							}
						});
					}
				}
			} else {
				DBPrintf ("Error: %lu\n", err1);
			}
			if (coords != nullptr)
				BMKillHandle (reinterpret_cast<GSHandle *>(&coords));
		}

		ACAPI_DisposeElemMemoHdls (&memo);
	}

}		// DumpBody

// -----------------------------------------------------------------------------
// Dump a light source
// -----------------------------------------------------------------------------

static void		DumpLight (Int32 ilght, bool detailed)
{
	API_Component3D 	component = {};
	GSErrCode			err;

	WriteReport ("LIGHT #%d", ilght);

	component.header.typeID = API_LghtID;
	component.header.index  = ilght;
	err = ACAPI_ModelAccess_GetComponent (&component);
	if (err != NoError) {
		WriteReport ("    ERROR");
		return;
	}

	if (!detailed)
		return;

	API_LghtType	&lght = component.lght;
	switch (lght.type) {
		case APILght_DistantID:
			WriteReport ("  Distant");
			break;
		case APILght_DirectionID:
			WriteReport ("  Direction");
			break;
		case APILght_SpotID:
			WriteReport ("  Spot");
			break;
		case APILght_PointID:
			WriteReport ("  Point");
			break;
		case APILght_SunID:
			WriteReport ("  Sun");
			break;
		case APILght_EyeID:
			WriteReport ("  Eye");
			break;
	}

	WriteReport ("  status: %d", lght.status);
	WriteReport ("  color : (%5.2f, %5.2f, %5.2f)",	lght.lightRGB.f_red,
													lght.lightRGB.f_green,
													lght.lightRGB.f_blue);
	WriteReport ("  pos   : (%5.2f, %5.2f, %5.2f)",	lght.posx,
													lght.posy,
													lght.posz);
	WriteReport ("  dir   : (%5.2f, %5.2f, %5.2f)",	lght.dirx,
													lght.diry,
													lght.dirz);
	WriteReport ("  radius: %5.2f",	lght.radius);
	WriteReport ("  cosa  : %5.2f",	lght.cosa);
	WriteReport ("  cosb  : %5.2f",	lght.cosb);
	WriteReport ("  afall : %5.2f",	lght.afall);
	WriteReport ("  dists : %5.2f, %5.2f",	lght.dist1,
											lght.dist2);
	WriteReport ("  dfall : %5.2f",	lght.dfall);

	return;
}		// DumpLight


// =============================================================================
//
// Main functions
//
// =============================================================================


// -----------------------------------------------------------------------------
// Get the 3D representation of the clicked element
// -----------------------------------------------------------------------------

static void		Do_Get3DOfClickedElem (void)
{
	API_ElemType			 type;
	API_Guid				 guid;
	API_ElemInfo3D 			 info3D;
	Int32					 ibody;
	bool					 detailed;
	GS::Array<API_Elem_Head> elems;
	API_Elem_Head			 elem = {};
	API_ElementMemo			 memo = {};
	API_Coord				 dbOffset;
	GSErrCode				 err = NoError;

	WriteReport ("# Get the 3D model of the clicked element");
	WriteReport ("#   - the 3D window don't have to be open or updated");
	WriteReport ("#   - only the first 5 bodies are Dumped in detail");

	if (!ClickAnElem ("Click an element to Dump its 3D representation", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	ACAPI_ProjectSetting_GetOffset (&dbOffset);

	if (type != API_CurtainWallID && type != API_ColumnID && type != API_BeamID) {
		elem.guid = guid;
		if (ACAPI_Element_GetHeader (&elem) == NoError)
			elems.Push (elem);
	}

	if (type == API_CurtainWallID || type == API_StairID || type == API_BeamID || type == API_ColumnID) {
		Int32 idx;
		bool isDegenerate;
		UInt64 mask = APIMemoMask_CWallFrames | APIMemoMask_CWallPanels | APIMemoMask_CWallJunctions | APIMemoMask_CWallAccessories |
					  APIMemoMask_StairRiser | APIMemoMask_StairTread | APIMemoMask_StairStructure |
					  APIMemoMask_BeamSegment | APIMemoMask_ColumnSegment;
		err = ACAPI_Element_GetMemo (guid, &memo, mask);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_GetMemo", err);
			ACAPI_DisposeElemMemoHdls (&memo);
			return;
		}

		if (type == API_CurtainWallID) {
			// frames
			GSSize nFrames = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.cWallFrames)) / sizeof (API_CWFrameType);
			for (idx = 0; idx < nFrames; ++idx)
				elems.Push (memo.cWallFrames[idx].head);

			// panels
			GSSize nPanels = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.cWallPanels)) / sizeof (API_CWPanelType);
			for (idx = 0; idx < nPanels; ++idx) {
				err = ACAPI_CurtainWall_IsCWPanelDegenerate (&memo.cWallPanels[idx].head.guid, &isDegenerate);
				if (!isDegenerate) {
					elems.Push (memo.cWallPanels[idx].head);
				} else {
					GS::Guid guid = APIGuid2GSGuid (memo.cWallPanels[idx].head.guid);
					WriteReport ("Degenerate Panel: %s", guid.ToUniString ().ToCStr ().Get ());
				}
			}

			// junctions
			GSSize nJunctions = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.cWallJunctions)) / sizeof (API_CWJunctionType);
			for (idx = 0; idx < nJunctions; ++idx)
				elems.Push (memo.cWallJunctions[idx].head);

			// accessories
			GSSize nAccessories = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.cWallAccessories)) / sizeof (API_CWAccessoryType);
			for (idx = 0; idx < nAccessories; ++idx)
				elems.Push (memo.cWallAccessories[idx].head);
		} else if (type == API_StairID) {
			// risers
			GSSize nRisers = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.stairRisers)) / sizeof (API_StairRiserType);
			for (idx = 0; idx < nRisers; ++idx)
				elems.Push (memo.stairRisers[idx].head);

			// treads
			GSSize nTreads = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.stairTreads)) / sizeof (API_StairTreadType);
			for (idx = 0; idx < nTreads; ++idx)
				elems.Push (memo.stairTreads[idx].head);

			// structures
			GSSize nStructures = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.stairStructures)) / sizeof (API_StairStructureType);
			for (idx = 0; idx < nStructures; ++idx)
				elems.Push (memo.stairStructures[idx].head);
		} else if (type == API_ColumnID) {
			GSSize nSegments = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.columnSegments)) / sizeof (API_ColumnSegmentType);
			for (idx = 0; idx < nSegments; ++idx)
				elems.Push (memo.columnSegments[idx].head);
		} else if (type == API_BeamID) {
			GSSize nSegments = BMGetPtrSize (reinterpret_cast<GSPtr>(memo.beamSegments)) / sizeof (API_BeamSegmentType);
			for (idx = 0; idx < nSegments; ++idx)
				elems.Push (memo.beamSegments[idx].head);
		}
	}

	for (auto& elemHead : elems.AsConst ()) {
		WriteReport ("%s GUID:%s (3D model Dump) ", ElemID_To_Name (elemHead.type).ToCStr (CC_UTF8).Get (),
													APIGuidToString (elemHead.guid).ToCStr ().Get ());

		err = ACAPI_ModelAccess_Get3DInfo (elemHead, &info3D);
		if (err == APIERR_BADID) {
			WriteReport_Alert ("This element does not have 3D information.");
			continue;
		}
		if (err != NoError) {
			ErrorBeep ("Error in ACAPI_ModelAccess_Get3DInfo", err);
			continue;
		}

		WriteReport ("  number of bodies: %d", info3D.lbody - info3D.fbody + 1);
		WriteReport ("  number of lights: %d", info3D.llight - info3D.flight + 1);
		WriteReport ("  bounding box: %5.2lf, %5.2lf, %5.2lf, %5.2lf", info3D.bounds.xMin, info3D.bounds.xMax,
						info3D.bounds.yMin, info3D.bounds.yMax);

		detailed = true;
		for (ibody = info3D.fbody; ibody <= info3D.lbody; ibody++) {
			// only the first 5 BODYs are written in detailed mode
			if (ibody == info3D.fbody + 5)
				detailed = false;
			DumpBody (ibody, detailed, dbOffset);
		}
	}

	WriteReport_End (err);

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_Get3DOfClickedElem


// -----------------------------------------------------------------------------
// Get the actual 3D model
// -----------------------------------------------------------------------------

static GSErrCode	Do_Dump3DModel (bool fromSaveAs)
{
	API_Coord	nullOffset = { 0.0, 0.0 };
	Int32		nbody, ibody, nlght, ilght;
	void		*origSight;
	bool		detailed;
	GSErrCode	err;

	WriteReport ("# Get the actual 3D model");
	WriteReport ("#   - the model will be not updated");

	WriteReport ("3D model Dump");
	if (!fromSaveAs) {		// we must switch to the 3D Window's sight
		err = ACAPI_Sight_SelectSight (nullptr, &origSight);
		if (err != NoError) {
			ErrorBeep ("Cannot switch to the 3D sight", err);
			return err;
		}
	}

	err = ACAPI_ModelAccess_GetNum (API_BodyID, &nbody);
	if (err != NoError) {
		ErrorBeep ("Error in ACAPI_ModelAccess_GetNum", err);
		if (!fromSaveAs)
			ACAPI_Sight_SelectSight (origSight, &origSight);
		return err;
	}

	detailed = true;
	for (ibody = 1; ibody <= nbody; ibody++) {
		// only the first 5 BODY-s are written in detailed mode
		if (ibody == 5 && !fromSaveAs)
			detailed = false;
		DumpBody (ibody, detailed, nullOffset);
	}

	err = ACAPI_ModelAccess_GetNum (API_LghtID, &nlght);
	if (err != NoError) {
		ErrorBeep ("Error in ACAPI_ModelAccess_GetNum", err);
		if (!fromSaveAs)
			ACAPI_Sight_SelectSight (origSight, &origSight);
		return err;
	}

	detailed = true;
	for (ilght = 1; ilght <= nlght; ilght++) {
		// only the first 5 BODY-s are written in detailed mode
		if (ilght == 5)
			detailed = false;
		DumpLight (ilght, detailed);
	}

	if (fromSaveAs) {
		WriteReport_Alert ("The output is written into the report window instead of the selected file.");
		// adjust return code to delete the empty output file
		err = APIERR_CANCEL;
	}

	if (!fromSaveAs)
		ACAPI_Sight_SelectSight (origSight, &origSight);

	WriteReport_End (err);

	return err;
}		// Do_Get3DModel


// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case ID_MENU_STRINGS:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		Do_Get3DOfClickedElem ();		break;
				case 2:		Do_Dump3DModel (false);			break;
			}
			break;
	}


	return NoError;
}		// MenuCommandHandler


// -----------------------------------------------------------------------------
// Dump 3D Model (called from another add-on)
// -----------------------------------------------------------------------------
GSErrCode DumpCommandHandler (GSHandle parHdl, GSPtr resultData, bool silentMode)
{
UNUSED_PARAMETER (parHdl);
UNUSED_PARAMETER (resultData);
UNUSED_PARAMETER (silentMode);

	GSErrCode	err = Do_Dump3DModel (false);

	return err;
}	// DumpCommandHandler


// -----------------------------------------------------------------------------
// Dump the 3D model
// -----------------------------------------------------------------------------

GSErrCode IOCommandProc (const API_IOParams * /*ioParams*/)
{
	GSErrCode	err = Do_Dump3DModel (true);
	return err;
}		// IOCommandProc


// =============================================================================
//
// Required functions
//
// =============================================================================


// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = NoError;
	err = ACAPI_MenuItem_RegisterMenu (ID_MENU_STRINGS, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);
	if (err == NoError)
		err = ACAPI_AddOnIntegration_RegisterFileType (1, 'TEXT', 'GSAC', "txt;", 0, ID_FILETYPE_STRINGS, 1, SaveAs3DSupported);
	if (err == NoError)
		err = ACAPI_AddOnAddOnCommunication_RegisterSupportedService ('DUMP', 1L);
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ID_MENU_STRINGS, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("3D_Test:: Initialize () ACAPI_MenuItem_InstallMenuHandler failed\n");

	err = ACAPI_AddOnIntegration_InstallFileTypeHandler (1, IOCommandProc);
	if (err != NoError)
		DBPrintf ("3D_Test:: Initialize () ACAPI_AddOnIntegration_InstallFileTypeHandler failed\n");

	err = ACAPI_AddOnIntegration_InstallModulCommandHandler ('DUMP', 1L, DumpCommandHandler);
	if (err != NoError)
		DBPrintf ("3D_Test:: Initialize () ACAPI_AddOnIntegration_InstallModulCommandHandler failed\n");


	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)

{
	return NoError;
}		// FreeData
