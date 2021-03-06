/****************************************************************************
**
** This file is part of the LibreCAD project, a 2D CAD program
**
**  Copyright (C) 2011 Rallaz, rallazz@gmail.com
** Copyright (C) 2010 R. van Twisk (librecad@rvt.dds.nl)
**
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License as published by the Free Software
** Foundation either version 2 of the License, or (at your option)
**  any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
**
**********************************************************************/


#include "rs_filterdxfrw.h"

#include <stdio.h>
//#include <map>

#include "rs_dimaligned.h"
#include "rs_dimangular.h"
#include "rs_dimdiametric.h"
#include "rs_dimlinear.h"
#include "rs_dimradial.h"
#include "rs_hatch.h"
#include "rs_image.h"
#include "rs_leader.h"
#include "rs_spline.h"
#include "rs_system.h"

#include <QStringList>

#include <qtextcodec.h>


/**
 * Default constructor.
 *
 */
RS_FilterDXFRW::RS_FilterDXFRW()
    :RS_FilterInterface(),DRW_Interface() {

    RS_DEBUG->print("RS_FilterDXFRW::RS_FilterDXFRW()");

    currentContainer = NULL;
    graphic = NULL;
    RS_DEBUG->print("RS_FilterDXFRW::RS_FilterDXFRW(): OK");
}

/**
 * Destructor.
 */
RS_FilterDXFRW::~RS_FilterDXFRW() {
    RS_DEBUG->print("RS_FilterDXFRW::~RS_FilterDXFRW(): OK");
}



/**
 * Implementation of the method used for RS_Import to communicate
 * with this filter.
 *
 * @param g The graphic in which the entities from the file
 * will be created or the graphics from which the entities are
 * taken to be stored in a file.
 */
bool RS_FilterDXFRW::fileImport(RS_Graphic& g, const QString& file, RS2::FormatType /*type*/) {
    RS_DEBUG->print("RS_FilterDXFRW::fileImport");

    RS_DEBUG->print("DXFRW Filter: importing file '%s'...", (const char*)QFile::encodeName(file));

    graphic = &g;
    currentContainer = graphic;
    this->file = file;
    // add some variables that need to be there for DXF drawings:
    graphic->addVariable("$DIMSTYLE", "Standard", 2);
    dimStyle = "Standard";
    codePage = "ANSI_1252";
    textStyle = "Standard";

    dxf = new dxfRW(QFile::encodeName(file));

    RS_DEBUG->print("RS_FilterDXFRW::fileImport: reading file");
    bool success = dxf->read(this, true);
    RS_DEBUG->print("RS_FilterDXFRW::fileImport: reading file: OK");
    //graphic->setAutoUpdateBorders(true);

    if (success==false) {
        RS_DEBUG->print(RS_Debug::D_WARNING,
                        "Cannot open DXF file '%s'.", (const char*)QFile::encodeName(file));
        return false;
    }

    RS_DEBUG->print("RS_FilterDXFRW::fileImport: updating inserts");
    graphic->updateInserts();

    RS_DEBUG->print("RS_FilterDXFRW::fileImport OK");

    delete dxf;
    return true;
}

/*
 * get teh encoding of the DXF files,
 * Acad versions >= 2007 are UTF-8, others in ANSI_1252
 */
QString RS_FilterDXFRW::getDXFEncoding() {

    // >= ACAD2007
    if (version >= 1021) {
        return RS_System::getEncoding("UTF-8");
    }

    // < ACAD2007
    return RS_System::getEncoding(codePage);
}


/**
 * Implementation of the method which handles layers.
 */
void RS_FilterDXFRW::addLayer(const DRW_Layer &data) {
    RS_DEBUG->print("RS_FilterDXF::addLayer");
    RS_DEBUG->print("  adding layer: %s", data.name.c_str());

    RS_DEBUG->print("RS_FilterDXF::addLayer: creating layer");

    QString name = toNativeString(data.name.c_str(),getDXFEncoding());
    if (name != "0" && graphic->findLayer(name)!=NULL) {
        return;
    }
    RS_Layer* layer = new RS_Layer(name);
    RS_DEBUG->print("RS_FilterDXF::addLayer: set pen");
    layer->setPen(attributesToPen(&data));

    RS_DEBUG->print("RS_FilterDXF::addLayer: flags");
    if (data.flags&0x01) {
        layer->freeze(true);
    }
    if (data.flags&0x04) {
        layer->lock(true);
    }
    //help layer doesn't appear in printing
    layer->setHelpLayer(! data.plotF);

    RS_DEBUG->print("RS_FilterDXF::addLayer: add layer to graphic");
    graphic->addLayer(layer);
    RS_DEBUG->print("RS_FilterDXF::addLayer: OK");
}

/**
 * Implementation of the method which handles blocks.
 *
 * @todo Adding blocks to blocks (stack for currentContainer)
 */
void RS_FilterDXFRW::addBlock(const DRW_Block& data) {

    RS_DEBUG->print("RS_FilterDXF::addBlock");

    RS_DEBUG->print("  adding block: %s", data.name.c_str());
/*TODO correct handle of model-space*/

    QString name = QString::fromStdString (data.name);
    QString mid = name.mid(1,11);
// Prevent special blocks (paper_space, model_space) from being added:
    if (mid.toLower() != "paper_space" && mid.toLower() != "model_space") {

            RS_Vector bp(data.basePoint.x, data.basePoint.y);
            RS_Block* block =
                new RS_Block(graphic,
//                             RS_BlockData(QString::fromUtf8(data.name.c_str()), bp, false));
                             RS_BlockData(toNativeString(data.name.c_str(),getDXFEncoding()), bp, false ));
            //block->setFlags(flags);

            if (graphic->addBlock(block)) {
                currentContainer = block;
            }
    }
}



/**
 * Implementation of the method which closes blocks.
 */
void RS_FilterDXFRW::endBlock() {
    if (currentContainer->rtti() == RS2::EntityBlock) {
        RS_Block *bk = (RS_Block *)currentContainer;
        if (bk->getName().startsWith("*D") )
            graphic->removeBlock(bk);
    }
    currentContainer = graphic;
}



/**
 * Implementation of the method which handles point entities.
 */
void RS_FilterDXFRW::addPoint(const DRW_Point& data) {
    RS_Vector v(data.basePoint.x, data.basePoint.y);

    RS_Point* entity = new RS_Point(currentContainer,
                                    RS_PointData(v));
    setEntityAttributes(entity, &data);

    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles line entities.
 */
void RS_FilterDXFRW::addLine(const DRW_Line& data) {
    RS_DEBUG->print("RS_FilterDXF::addLine");

    RS_Vector v1(data.basePoint.x, data.basePoint.y);
    RS_Vector v2(data.secPoint.x, data.secPoint.y);

    RS_DEBUG->print("RS_FilterDXF::addLine: create line");

    if (currentContainer==NULL) {
        RS_DEBUG->print("RS_FilterDXF::addLine: currentContainer is NULL");
    }

    RS_Line* entity = new RS_Line(currentContainer,
                                  RS_LineData(v1, v2));
    RS_DEBUG->print("RS_FilterDXF::addLine: set attributes");
    setEntityAttributes(entity, &data);

    RS_DEBUG->print("RS_FilterDXF::addLine: add entity");

    currentContainer->addEntity(entity);

    RS_DEBUG->print("RS_FilterDXF::addLine: OK");
}



/**
 * Implementation of the method which handles circle entities.
 */
void RS_FilterDXFRW::addCircle(const DRW_Circle& data) {
    RS_DEBUG->print("RS_FilterDXF::addCircle");

    RS_Vector v(data.basePoint.x, data.basePoint.y);
    RS_CircleData d(v, data.radious);
    RS_Circle* entity = new RS_Circle(currentContainer, d);
    setEntityAttributes(entity, &data);

    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles arc entities.
 *
 * @param angle1 Start angle in deg (!)
 * @param angle2 End angle in deg (!)
 */
void RS_FilterDXFRW::addArc(const DRW_Arc& data) {
    RS_DEBUG->print("RS_FilterDXF::addArc");
    RS_Vector v(data.basePoint.x, data.basePoint.y);
    RS_ArcData d(v, data.radious,
                 data.staangle/ARAD,
                 data.endangle/ARAD,
                 false);
    RS_Arc* entity = new RS_Arc(currentContainer, d);
    setEntityAttributes(entity, &data);

    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles ellipse entities.
 *
 * @param angle1 Start angle in rad (!)
 * @param angle2 End angle in rad (!)
 */
void RS_FilterDXFRW::addEllipse(const DRW_Ellipse& data) {
    RS_DEBUG->print("RS_FilterDXFRW::addEllipse");

    RS_Vector v1(data.basePoint.x, data.basePoint.y);
    RS_Vector v2(data.secPoint.x, data.secPoint.y);
    double ang2 = data.endparam;
    if ( (data.endparam- 6.28318530718) < 1.0e-10)
        ang2 = 0.0;
    RS_EllipseData ed(v1, v2, data.ratio, data.staparam,
                                    ang2, false);
    RS_Ellipse* entity = new RS_Ellipse(currentContainer, ed);
    setEntityAttributes(entity, &data);

    currentContainer->addEntity(entity);
}


/**
 * Implementation of the method which handles trace entities.
 */
void RS_FilterDXFRW::addTrace(const DRW_Trace& data) {
    RS_Solid* entity;
    RS_Vector v1(data.basePoint.x, data.basePoint.y);
    RS_Vector v2(data.secPoint.x, data.secPoint.y);
    RS_Vector v3(data.thirdPoint.x, data.thirdPoint.y);
    RS_Vector v4(data.fourPoint.x, data.fourPoint.y);
    if (v3 == v4)
        entity = new RS_Solid(currentContainer, RS_SolidData(v1, v2, v3));
    else
        entity = new RS_Solid(currentContainer, RS_SolidData(v1, v2, v3,v4));

    setEntityAttributes(entity, &data);
    currentContainer->addEntity(entity);
}

/**
 * Implementation of the method which handles solid entities.
 */
void RS_FilterDXFRW::addSolid(const DRW_Solid& data) {
    addTrace(data);
}

/**
 * Implementation of the method which handles lightweight polyline entities.
 */
void RS_FilterDXFRW::addLWPolyline(const DRW_LWPolyline& data) {
    RS_DEBUG->print("RS_FilterDXFRW::addLWPolyline");
    RS_PolylineData d(RS_Vector(false),
                      RS_Vector(false),
                      data.flags&0x1);
    RS_Polyline *polyline = new RS_Polyline(currentContainer, d);
    setEntityAttributes(polyline, &data);

    for (unsigned int i=0; i<data.vertlist.size(); i++) {
        DRW_Vertex2D *vert = data.vertlist.at(i);
        RS_Vector v(vert->x, vert->y);
        polyline->addVertex(v, vert->bulge);
    }

    currentContainer->addEntity(polyline);
}


/**
 * Implementation of the method which handles polyline entities.
 */
void RS_FilterDXFRW::addPolyline(const DRW_Polyline& data) {
    RS_DEBUG->print("RS_FilterDXFRW::addPolyline");
    if ( data.flags&0x10)
        return; //the polyline is a polygon mesh, not handled

    if ( data.flags&0x40)
        return; //the polyline is a poliface mesh, TODO convert

    RS_PolylineData d(RS_Vector(false),
                      RS_Vector(false),
                      data.flags&0x1);
    RS_Polyline *polyline = new RS_Polyline(currentContainer, d);
    setEntityAttributes(polyline, &data);
    for (unsigned int i=0; i<data.vertlist.size(); i++) {
        DRW_Vertex *vert = data.vertlist.at(i);
        RS_Vector v(vert->basePoint.x, vert->basePoint.y);
        polyline->addVertex(v, vert->bulge);
    }

    currentContainer->addEntity(polyline);
}


/**
 * Implementation of the method which handles splines.
 */
void RS_FilterDXFRW::addSpline(const DRW_Spline* data) {
    RS_DEBUG->print("RS_FilterDXFRW::addSpline: degree: %d", data->degree);

        RS_Spline* spline;
        if (data->degree>=1 && data->degree<=3) {
        RS_SplineData d(data->degree, ((data->flags&0x1)==0x1));
        spline = new RS_Spline(currentContainer, d);
        setEntityAttributes(spline, data);

        currentContainer->addEntity(spline);
    } else {
        RS_DEBUG->print(RS_Debug::D_WARNING,
                        "RS_FilterDXF::addSpline: Invalid degree for spline: %d. "
                        "Accepted values are 1..3.", data->degree);
        return;
    }
    for (unsigned int i=0; i<data->controllist.size(); i++) {
        DRW_Coord *vert = data->controllist.at(i);
        RS_Vector v(vert->x, vert->y);
        spline->addControlPoint(v);
    }
    spline->update();
}


/**
 * Implementation of the method which handles inserts.
 */
void RS_FilterDXFRW::addInsert(const DRW_Insert& data) {

    RS_DEBUG->print("RS_FilterDXF::addInsert");

    RS_Vector ip(data.basePoint.x, data.basePoint.y);
    RS_Vector sc(data.xscale, data.yscale);
    RS_Vector sp(data.colspace, data.rowspace);

    //cout << "Insert: " << name << " " << ip << " " << cols << "/" << rows << endl;

    RS_InsertData d(toNativeString(data.name.c_str(),getDXFEncoding()),
                    ip, sc, data.angle/ARAD,
                    data.colcount, data.rowcount,
                    sp, NULL, RS2::NoUpdate);
    RS_Insert* entity = new RS_Insert(currentContainer, d);
    setEntityAttributes(entity, &data);
    RS_DEBUG->print("  id: %d", entity->getId());
//    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * multi texts (MTEXT).
 */
void RS_FilterDXFRW::addMText(const DRW_MText& data) {
    RS_DEBUG->print("RS_FilterDXF::addMText: %s", data.text.c_str());

    RS_Vector ip(data.basePoint.x, data.basePoint.y);
    RS2::VAlign valign;
    RS2::HAlign halign;
    RS2::TextDrawingDirection dir;
    RS2::TextLineSpacingStyle lss;
    QString sty = toNativeString(data.style.c_str(),getDXFEncoding());
    sty=sty.toLower();

    if (data.textgen<=3) {
        valign=RS2::VAlignTop;
    } else if (data.textgen<=6) {
        valign=RS2::VAlignMiddle;
    } else {
        valign=RS2::VAlignBottom;
    }

    if (data.textgen%3==1) {
        halign=RS2::HAlignLeft;
    } else if (data.textgen%3==2) {
        halign=RS2::HAlignCenter;
    } else {
        halign=RS2::HAlignRight;
    }

    if (data.alignH==1) {
        dir = RS2::LeftToRight;
    } else if (data.alignH==3) {
        dir = RS2::TopToBottom;
    } else {
        dir = RS2::ByStyle;
    }

    if (data.alignV==1) {
        lss = RS2::AtLeast;
    } else {
        lss = RS2::Exact;
    }

    QString mtext = data.text.c_str();
    mtext = toNativeString(data.text.c_str(),getDXFEncoding());
    // use default style for the drawing:
    if (sty.isEmpty()) {
        // japanese, cyrillic:
        if (codePage=="ANSI_932" || codePage=="ANSI_1251") {
            sty = "Unicode";
        } else {
            sty = textStyle;
        }
    } else {
        // Change the QCAD "normal" style to the more correct ISO-3059
        if  (sty=="normal" || sty=="normallatin1" || sty=="normallatin2") {
            sty="iso";
        }
    }


    RS_DEBUG->print("Text as unicode:");
    RS_DEBUG->printUnicode(mtext);

    double interlin = 1.0;
    if (data.eType == DRW::MTEXT) {
        DRW_MText *ppp = (DRW_MText*)&data;
        interlin = ppp->interlin;
    }

    RS_TextData d(ip, data.height, data.widthscale,
                  valign, halign,
                  dir, lss,
                  interlin,
                  mtext, sty, data.angle*M_PI/180,
                  RS2::NoUpdate);
    RS_Text* entity = new RS_Text(currentContainer, d);

    setEntityAttributes(entity, &data);
    entity->update();
    currentContainer->addEntity(entity);

//    mtext = "";
}



/**
 * Implementation of the method which handles
 * texts (TEXT).
 */
void RS_FilterDXFRW::addText(const DRW_Text& data) {
    RS_DEBUG->print("RS_FilterDXFRW::addText");
    int attachmentPoint;
    RS_Vector refPoint;
    double angle = data.angle;
    DRW_MText text;
    // TODO: check, maybe implement a separate TEXT instead of using MTEXT

    // baseline has 5 vertical alignment modes:
    if (data.alignV !=0 || data.alignH!=0) {
        switch (data.alignH) {
        default:
        case 0: // left aligned
            attachmentPoint = 1;
            refPoint = RS_Vector(data.secPoint.x, data.secPoint.y);
            break;
        case 1: // centered
            attachmentPoint = 2;
            refPoint = RS_Vector(data.secPoint.x, data.secPoint.y);
            break;
        case 2: // right aligned
            attachmentPoint = 3;
            refPoint = RS_Vector(data.secPoint.x, data.secPoint.y);
            break;
        case 3: // aligned (TODO)
            attachmentPoint = 2;
            refPoint = RS_Vector((data.basePoint.x+data.secPoint.x)/2.0,
                                 (data.basePoint.y+data.secPoint.y)/2.0);
            angle =
                RS_Vector(data.basePoint.x, data.basePoint.y).angleTo(
                    RS_Vector(data.secPoint.x, data.secPoint.y));
            break;
        case 4: // Middle (TODO)
            attachmentPoint = 2;
            refPoint = RS_Vector(data.secPoint.x, data.secPoint.y);
            break;
        case 5: // fit (TODO)
            attachmentPoint = 2;
            refPoint = RS_Vector((data.basePoint.x+data.secPoint.x)/2.0,
                                 (data.basePoint.y+data.secPoint.y)/2.0);
            angle =
                RS_Vector(data.basePoint.x, data.basePoint.y).angleTo(
                    RS_Vector(data.secPoint.x, data.secPoint.y))*180/M_PI;
            break;
        }

        switch (data.alignV) {
        default:
        case 0: // baseline
        case 1: // bottom
            attachmentPoint += 6;
            break;

        case 2: // middle
            attachmentPoint += 3;
            break;

        case 3: // top
            break;
        }
    } else {
        //attachmentPoint = (data.hJustification+1)+(3-data.vJustification)*3;
        attachmentPoint = 7;
        refPoint = RS_Vector(data.basePoint.x, data.basePoint.y);
    }

//    int drawingDirection = 5;
//    double width = 100.0;

    text.basePoint.x = refPoint.x;
    text.basePoint.y = refPoint.y;
    text.widthscale = 100.0;
    text.height = data.height;
    text.textgen = attachmentPoint;
    text.alignH = (DRW::HAlign)5;
    text.interlin = 1.0;
    text.text = data.text;
    text.style = data.style;
    text.angle = angle;
    text.color = data.color;
    text.layer = data.layer;
    text.lineType = data.lineType;
//    text.by = attachmentPoint.y;
//    mtext = "";
/*    addMText(DL_MTextData(
                 refPoint.x,
                 refPoint.y,
#ifdef  RS_VECTOR2D
                 0.,
#else
                 refPoint.z,
#endif
                 data.height, width,
                 attachmentPoint,
                 drawingDirection,
                 RS2::Exact,
                 1.0,
                 data.text.c_str(), data.style,
                 angle), data);*/
    addMText(text);
}



/**
 * Implementation of the method which handles
 * dimensions (DIMENSION).
 */
RS_DimensionData RS_FilterDXFRW::convDimensionData(const  DRW_Dimension* data) {

    DRW_Coord crd = data->getBasePoint();
    RS_Vector defP(crd.x, crd.y);
    crd = data->getTextPoint();
    RS_Vector midP(crd.x, crd.y);
    RS2::VAlign valign;
    RS2::HAlign halign;
    RS2::TextLineSpacingStyle lss;
    QString sty = QString::fromUtf8(data->getStyle().c_str());

    QString t; //= data.text;

    // middlepoint of text can be 0/0 which is considered to be invalid (!):
    //  0/0 because older QCad versions save the middle of the text as 0/0
    //  althought they didn't suport saving of the middle of the text.
    if (fabs(crd.x)<1.0e-6 && fabs(crd.y)<1.0e-6) {
        midP = RS_Vector(false);
    }

    if (data->getAlign()<=3) {
        valign=RS2::VAlignTop;
    } else if (data->getAlign()<=6) {
        valign=RS2::VAlignMiddle;
    } else {
        valign=RS2::VAlignBottom;
    }

    if (data->getAlign()%3==1) {
        halign=RS2::HAlignLeft;
    } else if (data->getAlign()%3==2) {
        halign=RS2::HAlignCenter;
    } else {
        halign=RS2::HAlignRight;
    }

    if (data->getTextLineStyle()==1) {
        lss = RS2::AtLeast;
    } else {
        lss = RS2::Exact;
    }

    t = toNativeString(data->getText().c_str(), getDXFEncoding());

    if (sty.isEmpty()) {
        sty = dimStyle;
    }

    RS_DEBUG->print("Text as unicode:");
    RS_DEBUG->printUnicode(t);

    // data needed to add the actual dimension entity
    return RS_DimensionData(defP, midP,
                            valign, halign,
                            lss,
                            data->getTextLineFactor(),
                            t, sty, data->getDir());
}



/**
 * Implementation of the method which handles
 * aligned dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimAlign(const DRW_DimAligned *data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimAligned");

    RS_DimensionData dimensionData = convDimensionData((DRW_Dimension*)data);

    RS_Vector ext1(data->getDef1Point().x, data->getDef1Point().y);
    RS_Vector ext2(data->getDef2Point().x, data->getDef2Point().y);

    RS_DimAlignedData d(ext1, ext2);

    RS_DimAligned* entity = new RS_DimAligned(currentContainer,
                            dimensionData, d);
    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * linear dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimLinear(const DRW_DimLinear *data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimLinear");

    RS_DimensionData dimensionData = convDimensionData((DRW_Dimension*)data);

    RS_Vector dxt1(data->getDef1Point().x, data->getDef1Point().y);
    RS_Vector dxt2(data->getDef2Point().x, data->getDef2Point().y);

    RS_DimLinearData d(dxt1, dxt2, RS_Math::deg2rad(data->getAngle()),
                       RS_Math::deg2rad(data->getOblique()));

    RS_DimLinear* entity = new RS_DimLinear(currentContainer,
                                            dimensionData, d);
    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * radial dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimRadial(const DRW_DimRadial* data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimRadial");

    RS_DimensionData dimensionData = convDimensionData((DRW_Dimension*)data);
    RS_Vector dp(data->getDiameterPoint().x, data->getDiameterPoint().y);

    RS_DimRadialData d(dp, data->getLeaderLength());

    RS_DimRadial* entity = new RS_DimRadial(currentContainer,
                                            dimensionData, d);

    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * diametric dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimDiametric(const DRW_DimDiametric* data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimDiametric");

    RS_DimensionData dimensionData = convDimensionData((DRW_Dimension*)data);
    RS_Vector dp(data->getDiameter1Point().x, data->getDiameter1Point().y);

    RS_DimDiametricData d(dp, data->getLeaderLength());

    RS_DimDiametric* entity = new RS_DimDiametric(currentContainer,
                              dimensionData, d);

    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * angular dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimAngular(const DRW_DimAngular* data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimAngular");

    RS_DimensionData dimensionData = convDimensionData(data);
    RS_Vector dp1(data->getFirstLine1().x, data->getFirstLine1().y);
    RS_Vector dp2(data->getFirstLine2().x, data->getFirstLine2().y);
    RS_Vector dp3(data->getSecondLine1().x, data->getSecondLine1().y);
    RS_Vector dp4(data->getDimPoint().x, data->getDimPoint().y);

    RS_DimAngularData d(dp1, dp2, dp3, dp4);

    RS_DimAngular* entity = new RS_DimAngular(currentContainer,
                            dimensionData, d);

    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



/**
 * Implementation of the method which handles
 * angular dimensions (DIMENSION).
 */
void RS_FilterDXFRW::addDimAngular3P(const DRW_DimAngular3p* data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimAngular3P");

    RS_DimensionData dimensionData = convDimensionData(data);
    RS_Vector dp1(data->getFirstLine().x, data->getFirstLine().y);
    RS_Vector dp2(data->getSecondLine().x, data->getSecondLine().y);
    RS_Vector dp3(data->getVertex().x, data->getVertex().y);
    RS_Vector dp4 = dimensionData.definitionPoint;
    dimensionData.definitionPoint = RS_Vector(data->getVertex().x, data->getVertex().y);

    RS_DimAngularData d(dp1, dp2, dp3, dp4);

    RS_DimAngular* entity = new RS_DimAngular(currentContainer,
                            dimensionData, d);

    setEntityAttributes(entity, data);
    entity->update();
    currentContainer->addEntity(entity);
}



void RS_FilterDXFRW::addDimOrdinate(const DRW_DimOrdinate* /*data*/) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimOrdinate(const DL_DimensionData&, const DL_DimOrdinateData&) not yet implemented");
}


/**
 * Implementation of the method which handles leader entities.
 */
void RS_FilterDXFRW::addLeader(const DRW_Leader *data) {
    RS_DEBUG->print("RS_FilterDXFRW::addDimLeader");
    RS_LeaderData d(data->arrow!=0);
    RS_Leader* leader = new RS_Leader(currentContainer, d);
    setEntityAttributes(leader, data);

    currentContainer->addEntity(leader);

    for (unsigned int i=0; i<data->vertexlist.size(); i++) {
        DRW_Coord *vert = data->vertexlist.at(i);
        RS_Vector v(vert->x, vert->y);
        leader->addVertex(v);
    }
}



/**
 * Implementation of the method which handles hatch entities.
 */
void RS_FilterDXFRW::addHatch(const DRW_Hatch *data) {
    RS_DEBUG->print("RS_FilterDXF::addHatch()");
    RS_Hatch* hatch;
    RS_EntityContainer* hatchLoop;

    hatch = new RS_Hatch(currentContainer,
                         RS_HatchData(data->solid, data->scale, data->angle,
                                      QString(QString::fromUtf8(data->name.c_str()))));
    setEntityAttributes(hatch, data);
    currentContainer->addEntity(hatch);

    for (unsigned int i=0; i < data->looplist.size(); i++) {
        DRW_HatchLoop *loop = data->looplist.at(i);
        if ((loop->type & 32) == 32) continue;
        hatchLoop = new RS_EntityContainer(hatch);
        hatchLoop->setLayer(NULL);
        hatch->addEntity(hatchLoop);

        RS_Entity* e = NULL;
        if ((loop->type & 2) == 2){   //polyline, convert to lines & arcs
            DRW_LWPolyline *pline = (DRW_LWPolyline *)loop->objlist.at(0);
            RS_Polyline *polyline = new RS_Polyline(NULL,
                    RS_PolylineData(RS_Vector(false), RS_Vector(false), pline->flags) );
            for (unsigned int j=0; j < pline->vertlist.size(); j++) {
                    DRW_Vertex2D *vert = pline->vertlist.at(j);
                    polyline->addVertex(RS_Vector(vert->x, vert->y), vert->bulge);
            }
            for (RS_Entity* e=polyline->firstEntity(); e!=NULL;
                    e=polyline->nextEntity()) {
                RS_Entity* tmp = e->clone();
                tmp->reparent(hatchLoop);
                tmp->setLayer(NULL);
                hatchLoop->addEntity(tmp);
            }
            delete polyline;

        } else {
            for (unsigned int j=0; j<loop->objlist.size(); j++) {
                DRW_Entity *ent = loop->objlist.at(j);
                switch (ent->eType) {
                case DRW::LINE: {
                    DRW_Line *e2 = (DRW_Line *)ent;
                    e = new RS_Line(hatchLoop,
                                    RS_LineData(RS_Vector(e2->basePoint.x, e2->basePoint.y),
                                                RS_Vector(e2->secPoint.x, e2->secPoint.y)));
                    break;
                }
                case DRW::ARC: {
                    DRW_Arc *e2 = (DRW_Arc *)ent;
                    if (e2->isccw && e2->staangle<1.0e-6 && e2->endangle>360-1.0e-6) {
                        e = new RS_Circle(hatchLoop,
                                          RS_CircleData(RS_Vector(e2->basePoint.x, e2->basePoint.y),
                                                        e2->radious));
                    } else {

                        if (e2->isccw) {
                            e = new RS_Arc(hatchLoop,
                                        RS_ArcData(RS_Vector(e2->basePoint.x, e2->basePoint.y), e2->radious,
                                                   RS_Math::correctAngle(RS_Math::deg2rad(e2->staangle)),
                                                   RS_Math::correctAngle(RS_Math::deg2rad(e2->endangle)),
                                                   false));
                        } else {
                            e = new RS_Arc(hatchLoop,
                                        RS_ArcData(RS_Vector(e2->basePoint.x, e2->basePoint.y), e2->radious,
                                                   RS_Math::correctAngle(2*M_PI-RS_Math::deg2rad(e2->staangle)),
                                                   RS_Math::correctAngle(2*M_PI-RS_Math::deg2rad(e2->endangle)),
                                                   true));
                        }
                    }
                    break;
                }
                default:
                    break;
                }
                if (e!=NULL) {
                    e->setLayer(NULL);
                    hatchLoop->addEntity(e);
                }
            }
        }

    }

    RS_DEBUG->print("hatch->update()");
    if (hatch->validate()) {
        hatch->update();
    } else {
        graphic->removeEntity(hatch);
        RS_DEBUG->print(RS_Debug::D_ERROR,
                    "RS_FilterDXFRW::endEntity(): updating hatch failed: invalid hatch area");
    }
}


/**
 * Implementation of the method which handles image entities.
 */
void RS_FilterDXFRW::addImage(const DRW_Image *data) {
    RS_DEBUG->print("RS_FilterDXF::addImage");

    RS_Vector ip(data->basePoint.x, data->basePoint.y);
    RS_Vector uv(data->secPoint.x, data->secPoint.y);
    RS_Vector vv(data->vx, data->vy);
    RS_Vector size(data->sizeu, data->sizev);

    RS_Image* image = new RS_Image( currentContainer,
            RS_ImageData(QString(data->ref.c_str()).toInt(NULL, 16),
                         ip, uv, vv, size,
                         QString(""), data->brightness,
                         data->contrast, data->fade));

    setEntityAttributes(image, data);
    currentContainer->addEntity(image);
}



/**
 * Implementation of the method which links image entities to image files.
 */
void RS_FilterDXFRW::linkImage(const DRW_ImageDef *data) {
    RS_DEBUG->print("RS_FilterDXFRW::linkImage");

    int handle = QString(data->handle.c_str()).toInt(NULL, 16);
    QString sfile(QString::fromUtf8(data->name.c_str()));
    QFileInfo fiDxf(file);
    QFileInfo fiBitmap(sfile);

    // try to find the image file:

    // first: absolute path:
    if (!fiBitmap.exists()) {
        RS_DEBUG->print("File %s doesn't exist.",
                        (const char*)QFile::encodeName(sfile));
        // try relative path:
        QString f1 = fiDxf.absolutePath() + "/" + sfile;
        if (QFileInfo(f1).exists()) {
            sfile = f1;
        } else {
            RS_DEBUG->print("File %s doesn't exist.", (const char*)QFile::encodeName(f1));
            // try drawing path:
            QString f2 = fiDxf.absolutePath() + "/" + fiBitmap.fileName();
            if (QFileInfo(f2).exists()) {
                sfile = f2;
            } else {
                RS_DEBUG->print("File %s doesn't exist.", (const char*)QFile::encodeName(f2));
            }
        }
    }

    // Also link images in subcontainers (e.g. inserts):
    for (RS_Entity* e=graphic->firstEntity(RS2::ResolveNone);
            e!=NULL; e=graphic->nextEntity(RS2::ResolveNone)) {
        if (e->rtti()==RS2::EntityImage) {
            RS_Image* img = (RS_Image*)e;
            if (img->getHandle()==handle) {
                img->setFile(sfile);
                RS_DEBUG->print("image found: %s", (const char*)QFile::encodeName(img->getFile()));
                img->update();
            }
        }
    }

    // update images in blocks:
    for (uint i=0; i<graphic->countBlocks(); ++i) {
        RS_Block* b = graphic->blockAt(i);
        for (RS_Entity* e=b->firstEntity(RS2::ResolveNone);
                e!=NULL; e=b->nextEntity(RS2::ResolveNone)) {
            if (e->rtti()==RS2::EntityImage) {
                RS_Image* img = (RS_Image*)e;
                if (img->getHandle()==handle) {
                    img->setFile(sfile);
                    RS_DEBUG->print("image in block found: %s",
                                    (const char*)QFile::encodeName(img->getFile()));
                    img->update();
                }
            }
        }
    }
    RS_DEBUG->print("linking image: OK");
}

using std::map;
/**
 * Sets the header variables from the DXF file.
 */
void RS_FilterDXFRW::addHeader(const DRW_Header* data){
    RS_Graphic* container = NULL;
    if (currentContainer->rtti()==RS2::EntityGraphic) {
        container = (RS_Graphic*)currentContainer;
    } else return;

    map<std::string,DRW_Variant *>::const_iterator it;
    for ( it=data->vars.begin() ; it != data->vars.end(); it++ ){
        QString key = QString::fromStdString((*it).first);
        DRW_Variant *var = (*it).second;
        switch (var->type) {
        case DRW_Variant::COORD:
            container->addVariable(key,
#ifdef  RS_VECTOR2D
            RS_Vector(var->content.v->x, var->content.v->y), var->code);
#else
            RS_Vector(var->content.v->x, var->content.v->y, var->content.v->z), var->code);
#endif
            break;
        case DRW_Variant::STRING:
            container->addVariable(key, QString::fromStdString(*var->content.s), var->code);
            break;
        case DRW_Variant::INTEGER:
            container->addVariable(key, var->content.i, var->code);
            break;
        case DRW_Variant::DOUBLE:
            container->addVariable(key, var->content.d, var->code);
            break;
        default:
            break;
        }

    }
    codePage = graphic->getVariableString("$DWGCODEPAGE", "ANSI_1252");
    textStyle = graphic->getVariableString("$TEXTSTYLE", "Standard");
    dimStyle = graphic->getVariableString("$DIMSTYLE", "Standard");

    QString acadver = versionStr = graphic->getVariableString("$ACADVER", "");
    acadver.replace(QRegExp("[a-zA-Z]"), "");
    bool ok;
    version=acadver.toInt(&ok);
    if (!ok) { version = 1015;}
}


/**
 * Implementation of the method used for RS_Export to communicate
 * with this filter.
 *
 * @param file Full path to the DXF file that will be written.
 */
bool RS_FilterDXFRW::fileExport(RS_Graphic& g, const QString& file, RS2::FormatType type) {

    RS_DEBUG->print("RS_FilterDXFDW::fileExport: exporting file '%s'...",
                    (const char*)QFile::encodeName(file));
    RS_DEBUG->print("RS_FilterDXFDW::fileExport: file type '%d'", (int)type);

    this->graphic = &g;

    // check if we can write to that directory:
#ifndef Q_OS_WIN

    QString path = QFileInfo(file).absolutePath();
    if (QFileInfo(path).isWritable()==false) {
        RS_DEBUG->print("RS_FilterDXFDW::fileExport: can't write file: "
                        "no permission");
        return false;
    }
    //
#endif

    // set version for DXF filter:
    DRW::Version exportVersion;
    if (type==RS2::FormatDXF12) {
        exportVersion = DRW::AC1009;
    } else {
        exportVersion = DRW::AC1015;
    }

    //DL_WriterA* dw = dxf.out(file, VER_R12);
    dxf = new dxfRW(QFile::encodeName(file));
    bool success = dxf->write(this, exportVersion, false); //ascii
//    bool success = dxf->write(this, exportVersion, true); //binary

    if (!success) {
        RS_DEBUG->print("RS_FilterDXFDW::fileExport: can't write file");
        return false;
    }
/*RLZ pte*/
    // Header
/*    RS_DEBUG->print("writing headers...");
    dxf.writeHeader(*dw);

    // Variables
    RS_DEBUG->print("writing variables...");
    writeVariables(*dw);

    // Section TABLES
    RS_DEBUG->print("writing tables...");
    dw->sectionTables();

    // VPORT:
    dxf.writeVPort(*dw);

    // Line types:
    RS_DEBUG->print("writing line types...");
    int numLT = (int)RS2::BorderLineX2-(int)RS2::LineByBlock;
    if (type==RS2::FormatDXF12) {
        numLT-=2;
    }
    dw->tableLineTypes(numLT);
    for (int t=(int)RS2::LineByBlock; t<=(int)RS2::BorderLineX2; ++t) {
        if ((RS2::LineType)t!=RS2::NoPen) {
            writeLineType(*dw, (RS2::LineType)t);
        }
    }
    dw->tableEnd();

    // Layers:
    RS_DEBUG->print("writing layers...");
    dw->tableLayers(graphic->countLayers());
    for (uint i=0; i<graphic->countLayers(); ++i) {
        RS_Layer* l = graphic->layerAt(i);
        writeLayer(*dw, l);
    }
    dw->tableEnd();

    // STYLE:
    RS_DEBUG->print("writing styles...");
    dxf.writeStyle(*dw);

    // VIEW:
    RS_DEBUG->print("writing views...");
    dxf.writeView(*dw);

    // UCS:
    RS_DEBUG->print("writing ucs...");
    dxf.writeUcs(*dw);

    // Appid:
    RS_DEBUG->print("writing appid...");
    dw->tableAppid(1);
    writeAppid(*dw, "ACAD");
    dw->tableEnd();

    // DIMSTYLE:
    RS_DEBUG->print("writing dim styles...");
    dxf.writeDimStyle(*dw,
                      graphic->getVariableDouble("$DIMASZ", 2.5),
                      graphic->getVariableDouble("$DIMEXE", 1.25),
                      graphic->getVariableDouble("$DIMEXO", 0.625),
                      graphic->getVariableDouble("$DIMGAP", 0.625),
                      graphic->getVariableDouble("$DIMTXT", 2.5));

    // BLOCK_RECORD:
    if (type==RS2::FormatDXF) {
        RS_DEBUG->print("writing block records...");
        dxf.writeBlockRecord(*dw);

        for (uint i=0; i<graphic->countBlocks(); ++i) {
            RS_Block* blk = graphic->blockAt(i);
                        dxf.writeBlockRecord(*dw,
                            std::string(blk->getName().toLocal8Bit()));
        }
        dw->tableEnd();
    }

    // end of tables:
    RS_DEBUG->print("writing end of section TABLES...");
    dw->sectionEnd();


    // Section BLOCKS:
    RS_DEBUG->print("writing blocks...");
    dw->sectionBlocks();

    if (type==RS2::FormatDXF) {
        RS_Block b1(graphic, RS_BlockData("*Model_Space",
                                          RS_Vector(0.0,0.0), false));
        writeBlock(*dw, &b1);
        RS_Block b2(graphic, RS_BlockData("*Paper_Space",
                                          RS_Vector(0.0,0.0), false));
        writeBlock(*dw, &b2);
        RS_Block b3(graphic, RS_BlockData("*Paper_Space0",
                                          RS_Vector(0.0,0.0), false));
        writeBlock(*dw, &b3);
    }

    for (uint i=0; i<graphic->countBlocks(); ++i) {
        RS_Block* blk = graphic->blockAt(i);

        // Save block if it's not a model or paper space:
        // Careful: other blocks with * / $ exist
        //if (blk->getName().at(0)!='*' &&
        //        blk->getName().at(0)!='$') {
        writeBlock(*dw, blk);
        //}
    }
    dw->sectionEnd();


    // Section ENTITIES:
    RS_DEBUG->print("writing section ENTITIES...");
    dw->sectionEntities();
    for (RS_Entity* e=graphic->firstEntity(RS2::ResolveNone);
            e!=NULL;
            e=graphic->nextEntity(RS2::ResolveNone)) {

        writeEntity(*dw, e);
    }
    RS_DEBUG->print("writing end of section ENTITIES...");
    dw->sectionEnd();

    if (type==RS2::FormatDXF) {
        RS_DEBUG->print("writing section OBJECTS...");
        dxf.writeObjects(*dw);

        // IMAGEDEF's from images in entities and images in blocks
        QStringList written;
        for (uint i=0; i<graphic->countBlocks(); ++i) {
            RS_Block* block = graphic->blockAt(i);
            for (RS_Entity* e=block->firstEntity(RS2::ResolveAll);
                    e!=NULL;
                    e=block->nextEntity(RS2::ResolveAll)) {

                if (e->rtti()==RS2::EntityImage) {
                    RS_Image* img = ((RS_Image*)e);
                    if (written.contains(file)==0 && img->getHandle()!=0) {
                        writeImageDef(*dw, img);
                        written.append(img->getFile());
                    }
                }
            }
        }
        for (RS_Entity* e=graphic->firstEntity(RS2::ResolveNone);
                e!=NULL;
                e=graphic->nextEntity(RS2::ResolveNone)) {

            if (e->rtti()==RS2::EntityImage) {
                RS_Image* img = ((RS_Image*)e);
                if (written.contains(file)==0 && img->getHandle()!=0) {
                    writeImageDef(*dw, img);
                    written.append(img->getFile());
                }
            }
        }
        RS_DEBUG->print("writing end of section OBJECTS...");
        dxf.writeObjectsEnd(*dw);
    }

    RS_DEBUG->print("writing EOF...");
    dw->dxfEOF();


    RS_DEBUG->print("close..");
    dw->close();

    delete dw;

    // check if file was actually written (strange world of windoze xp):
    if (QFileInfo(file).exists()==false) {
        RS_DEBUG->print("RS_FilterDXF::fileExport: file could not be written");
        return false;
    }
*/
    return success;
}

/**
 * Writes all known variable settings to the DXF file.
 */
/*void RS_FilterDXFRW::writeVariables(DL_WriterA& dw) {
    QHash<QString, RS_Variable>vars = graphic->getVariableDict();
    QHash<QString, RS_Variable>::iterator it = vars.begin();
    while (it != vars.end()) {
        // exclude variables that are not known to DXF 12:
        if (!DL_Dxf::checkVariable(it.key().toLatin1(), dxf.getVersion())) {
            continue;
        }

        if (it.key()!="$ACADVER" && it.key()!="$HANDSEED") {

            dw.dxfString(9, it.key().toLatin1());
            switch (it.value().getType()) {
            case RS2::VariableVoid:
                break;
            case RS2::VariableInt:
                dw.dxfInt(it.value().getCode(), it.value().getInt());
                break;
            case RS2::VariableDouble:
                dw.dxfReal(it.value().getCode(), it.value().getDouble());
                break;
            case RS2::VariableString:
                dw.dxfString(it.value().getCode(),
                             it.value().getString().toLatin1());
                break;
            case RS2::VariableVector:
                dw.dxfReal(it.value().getCode(),
                           it.value().getVector().x);
                dw.dxfReal(it.value().getCode()+10,
                           it.value().getVector().y);
                if ( isVariableTwoDimensional(it.key()) == false) {
                    dw.dxfReal(it.value().getCode()+20,
#ifdef  RS_VECTOR2D
                               0.);
#else
                               it.value().getVector().z);
#endif
                }
                break;
            }
        }
        ++it;
    }
    dw.sectionEnd();
}*/



/**
 * Writes one layer to the DXF file.
 *
 * @todo Add support for unicode layer names
 */
/*void RS_FilterDXFRW::writeLayer(DL_WriterA& dw, RS_Layer* l) {
    if (l==NULL) {
                RS_DEBUG->print(RS_Debug::D_WARNING,
                        "RS_FilterDXF::writeLayer: layer is NULL");
        return;
    }

    RS_DEBUG->print("RS_FilterDXF::writeLayer %s", l->getName().toLatin1().data());

    dxf.writeLayer(
        dw,
        DL_LayerData(toDxfString(l->getName()).toStdString(),  //RLZ: verify layername whit locales
                     l->isFrozen() + (l->isLocked()<<2)),
        DL_Attributes(std::string(""),
                      colorToNumber(l->getPen().getColor()),
                      widthToNumber(l->getPen().getWidth()),
                      (const char*)lineTypeToName(
                          l->getPen().getLineType()).toLocal8Bit()));

    RS_DEBUG->print("RS_FilterDXF::writeLayer end");
}*/


/**
 * Writes an application id to the DXF file.
 *
 * @param appid Application ID (e.g. "LibreCAD").
 */
/*void RS_FilterDXFRW::writeAppid(DL_WriterA& dw, const char* appid) {
//    dxf.writeAppid(dw, appid);
}*/



/**
 * Writes blocks (just the definition, not the entities in it).
 */
void RS_FilterDXFRW::writeBlockRecords(){
    RS_Block *blk;
    for (uint i = 0; i < graphic->countBlocks(); i++) {
        blk = graphic->blockAt(i);
        RS_DEBUG->print("writing block record: %s", (const char*)blk->getName().toLocal8Bit());
        dxf->writeBlockRecord(blk->getName().toStdString());
    }
}

void RS_FilterDXFRW::writeBlocks() {
    RS_Block *blk;
    for (uint i = 0; i < graphic->countBlocks(); i++) {
        blk = graphic->blockAt(i);
        RS_DEBUG->print("writing block: %s", (const char*)blk->getName().toLocal8Bit());

        DRW_Block block;
        block.name = blk->getName().toStdString();
        block.basePoint.x = blk->getBasePoint().x;
        block.basePoint.y = blk->getBasePoint().y;
#ifndef  RS_VECTOR2D
        block.basePoint.z = blk->getBasePoint().z;
#endif
        dxf->writeBlock(&block);
        for (RS_Entity* e=blk->firstEntity(RS2::ResolveNone);
                e!=NULL; e=blk->nextEntity(RS2::ResolveNone)) {
            writeEntity(e);
        }
    }
//    RS_Block blk;
//    DRW_Block block;
/*    dxf.writeBlock(dw,
                   DL_BlockData((const char*)blk->getName().toLocal8Bit(), 0,
                                blk->getBasePoint().x,
                                blk->getBasePoint().y,
#ifdef  RS_VECTOR2D
                                0.));
#else
                                blk->getBasePoint().z));
#endif*/
/*    for (RS_Entity* e=blk->firstEntity(RS2::ResolveNone);
            e!=NULL;
            e=blk->nextEntity(RS2::ResolveNone)) {
        writeEntity(dw, e);
    }*/
//    dxf.writeEndBlock(dw, (const char*)blk->getName().toLocal8Bit());
}


void RS_FilterDXFRW::writeHeader(DRW_Header& data){
    DRW_Variant *curr;
/*TODO $ISOMETRICGRID and "GRID on/off" not handled because is part of
 active vport to save is required read/write VPORT table */
    QHash<QString, RS_Variable>vars = graphic->getVariableDict();
    QHash<QString, RS_Variable>::iterator it = vars.begin();
    while (it != vars.end()) {
        curr = new DRW_Variant();

            switch (it.value().getType()) {
            case RS2::VariableInt:
                curr->addInt(it.value().getInt());
                curr->code = it.value().getCode();
                break;
            case RS2::VariableDouble:
                curr->addDouble(it.value().getDouble());
                curr->code = it.value().getCode();
                break;
            case RS2::VariableString:
                curr->addString(it.value().getString().toStdString());
                curr->code = it.value().getCode();
                break;
            case RS2::VariableVector:
                curr->addCoord(new DRW_Coord());
                curr->setCoordX(it.value().getVector().x);
                curr->setCoordY(it.value().getVector().y);
#ifndef  RS_VECTOR2D
                curr->setCoordZ(it.value().getVector().z);
#endif
                curr->code = it.value().getCode();
                break;
            default:
                break;
            }
            data.vars[it.key().toStdString()] =curr;
            ++it;
    }
}

void RS_FilterDXFRW::writeLTypes(){
    DRW_LType ltype;
    // Standard linetypes for LibreCAD / AutoCAD
    ltype.name = "CONTINUOUS";
    ltype.desc = "Solid line";
    dxf->writeLineType(&ltype);
    ltype.name = "ByLayer";
    dxf->writeLineType(&ltype);
    ltype.name = "ByBlock";
    dxf->writeLineType(&ltype);

    ltype.name = "DOT";
    ltype.desc = "Dot . . . . . . . . . . . . . . . . . . . . . .";
    ltype.size = 2;
    ltype.length = 6.35;
    ltype.path.push_back(0.0);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DOT2";
    ltype.desc = "Dot (.5x) .....................................";
    ltype.size = 2;
    ltype.length = 3.175;
    ltype.path.push_back(0.0);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DOTX2";
    ltype.desc = "Dot (2x) .  .  .  .  .  .  .  .  .  .  .  .  .";
    ltype.size = 2;
    ltype.length = 12.7;
    ltype.path.push_back(0.0);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHED";
    ltype.desc = "Dot . . . . . . . . . . . . . . . . . . . . . .";
    ltype.size = 2;
    ltype.length = 19.05;
    ltype.path.push_back(12.7);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHED2";
    ltype.desc = "Dashed (.5x) _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _";
    ltype.size = 2;
    ltype.length = 9.525;
    ltype.path.push_back(6.35);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHEDX2";
    ltype.desc = "Dashed (2x) ____  ____  ____  ____  ____  ___";
    ltype.size = 2;
    ltype.length = 38.1;
    ltype.path.push_back(25.4);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHDOT";
    ltype.desc = "Dash dot __ . __ . __ . __ . __ . __ . __ . __";
    ltype.size = 4;
    ltype.length = 25.4;
    ltype.path.push_back(12.7);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHDOT2";
    ltype.desc = "Dash dot (.5x) _._._._._._._._._._._._._._._.";
    ltype.size = 4;
    ltype.length = 12.7;
    ltype.path.push_back(6.35);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DASHDOTX2";
    ltype.desc = "Dash dot (2x) ____  .  ____  .  ____  .  ___";
    ltype.size = 4;
    ltype.length = 50.8;
    ltype.path.push_back(25.4);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DIVIDE";
    ltype.desc = "Divide ____ . . ____ . . ____ . . ____ . . ____";
    ltype.size = 6;
    ltype.length = 31.75;
    ltype.path.push_back(12.7);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DIVIDE2";
    ltype.desc = "Divide (.5x) __..__..__..__..__..__..__..__.._";
    ltype.size = 6;
    ltype.length = 15.875;
    ltype.path.push_back(6.35);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "DIVIDEX2";
    ltype.desc = "Divide (2x) ________  .  .  ________  .  .  _";
    ltype.size = 6;
    ltype.length = 63.5;
    ltype.path.push_back(25.4);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "BORDER";
    ltype.desc = "Border __ __ . __ __ . __ __ . __ __ . __ __ .";
    ltype.size = 6;
    ltype.length = 44.45;
    ltype.path.push_back(12.7);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(12.7);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "BORDER2";
    ltype.desc = "Border (.5x) __.__.__.__.__.__.__.__.__.__.__.";
    ltype.size = 6;
    ltype.length = 22.225;
    ltype.path.push_back(6.35);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(6.35);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "BORDERX2";
    ltype.desc = "Border (2x) ____  ____  .  ____  ____  .  ___";
    ltype.size = 6;
    ltype.length = 88.9;
    ltype.path.push_back(25.4);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(25.4);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(0.0);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "CENTER";
    ltype.desc = "Center ____ _ ____ _ ____ _ ____ _ ____ _ ____";
    ltype.size = 4;
    ltype.length = 50.8;
    ltype.path.push_back(31.75);
    ltype.path.push_back(-6.35);
    ltype.path.push_back(6.35);
    ltype.path.push_back(-6.35);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "CENTER2";
    ltype.desc = "Center (.5x) ___ _ ___ _ ___ _ ___ _ ___ _ ___";
    ltype.size = 4;
    ltype.length = 28.575;
    ltype.path.push_back(19.05);
    ltype.path.push_back(-3.175);
    ltype.path.push_back(3.175);
    ltype.path.push_back(-3.175);
    dxf->writeLineType(&ltype);

    ltype.path.clear();
    ltype.name = "CENTERX2";
    ltype.desc = "Center (2x) ________  __  ________  __  _____";
    ltype.size = 4;
    ltype.length = 101.6;
    ltype.path.push_back(63.5);
    ltype.path.push_back(-12.7);
    ltype.path.push_back(12.7);
    ltype.path.push_back(-12.7);
    dxf->writeLineType(&ltype);
}

void RS_FilterDXFRW::writeLayers(){
    DRW_Layer lay;
    RS_LayerList* ll = graphic->getLayerList();
    for (unsigned int i = 0; i < ll->count(); i++) {
        RS_Layer* l = ll->at(i);
        RS_Pen pen = l->getPen();
        lay.name = l->getName().toStdString();
        lay.color = colorToNumber(pen.getColor());
        lay.lWeight = widthToNumber(pen.getWidth());
        lay.lineType = lineTypeToName(pen.getLineType()).toStdString();
        lay.plotF = ! l->isHelpLayer(); // a help layer should not appear in print
//        lay.lineType = lineType.toStdString(); //.toLatin1().data();
        dxf->writeLayer(&lay);
    }
}


void RS_FilterDXFRW::writeEntities(){
    for (RS_Entity *e = graphic->firstEntity(RS2::ResolveNone);
         e != NULL; e = graphic->nextEntity(RS2::ResolveNone)) {
        writeEntity(e);
    }
}

void RS_FilterDXFRW::writeEntity(RS_Entity* e){
    switch (e->rtti()) {
    case RS2::EntityPoint:
        writePoint((RS_Point*)e);
        break;
    case RS2::EntityLine:
        writeLine((RS_Line*)e);
        break;
    case RS2::EntityCircle:
        writeCircle((RS_Circle*)e);
        break;
    case RS2::EntityArc:
        writeArc((RS_Arc*)e);
        break;
    case RS2::EntitySolid:
        writeSolid((RS_Solid*)e);
        break;
    case RS2::EntityEllipse:
        writeEllipse((RS_Ellipse*)e);
        break;
    case RS2::EntityPolyline:
        writeLWPolyline((RS_Polyline*)e);
        break;
    case RS2::EntitySpline:
        writeSpline((RS_Spline*)e);
        break;
//    case RS2::EntityVertex:
//        break;
    case RS2::EntityInsert:
        writeInsert((RS_Insert*)e);
        break;
    case RS2::EntityText:
        writeText((RS_Text*)e);
        break;

    case RS2::EntityDimAligned:
            writeDimAligned( (RS_DimAligned*)e);
            break;
/*    case RS2::EntityDimAngular:
    case RS2::EntityDimLinear:
    case RS2::EntityDimRadial:
    case RS2::EntityDimDiametric:
        writeDimension(dw, (RS_Dimension*)e, attrib);
        break;*/
    case RS2::EntityDimLeader:
        writeLeader((RS_Leader*)e);
        break;
    case RS2::EntityHatch:
        writeHatch((RS_Hatch*)e);
        break;
    case RS2::EntityImage:
        writeImage((RS_Image*)e);
        break;
/*    case RS2::EntityContainer:
        writeEntityContainer(dw, (RS_EntityContainer*)e, attrib);
        break;*/
    default:
        break;
    }
}


/**
 * Writes the given Point entity to the file.
 */
void RS_FilterDXFRW::writePoint(RS_Point* p) {
    DRW_Point point;
    getEntityAttributes(&point, p);
    point.basePoint.x = p->getStartpoint().x;
    point.basePoint.y = p->getStartpoint().y;
    dxf->writePoint(&point);
}


/**
 * Writes the given Line( entity to the file.
 */
void RS_FilterDXFRW::writeLine(RS_Line* l) {
    DRW_Line line;
    getEntityAttributes(&line, l);
    line.basePoint.x = l->getStartpoint().x;
    line.basePoint.y = l->getStartpoint().y;
    line.secPoint.x = l->getEndpoint().x;
    line.secPoint.y = l->getEndpoint().y;
    dxf->writeLine(&line);
}


/**
 * Writes the given circle entity to the file.
 */
void RS_FilterDXFRW::writeCircle(RS_Circle* c) {
    DRW_Circle circle;
    getEntityAttributes(&circle, c);
    circle.basePoint.x = c->getCenter().x;
    circle.basePoint.y = c->getCenter().y;
    circle.radious = c->getRadius();
    dxf->writeCircle(&circle);
}


/**
 * Writes the given arc entity to the file.
 */
void RS_FilterDXFRW::writeArc(RS_Arc* a) {
    DRW_Arc arc;
    getEntityAttributes(&arc, a);
    arc.basePoint.x = a->getCenter().x;
    arc.basePoint.y = a->getCenter().y;
    arc.radious = a->getRadius();
    if (a->isReversed()) {
        arc.staangle = a->getAngle2()*ARAD;
        arc.endangle = a->getAngle1()*ARAD;
    } else {
        arc.staangle = a->getAngle1()*ARAD;
        arc.endangle = a->getAngle2()*ARAD;
    }
    dxf->writeArc(&arc);
}


/**
 * Writes the given polyline entity to the file as lwpolyline.
 */
void RS_FilterDXFRW::writeLWPolyline(RS_Polyline* l) {
    DRW_LWPolyline pol;
    RS_Entity* currEntity = 0;
    RS_Entity* nextEntity = 0;
    RS_AtomicEntity* ae = NULL;
    double bulge=0.0;

    for (RS_Entity* e=l->firstEntity(RS2::ResolveNone);
         e!=NULL; e=nextEntity) {

        currEntity = e;
        nextEntity = l->nextEntity(RS2::ResolveNone);

        if (!e->isAtomic()) {
            continue;
        }
        ae = (RS_AtomicEntity*)e;

        // Write vertex:
            if (e->rtti()==RS2::EntityArc) {
                bulge = ((RS_Arc*)e)->getBulge();
            } else
                bulge = 0.0;
            pol.addVertex( DRW_Vertex2D(ae->getStartpoint().x,
                                      ae->getStartpoint().y, bulge));
    }
    if (l->isClosed()) {
        pol.flags = 1;
    } else {
        ae = (RS_AtomicEntity*)currEntity;
        if (ae->rtti()==RS2::EntityArc) {
            bulge = ((RS_Arc*)ae)->getBulge();
        }
        pol.addVertex( DRW_Vertex2D(ae->getEndpoint().x,
                                  ae->getEndpoint().y, bulge));
    }
    pol.vertexnum = pol.vertlist.size();
    getEntityAttributes(&pol, l);
    dxf->writeLWPolyline(&pol);

}

/**
 * Writes the given polyline entity to the file.
 */
void RS_FilterDXFRW::writePolyline(DL_WriterA& /*dw*/,
                                 RS_Polyline* /*l*/,
                                 const DRW_Entity& /*attrib*/) {

/*        int count = l->count();
        if (l->isClosed()==false) {
                count++;
        }

        dxf.writePolyline(
        dw,
        DL_PolylineData(count,
                        0, 0,
                        l->isClosed()*0x1),
        attrib);
    bool first = true;
    RS_Entity* nextEntity = 0;
    RS_AtomicEntity* ae = NULL;
        RS_Entity* lastEntity = l->lastEntity(RS2::ResolveNone);
    for (RS_Entity* v=l->firstEntity(RS2::ResolveNone);
            v!=NULL;
            v=nextEntity) {

        nextEntity = l->nextEntity(RS2::ResolveNone);

        if (!v->isAtomic()) {
            continue;
        }

        ae = (RS_AtomicEntity*)v;
        double bulge=0.0;

                // Write vertex:
        if (first) {
            if (v->rtti()==RS2::EntityArc) {
                bulge = ((RS_Arc*)v)->getBulge();
            }
            dxf.writeVertex(dw,
                            DL_VertexData(ae->getStartpoint().x,
                                          ae->getStartpoint().y,
                                          0.0,
                                                                                  bulge));
            first = false;
        }

        //if (dxf.getVersion()==VER_R12) {
            if (nextEntity!=NULL) {
                if (nextEntity->rtti()==RS2::EntityArc) {
                    bulge = ((RS_Arc*)nextEntity)->getBulge();
                }
                                else {
                                        bulge = 0.0;
                                }
            }


                if (l->isClosed()==false || v!=lastEntity) {
                dxf.writeVertex(dw,
                        DL_VertexData(ae->getEndpoint().x,
                                      ae->getEndpoint().y,
                                      0.0,
                                      bulge));
                }
    }
    dxf.writePolylineEnd(dw);*/
}



/**
 * Writes the given spline entity to the file.
 */
void RS_FilterDXFRW::writeSpline(RS_Spline *s) {

    // split spline into atomic entities for DXF R12:
/*RLZ: TODO    if (dxf.getVersion()==VER_R12) {
        writeAtomicEntities(dw, s, attrib, RS2::ResolveNone);
        return;
    }
*/
    if (s->getNumberOfControlPoints() < s->getDegree()+1) {
        RS_DEBUG->print(RS_Debug::D_ERROR, "RS_FilterDXF::writeSpline: "
                        "Discarding spline: not enough control points given.");
        return;
    }
    DRW_Spline sp;

    if (s->isClosed())
        sp.flags = 11;
    else
        sp.flags = 8;
    sp.ncontrol = s->getNumberOfControlPoints();
    sp.degree = s->getDegree();
    sp.nknots = sp.ncontrol + sp.degree + 1;

    // write spline knots:
    int k = sp.degree+1;
    for (int i=1; i<=sp.nknots; i++) {
        if (i<=k) {
            sp.knotslist.push_back(0.0);
        } else if (i<=sp.nknots-k) {
            sp.knotslist.push_back(1.0/(sp.nknots-2*k+1) * (i-k));
        } else {
            sp.knotslist.push_back(1.0);
        }
    }

    // write spline control points:
    QList<RS_Vector> cp = s->getControlPoints();
    for (int i = 0; i < cp.size(); ++i) {
        DRW_Coord *controlpoint = new DRW_Coord();
        sp.controllist.push_back(controlpoint);
        controlpoint->x = cp.at(i).x;
        controlpoint->y = cp.at(i).y;
     }
    getEntityAttributes(&sp, s);
    dxf->writeSpline(&sp);

}


/**
 * Writes the given Ellipse entity to the file.
 */
void RS_FilterDXFRW::writeEllipse(RS_Ellipse* s) {
    DRW_Ellipse el;
    getEntityAttributes(&el, s);
    el.basePoint.x = s->getCenter().x;
    el.basePoint.y = s->getCenter().y;
    el.secPoint.x = s->getMajorP().x;
    el.secPoint.y = s->getMajorP().y;
    el.ratio = s->getRatio();
    if (s->isReversed()) {
        el.staparam = s->getAngle2();
        el.endparam = s->getAngle1();
    } else {
        el.staparam = s->getAngle1();
        el.endparam = s->getAngle2();
    }
    dxf->writeEllipse(&el);
}

void RS_FilterDXFRW::writeInsert(RS_Insert* i) {
    DRW_Insert in;
    getEntityAttributes(&in, i);
    in.basePoint.x = i->getInsertionPoint().x;
    in.basePoint.y = i->getInsertionPoint().y;
#ifndef  RS_VECTOR2D
    in.basePoint.z = i->getInsertionPoint().z;
#endif
    in.name = i->getName().toStdString();
    in.xscale = i->getScale().x;
    in.yscale = i->getScale().y;
#ifndef  RS_VECTOR2D
    in.zscale = i->getScale().z;
#endif
    in.angle = i->getAngle();
    in.colcount = i->getCols();
    in.rowcount = i->getRows();
    in.colspace = i->getSpacing().x;
    in.rowspace =i->getSpacing().y;
    dxf->writeInsert(&in);
}


void RS_FilterDXFRW::writeText(RS_Text* t) {
    DRW_Text *text;
    DRW_Text txt1;
    DRW_MText txt2;

    if (version==1009)
        text = &txt1;
    else
        text = &txt2;

    getEntityAttributes(text, t);
    text->basePoint.x = t->getInsertionPoint().x;
    text->basePoint.y = t->getInsertionPoint().y;
    text->height = t->getHeight();
    text->angle = t->getAngle();
    text->style = t->getStyle().toStdString();

    if (t->getHAlign()==RS2::HAlignLeft) {
        text->alignH =DRW::HAlignLeft;
    } else if (t->getHAlign()==RS2::HAlignCenter) {
        text->alignH =DRW::HAlignCenter;
    } else if (t->getHAlign()==RS2::HAlignRight) {
        text->alignH = DRW::HRight;
    }
    if (t->getVAlign()==RS2::VAlignTop) {
        text->alignV = DRW::VAlignTop;
    } else if (t->getVAlign()==RS2::VAlignMiddle) {
        text->alignV = DRW::VAlignMiddle;
    } else if (t->getVAlign()==RS2::VAlignBottom) {
        text->alignV = DRW::VAlignBottom;
    }

    if (version==1009) {
        QStringList txtList = t->getText().split('\n',QString::KeepEmptyParts);
        double dist = t->getSize().y / txtList.size();
        bool setSec = false;
        if (text->alignH != DRW::HAlignLeft || text->alignV != DRW::VAlignBaseLine) {
            text->secPoint.x = t->getInsertionPoint().x;
            text->secPoint.y = t->getInsertionPoint().y;
            setSec = true;
        }
        if (text->alignV == DRW::VAlignTop)
            dist = dist * -1;
        for (int i=0; i<txtList.size();++i){
            if (!txtList.at(i).isEmpty()) {
                text->text = toDxfString(txtList.at(i)).toStdString();
                if (setSec)
                    text->secPoint.y += dist*i;
                else
                    text->basePoint.y += dist*i;
                dxf->writeText(text);
        }
        }
    } else {
        text->text = toDxfString(t->getText()).toStdString();
//        text->widthscale =t->getWidth();
        text->widthscale =t->getSize().x;

        dxf->writeMText((DRW_MText*)text);
    }
}


void RS_FilterDXFRW::writeDimAligned(RS_DimAligned* d) {
/*    DRW_DimAligned dim;
    getEntityAttributes(&dim, d);
    dim.basePoint.x = d->getExtensionPoint1().x;
    dim.basePoint.y = d->getExtensionPoint1().y;
    dim.secPoint.x = d->getExtensionPoint1().x;
    dim.secPoint.y = d->getExtensionPoint1().y;
    dxf->writeDimAligned(&dim);*/
}
void RS_FilterDXFRW::writeDimension(DL_WriterA& /*dw*/, RS_Dimension* /*d*/,
                                  const DRW_Entity& /*attrib*/) {

    // split hatch into atomic entities:
/*    if (dxf.getVersion()==VER_R12) {
        writeAtomicEntities(dw, d, attrib, RS2::ResolveNone);
        return;
    }

    int type;
    int attachmentPoint=1;
    if (d->getHAlign()==RS2::HAlignLeft) {
        attachmentPoint=1;
    } else if (d->getHAlign()==RS2::HAlignCenter) {
        attachmentPoint=2;
    } else if (d->getHAlign()==RS2::HAlignRight) {
        attachmentPoint=3;
    }
    if (d->getVAlign()==RS2::VAlignTop) {
        attachmentPoint+=0;
    } else if (d->getVAlign()==RS2::VAlignMiddle) {
        attachmentPoint+=3;
    } else if (d->getVAlign()==RS2::VAlignBottom) {
        attachmentPoint+=6;
    }

    switch (d->rtti()) {
    case RS2::EntityDimAligned:
        type = 1;
        break;
    case RS2::EntityDimLinear:
        type = 0;
        break;
    case RS2::EntityDimRadial:
        type = 4;
        break;
    case RS2::EntityDimDiametric:
        type = 3;
        break;
    default:
        type = 0;
        break;
    }

    DL_DimensionData dimData(d->getDefinitionPoint().x,
                             d->getDefinitionPoint().y,
                             0.0,
                             d->getMiddleOfText().x,
                             d->getMiddleOfText().y,
                             0.0,
                             type,
                             attachmentPoint,
                             d->getLineSpacingStyle(),
                             d->getLineSpacingFactor(),
                             (const char*)toDxfString(
                                 d->getText()).toLocal8Bit(),
                             (const char*)d->getStyle().toLocal8Bit(),
                             d->getAngle());

    if (d->rtti()==RS2::EntityDimAligned) {
        RS_DimAligned* da = (RS_DimAligned*)d;

        DL_DimAlignedData dimAlignedData(da->getExtensionPoint1().x,
                                         da->getExtensionPoint1().y,
                                         0.0,
                                         da->getExtensionPoint2().x,
                                         da->getExtensionPoint2().y,
                                         0.0);

        dxf.writeDimAligned(dw, dimData, dimAlignedData, attrib);
    } else if (d->rtti()==RS2::EntityDimLinear) {
        RS_DimLinear* dl = (RS_DimLinear*)d;

        DL_DimLinearData dimLinearData(dl->getExtensionPoint1().x,
                                       dl->getExtensionPoint1().y,
                                       0.0,
                                       dl->getExtensionPoint2().x,
                                       dl->getExtensionPoint2().y,
                                       0.0,
                                       dl->getAngle(),
                                       dl->getOblique());

        dxf.writeDimLinear(dw, dimData, dimLinearData, attrib);
    } else if (d->rtti()==RS2::EntityDimRadial) {
        RS_DimRadial* dr = (RS_DimRadial*)d;

        DL_DimRadialData dimRadialData(dr->getDefinitionPoint().x,
                                       dr->getDefinitionPoint().y,
                                       0.0,
                                       dr->getLeader());

        dxf.writeDimRadial(dw, dimData, dimRadialData, attrib);
    } else if (d->rtti()==RS2::EntityDimDiametric) {
        RS_DimDiametric* dr = (RS_DimDiametric*)d;

        DL_DimDiametricData dimDiametricData(dr->getDefinitionPoint().x,
                                             dr->getDefinitionPoint().y,
                                             0.0,
                                             dr->getLeader());

        dxf.writeDimDiametric(dw, dimData, dimDiametricData, attrib);
    } else if (d->rtti()==RS2::EntityDimAngular) {
        RS_DimAngular* da = (RS_DimAngular*)d;

        DL_DimAngularData dimAngularData(da->getDefinitionPoint1().x,
                                         da->getDefinitionPoint1().y,
                                         0.0,
                                         da->getDefinitionPoint2().x,
                                         da->getDefinitionPoint2().y,
                                         0.0,
                                         da->getDefinitionPoint3().x,
                                         da->getDefinitionPoint3().y,
                                         0.0,
                                         da->getDefinitionPoint4().x,
                                         da->getDefinitionPoint4().y,
                                         0.0);

        dxf.writeDimAngular(dw, dimData, dimAngularData, attrib);
    }*/

}


void RS_FilterDXFRW::writeLeader(RS_Leader* l) {
    if (l->count()<=0)
        RS_DEBUG->print(RS_Debug::D_WARNING, "dropping leader with no vertices");

    DRW_Leader leader;
    getEntityAttributes(&leader, l);
    leader.style = "Standard";
    leader.arrow = l->hasArrowHead();
    leader.leadertype = 0;
    leader.flag = 3;
    leader.hookline = 0;
    leader.hookflag = 0;
    leader.textheight = 1;
    leader.textwidth = 10;
    leader.vertnum = l->count();
    RS_Line* li =NULL;
    for (RS_Entity* v=l->firstEntity(RS2::ResolveNone);
            v!=NULL;   v=l->nextEntity(RS2::ResolveNone)) {
        if (v->rtti()==RS2::EntityLine) {
            li = (RS_Line*)v;
            leader.vertexlist.push_back(new DRW_Coord(li->getStartpoint().x, li->getStartpoint().y, 0.0));
        }
    }
    if (li != NULL) {
        leader.vertexlist.push_back(new DRW_Coord(li->getEndpoint().x, li->getEndpoint().y, 0.0));
    }
    dxf->writeLeader(&leader);
}


void RS_FilterDXFRW::writeHatch(RS_Hatch * h) {
//In ver_r12 add as unnamed block (*u???)
    // split hatch into atomic entities:
/*    if (dxf.getVersion()==VER_R12) {
        writeAtomicEntities(dw, h, attrib, RS2::ResolveAll);
        return;
    }*/

    bool writeIt = true;
    if (h->countLoops()>0) {
        // check if all of the loops contain entities:
        for (RS_Entity* l=h->firstEntity(RS2::ResolveNone);
                l!=NULL;
                l=h->nextEntity(RS2::ResolveNone)) {

            if (l->isContainer() && !l->getFlag(RS2::FlagTemp)) {
                if (l->count()==0) {
                    writeIt = false;
                }
            }
        }
    } else {
        writeIt = false;
    }

    if (!writeIt) {
        RS_DEBUG->print(RS_Debug::D_WARNING,
                        "RS_FilterDXF::writeHatch: Dropping Hatch");
        return;
    }

    DRW_Hatch ha;
    getEntityAttributes(&ha, h);
    ha.solid = h->isSolid();
    ha.scale = h->getScale();
    ha.angle = h->getAngle();
    ha.name = h->getPattern().toStdString();
    ha.loopsnum = h->countLoops();

    for (RS_Entity* l=h->firstEntity(RS2::ResolveNone);
         l!=NULL;
         l=h->nextEntity(RS2::ResolveNone)) {

        // Write hatch loops:
        if (l->isContainer() && !l->getFlag(RS2::FlagTemp)) {
            RS_EntityContainer* loop = (RS_EntityContainer*)l;
            DRW_HatchLoop *lData = new DRW_HatchLoop(0);

            for (RS_Entity* ed=loop->firstEntity(RS2::ResolveNone);
                 ed!=NULL;
                 ed=loop->nextEntity(RS2::ResolveNone)) {

                // Write hatch loop edges:
                if (ed->rtti()==RS2::EntityLine) {
                    RS_Line* ln = (RS_Line*)ed;
                    DRW_Line *line = new DRW_Line();
                    line->basePoint.x = ln->getStartpoint().x;
                    line->basePoint.y = ln->getStartpoint().y;
                    line->secPoint.x = ln->getEndpoint().x;
                    line->secPoint.y = ln->getEndpoint().y;
                    lData->objlist.push_back(line);
                } else if (ed->rtti()==RS2::EntityArc) {
                    RS_Arc* ar = (RS_Arc*)ed;
                    DRW_Arc *arc = new DRW_Arc();
                    arc->basePoint.x = ar->getStartpoint().x;
                    arc->basePoint.y = ar->getStartpoint().y;
                    arc->radious = ar->getRadius();
                    if (!ar->isReversed()) {
                        arc->staangle = ar->getAngle1();
                        arc->endangle = ar->getAngle2();
                        arc->isccw = true;
                    } else {
                        arc->staangle = 2*M_PI-ar->getAngle1();
                        arc->endangle = 2*M_PI-ar->getAngle2();
                        arc->isccw = false;
                    }
                    lData->objlist.push_back(arc);
                } else if (ed->rtti()==RS2::EntityCircle) {
                    RS_Circle* ci = (RS_Circle*)ed;
                    DRW_Arc *arc= new DRW_Arc();
                    arc->basePoint.x = ci->getStartpoint().x;
                    arc->basePoint.y = ci->getStartpoint().y;
                    arc->radious = ci->getRadius();
                    arc->staangle = 0.0;
                    arc->endangle = 2*M_PI;
                    arc->isccw = true;
                    lData->objlist.push_back(arc);
                }
            }
            lData->update(); //change to DRW_HatchLoop
            ha.appendLoop(lData);
        }
    }
    dxf->writeHatch(&ha);
}


/**
 * Writes the given Solid( entity to the file.
 */
void RS_FilterDXFRW::writeSolid(RS_Solid* s) {
    RS_SolidData data;
    DRW_Solid solid;
    RS_Vector corner;
    getEntityAttributes(&solid, s);
    corner = s->getCorner(0);
    solid.basePoint.x = corner.x;
    solid.basePoint.y = corner.y;
    corner = s->getCorner(1);
    solid.secPoint.x = corner.x;
    solid.secPoint.y = corner.y;
    corner = s->getCorner(2);
    solid.thirdPoint.x = corner.x;
    solid.thirdPoint.y = corner.y;
    if (s->isTriangle()) {
        solid.fourPoint.x = solid.thirdPoint.x;
        solid.fourPoint.y = solid.thirdPoint.y;
    } else {
        corner = s->getCorner(3);
        solid.fourPoint.x = corner.x;
        solid.fourPoint.y = corner.y;
    }
    dxf->writeSolid(&solid);
}


void RS_FilterDXFRW::writeImage(RS_Image * i) {
    DRW_Image image;
    getEntityAttributes(&image, i);

    image.basePoint.x = i->getInsertionPoint().x;
    image.basePoint.y = i->getInsertionPoint().y;
    image.secPoint.x = i->getUVector().x;
    image.secPoint.y = i->getUVector().y;
    image.vx = i->getVVector().x;
    image.vy = i->getVVector().y;
    image.sizeu = i->getWidth();
    image.sizev = i->getHeight();
    image.brightness = i->getBrightness();
    image.contrast = i->getContrast();
    image.fade = i->getFade();

    DRW_ImageDef *imgDef = dxf->writeImage(&image, i->getFile().toStdString());
    if (imgDef != NULL) {
        imgDef->loaded = 1;
        imgDef->u = i->getData().size.x;
        imgDef->v = i->getData().size.y;
        imgDef->up = 1;
        imgDef->vp = 1;
        imgDef->resolution = 0;
    }
}



void RS_FilterDXFRW::writeEntityContainer(DL_WriterA& /*dw*/, RS_EntityContainer* /*con*/,
                                        const DRW_Entity& /*attrib*/) {
/*    QString blkName;
    blkName = "__CE";

    // Creating an unique ID from the element ID
    int tmp, c=1; // tmp = temporary var c = counter var
    tmp = con->getId();

    while (true) {
        tmp = tmp/c;
        blkName.append((char) tmp %10 + 48);
        c *= 10;
        if (tmp < 10) {
            break;
        }
    }

    //Block definition
    dw.sectionTables();
    dxf.writeBlockRecord(dw);
    dw.dxfString(  0, "BLOCK_RECORD");

    dw.handle();
    dw.dxfHex(330, 1);
    dw.dxfString(100, "AcDbSymbolTableRecord");
    dw.dxfString(100, "AcDbBlockTableRecord");
    dw.dxfString(  2, blkName.toLatin1().data());
    dw.dxfHex(340, 0);
    dw.dxfString(0, "ENDTAB");

    //Block creation
    RS_BlockData blkdata(blkName, RS_Vector(0,0), false);

    RS_Block* blk = new RS_Block(graphic, blkdata);

    for (RS_Entity* e1 = con->firstEntity(); e1 != NULL;
            e1 = con->nextEntity() ) {
        blk->addEntity(e1);
    }
    writeBlock(dw, blk);
    //delete e1;
*/
}



/**
 * Writes the atomic entities of the given cotnainer to the file.
 */
void RS_FilterDXFRW::writeAtomicEntities(DL_WriterA& /*dw*/, RS_EntityContainer* c,
                                       const DRW_Entity& /*attrib*/,
                                       RS2::ResolveLevel level) {

    for (RS_Entity* e=c->firstEntity(level);
            e!=NULL;
            e=c->nextEntity(level)) {

//RLZ        writeEntity(dw, e, attrib);
    }
}


/**
 * Sets the entities attributes according to the attributes
 * that come from a DXF file.
 */
void RS_FilterDXFRW::setEntityAttributes(RS_Entity* entity,
                                       const DRW_Entity* attrib) {
    RS_DEBUG->print("RS_FilterDXF::setEntityAttributes");

    RS_Pen pen;
    pen.setColor(Qt::black);
    pen.setLineType(RS2::SolidLine);
    QString layName = toNativeString(attrib->layer.c_str(),getDXFEncoding());

    // Layer: add layer in case it doesn't exist:
    if (graphic->findLayer(layName)==NULL) {
        DRW_Layer lay;
        lay.name = attrib->layer;
        addLayer(lay);
    }
    entity->setLayer(layName);

    // Color:
    pen.setColor(numberToColor(attrib->color));

    // Linetype:
    pen.setLineType(nameToLineType(attrib->lineType.c_str()));

    // Width:
    pen.setWidth(numberToWidth(attrib->lWeight));

    entity->setPen(pen);
    RS_DEBUG->print("RS_FilterDXF::setEntityAttributes: OK");
}



/**
 * Gets the entities attributes as a DL_Attributes object.
 */
void RS_FilterDXFRW::getEntityAttributes(DRW_Entity* ent, const RS_Entity* entity) {
//DRW_Entity RS_FilterDXFRW::getEntityAttributes(RS_Entity* /*entity*/) {

    // Layer:
    RS_Layer* layer = entity->getLayer();
    QString layerName;
    if (layer!=NULL) {
        layerName = layer->getName();
    } else {
        layerName = "0";
    }

    RS_Pen pen = entity->getPen(false);

    // Color:
    int color = colorToNumber(pen.getColor());
    //printf("Color is: %s -> %d\n", pen.getColor().name().toLatin1().data(), color);

    // Linetype:
    QString lineType = lineTypeToName(pen.getLineType());

    // Width:
    int width = widthToNumber(pen.getWidth());

    ent->layer = layerName.toStdString();
    ent->color = color;
    ent->lWeight = width;
    ent->lineType = lineType.toLatin1().data();
}



/**
 * @return Pen with the same attributes as 'attrib'.
 */
RS_Pen RS_FilterDXFRW::attributesToPen(const DRW_Layer* att) const {

    RS_Pen pen(numberToColor(att->color),
               numberToWidth(att->lWeight),
               nameToLineType(att->lineType.c_str()) );
    return pen;
}



/**
 * Converts a color index (num) into a RS_Color object.
 * Please refer to the dxflib documentation for details.
 *
 * @param num Color number.
 * @param comp Compatibility with older QCad versions (1.5.3 and older)
 */
RS_Color RS_FilterDXFRW::numberToColor(int num, bool comp) {
    // Compatibility with QCad 1.5.3 and older:
    if (comp) {
        switch(num) {
        case 0:
            return Qt::black;
            break;
        case 1:
            return Qt::darkBlue;
            break;
        case 2:
            return Qt::darkGreen;
            break;
        case 3:
            return Qt::darkCyan;
            break;
        case 4:
            return Qt::darkRed;
            break;
        case 5:
            return Qt::darkMagenta;
            break;
        case 6:
            return Qt::darkYellow;
            break;
        case 7:
            return Qt::lightGray;
            break;
        case 8:
            return Qt::darkGray;
            break;
        case 9:
            return Qt::blue;
            break;
        case 10:
            return Qt::green;
            break;
        case 11:
            return Qt::cyan;
            break;
        case 12:
            return Qt::red;
            break;
        case 13:
            return Qt::magenta;
            break;
        case 14:
            return Qt::yellow;
            break;
        case 15:
            return Qt::black;
            break;
        default:
            break;
        }
    } else {
        if (num==0) {
            return RS_Color(RS2::FlagByBlock);
        } else if (num==256) {
            return RS_Color(RS2::FlagByLayer);
        } else if (num<=255 && num>=0) {
            return RS_Color((int)(DRW::dxfColors[num][0]*255),
                            (int)(DRW::dxfColors[num][1]*255),
                            (int)(DRW::dxfColors[num][2]*255));
        } else {
            RS_DEBUG->print(RS_Debug::D_WARNING,
                                "RS_FilterDXF::numberToColor: Invalid color number given.");
            return RS_Color(RS2::FlagByLayer);
        }
    }
    return RS_Color();
}



/**
 * Converts a color into a color number in the DXF palette.
 * The color that fits best is chosen.
 */
int RS_FilterDXFRW::colorToNumber(const RS_Color& col) {

    //printf("Searching color for %s\n", col.name().toLatin1().data());

    // Special color BYBLOCK:
    if (col.getFlag(RS2::FlagByBlock)) {
        return 0;
    }

    // Special color BYLAYER
    else if (col.getFlag(RS2::FlagByLayer)) {
        return 256;
    }

    // Special color black is not in the table but white represents both
    // black and white
    else if (col.red()==0 && col.green()==0 && col.blue()==0) {
        return 7;
    }

    // All other colors
    else {
        int num=0;
        int diff=255*3;  // smallest difference to a color in the table found so far

        // Run through the whole table and compare
        for (int i=1; i<=255; i++) {
            int d = abs(col.red()-(int)(DRW::dxfColors[i][0]*255))
                    + abs(col.green()-(int)(DRW::dxfColors[i][1]*255))
                    + abs(col.blue()-(int)(DRW::dxfColors[i][2]*255));

            if (d<diff) {
                /*
                printf("color %f,%f,%f is closer\n",
                       dxfColors[i][0],
                       dxfColors[i][1],
                       dxfColors[i][2]);
                */
                diff = d;
                num = i;
                if (d==0) {
                    break;
                }
            }
        }
        //printf("  Found: %d, diff: %d\n", num, diff);
        return num;
    }
}

void RS_FilterDXFRW::add3dFace(const DRW_3Dface& data) {
    RS_DEBUG->print("RS_FilterDXFRW::add3dFace");
    RS_PolylineData d(RS_Vector(false),
                      RS_Vector(false),
                      !data.invisibleflag);
    RS_Polyline *polyline = new RS_Polyline(currentContainer, d);
    setEntityAttributes(polyline, &data);
    RS_Vector v1(data.basePoint.x, data.basePoint.y);
    RS_Vector v2(data.secPoint.x, data.secPoint.y);
    RS_Vector v3(data.thirdPoint.x, data.thirdPoint.y);
    RS_Vector v4(data.fourPoint.x, data.fourPoint.y);

    polyline->addVertex(v1, 0.0);
    polyline->addVertex(v2, 0.0);
    polyline->addVertex(v3, 0.0);
    polyline->addVertex(v4, 0.0);

    currentContainer->addEntity(polyline);
}

void RS_FilterDXFRW::addComment(const char*) {
    RS_DEBUG->print("RS_FilterDXF::addComment(const char*) not yet implemented.");
}


/**
 * Converts a line type name (e.g. "CONTINUOUS") into a RS2::LineType
 * object.
 */
RS2::LineType RS_FilterDXFRW::nameToLineType(const QString& name) {

    QString uName = name.toUpper();

    // Standard linetypes for QCad II / AutoCAD
    if (uName.isEmpty() || uName=="BYLAYER") {
        return RS2::LineByLayer;

    } else if (uName=="BYBLOCK") {
        return RS2::LineByBlock;

    } else if (uName=="CONTINUOUS" || uName=="ACAD_ISO01W100") {
        return RS2::SolidLine;

    } else if (uName=="ACAD_ISO07W100" || uName=="DOT") {
        return RS2::DotLine;

    } else if (uName=="DOT2") {
        return RS2::DotLine2;

    } else if (uName=="DOTX2") {
        return RS2::DotLineX2;


    } else if (uName=="ACAD_ISO02W100" || uName=="ACAD_ISO03W100" ||
               uName=="DASHED" || uName=="HIDDEN") {
        return RS2::DashLine;

    } else if (uName=="DASHED2" || uName=="HIDDEN2") {
        return RS2::DashLine2;

    } else if (uName=="DASHEDX2" || uName=="HIDDENX2") {
        return RS2::DashLineX2;


    } else if (uName=="ACAD_ISO10W100" ||
               uName=="DASHDOT") {
        return RS2::DashDotLine;

    } else if (uName=="DASHDOT2") {
        return RS2::DashDotLine2;

    } else if (uName=="ACAD_ISO04W100" ||
               uName=="DASHDOTX2") {
        return RS2::DashDotLineX2;


    } else if (uName=="ACAD_ISO12W100" || uName=="DIVIDE") {
        return RS2::DivideLine;

    } else if (uName=="DIVIDE2") {
        return RS2::DivideLine2;

    } else if (uName=="ACAD_ISO05W100" || uName=="DIVIDEX2") {
        return RS2::DivideLineX2;


    } else if (uName=="CENTER") {
        return RS2::CenterLine;

    } else if (uName=="CENTER2") {
        return RS2::CenterLine2;

    } else if (uName=="CENTERX2") {
        return RS2::CenterLineX2;


    } else if (uName=="BORDER") {
        return RS2::BorderLine;

    } else if (uName=="BORDER2") {
        return RS2::BorderLine2;

    } else if (uName=="BORDERX2") {
        return RS2::BorderLineX2;
    }

    return RS2::SolidLine;
}



/**
 * Converts a RS_LineType into a name for a line type.
 */
QString RS_FilterDXFRW::lineTypeToName(RS2::LineType lineType) {

    // Standard linetypes for QCad II / AutoCAD
    switch (lineType) {

    case RS2::SolidLine:
        return "CONTINUOUS";
        break;

    case RS2::DotLine:
        return "DOT";
        break;
    case RS2::DotLine2:
        return "DOT2";
        break;
    case RS2::DotLineX2:
        return "DOTX2";
        break;

    case RS2::DashLine:
        return "DASHED";
        break;
    case RS2::DashLine2:
        return "DASHED2";
        break;
    case RS2::DashLineX2:
        return "DASHEDX2";
        break;

    case RS2::DashDotLine:
        return "DASHDOT";
        break;
    case RS2::DashDotLine2:
        return "DASHDOT2";
        break;
    case RS2::DashDotLineX2:
        return "DASHDOTX2";
        break;

    case RS2::DivideLine:
        return "DIVIDE";
        break;
    case RS2::DivideLine2:
        return "DIVIDE2";
        break;
    case RS2::DivideLineX2:
        return "DIVIDEX2";
        break;

    case RS2::CenterLine:
        return "CENTER";
        break;
    case RS2::CenterLine2:
        return "CENTER2";
        break;
    case RS2::CenterLineX2:
        return "CENTERX2";
        break;

    case RS2::BorderLine:
        return "BORDER";
        break;
    case RS2::BorderLine2:
        return "BORDER2";
        break;
    case RS2::BorderLineX2:
        return "BORDERX2";
        break;

    case RS2::LineByLayer:
        return "ByLayer";
        break;
    case RS2::LineByBlock:
        return "ByBlock";
        break;
    default:
        break;
    }

    return "CONTINUOUS";
}



/**
 * Converts a RS_LineType into a name for a line type.
 */
/*QString RS_FilterDXFRW::lineTypeToDescription(RS2::LineType lineType) {

    // Standard linetypes for QCad II / AutoCAD
    switch (lineType) {
    case RS2::SolidLine:
        return "Solid line";
    case RS2::DotLine:
        return "ISO Dashed __ __ __ __ __ __ __ __ __ __ _";
    case RS2::DashLine:
        return "ISO Dashed with Distance __    __    __    _";
    case RS2::DashDotLine:
        return "ISO Long Dashed Dotted ____ . ____ . __";
    case RS2::DashDotDotLine:
        return "ISO Long Dashed Double Dotted ____ .. __";
    case RS2::LineByLayer:
        return "";
    case RS2::LineByBlock:
        return "";
    default:
        break;
    }

    return "CONTINUOUS";
}*/



/**
 * Converts a line width number (e.g. 1) into a RS2::LineWidth.
 */
RS2::LineWidth RS_FilterDXFRW::numberToWidth(int num) {
    switch (num) {
    case -1:
        return RS2::WidthByLayer;
        break;
    case -2:
        return RS2::WidthByBlock;
        break;
    case -3:
        return RS2::WidthDefault;
        break;
    default:
        if (num<3) {
            return RS2::Width00;
        } else if (num<7) {
            return RS2::Width01;
        } else if (num<11) {
            return RS2::Width02;
        } else if (num<14) {
            return RS2::Width03;
        } else if (num<16) {
            return RS2::Width04;
        } else if (num<19) {
            return RS2::Width05;
        } else if (num<22) {
            return RS2::Width06;
        } else if (num<27) {
            return RS2::Width07;
        } else if (num<32) {
            return RS2::Width08;
        } else if (num<37) {
            return RS2::Width09;
        } else if (num<45) {
            return RS2::Width10;
        } else if (num<52) {
            return RS2::Width11;
        } else if (num<57) {
            return RS2::Width12;
        } else if (num<65) {
            return RS2::Width13;
        } else if (num<75) {
            return RS2::Width14;
        } else if (num<85) {
            return RS2::Width15;
        } else if (num<95) {
            return RS2::Width16;
        } else if (num<103) {
            return RS2::Width17;
        } else if (num<112) {
            return RS2::Width18;
        } else if (num<130) {
            return RS2::Width19;
        } else if (num<149) {
            return RS2::Width20;
        } else if (num<180) {
            return RS2::Width21;
        } else if (num<205) {
            return RS2::Width22;
        } else {
            return RS2::Width23;
        }
        break;
    }
    return (RS2::LineWidth)num;
}



/**
 * Converts a RS2::LineWidth into an int width.
 */
int RS_FilterDXFRW::widthToNumber(RS2::LineWidth width) {
    switch (width) {
    case RS2::WidthByLayer:
        return -1;
        break;
    case RS2::WidthByBlock:
        return -2;
        break;
    case RS2::WidthDefault:
        return -3;
        break;
    default:
        return (int)width;
        break;
    }
    return (int)width;
}



/**
 * Converts a native unicode string into a DXF encoded string.
 *
 * DXF endoding includes the following special sequences:
 * - %%%c for a diameter sign
 * - %%%d for a degree sign
 * - %%%p for a plus/minus sign
 */
QString RS_FilterDXFRW::toDxfString(const QString& str) {
    QString res = "";
    int j=0;
    for (int i=0; i<str.length(); ++i) {
        int c = str.at(i).unicode();
        if (c>127 || c<11){
            res.append(str.mid(j,i-j));
            j=i;

            switch (c) {
            case 0x0A:
                res+="\\P";
                break;
                // diameter:
            case 0x2205:
                res+="%%c";
                break;
                // degree:
            case 0x00B0:
                res+="%%d";
                break;
                // plus/minus
            case 0x00B1:
                res+="%%p";
                break;
            default:
                if (c>127) {
                    QString hex = QString("%1").arg(c, 4, 16);
                    hex = hex.replace(' ', '0');
                    res+=QString("\\U+%1").arg(hex);
                }
                break;
            }
            j++;
        }

    }
    res.append(str.mid(j));
    return res;
}



/**
 * Converts a DXF encoded string into a native Unicode string.
 */
QString RS_FilterDXFRW::toNativeString(const char* data, const QString& encoding) {
    QString res = QString(data);

    /*	- If the given string doesn't contain any unicode characters, we pass
     *	  the string through a textcoder.
     *	--------------------------------------------------------------------- */
    if (!res.contains("\\U+")) {
        QTextCodec *codec = QTextCodec::codecForName(encoding.toAscii());
        if (codec)
            res = codec->toUnicode(data);
    }

    // Line feed:
    res = res.replace(QRegExp("\\\\P"), "\n");
    // Space:
    res = res.replace(QRegExp("\\\\~"), " ");
    // diameter:
    res = res.replace(QRegExp("%%c"), QChar(0x2205));
    // degree:
    res = res.replace(QRegExp("%%d"), QChar(0x00B0));
    // plus/minus
    res = res.replace(QRegExp("%%p"), QChar(0x00B1));

    // Unicode characters:
    QString cap = "";
    int uCode = 0;
    bool ok = false;
    do {
        QRegExp regexp("\\\\U\\+[0-9A-Fa-f]{4,4}");
        regexp.indexIn(res);
        cap = regexp.cap();
        if (!cap.isNull()) {
            uCode = cap.right(4).toInt(&ok, 16);
            // workaround for Qt 3.0.x:
            res.replace(QRegExp("\\\\U\\+" + cap.right(4)), QChar(uCode));
            // for Qt 3.1:
            //res.replace(cap, QChar(uCode));
        }
    }
    while (!cap.isNull());

    // ASCII code:
    cap = "";
    uCode = 0;
    ok = false;
    do {
        QRegExp regexp("%%[0-9]{3,3}");
        regexp.indexIn(res);
        cap = regexp.cap();
        if (!cap.isNull()) {
            uCode = cap.right(3).toInt(&ok, 10);
            // workaround for Qt 3.0.x:
            res.replace(QRegExp("%%" + cap.right(3)), QChar(uCode));
            // for Qt 3.1:
            //res.replace(cap, QChar(uCode));
        }
    }
    while (!cap.isNull());

    // Ignore font tags:
    res = res.replace(QRegExp("\\\\f[0-9A-Za-z| ]{0,};"), "");

    // Ignore {}
    res = res.replace("\\{", "#curly#");
    res = res.replace("{", "");
    res = res.replace("#curly#", "{");

    res = res.replace("\\}", "#curly#");
    res = res.replace("}", "");
    res = res.replace("#curly#", "}");

    RS_DEBUG->print("RS_FilterDXF::toNativeString:");
    RS_DEBUG->printUnicode(res);
    return res;
}



/**
 * Converts the given number from a DXF file into an AngleFormat enum.
 *
 * @param num $DIMAUNIT from DXF (0: decimal deg, 1: deg/min/sec, 2: gradians,
 *                                3: radians, 4: surveyor's units)
 *
 * @ret Matching AngleFormat enum value.
 */
RS2::AngleFormat RS_FilterDXFRW::numberToAngleFormat(int num) {

    RS2::AngleFormat af;

    switch (num) {
    default:
    case 0:
        af = RS2::DegreesDecimal;
        break;
    case 1:
        af = RS2::DegreesMinutesSeconds;
        break;
    case 2:
        af = RS2::Gradians;
        break;
    case 3:
        af = RS2::Radians;
        break;
    case 4:
        af = RS2::Surveyors;
        break;
    }

    return af;
}


/**
 * Converts AngleFormat enum to DXF number.
 */
int RS_FilterDXFRW::angleFormatToNumber(RS2::AngleFormat af) {

    int num;

    switch (af) {
    default:
    case RS2::DegreesDecimal:
        num = 0;
        break;
    case RS2::DegreesMinutesSeconds:
        num = 1;
        break;
    case RS2::Gradians:
        num = 2;
        break;
    case RS2::Radians:
        num = 3;
        break;
    case RS2::Surveyors:
        num = 4;
        break;
    }

    return num;
}



/**
 * converts a DXF unit setting (e.g. INSUNITS) to a unit enum.
 */
RS2::Unit RS_FilterDXFRW::numberToUnit(int num) {
    switch (num) {
    default:
    case  0:
        return RS2::None;
        break;
    case  1:
        return RS2::Inch;
        break;
    case  2:
        return RS2::Foot;
        break;
    case  3:
        return RS2::Mile;
        break;
    case  4:
        return RS2::Millimeter;
        break;
    case  5:
        return RS2::Centimeter;
        break;
    case  6:
        return RS2::Meter;
        break;
    case  7:
        return RS2::Kilometer;
        break;
    case  8:
        return RS2::Microinch;
        break;
    case  9:
        return RS2::Mil;
        break;
    case 10:
        return RS2::Yard;
        break;
    case 11:
        return RS2::Angstrom;
        break;
    case 12:
        return RS2::Nanometer;
        break;
    case 13:
        return RS2::Micron;
        break;
    case 14:
        return RS2::Decimeter;
        break;
    case 15:
        return RS2::Decameter;
        break;
    case 16:
        return RS2::Hectometer;
        break;
    case 17:
        return RS2::Gigameter;
        break;
    case 18:
        return RS2::Astro;
        break;
    case 19:
        return RS2::Lightyear;
        break;
    case 20:
        return RS2::Parsec;
        break;
    }

    return RS2::None;
}



/**
 * Converst a unit enum into a DXF unit number e.g. for INSUNITS.
 */
int RS_FilterDXFRW::unitToNumber(RS2::Unit unit) {
    switch (unit) {
    default:
    case RS2::None:
        return  0;
        break;
    case RS2::Inch:
        return  1;
        break;
    case RS2::Foot:
        return  2;
        break;
    case RS2::Mile:
        return  3;
        break;
    case RS2::Millimeter:
        return  4;
        break;
    case RS2::Centimeter:
        return  5;
        break;
    case RS2::Meter:
        return  6;
        break;
    case RS2::Kilometer:
        return  7;
        break;
    case RS2::Microinch:
        return  8;
        break;
    case RS2::Mil:
        return  9;
        break;
    case RS2::Yard:
        return 10;
        break;
    case RS2::Angstrom:
        return 11;
        break;
    case RS2::Nanometer:
        return 12;
        break;
    case RS2::Micron:
        return 13;
        break;
    case RS2::Decimeter:
        return 14;
        break;
    case RS2::Decameter:
        return 15;
        break;
    case RS2::Hectometer:
        return 16;
        break;
    case RS2::Gigameter:
        return 17;
        break;
    case RS2::Astro:
        return 18;
        break;
    case RS2::Lightyear:
        return 19;
        break;
    case RS2::Parsec:
        return 20;
        break;
    }

    return 0;
}



/**
 * Checks if the given variable is two-dimensional (e.g. $LIMMIN).
 */
bool RS_FilterDXFRW::isVariableTwoDimensional(const QString& var) {
    if (var=="$LIMMIN" ||
            var=="$LIMMAX" ||
            var=="$PLIMMIN" ||
            var=="$PLIMMAX" ||
            var=="$GRIDUNIT" ||
            var=="$VIEWCTR") {

        return true;
    } else {
        return false;
    }
}

// EOF

