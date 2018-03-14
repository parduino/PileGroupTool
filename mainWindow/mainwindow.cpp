#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "math.h"
#include "utilWindows/copyrightdialog.h"
#include "utilWindows/dialogpreferences.h"
#include "utilWindows/dialogabout.h"
#include "utilWindows/dialogfuturefeature.h"
#include "pilefeamodeler.h"

#include "systemplotsuper.h"
#include "systemplotqcp.h"
#include "systemplotqwt.h"

#include "resultplotsuper.h"
#include "resultplotqcp.h"
#include "resultplotqwt.h"

#include <QApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QDateTime>
#include <QFileDialog>
#include <QDir>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

extern int getTzParam(double phi, double b, double sigV, double pEleLength, double *tult, double *z50);
extern int getQzParam(double phiDegree, double b, double sigV, double G, double *qult, double *z50);
extern int getPyParam(double pyDepth,
                      double gamma,
                      double phiDegree,
                      double b,
                      double pEleLength,
                      int puSwitch,
                      int kSwitch,
                      int gwtSwitch,
                      double *pult,
                      double *y50);

// OpenSees include files
#include <Node.h>
#include <ID.h>
#include <SP_Constraint.h>
#include <MP_Constraint.h>
#include <Domain.h>
#include <StandardStream.h>
#include <LinearCrdTransf3d.h>
#include <DispBeamColumn3d.h>
#include <PySimple1.h>
#include <TzSimple1.h>
#include <QzSimple1.h>
#include <ZeroLength.h>
#include <LegendreBeamIntegration.h>
#include <ElasticSection3d.h>
#include <LinearSeries.h>
#include <NodalLoad.h>
#include <LoadPattern.h>
#include <SimulationInformation.h>

#include <LoadControl.h>
#include <RCM.h>
#include <PlainNumberer.h>
#include <NewtonRaphson.h>
#include <CTestNormDispIncr.h>
#include <TransformationConstraintHandler.h>
#include <PenaltyConstraintHandler.h>
#include <BandGenLinSOE.h>
#include <BandGenLinLapackSolver.h>
#include <StaticAnalysis.h>
#include <AnalysisModel.h>

#include <soilmat.h>

StandardStream sserr;
OPS_Stream *opserrPtr = &sserr;
Domain theDomain;

//SimulationInformation simulationInfo;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    inSetupState = true;

    ui->setupUi(this);

    this->fetchSettings();

    if (useGraphicsLib == "Qwt")
    {
        systemPlot  = new SystemPlotQwt(ui->systemTab);

        displPlot   = new ResultPlotQwt(ui->dispTab);
        pullOutPlot = new ResultPlotQwt(ui->pulloutTab);
        momentPlot  = new ResultPlotQwt(ui->momentTab);
        shearPlot   = new ResultPlotQwt(ui->shearTab);
        axialPlot   = new ResultPlotQwt(ui-> axialTab);
        stressPlot  = new ResultPlotQwt(ui->stressTab);
        pultPlot    = new ResultPlotQwt(ui->pultTab);
        y50Plot     = new ResultPlotQwt(ui->y50Tab);
    }
    else
    {
        systemPlot  = new SystemPlotQCP(ui->systemTab);

        displPlot   = new ResultPlotQCP(ui->dispTab);
        pullOutPlot = new ResultPlotQCP(ui->pulloutTab);
        momentPlot  = new ResultPlotQCP(ui->momentTab);
        shearPlot   = new ResultPlotQCP(ui->shearTab);
        axialPlot   = new ResultPlotQCP(ui->axialTab);
        stressPlot  = new ResultPlotQCP(ui->stressTab);
        pultPlot    = new ResultPlotQCP(ui->pultTab);
        y50Plot     = new ResultPlotQCP(ui->y50Tab);
    }

    //
    // place widgets into their respective layouts
    //
    QLayout *lyt;

    lyt = ui->systemTab->layout();  lyt->addWidget(systemPlot);
    lyt = ui->dispTab->layout();    lyt->addWidget(displPlot);
    lyt = ui->pulloutTab->layout(); lyt->addWidget(pullOutPlot);
    lyt = ui->momentTab->layout();  lyt->addWidget(momentPlot);
    lyt = ui->shearTab->layout();   lyt->addWidget(shearPlot);
    lyt = ui->axialTab->layout();   lyt->addWidget(axialPlot);
    lyt = ui->stressTab->layout();  lyt->addWidget(stressPlot);
    lyt = ui->pultTab->layout();    lyt->addWidget(pultPlot);
    lyt = ui->y50Tab->layout();     lyt->addWidget(y50Plot);

    ui->tabWidget->setCurrentWidget(ui->dispTab);

    //
    // general setup
    //
    this->updateUI();
    ui->headerWidget->setHeadingText("SimCenter Pile Group Tool");
    ui->appliedHorizontalForce->setMaximum(MAX_FORCE);
    ui->appliedHorizontalForce->setMinimum(-MAX_FORCE);

    ui->textBrowser->clear();
#ifdef Q_OS_WIN
    QFont font = ui->textBrowser->font();
    font.setPointSize(8);
    ui->textBrowser->setFont(font);
#endif

    ui->textBrowser->setHtml("<b>Hints</b><p><ul><li>The Pile Group Tool uses metric units: meters, kN, and kPa. </li><li>Select piles or soil layers to display and/or change by clicking on the pile inside the System Plot </li><li>go to Preferences to select which result plots are shown. </li></ul>");

    /* connect a FEA modeler */
    pileFEAmodel = new PileFEAmodeler();

    // setup data
    numPiles = 1;
    P        = 1000.0;
    PV       =    0.0;
    PMom     =    0.0;

    HDisp = 0.0; // prescribed horizontal displacement
    VDisp = 0.0; // prescriber vertical displacement

    surfaceDisp = 0.0;    // prescribed soil surface displacement
    percentage12 = 1.0;   // percentage of surface displacement at 1st layer interface
    percentage23 = 0.0;   // percentage of surface displacement at 2nd layer interface
    percentageBase = 0.0; // percentage of surface displacement at base of soil column

    L1                       = 1.0;
    L2[numPiles-1]           = 20.0;
    pileDiameter[numPiles-1] = 1.0;
    E[numPiles-1]            = 25.0e6;
    xOffset[numPiles-1]      = 0.0;

    gwtDepth = 4.00;
    gSoil    = 150000;
    puSwitch = 2;
    kSwitch  = 1;
    gwtSwitch= 1;

     // set initial state of check boxes
    useToeResistance    = false;
    assumeRigidPileHeadConnection = false;

    // set up initial values before activating live analysis (= connecting the slots)
    //    or the program will fail in the analysis due to missing information
    setupLayers();

    //reDrawTable();

    setActiveLayer(0);

    // set up pile input
    this->refreshUI();

    // plot setting
    activePileIdx = 0;
    activeLayerIdx = -1;

    //
    // this connect statement needs to be updated to reflect changes to the SystemPlotSuper class
    //
    QObject::connect(systemPlot, SIGNAL(on_pileSelected(int)), this, SLOT(onSystemPlot_pileSelected(int)));
    QObject::connect(systemPlot, SIGNAL(on_soilLayerSelected(int)), this, SLOT(onSystemPlot_soilLayerSelected(int)));
    QObject::connect(systemPlot, SIGNAL(on_groundWaterSelected()), this, SLOT(onSystemPlot_groundWaterSelected()));

    inSetupState = false;

    //
    // adjust size of application window to the available display
    //
    QRect rec = QApplication::desktop()->screenGeometry();
    int height = this->height()<0.85*rec.height()?this->height():0.85*rec.height();
    int width  = this->width()<0.85*rec.width()?this->width():0.85*rec.width();
    this->resize(width, height);

    //
    // create the initial set of plots
    //
    this->doAnalysis();
    this->updateSystemPlot();

    //
    // connect to the application usage counter
    //
    manager = new QNetworkAccessManager(this);

    connect(manager, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(replyFinished(QNetworkReply*)));

    manager->get(QNetworkRequest(QUrl("http://opensees.berkeley.edu/OpenSees/developer/qtPile/use.php")));
    //manager->get(QNetworkRequest(QUrl("https://simcenter.designsafe-ci.org/pile-group-analytics/")));
}

MainWindow::~MainWindow()
{
    delete ui;

    if (pileFEAmodel != NULL) delete pileFEAmodel;
}

void MainWindow::refreshUI() {

    inSetupState = true;

    ui->pileIndex->setValue(1);
    ui->pileIndex->setMinimum(1);
    ui->pileIndex->setMaximum(numPiles);

    if (loadControlType == LoadControlType::ForceControl) { ui->forceTypeSelector->setCurrentIndex(0); }
    else if (loadControlType == LoadControlType::PushOver) { ui->forceTypeSelector->setCurrentIndex(1); }
    else if (loadControlType == LoadControlType::SoilMotion) { ui->forceTypeSelector->setCurrentIndex(2); }
    else { ui->forceTypeSelector->setCurrentIndex(0); }

    ui->appliedHorizontalForce->setValue(P);
    ui->appliedVerticalForce->setValue(PV);
    ui->appliedMoment->setValue(PMom);

    int pileIdx = ui->pileIndex->value() - 1;

    ui->xOffset->setValue(xOffset[pileIdx]);
    ui->pileDiameter->setValue(pileDiameter[pileIdx]);
    ui->freeLength->setValue(L1);
    ui->embeddedLength->setValue(L2[pileIdx]);
    ui->Emodulus->setValue( (E[pileIdx]/1.0e+6) );

    ui->groundWaterTable->setValue(gwtDepth);

    ui->chkBox_assume_rigid_cap->setCheckState(assumeRigidPileHeadConnection?Qt::Checked:Qt::Unchecked);
    ui->chkBox_include_toe_resistance->setCheckState(useToeResistance?Qt::Checked:Qt::Unchecked);

    this->updateLayerState();

    inSetupState = false;
}

void MainWindow::doAnalysis(void)
{
    //
    // create pile information for plotting
    //
    QVector<PILE_INFO> pileInfo;

    pileInfo.clear();

    for (int i=0; i<numPiles; i++)
    {
        PILE_INFO thisPile;
        thisPile.L1           = L1;
        thisPile.L2           = L2[i];
        thisPile.pileDiameter = pileDiameter[i];
        thisPile.xOffset      = xOffset[i];
        thisPile.E            = E[i];
        pileInfo.append(thisPile);
    }
    pileFEAmodel->updatePiles(pileInfo);

    //
    // set the soil layer information
    //
    pileFEAmodel->updateSoil(mSoilLayers);

    //
    // update ground water table
    //
    pileFEAmodel->updateGWtable(gwtDepth);

    //
    // set the load control type
    //
    pileFEAmodel->setLoadType(loadControlType);

    //
    // update the load control data
    //
    switch (loadControlType) {
    case LoadControlType::ForceControl:
        pileFEAmodel->updateLoad(P, PV, PMom);
        break;
    case LoadControlType::PushOver:
        pileFEAmodel->updateDisplacement(HDisp, VDisp);
        break;
    case LoadControlType::SoilMotion:
        QVector<double> profile;
        profile.append(surfaceDisp);
        profile.append(percentage12);
        profile.append(percentage23);
        profile.append(percentageBase);
        pileFEAmodel->updateDispProfile(profile);
        break;
    }

    //
    // run the analysis
    //
    //pileFEAmodel->doAnalysis();

    /* ******************* everything below will become obsolete once the PileFEAmodeler is functional **** */

    double overburdonStress;

    //QVector<HEAD_NODE_TYPE> headNodeList(MAXPILES, {-1,-1,0.0, 1.0, 1.0});

    for (int k=0; k<MAXPILES; k++) {
        headNodeList[k] = {-1, -1, 0.0, 1.0, 1.0};
    }

    if (inSetupState) return;

    // clear existing model
    theDomain.clearAll();
    OPS_clearAllUniaxialMaterial();
    ops_Dt = 0.0;

    //
    // find meshing parameters
    //

    QVector<QVector<int>> elemsInLayer(MAXPILES,QVector<int>(3,minElementsPerLayer));
    QVector<double> depthOfLayer = QVector<double>(4, 0.0); // add a buffer element for bottom of the third layer

    numNodePiles = numPiles;

    for (int k=0; k<MAXPILES; k++) {
        numNodePile[k]  = 0;
        maxLayers[k]    = MAXLAYERS;
        nodeIDoffset[k] = 0;
        elemIDoffset[k] = 0;
    }

    /* ******** sizing and adjustments ******** */

    for (int pileIdx=0; pileIdx<numPiles; pileIdx++)
    {
        numNodePile[pileIdx] = 2; // bottom plus surface node
        if (L1 > 0.0001) numNodePile[pileIdx] += numElementsInAir; // free standing

        //
        // find active layers for pile #pileIdx
        //
        for (int iLayer=0; iLayer < maxLayers[pileIdx]; iLayer++)
        {
            if (depthOfLayer[iLayer] >= L2[pileIdx]) {
                // this pile only penetrates till layer #iLayer
                maxLayers[pileIdx] = iLayer;
                break;
            }

            double thickness = mSoilLayers[iLayer].getLayerThickness();

            // compute bottom of this layer/top of the next layer
            if (depthOfLayer[iLayer] + thickness < L2[pileIdx] && iLayer == MAXLAYERS - 1) {
                thickness = L2[pileIdx] - depthOfLayer[iLayer];
                mSoilLayers[iLayer].setLayerThickness(thickness);
            }
            depthOfLayer[iLayer+1] = depthOfLayer[iLayer] + thickness;

            // check if layer ends below the pile toe and adjust thickness for mesh generation
            if (depthOfLayer[iLayer+1] > L2[pileIdx]) {
                thickness = L2[pileIdx] - depthOfLayer[iLayer];
            }

            int numElemThisLayer = int(thickness/pileDiameter[pileIdx]);
            if (numElemThisLayer < minElementsPerLayer) numElemThisLayer = minElementsPerLayer;
            if (numElemThisLayer > maxElementsPerLayer) numElemThisLayer = maxElementsPerLayer;

            // remember number of elements in this layer
            elemsInLayer[pileIdx][iLayer] = numElemThisLayer;

            // total node count
            numNodePile[pileIdx] += numElemThisLayer;

            // update layer information on overburdon stress and water table
            if (iLayer > 0) {
                overburdonStress = mSoilLayers[iLayer-1].getLayerBottomStress();
            }
            else {
                overburdonStress = 0.00;
            }
            mSoilLayers[iLayer].setLayerOverburdenStress(overburdonStress);

            double groundWaterHead = gwtDepth - depthOfLayer[iLayer];
            mSoilLayers[iLayer].setLayerGWHead(groundWaterHead);
        }

        // add numper of nodes in this pile to global number of nodes
        numNodePiles += numNodePile[pileIdx];
    }

    /* ******** done with sizing and adjustments ******** */

    QVector<QVector<double> *> locList;
    QVector<QVector<double> *> pultList;
    QVector<QVector<double> *> y50List;

    for (int i=0; i<numPiles; i++)
    {
        locList.append(new QVector<double>(numNodePile[i],0.0));
        pultList.append(new QVector<double>(numNodePile[i],0.0));
        y50List.append(new QVector<double>(numNodePile[i],0.0));
    }

    int ioffset  = numNodePiles;              // for p-y spring nodes
    int ioffset2 = ioffset + numNodePiles;    // for pile nodes
    int ioffset3 = ioffset2 + numNodePiles;   // for t-z spring nodes
    int ioffset4 = ioffset3 + numNodePiles;   // for toe resistance nodes
    int ioffset5 = ioffset4 + numNodePiles;   // for pile cap nodes

    /* ******** build the finite element mesh ******** */

    //
    // matrix used to constrain spring and pile nodes with equalDOF (identity constraints)
    //
    static Matrix Ccr (2, 2);
    Ccr.Zero(); Ccr(0,0)=1.0; Ccr(1,1)=1.0;
    static ID rcDof (2);
    rcDof(0) = 0;
    rcDof(1) = 2;

    // create the vectors for the spring elements orientation
    static Vector x(3); x(0) = 1.0; x(1) = 0.0; x(2) = 0.0;
    static Vector y(3); y(0) = 0.0; y(1) = 1.0; y(2) = 0.0;

    // direction for spring elements
    ID direction(2);
    direction[0] = 0;
    direction[1] = 2;

    // initialization of counters
    int numNode = 0;
    int numElem = ioffset2;
    int nodeTag = 0;

    nodeIDoffset[0] = ioffset2;
    elemIDoffset[0] = ioffset2;
    for (int k=1; k<numPiles; k++) {
        nodeIDoffset[k] = nodeIDoffset[k-1] + numNodePile[k-1];
        elemIDoffset[k] = elemIDoffset[k-1] + numNodePile[k-1] - 1;
    }

    // build the FE-model one pile at a time

    for (int pileIdx=0; pileIdx<numPiles; pileIdx++)
    {
        //qDebug() << "+ pile index: " << pileIdx;

        //
        // compute pile properties (compute once; used for all pile elements)
        //
        double PI = 3.14159;
        double A  = 0.2500 * PI * pileDiameter[pileIdx] * pileDiameter[pileIdx];
        double Iz = 0.0625 *  A * pileDiameter[pileIdx] * pileDiameter[pileIdx];
        double G  = E[pileIdx]/(2.0*(1.+0.3));
        double J  = 1.0e10;

        // suitable pile head parameters (make pile head stiff)
        if (100.*E[pileIdx]*A > EA ) EA = 100.*E[pileIdx]*A;
        if (100.*E[pileIdx]*Iz > EI) EI = 100.*E[pileIdx]*Iz;
        if (10.*G*J > GJ)            GJ = 10.*G*J;

        //
        // Ready to generate the structure
        //

        /* embedded pile portion */

        zCoord = -L2[pileIdx];

        //
        // create bottom pile node
        //
        numNode += 1;

        Node *theNode = 0;
        nodeTag = numNode+ioffset2;

        //qDebug() << "Node(" << nodeTag << "," << 6 << "," << xOffset[pileIdx] << "," << 0.0 << "," << zCoord << ")";

        theNode = new Node(nodeTag, 6, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);
        if (numNode != 1) {
            SP_Constraint *theSP = 0;
            theSP = new SP_Constraint(nodeTag, 1, 0., true); theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(nodeTag, 3, 0., true); theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(nodeTag, 5, 0., true); theDomain.addSP_Constraint(theSP);
        }

        //
        // add toe resistance (if requested)
        //
    /*
     *  CHECK IN WITH FRANK:
     *     how to properly implement a Q-z spring at the end WITHOUT the p-y spring.
     */

        if (useToeResistance) {
            Node *theNode = 0;

            theNode = new Node(numNode,         3, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);
            theNode = new Node(numNode+ioffset, 3, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);

            SP_Constraint *theSP = 0;
            theSP = new SP_Constraint(numNode, 0, 0., true);  theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(numNode, 1, 0., true);  theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(numNode, 2, 0., true);  theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(numNode+ioffset, 0, 0., true);  theDomain.addSP_Constraint(theSP); // ?
            theSP = new SP_Constraint(numNode+ioffset, 1, 0., true);  theDomain.addSP_Constraint(theSP);
            theSP = new SP_Constraint(numNode+ioffset, 2, 0., true);  theDomain.addSP_Constraint(theSP);
        }

        (*locList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = zCoord;
        (*pultList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]] = 0.001;
        (*y50List[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = 0.00001;

        //
        // work the way up layer by layer
        //

        for (int iLayer=maxLayers[pileIdx]-1; iLayer >= 0; iLayer--)
        {
            double thickness = mSoilLayers[iLayer].getLayerThickness();
            if (depthOfLayer[iLayer+1] > L2[pileIdx]) {
                thickness = L2[pileIdx] - depthOfLayer[iLayer];
            }

            eleSize = thickness/(1.0*elemsInLayer[pileIdx][iLayer]);
            int numNodesLayer = elemsInLayer[pileIdx][iLayer] + 1;

            //
            // create spring nodes
            //
            zCoord += 0.5*eleSize;

            for (int i=1; i<numNodesLayer; i++) {
                numNode += 1;

                //
                // spring nodes
                //

                Node *theNode = 0;

                theNode = new Node(numNode,         3, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);
                theNode = new Node(numNode+ioffset, 3, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);

                SP_Constraint *theSP = 0;
                theSP = new SP_Constraint(numNode, 0, 0., true);  theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(numNode, 1, 0., true);  theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(numNode, 2, 0., true);  theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(numNode+ioffset, 1, 0., true);  theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(numNode+ioffset, 2, 0., true);  theDomain.addSP_Constraint(theSP);

                //
                // pile nodes
                //

                nodeTag = numNode+ioffset2;

                //qDebug() << "Node(" << nodeTag << "," << 6 << "," << xOffset[pileIdx] << "," << 0.0 << "," << zCoord << ")";

                theNode = new Node(nodeTag, 6, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);
                if (numNode != 1) {
                    SP_Constraint *theSP = 0;
                    theSP = new SP_Constraint(nodeTag, 1, 0., true); theDomain.addSP_Constraint(theSP);
                    theSP = new SP_Constraint(nodeTag, 3, 0., true); theDomain.addSP_Constraint(theSP);
                    theSP = new SP_Constraint(nodeTag, 5, 0., true); theDomain.addSP_Constraint(theSP);
                }

                // constrain spring and pile nodes with equalDOF (identity constraints)
                MP_Constraint *theMP = new MP_Constraint(numNode+ioffset2, numNode+ioffset, Ccr, rcDof, rcDof);
                theDomain.addMP_Constraint(theMP);

                //
                // create soil-spring materials
                //

                // # p-y spring material
                puSwitch  = 2;  // Hanson
                //puSwitch  = 1;  // API // temporary switch
                kSwitch   = 1;  // API

                gwtSwitch = (gwtDepth > -zCoord)?1:2;

                int dummy = iLayer;

                double depthInLayer = -zCoord - depthOfLayer[iLayer];
                sigV = mSoilLayers[iLayer].getEffectiveStress(depthInLayer);
                double phi  = mSoilLayers[iLayer].getLayerFrictionAng();

                UniaxialMaterial *theMat;
                getPyParam(-zCoord, sigV, phi, pileDiameter[pileIdx], eleSize, puSwitch, kSwitch, gwtSwitch, &pult, &y50);

                if(pult <= 0.0 || y50 <= 0.0) {
                    qDebug() << "WARNING -- only accepts positive nonzero pult and y50";
                    qDebug() << "*** iLayer: " << iLayer << "   pile number" << pileIdx+1
                             << "   depth: " << -zCoord
                             << "   depth in layer: " << depthInLayer
                             << "   sigV: "  << sigV
                             << "   diameter: " << pileDiameter[pileIdx] << "   eleSize: " << eleSize;
                    qDebug() << "*** pult: " << pult << "   y50: " << y50;
                }

                theMat = new PySimple1(numNode, 0, 2, pult, y50, 0.0, 0.0);
                OPS_addUniaxialMaterial(theMat);

                (*locList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = zCoord;
                // pult is a nodal value for the p-y spring.
                // It needs to be scaled by element length ito represent a line load
                (*pultList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]] = pult/eleSize;
                (*y50List[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = y50;

                // t-z spring material
                getTzParam(phi, pileDiameter[pileIdx],  sigV,  eleSize, &tult, &z50);

                if(tult <= 0.0 || z50 <= 0.0) {
                    qDebug() << "WARNING -- only accepts positive nonzero tult and z50";
                    qDebug() << "*** iLayer: " << iLayer << "   pile number" << pileIdx+1
                             << "   depth: " << -zCoord << "   sigV: "  << sigV
                             << "   diameter: " << pileDiameter[pileIdx] << "   eleSize: " << eleSize;
                    qDebug() << "*** tult: " << tult << "   z50: " << z50;
                }

                theMat = new TzSimple1(numNode+ioffset, 0, 2, tult, z50, 0.0);
                OPS_addUniaxialMaterial(theMat);

                //
                // create soil spring elements
                //

                UniaxialMaterial *theMaterials[2];
                theMaterials[0] = OPS_getUniaxialMaterial(numNode);
                theMaterials[1] = OPS_getUniaxialMaterial(numNode+ioffset);
                Element *theEle = new ZeroLength(numNode+ioffset3, 3, numNode, numNode+ioffset, x, y, 2, theMaterials, direction);
                theDomain.addElement(theEle);

                zCoord += eleSize;
            }
            // back to the layer interface
            zCoord -= 0.5*eleSize;

            //qDebug() << "Layer interface at " << "zCoord: " << zCoord << ", eleSize: " << eleSize;

        } // on to the next layer, working our way up from the bottom

        if (ABS(zCoord) > 1.0e-2) {
            qDebug() << "ERROR in node generation: surface reached at " << zCoord << endln;
        }
        //
        // add elements above ground
        //

        if (L1 > 0.0001) {
            eleSize = L1 / (1.0*numElementsInAir);
            zCoord = 0.0;
        }
        else {
            eleSize = 999.99;
            zCoord = 0.0;
        }

        while (zCoord < (L1 + 1.0e-6)) {
            numNode += 1;

            nodeTag = numNode+ioffset2;

            //qDebug() << "Node(" << nodeTag << "," << 6 << "," << xOffset[pileIdx] << "," << 0.0 << "," << zCoord << ")";

            Node *theNode = new Node(nodeTag, 6, xOffset[pileIdx], 0., zCoord);  theDomain.addNode(theNode);
            if (numNode != 1) {
                SP_Constraint *theSP = 0;
                theSP = new SP_Constraint(nodeTag, 1, 0., true); theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(nodeTag, 3, 0., true); theDomain.addSP_Constraint(theSP);
                theSP = new SP_Constraint(nodeTag, 5, 0., true); theDomain.addSP_Constraint(theSP);
            }

            /*
            (*locList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = zCoord;
            (*pultList[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]] = 0.001;
            (*y50List[pileIdx])[numNode+ioffset2-nodeIDoffset[pileIdx]]  = 0.00001;
            */

            locList[pileIdx]->append(zCoord);
            pultList[pileIdx]->append(0.001);
            y50List[pileIdx]->append(0.00001);

            zCoord += eleSize;
        }


        headNodeList[pileIdx] = {pileIdx, nodeTag, xOffset[pileIdx]};

        //
        // create pile elements
        //

        static Vector crdV(3); crdV(0)=0.; crdV(1)=-1; crdV(2) = 0.;
        CrdTransf *theTransformation = new LinearCrdTransf3d(1, crdV);

        for (int i=0; i<numNodePile[pileIdx]-1; i++) {
            numElem++;
            BeamIntegration *theIntegration = new LegendreBeamIntegration();
            SectionForceDeformation *theSections[3];
            SectionForceDeformation *theSection = new ElasticSection3d(pileIdx+1, E[pileIdx], A, Iz, Iz, G, J);
            theSections[0] = theSection;
            theSections[1] = theSection;
            theSections[2] = theSection;

            //qDebug() << "DispBeamColumn3d("
            //         << numElem << ","
            //         << nodeIDoffset[pileIdx]+i+1 << ","
            //         << nodeIDoffset[pileIdx]+i+2 << ")";

            Element *theEle = new DispBeamColumn3d(numElem,
                                                   nodeIDoffset[pileIdx]+i+1,
                                                   nodeIDoffset[pileIdx]+i+2,
                                                   3, theSections, *theIntegration, *theTransformation);
            theDomain.addElement(theEle);
            //delete theSection;
            //delete theIntegration;
        }

        //
        // set up toe resistance, if requested
        //
        if (useToeResistance) {

            // # q-z spring material
            // # vertical effective stress at pile tip, no water table (depth is embedded pile length)
            double sigVq  = mSoilLayers[maxLayers[pileIdx]-1].getLayerBottomStress();
            double phi  = mSoilLayers[maxLayers[pileIdx]-1].getLayerFrictionAng();

            getQzParam(phi, pileDiameter[pileIdx],  sigVq,  gSoil, &qult, &z50q);
            UniaxialMaterial *theMat = new QzSimple1(1+ioffset, 2, qult, z50q, 0.0, 0.0);
            OPS_addUniaxialMaterial(theMat);

            ID Onedirection(1);
            Onedirection[0] = 2;

            // pile toe
            Element *theEle = new ZeroLength(1+pileIdx+ioffset4, 3, 1, 1+ioffset, x, y, 1, &theMat, Onedirection);
            theDomain.addElement(theEle);
        }
    }

    //
    // *** construct the pile head ***
    //
    if (numPiles > 0) {

        // sort piles by xOffset

        for (int i=0; i<numPiles; i++) {
            for (int j=0; j<numPiles-i; j++) {
                if (headNodeList[i].x < headNodeList[j].x) SWAP(headNodeList[i],headNodeList[j]);
            }
        }

        // set up transformation and orientation for the pile cap elements

        static Vector crdV(3); crdV(0)=0.; crdV(1)=-1; crdV(2) = 0.;
        CrdTransf *theTransformation = new LinearCrdTransf3d(1, crdV);

        // define beam element integration and cross sections

        BeamIntegration *theIntegration = new LegendreBeamIntegration();
        SectionForceDeformation *theSections[3];
        SectionForceDeformation *theSection = new ElasticSection3d(numPiles+1, 1., EA, EI, EI, 1., GJ);
        theSections[0] = theSection;
        theSections[1] = theSection;
        theSections[2] = theSection;

        int prevNode = -1;

        for (int pileIdx=0; pileIdx<numPiles; pileIdx++) {

            // create node for pile head
            numNode++;
            int nodeTag = numNode + ioffset5;

            //qDebug() << "Node(" << nodeTag << "," << 6 << "," << headNodeList[pileIdx].x << "," << 0.0 << "," << L1 << ")";

            Node *theNode = new Node(nodeTag, 6, headNodeList[pileIdx].x, 0., L1);  theDomain.addNode(theNode);

            // create single point constraints
            if (assumeRigidPileHeadConnection) {
                // constrain spring and pile nodes with equalDOF (identity constraints)
                static Matrix Chr (6, 6);
                Chr.Zero();
                Chr(0,0)=1.0; Chr(1,1)=1.0; Chr(2,2)=1.0; Chr(3,3)=1.0; Chr(4,4)=1.0; Chr(5,5)=1.0;
                static ID hcDof (6);
                hcDof(0) = 0; hcDof(1) = 1; hcDof(2) = 2; hcDof(3) = 3; hcDof(4) = 4; hcDof(5) = 5;

                //qDebug() << "MP_Constraint(" << nodeTag << "," << headNodeList[pileIdx].nodeIdx << "," << "Idty(6,6)" << "," << "{0,1,2,3,4,5}" << "," << "{0,1,2,3,4,5}" << ")";

                MP_Constraint *theMP = new MP_Constraint(nodeTag, headNodeList[pileIdx].nodeIdx, Chr, hcDof, hcDof);
                theDomain.addMP_Constraint(theMP);
            }
            else {
                // constrain spring and pile nodes with equalDOF (identity constraints)
                static Matrix Chl (5, 5);
                Chl.Zero();
                Chl(0,0)=1.0; Chl(1,1)=1.0; Chl(2,2)=1.0; Chl(3,3)=1.0; Chl(4,4)=1.0;
                static ID hlDof (5);
                hlDof(0) = 0; hlDof(1) = 1; hlDof(2) = 2; hlDof(3) = 3; hlDof(4) = 5;

                //qDebug() << "MP_Constraint(" << nodeTag << "," << headNodeList[pileIdx].nodeIdx << "," << "Idty(5,5)" << "," << "{0,1,2,3,5}" << "," << "{0,1,2,3,5}" << ")";

                MP_Constraint *theMP = new MP_Constraint(nodeTag, headNodeList[pileIdx].nodeIdx, Chl, hlDof, hlDof);
                theDomain.addMP_Constraint(theMP);
            }

            // create beams for pile head
            if (prevNode > 0) {
                numElem++;

                //qDebug() << "DispBeamColumn3d(" << numElem << "," << prevNode << "," << numNode << ")";

                Element *theEle = new DispBeamColumn3d(numElem, prevNode, nodeTag,
                                                       3, theSections, *theIntegration, *theTransformation);
                theDomain.addElement(theEle);
            }

            prevNode = nodeTag;
        }
    }

    if (numPiles == 1) {
        /* need extra fixities to prevent singular system matrix */
        int nodeTag = numNode + ioffset5;

        SP_Constraint *theSP = 0;
        theSP = new SP_Constraint(nodeTag, 4, 0., true); theDomain.addSP_Constraint(theSP);
    }

    int numLoadedNode = headNodeList[0].nodeIdx;

    /* *** done with the pile head *** */

    if (numNode != numNodePiles) {
        qDebug() << "ERROR: " << numNode << " nodes generated but " << numNodePiles << "expected" << endln;
    }

    //
    // create load pattern and add loads
    //

    LinearSeries *theTimeSeries = new LinearSeries(1, 1.0);
    LoadPattern *theLoadPattern = new LoadPattern(1);
    theLoadPattern->setTimeSeries(theTimeSeries);
    static Vector load(6); load.Zero(); load(0) = P;
    NodalLoad *theLoad = new NodalLoad(0, numLoadedNode, load);
    theLoadPattern->addNodalLoad(theLoad);
    theDomain.addLoadPattern(theLoadPattern);

    //
    // create the analysis
    //

    AnalysisModel     *theModel = new AnalysisModel();
    CTestNormDispIncr *theTest = new CTestNormDispIncr(1.0e-3, 20, 0);
    EquiSolnAlgo      *theSolnAlgo = new NewtonRaphson();
    StaticIntegrator  *theIntegrator = new LoadControl(0.05, 1, 0.05, 0.05);
    //ConstraintHandler *theHandler = new TransformationConstraintHandler();
    ConstraintHandler *theHandler = new PenaltyConstraintHandler(1.0e14, 1.0e14);
    RCM               *theRCM = new RCM();
    DOF_Numberer      *theNumberer = new DOF_Numberer(*theRCM);
    BandGenLinSolver  *theSolver = new BandGenLinLapackSolver();
    LinearSOE         *theSOE = new BandGenLinSOE(*theSolver);

    StaticAnalysis    theAnalysis(theDomain,
                                  *theHandler,
                                  *theNumberer,
                                  *theModel,
                                  *theSolnAlgo,
                                  *theSOE,
                                  *theIntegrator);
    theSolnAlgo->setConvergenceTest(theTest);

    //
    //analyze & get results
    //
    theAnalysis.analyze(20);
    theDomain.calculateNodalReactions(0);

    this->updateResultPlots();







    QVector<QVector<double> *> loc;
    QVector<QVector<double> *> Hdisps;
    QVector<QVector<double> *> Vdisps;
    QVector<QVector<double> *> moment;
    QVector<QVector<double> *> shear;
    QVector<QVector<double> *> axial;
    QVector<QVector<double> *> stress;

    for (int i=0; i<numPiles; i++)
    {
        loc.append(new QVector<double>(numNodePile[i],0.0));
        Hdisps.append(new QVector<double>(numNodePile[i],0.0));
        Vdisps.append(new QVector<double>(numNodePile[i],0.0));
        moment.append(new QVector<double>(numNodePile[i],0.0));
        shear.append(new QVector<double>(numNodePile[i],0.0));
        axial.append(new QVector<double>(numNodePile[i],0.0));
        stress.append(new QVector<double>(numNodePile[i],0.0));
    }

    double maxHDisp  = 0.0;
    double minHDisp  = 0.0;
    double maxVDisp  = 0.0;
    double minVDisp  = 0.0;
    double maxAxial  = 0.0;
    double minAxial  = 0.0;
    double maxShear  = 0.0;
    double minShear  = 0.0;
    double maxMoment = 0.0;
    double minMoment = 0.0;

    int pileIdx;

    for (pileIdx=0; pileIdx<numPiles; pileIdx++) {

        for (int i=1; i<=numNodePile[pileIdx]; i++) {

            Node *theNode = theDomain.getNode(i+nodeIDoffset[pileIdx]);
            const Vector &nodeCoord = theNode->getCrds();
            (*loc[pileIdx])[i-1] = nodeCoord(2);
            int iLayer;
            for (iLayer=0; iLayer<maxLayers[pileIdx]; iLayer++) { if (-nodeCoord(2) <= depthOfLayer[iLayer+1]) break;}
            (*stress[pileIdx])[i-1] = mSoilLayers[iLayer].getEffectiveStress(-nodeCoord(2)-depthOfLayer[iLayer]);
            //if (stress[pileIdx][i-1] > maxStress) maxStress = stress[pileIdx][i-1];
            //if (stress[pileIdx][i-1] < minStress) minStress = stress[pileIdx][i-1];
            const Vector &nodeDisp = theNode->getDisp();
            (*Hdisps[pileIdx])[i-1] = nodeDisp(0);
            if ((*Hdisps[pileIdx])[i-1] > maxHDisp) maxHDisp = Hdisps[pileIdx]->value(i-1);
            if ((*Hdisps[pileIdx])[i-1] < minHDisp) minHDisp = Hdisps[pileIdx]->value(i-1);
            (*Vdisps[pileIdx])[i-1] = nodeDisp(2);
            if ((*Vdisps[pileIdx])[i-1] > maxVDisp) maxVDisp = Vdisps[pileIdx]->value(i-1);
            if ((*Vdisps[pileIdx])[i-1] < minVDisp) minVDisp = Vdisps[pileIdx]->value(i-1);
        }
    }

    for (pileIdx=0; pileIdx<numPiles; pileIdx++) {

        //qDebug() << "= pile index: " << pileIdx ;

        (*moment[pileIdx])[0] = 0.0;
        (*shear[pileIdx])[0]  = 0.0;
        (*axial[pileIdx])[0]  = 0.0;

        for (int i=1; i<numNodePile[pileIdx]; i++) {

            /*
             *  identify force components
             *  components identified by *** will be shown in plots
             *
             * eleForces[0] ... Px on node 1 == in plane shear force
             * eleForces[1] ... Py on node 1 == out of plane shear force
             * eleForces[2] ... Pz on node 1 == axial force
             * eleForces[3] ... Mx on node 1 == out of plane bending moment
             * eleForces[4] ... My on node 1 == in plane bending moment
             * eleForces[5] ... Mz on node 1 == torsion
             * eleForces[6] ... Px on node 2 == in plane shear force        ***
             * eleForces[7] ... Py on node 2 == out of plane shear force
             * eleForces[8] ... Pz on node 2 == axial force                 ***
             * eleForces[9] ... Mx on node 2 == out of plane bending moment
             * eleForces[10] .. My on node 2 == in plane bending moment     ***
             * eleForces[11] .. Mz on node 2 == torsion
             */

            Element *theEle = theDomain.getElement(i+elemIDoffset[pileIdx]);
            const Vector &eleForces = theEle->getResistingForce();
            (*moment[pileIdx])[i] = eleForces(10);
            if ((*moment[pileIdx])[i] > maxMoment) maxMoment = moment[pileIdx]->value(i);
            if ((*moment[pileIdx])[i] < minMoment) minMoment = moment[pileIdx]->value(i);
            (*shear[pileIdx])[i] = eleForces(6);
            if ((*shear[pileIdx])[i] > maxShear) maxShear = shear[pileIdx]->value(i);
            if ((*shear[pileIdx])[i] < minShear) minShear = shear[pileIdx]->value(i);
            (*axial[pileIdx])[i] = eleForces(8);
            if ((*axial[pileIdx])[i] > maxAxial) maxAxial = axial[pileIdx]->value(i);
            if ((*axial[pileIdx])[i] < minAxial) minAxial = axial[pileIdx]->value(i);
        }
    }

    //
    // plot results
    //

    //qDebug() << numNodePile[0] << numNodePile[1] << numNodePile[2] ;

    // lateral displacements
    if (showDisplacements) {
        displPlot->plotResults(Hdisps, loc);
    }

    // axial displacements
    if (showPullOut) {
        pullOutPlot->plotResults(Vdisps, loc);
    }

    // axial
    if (showAxial) {
        axialPlot->plotResults(axial, loc);
    }

    // shear
    if (showShear) {
        shearPlot->plotResults(shear, loc);
    }

    // moments
    if (showMoments) {
        momentPlot->plotResults(moment, loc);
    }

    // vertical stress
    if (showStress) {
        stressPlot->plotResults(stress, loc);
    }

    // p_ultimate
    if (showPultimate) {
        pultPlot->plotResults(pultList, locList);
    }

    // y_50
    if (showY50) {
        y50Plot->plotResults(y50List, locList);
    }

    for (int i=0; i<numPiles; i++)
    {
        if (loc[i]      != NULL) { delete loc[i];      loc[i]      = NULL; }
        if (Hdisps[i]   != NULL) { delete Hdisps[i];   Hdisps[i]   = NULL; }
        if (Vdisps[i]   != NULL) { delete Vdisps[i];   Vdisps[i]   = NULL; }
        if (moment[i]   != NULL) { delete moment[i];   moment[i]   = NULL; }
        if (shear[i]    != NULL) { delete shear[i];    shear[i]    = NULL; }
        if (axial[i]    != NULL) { delete axial[i];    axial[i]    = NULL; }
        if (stress[i]   != NULL) { delete stress[i];   stress[i]   = NULL; }

        if (locList[i]  != NULL) { delete locList[i];  locList[i]  = NULL; }
        if (pultList[i] != NULL) { delete pultList[i]; pultList[i] = NULL; }
        if (y50List[i]  != NULL) { delete y50List[i];  y50List[i]  = NULL; }
    }

    /*
     *
    //
    // the following lines will become necessary once those vectors become member variables
    //
    loc.clear();
    Hdisps.clear();
    Vdisps.clear();
    moment.clear();
    shear.clear();
    axial.clear();
    stress.clear();

    locList.clear();
    pultList.clear();
    y50List.clear();
    *
    */

}

void MainWindow::updateResultPlots()
{
    // this should call the results update from th eanalysis modeler and the plot methods
}

void MainWindow::fetchSettings()
{
    if (settings != NULL) { delete settings; }
    settings = new QSettings("NHERI SimCenter","Pile Group Tool");

    // general settings
    settings->beginGroup("general");
        useGraphicsLib    = settings->value("graphicsLibrary", QString("QCP")).toString();
        useFEAnalyzer     = settings->value("femAnalyzer", QString("OpenSeesInt")).toString();
    settings->endGroup();

    // viewer settings
    settings->beginGroup("viewer");
        showDisplacements = settings->value("displacements",1).toBool();
        showPullOut       = settings->value("pullout",1).toBool();
        showMoments       = settings->value("moments",1).toBool();
        showShear         = settings->value("shear",1).toBool();
        showAxial         = settings->value("axial",1).toBool();
        showStress        = settings->value("stress",1).toBool();
        showPultimate     = settings->value("pult",1).toBool();
        showY50           = settings->value("compliance",1).toBool();
    settings->endGroup();

    // meshing parameters
    settings->beginGroup("fea");
        minElementsPerLayer = settings->value("minElemementsPerLayer",15).toInt();
        maxElementsPerLayer = settings->value("maxElemementsPerLayer",40).toInt();
        numElementsInAir    = settings->value("numElementsInAir", 4).toInt();
    settings->endGroup();

    /****** end of settings ******/
}

/* ***** menu actions ***** */

void MainWindow::on_actionExit_triggered()
{
    this->close();
}

/* ***** check box status changes ***** */

void MainWindow::on_chkBox_assume_rigid_cap_clicked(bool checked)
{
    assumeRigidPileHeadConnection = checked;

    this->doAnalysis();
}

void MainWindow::on_chkBox_include_toe_resistance_clicked(bool checked)
{
    useToeResistance = checked;

    this->doAnalysis();
}

//
// UI helper functions
//

void MainWindow::setupLayers()
{
    mSoilLayers.clear();
    mSoilLayers.push_back(soilLayer("Layer 1", 3.0, 15.0, 18.0, 2.0e5, 30, 0.0, QColor(100,0,0,100)));
    mSoilLayers.push_back(soilLayer("Layer 2", 3.0, 16.0, 19.0, 2.0e5, 35, 0.0, QColor(0,100,0,100)));
    mSoilLayers.push_back(soilLayer("Layer 3", 4.0, 14.0, 17.0, 2.0e5, 28, 0.0, QColor(0,0,100,100)));

    updateLayerState();
}

void MainWindow::updateLayerState()
{
    // set depth and GWT depth for each layer
    int numLayers = mSoilLayers.size();

    double layerDepthFromSurface = 0.0;
    for (int ii = 0; ii < numLayers; ii++)
    {
        mSoilLayers[ii].setLayerDepth(layerDepthFromSurface);
        mSoilLayers[ii].setGWTdepth(gwtDepth - layerDepthFromSurface);
        layerDepthFromSurface += mSoilLayers[ii].getLayerThickness();
        if (ii > 0)
            mSoilLayers[ii].setLayerTopStress(mSoilLayers[ii-1].getLayerBottomStress());
    }
}

void MainWindow::updateUI()
{
    if (!showDisplacements && ui->tabWidget->indexOf(ui->dispTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->dispTab));
    }
    if (!showPullOut && ui->tabWidget->indexOf(ui->pulloutTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->pulloutTab));
    }
    if (!showMoments && ui->tabWidget->indexOf(ui->momentTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->momentTab));
    }
    if (!showShear && ui->tabWidget->indexOf(ui->shearTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->shearTab));
    }
    if (!showAxial && ui->tabWidget->indexOf(ui->axialTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->axialTab));
    }
    if (!showStress && ui->tabWidget->indexOf(ui->stressTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->stressTab));
    }
    if (!showPultimate && ui->tabWidget->indexOf(ui->pultTab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->pultTab));
    }
    if (!showY50 && ui->tabWidget->indexOf(ui->y50Tab)>=0 ) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->y50Tab));
    }

    int numTabs = ui->tabWidget->count();

    if (showDisplacements && ui->tabWidget->indexOf(ui->dispTab) < 0 ) {
        ui->tabWidget->addTab(ui->dispTab,"Lateral Disp");
    }
    if (showPullOut && ui->tabWidget->indexOf(ui->pulloutTab) < 0 ) {
        ui->tabWidget->addTab(ui->pulloutTab,"Axial Disp");
    }
    if (showMoments && ui->tabWidget->indexOf(ui->momentTab) < 0 ) {
        ui->tabWidget->addTab(ui->momentTab,"Moment");
    }
    if (showShear && ui->tabWidget->indexOf(ui->shearTab) < 0 ) {
        ui->tabWidget->addTab(ui->shearTab,"Shear");
    }
    if (showAxial && ui->tabWidget->indexOf(ui->axialTab) < 0 ) {
        ui->tabWidget->addTab(ui->axialTab,"Axial");
    }
    if (showStress && ui->tabWidget->indexOf(ui->stressTab) < 0 ) {
        ui->tabWidget->addTab(ui->stressTab,"Stress");
    }
    if (showPultimate && ui->tabWidget->indexOf(ui->pultTab) < 0 ) {
        ui->tabWidget->addTab(ui->pultTab,"p_ult");
    }
    if (showY50 && ui->tabWidget->indexOf(ui->y50Tab) < 0) {
        ui->tabWidget->addTab(ui->y50Tab,"y50");
    }
}

//
// menu actions
//

void MainWindow::on_actionNew_triggered()
{
    this->on_actionReset_triggered();
}

void MainWindow::on_action_Open_triggered()
{
    if ( this->ReadFile("PileTool.json") ) {
        this->refreshUI();
        this->updateSystemPlot();
        this->doAnalysis();
    }
}

void MainWindow::on_actionSave_triggered()
{
    this->WriteFile("PileTool.json");
}

void MainWindow::on_actionExport_to_OpenSees_triggered()
{
    DialogFutureFeature *dlg = new DialogFutureFeature();
    dlg->exec();
    delete dlg;
}

void MainWindow::on_actionReset_triggered()
{
    inSetupState = true;

    this->fetchSettings();
    this->updateUI();

    // setup data
    numPiles = 1;
    P        = 1000.0;
    PV       =    0.0;
    PMom     =    0.0;

    HDisp = 0.0; // prescribed horizontal displacement
    VDisp = 0.0; // prescriber vertical displacement

    surfaceDisp    = 0.0; // prescribed soil surface displacement
    percentage12   = 1.0; // percentage of surface displacement at 1st layer interface
    percentage23   = 0.0; // percentage of surface displacement at 2nd layer interface
    percentageBase = 0.0; // percentage of surface displacement at base of soil column

    L1                       = 1.0;
    L2[numPiles-1]           = 20.0;
    pileDiameter[numPiles-1] = 1.0;
    E[numPiles-1]            = 25.0e6;
    xOffset[numPiles-1]      = 0.0;

    gwtDepth = 4.00;
    gSoil    = 150000;
    puSwitch = 2;
    kSwitch  = 1;
    gwtSwitch= 1;

     // set initial state of check boxes
    useToeResistance              = false;
    assumeRigidPileHeadConnection = false;

    // meshing parameters
    minElementsPerLayer = MIN_ELEMENTS_PER_LAYER;
    maxElementsPerLayer = MAX_ELEMENTS_PER_LAYER;
    numElementsInAir    = NUM_ELEMENTS_IN_AIR;

    // set up initial values before activating live analysis (= connecting the slots)
    //    or the program will fail in the analysis due to missing information
    setupLayers();

    //reDrawTable();

    setActiveLayer(0);

    // set up pile input

    ui->pileIndex->setValue(1);
    ui->pileIndex->setMinimum(1);
    ui->pileIndex->setMaximum(numPiles);

    int pileIdx = ui->pileIndex->value() - 1;

    ui->appliedHorizontalForce->setValue(P);
    ui->xOffset->setValue(xOffset[pileIdx]);
    ui->pileDiameter->setValue(pileDiameter[pileIdx]);
    ui->freeLength->setValue(L1);
    ui->embeddedLength->setValue(L2[pileIdx]);
    ui->Emodulus->setValue( (E[pileIdx]/1.0e+6) );

    ui->groundWaterTable->setValue(gwtDepth);

    ui->chkBox_assume_rigid_cap->setCheckState(assumeRigidPileHeadConnection?Qt::Checked:Qt::Unchecked);
    ui->chkBox_include_toe_resistance->setCheckState(useToeResistance?Qt::Checked:Qt::Unchecked);

    // plotsetting
    activePileIdx = 0;
    activeLayerIdx = -1;

    inSetupState = false;

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_actionFEA_parameters_triggered()
{
    this->on_actionPreferences_triggered();
}


void MainWindow::on_action_About_triggered()
{
    DialogAbout *dlg = new DialogAbout();
    dlg->exec();
    delete dlg;
}

void MainWindow::on_actionPreferences_triggered()
{
    DialogPreferences *dlg = new DialogPreferences(this, settings);
    dlg->exec();
    delete dlg;

    this->fetchSettings();
    this->updateUI();
    this->doAnalysis();
}

//
// SLOTS (call-back functions)
//

void MainWindow::on_btn_deletePile_clicked()
{
    int pileIdx = ui->pileIndex->value() - 1;
    if (pileIdx < numPiles && numPiles > 1) {
        if ( pileIdx < numPiles ) {
            for (int j=pileIdx+1; j<numPiles; j++) {
                L2[j-1]           = L2[j];
                pileDiameter[j-1] = pileDiameter[j];
                E[j-1]            = E[j];
                xOffset[j-1]      = xOffset[j];
            }
        }
        numPiles--;
        while (pileIdx >= numPiles) pileIdx--;

        ui->pileIndex->setMaximum(numPiles);
        ui->pileIndex->setValue(pileIdx+1);

        this->updateSystemPlot();
        this->doAnalysis();
    }
}

void MainWindow::on_btn_newPile_clicked()
{
    if (numPiles < MAXPILES) {
        L2[numPiles]           = L2[numPiles-1];
        pileDiameter[numPiles] = pileDiameter[numPiles-1];
        E[numPiles]            = E[numPiles-1];
        xOffset[numPiles]      = xOffset[numPiles-1] + 2.0*pileDiameter[numPiles-1];
        numPiles++;

        systemPlot->setActivePile(numPiles);
    }
    else
    {
        QMessageBox msg(QMessageBox::Information,"Info","Maximum number of piles reached.");
        msg.exec();
    }

    ui->pileIndex->setMaximum(numPiles);
    ui->pileIndex->setValue(numPiles);

    //this->updateSystemPlot();
    this->doAnalysis();
}

void MainWindow::on_xOffset_valueChanged(double arg1)
{
    int pileIdx = ui->pileIndex->value() - 1;
    xOffset[pileIdx] = ui->xOffset->value();

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_pileIndex_valueChanged(int arg1)
{
    int pileIdx = ui->pileIndex->value() - 1;

    ui->pileDiameter->setValue(pileDiameter[pileIdx]);
    ui->Emodulus->setValue( (E[pileIdx]/1.0e+6) );
    ui->embeddedLength->setValue(L2[pileIdx]);
    ui->freeLength->setValue(L1);
    ui->xOffset->setValue(xOffset[pileIdx]);

    // refresh graphics

    activePileIdx  = pileIdx;
    activeLayerIdx = -1;

    this->updateSystemPlot();
}

/* ***** pile parameter changes ***** */

void MainWindow::on_pileDiameter_valueChanged(double arg1)
{
    int pileIdx = ui->pileIndex->value() - 1;

    pileDiameter[pileIdx] = arg1;
    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_embeddedLength_valueChanged(double arg1)
{
    int pileIdx = ui->pileIndex->value() - 1;

    L2[pileIdx] = arg1;
    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_freeLength_valueChanged(double arg1)
{
    L1 = arg1;
    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_Emodulus_valueChanged(double arg1)
{
    int pileIdx = ui->pileIndex->value() - 1;

    E[pileIdx] = arg1*1.0e6;
    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_groundWaterTable_valueChanged(double arg1)
{
    gwtDepth = arg1;
    this->doAnalysis();
    this->updateSystemPlot();
}


//
// layer properties and setup methods
//

void MainWindow::setActiveLayer(int layerIdx)
{
    ui->chkBox_layer1->setChecked((layerIdx==0));
    ui->chkBox_layer2->setChecked((layerIdx==1));
    ui->chkBox_layer3->setChecked((layerIdx==2));

    inSetupState = true;  // temporary deactivate live recomputation

    ui->layerThickness->setValue(mSoilLayers[layerIdx].getLayerThickness());
    ui->layerDryWeight->setValue(mSoilLayers[layerIdx].getLayerUnitWeight());
    ui->layerSaturatedWeight->setValue(mSoilLayers[layerIdx].getLayerSatUnitWeight());
    ui->layerFrictionAngle->setValue(mSoilLayers[layerIdx].getLayerFrictionAng());
    ui->layerShearModulus->setValue((mSoilLayers[layerIdx].getLayerStiffness()/1.e3));

    inSetupState = false;

    activePileIdx  = -1;
    activeLayerIdx = layerIdx;

    this->updateSystemPlot();
}

void MainWindow::on_chkBox_layer1_clicked()
{
    setActiveLayer(0);
}

void MainWindow::on_chkBox_layer2_clicked()
{
    setActiveLayer(1);
}

void MainWindow::on_chkBox_layer3_clicked()
{
    setActiveLayer(2);
}

int MainWindow::findActiveLayer()
{
    int layerIdx = -1;
    if (ui->chkBox_layer1->checkState() == Qt::Checked) layerIdx = 0;
    if (ui->chkBox_layer2->checkState() == Qt::Checked) layerIdx = 1;
    if (ui->chkBox_layer3->checkState() == Qt::Checked) layerIdx = 2;

    activePileIdx  = -1;
    activeLayerIdx = layerIdx;

    return layerIdx;
}

void MainWindow::on_layerThickness_valueChanged(double arg1)
{
    double val;
    int layerIdx = findActiveLayer();

    if (arg1 < 0.10) {
        val = 0.10;
        ui->layerThickness->setValue(val);
    }
    else {
        val = arg1;
    }
    if (layerIdx >= 0) {
        mSoilLayers[layerIdx].setLayerThickness(val);
        this->updateLayerState();
        this->doAnalysis();
        this->updateSystemPlot();
    }
}

void MainWindow::on_layerDryWeight_valueChanged(double arg1)
{
    double val;
    int layerIdx = findActiveLayer();

    if (arg1 < 0.5*GAMMA_WATER) {
        val = 0.5*GAMMA_WATER;
        ui->layerDryWeight->setValue(val);
    }
    else {
        val = arg1;
    }
    if (layerIdx >= 0) {
        mSoilLayers[layerIdx].setLayerUnitWeight(val);
        this->updateLayerState();
        this->doAnalysis();
        this->updateSystemPlot();
    }
}

void MainWindow::on_layerSaturatedWeight_valueChanged(double arg1)
{
    double val;
    int layerIdx = findActiveLayer();

    if (arg1 < GAMMA_WATER) {
        val = GAMMA_WATER;
        ui->layerSaturatedWeight->setValue(val);
    }
    else {
        val = arg1;
    }
    if (layerIdx >= 0) {
        mSoilLayers[layerIdx].setLayerSatUnitWeight(val);
        this->updateLayerState();
        this->doAnalysis();
        this->updateSystemPlot();
    }
}

void MainWindow::on_layerFrictionAngle_valueChanged(double arg1)
{
    double val;
    int layerIdx = findActiveLayer();

    if (arg1 < 1.0) {
        val = 1.0;
        ui->layerFrictionAngle->setValue(val);
    }
    else {
        val = arg1;
    }
    if (layerIdx >= 0) {
        mSoilLayers[layerIdx].setLayerFrictionAng(val);
        this->updateLayerState();
        this->doAnalysis();
        this->updateSystemPlot();
    }
}

void MainWindow::on_layerShearModulus_valueChanged(double arg1)
{
    double val;
    int layerIdx = findActiveLayer();

    if (arg1 < 1.0) {
        val = 1.0e3;
        ui->layerShearModulus->setValue(1.0);
    }
    else {
        val = arg1*1.0e3;
    }
    if (layerIdx >= 0) {
        mSoilLayers[layerIdx].setLayerStiffness( (val) );
        this->updateLayerState();
        this->doAnalysis();
        this->updateSystemPlot();
    }
}

int  MainWindow::adjustLayersToPiles()
{
    return 0;
}

void MainWindow::on_properties_currentChanged(int index)
{
    switch (index) {
    case 0:
        activePileIdx  = ui->pileIndex->value() - 1;
        activeLayerIdx = -1;
        break;
    case 1:
        activePileIdx  = -1;
        this->findActiveLayer();
        break;
    default:
        activePileIdx  = -1;
        activeLayerIdx = -1;
    }

    this->updateSystemPlot();
}

void MainWindow::updateSystemPlot()
{
    if (inSetupState) return;

    //
    // create pile information for plotting
    //
    QVector<PILE_INFO> pileInfo;

    pileInfo.clear();

    for (int i=0; i<numPiles; i++)
    {
        PILE_INFO thisPile;
        thisPile.L1           = L1;
        thisPile.L2           = L2[i];
        thisPile.pileDiameter = pileDiameter[i];
        thisPile.xOffset      = xOffset[i];
        thisPile.E            = E[i];
        pileInfo.append(thisPile);
    }
    systemPlot->updatePiles(pileInfo);

    //
    // create list of layer interface positions
    //
    this->updateLayerState();

    QVector<double> layerDepth(MAXLAYERS+1, 0.00);
    for (int i=1; i<=MAXLAYERS; i++)
    {
        if (mSoilLayers.size() >= i)
            { layerDepth[i] = mSoilLayers[i-1].getLayerDepth() + mSoilLayers[i-1].getLayerThickness(); }
        else
            { layerDepth[i] = layerDepth[i-1]; }
    }
    systemPlot->updateSoil(layerDepth);

    //
    // update ground water table
    //
    systemPlot->updateGWtable(gwtDepth);

    //
    // set the load control type
    //
    systemPlot->setLoadType(loadControlType);

    //
    // update the load control data
    //
    switch (loadControlType) {
    case LoadControlType::ForceControl:
        systemPlot->updateLoad(P, PV, PMom);
        break;
    case LoadControlType::PushOver:
        systemPlot->updateDisplacement(HDisp, VDisp);
        break;
    case LoadControlType::SoilMotion:
        QVector<double> profile;
        profile.append(surfaceDisp);
        profile.append(percentage12);
        profile.append(percentage23);
        profile.append(percentageBase);
        systemPlot->updateDispProfile(profile);
        break;
    }

    //
    // refresh the plot
    //
    systemPlot->refresh();
}

void MainWindow::on_layerSelectedInSystemPlot(bool selected)
{
    qDebug() << "on_layerSelectedInSystemPlot(" << selected << ") triggered";
}


void MainWindow::onSystemPlot_pileSelected(int index)
{
    activePileIdx  = index;
    activeLayerIdx = -1;

    // make pile controls visible
    ui->properties->setCurrentWidget(ui->pilePropertiesWidget);
    ui->pileIndex->setValue(index+1);
}

void MainWindow::onSystemPlot_soilLayerSelected(int index)
{
    activePileIdx  = -1;
    activeLayerIdx = index;

    // make soil layer controls visible
    ui->properties->setCurrentWidget(ui->soilPropertiesWidget);
    setActiveLayer(index);
}

void MainWindow::onSystemPlot_groundWaterSelected()
{
    // make groundwater settings visible:
    ui->properties->setCurrentWidget(ui->soilPropertiesWidget);
}

void MainWindow::on_actionLicense_Information_triggered()
{
    CopyrightDialog *dlg = new CopyrightDialog(this);
    dlg->exec();
}

void MainWindow::on_actionLicense_triggered()
{
    CopyrightDialog *dlg = new CopyrightDialog(this);
    dlg->exec();
}

void MainWindow::on_actionVersion_triggered()
{
    QMessageBox::about(this, tr("Version"),
                       tr("Version 1.0 "));
}

void MainWindow::on_actionProvide_Feedback_triggered()
{
    // QDesktopServices::openUrl(QUrl("https://github.com/NHERI-SimCenter/QtPile/issues", QUrl::TolerantMode));
  QDesktopServices::openUrl(QUrl("https://www.designsafe-ci.org/help/new-ticket/", QUrl::TolerantMode));

}

bool MainWindow::ReadFile(QString s)
{
    /* identify filename and location for loading */

    //QString filename = "PileGroupTool.json";

    QString theFolder = QDir::homePath();
    QString theFilter = "Json file (*.json)";
    QFileDialog dlg;

    QString filename = dlg.getOpenFileName(this, "Load file", theFolder, theFilter);

    qWarning() << filename;

    /* load JSON object from file */
    QFile loadFile;
    loadFile.setFileName(filename);

    if (!loadFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Couldn't open load file.");
        return false;
    }

    QString theFile = loadFile.readAll();
    loadFile.close();

    //qWarning() << theFile;

    bool fileTypeError = false;

    QJsonDocument infoDoc = QJsonDocument::fromJson(theFile.toUtf8());

    /* start a JSON object to represent the system */
    QJsonObject json = infoDoc.object();

    QString creator;
    creator  = json["creator"].toString();

    if (creator != "PileGroupTool") fileTypeError = true;

    QString version;
    version  = json["version"].toString();

    fileTypeError = true;
    if (version == "1.0")   fileTypeError = false;
    if (version == "1.99")  fileTypeError = false;
    if (version == "2.0")   fileTypeError = false;

    if (fileTypeError) {
        QMessageBox msg(QMessageBox::Information, "Info", "Not a valid model file.");
        msg.exec();
        return false;
    }

    QString username;
    username = json["username"].toString();
    QString author;
    author   = json["author"].toString();
    QString filedate;
    filedate = json["date"].toString();

    /* write layer information */
    QJsonArray layerInfo = json["layers"].toArray();

    int nLayer = 0;

    foreach (QJsonValue jval, layerInfo) {

        QJsonObject aLayer = jval.toObject();

        mSoilLayers[nLayer].setLayerDepth(aLayer["depth"].toDouble());
        mSoilLayers[nLayer].setLayerThickness(aLayer["thickness"].toDouble());
        mSoilLayers[nLayer].setLayerUnitWeight(aLayer["gamma"].toDouble());
        mSoilLayers[nLayer].setLayerSatUnitWeight(aLayer["gammaSaturated"].toDouble());
        mSoilLayers[nLayer].setLayerFrictionAng(aLayer["phi"].toDouble());
        mSoilLayers[nLayer].setLayerCohesion(aLayer["cohesion"].toDouble());
        mSoilLayers[nLayer].setLayerStiffness(aLayer["Gmodulus"].toDouble());

        nLayer++;
        if (nLayer >= MAXLAYERS)  break;
    }

    while (nLayer+1 < MAXLAYERS) {
        /* fill layer array with identical layer properties */
        mSoilLayers[nLayer+1].setLayerDepth(mSoilLayers[nLayer].getLayerDepth());
        mSoilLayers[nLayer+1].setLayerThickness(mSoilLayers[nLayer].getLayerThickness());
        mSoilLayers[nLayer+1].setLayerUnitWeight(mSoilLayers[nLayer].getLayerUnitWeight());
        mSoilLayers[nLayer+1].setLayerSatUnitWeight(mSoilLayers[nLayer].getLayerSatUnitWeight());
        mSoilLayers[nLayer+1].setLayerFrictionAng(mSoilLayers[nLayer].getLayerFrictionAng());
        mSoilLayers[nLayer+1].setLayerCohesion(mSoilLayers[nLayer].getLayerCohesion());
        mSoilLayers[nLayer+1].setLayerStiffness(mSoilLayers[nLayer].getLayerStiffness());

        nLayer++;
    }

    double groundWaterTable = json["groundWaterTable"].toDouble();
    if (groundWaterTable < 0.0) groundWaterTable = 0.0;
    gwtDepth = groundWaterTable;

    /* write pile information */
    QJsonArray pileInfo = json["piles"].toArray();

    int nPile = 0;

    foreach (QJsonValue jval, pileInfo) {
        QJsonObject aPile = jval.toObject();

        L2[nPile]           = aPile["embeddedLength"].toDouble();
        L1                  = aPile["freeLength"].toDouble();
        pileDiameter[nPile] = aPile["diameter"].toDouble();
        E[nPile]            = aPile["YoungsModulus"].toDouble();
        xOffset[nPile]      = aPile["xOffset"].toDouble();

        nPile++;
        if (nPile >= MAXPILES) break;
    }

    numPiles = nPile;

    useToeResistance              = json["useToeResistance"].toBool();
    assumeRigidPileHeadConnection = json["assumeRigidPileHeadConnection"].toBool();


    /* read load information */
    if (version == "1.0")
    {
        QJsonObject loadInfo = json["loads"].toObject();

        P    = loadInfo["HForce"].toDouble();
        PV   = loadInfo["VForce"].toDouble();
        PMom = loadInfo["Moment"].toDouble();

        HDisp = 0.0;
        VDisp = 0.0;

        surfaceDisp  = 0.0;
        percentage12 = 100.;
        percentage23 = 0.0;
        percentageBase = 0.0;

        loadControlType = LoadControlType::ForceControl;
    }
    else if (version == "1.99" || version == "2.0")
    {
        QJsonObject loadInfo = json["loads"].toObject();
        QJsonObject forceControlObj = loadInfo["forceControl"].toObject();
        QJsonObject pushOverObj     = loadInfo["pushOver"].toObject();
        QJsonObject soilMotionObj   = loadInfo["soilMotion"].toObject();

        QString loadType = loadInfo["loadControlType"].toString();

        if (loadType.toLower() == "forcecontrol")
            { loadControlType = LoadControlType::ForceControl; }
        else if (loadType.toLower() == "pushover")
            { loadControlType = LoadControlType::PushOver; }
        else if (loadType.toLower() == "soilmotion")
            { loadControlType = LoadControlType::SoilMotion; }
        else
            { loadControlType = LoadControlType::ForceControl; }

        P    = forceControlObj["HForce"].toDouble();
        PV   = forceControlObj["VForce"].toDouble();
        PMom = forceControlObj["Moment"].toDouble();

        HDisp = pushOverObj["HDisp"].toDouble();
        VDisp = pushOverObj["VDisp"].toDouble();

        surfaceDisp    = soilMotionObj["surfaceDisp"].toDouble();
        percentage12   = soilMotionObj["percentage12"].toDouble();
        percentage23   = soilMotionObj["percentage23"].toDouble();
        percentageBase = soilMotionObj["percentageBase"].toDouble();
    }

    /* FEA parameters */
    QJsonObject FEAparameters = json["FEAparameters"].toObject();

    minElementsPerLayer = FEAparameters["minElementsPerLayer"].toInt();
    maxElementsPerLayer = FEAparameters["maxElementsPerLayer"].toInt();
    numElementsInAir    = FEAparameters["numElementsInAir"].toInt();

    if (minElementsPerLayer < MIN_ELEMENTS_PER_LAYER)   minElementsPerLayer = MIN_ELEMENTS_PER_LAYER;
    if (maxElementsPerLayer > 3*MAX_ELEMENTS_PER_LAYER) maxElementsPerLayer = 3*MAX_ELEMENTS_PER_LAYER;
    if (numElementsInAir < NUM_ELEMENTS_IN_AIR)         numElementsInAir = NUM_ELEMENTS_IN_AIR;
    if (numElementsInAir > MAX_ELEMENTS_PER_LAYER)      numElementsInAir = MAX_ELEMENTS_PER_LAYER;

    return true;
}

bool MainWindow::WriteFile(QString s)
{
    /* identify filename and location for saving */

    QString path = QDir::homePath();
    QDir d;
    d.setPath(path);
    QString filename = d.filePath("PileGroupTool.json");
    QString theFilter = "Json file (*.json)";
    QFileDialog dlg;

    filename = dlg.getSaveFileName(this, "Save file", filename, theFilter );

    // check if cancelled
    if (filename.isEmpty()) return false;

    /* start a JSON object to represent the system */
    QJsonObject *json = new QJsonObject();

    json->insert("creator", QString("PileGroupTool"));
    json->insert("version", QString(APP_VERSION));
#ifdef Q_OS_WIN
    QString username = qgetenv("USERNAME");
#else
    QString username = qgetenv("USER");
#endif
    json->insert("author", username);
    json->insert("date", QDateTime::currentDateTime().toString());

    /* write layer information */
    QJsonArray *layerInfo = new QJsonArray();
    for (int lid=0; lid<MAXLAYERS; lid++) {
        QJsonObject aLayer;

        aLayer.insert("depth", mSoilLayers[lid].getLayerDepth());
        aLayer.insert("thickness", mSoilLayers[lid].getLayerThickness());
        aLayer.insert("gamma", mSoilLayers[lid].getLayerUnitWeight());
        aLayer.insert("gammaSaturated", mSoilLayers[lid].getLayerSatUnitWeight());
        aLayer.insert("phi", mSoilLayers[lid].getLayerFrictionAng());
        aLayer.insert("cohesion", mSoilLayers[lid].getLayerCohesion());
        aLayer.insert("Gmodulus", mSoilLayers[lid].getLayerStiffness());

        layerInfo->append(aLayer);
    }

    json->insert("layers", *layerInfo);

    json->insert("groundWaterTable", gwtDepth);

    /* write pile information */
    QJsonArray *pileInfo = new QJsonArray();
    for (int pid=0; pid<numPiles; pid++) {
        QJsonObject aPile;

        aPile.insert("embeddedLength", L2[pid]);
        aPile.insert("freeLength", L1);
        aPile.insert("diameter", pileDiameter[pid]);
        aPile.insert("YoungsModulus", E[pid]);
        aPile.insert("xOffset", xOffset[pid]);

        pileInfo->append(aPile);
    }

    json->insert("piles", *pileInfo);

    json->insert("useToeResistance", useToeResistance);
    json->insert("assumeRigidPileHeadConnection", assumeRigidPileHeadConnection);


    /* write load information */
    QJsonObject *loadInfo = new QJsonObject();

    int selection = ui->forceTypeSelector->currentIndex();
    QString loadType;

    switch (selection)
    {
    case 0:
        loadType = "forceControl";
        break;
    case 1:
        loadType = "pushOver";
        break;
    case 2:
        loadType = "soilMotion";
        break;
    default:
        qWarning() << "cannot identify the loadControlType";
    }

    loadInfo->insert("loadControlType", loadType);

    QJsonObject *forceControlObj = new QJsonObject();
    forceControlObj->insert("HForce", P);
    forceControlObj->insert("VForce", PV);
    forceControlObj->insert("Moment", PMom);
    loadInfo->insert("forceControl", *forceControlObj);

    QJsonObject *pushOverObj     = new QJsonObject();
    pushOverObj->insert("HDisp", HDisp);
    pushOverObj->insert("VDisp", VDisp);
    loadInfo->insert("pushOver", *pushOverObj);

    QJsonObject *soilMotionObj   = new QJsonObject();
    soilMotionObj->insert("surfaceDisp", surfaceDisp);
    soilMotionObj->insert("percentage12", percentage12);
    soilMotionObj->insert("percentage23", percentage23);
    soilMotionObj->insert("percentageBase", percentageBase);
    loadInfo->insert("soilMotion", *soilMotionObj);

    json->insert("loads", *loadInfo);

    /* FEA parameters */
    QJsonObject FEAparameters;

    FEAparameters.insert("minElementsPerLayer",minElementsPerLayer);
    FEAparameters.insert("maxElementsPerLayer",maxElementsPerLayer);
    FEAparameters.insert("numElementsInAir", numElementsInAir);

    json->insert("FEAparameters", FEAparameters);

    QJsonDocument infoDoc = QJsonDocument(*json);

    /* write JSON object to file */

    QFile saveFile( filename );

    if (!saveFile.open(QIODevice::WriteOnly)) {
        qWarning("Couldn't open save file.");
        return false;
    }

    saveFile.write( infoDoc.toJson() );
    saveFile.close();

    // clean up
    delete layerInfo;
    delete pileInfo;
    delete loadInfo;
    delete forceControlObj;
    delete pushOverObj;
    delete soilMotionObj;
    delete json;

    return true;
}


void MainWindow::replyFinished(QNetworkReply *pReply)
{
    return;
}



void MainWindow::on_forceTypeSelector_activated(int index)
{
    ui->loadTypesStack->setCurrentIndex(index);
}


/* ***** loading parameter changes ***** */

void MainWindow::on_appliedHorizontalForce_editingFinished()
{
    P = ui->appliedHorizontalForce->value();

    int sliderPosition = nearbyint(100.*P/MAX_FORCE);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->horizontalForceSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_horizontalForceSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    P = MAX_FORCE * sliderRatio;

    ui->appliedHorizontalForce->setValue(P);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_appliedVerticalForce_editingFinished()
{
    PV = ui->appliedHorizontalForce->value();

    int sliderPosition = nearbyint(100.*PV/MAX_FORCE);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->verticalForceSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_verticalForceSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    PV = MAX_FORCE * sliderRatio;

    ui->appliedVerticalForce->setValue(PV);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_appliedMoment_editingFinished()
{
    PMom = ui->appliedMoment->value();

    int sliderPosition = nearbyint(100.*PMom/MAX_MOMENT);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->momentSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_momentSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    PMom = MAX_MOMENT * sliderRatio;

    ui->appliedMoment->setValue(PMom);

    this->doAnalysis();
    this->updateSystemPlot();
}





void MainWindow::on_pushoverDisplacement_editingFinished()
{
    HDisp = ui->pushoverDisplacement->value();

    int sliderPosition = nearbyint(100.*HDisp/MAX_DISP);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->pushoverDisplacementSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_pushoverDisplacementSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    HDisp = MAX_DISP * sliderRatio;

    ui->pushoverDisplacement->setValue(HDisp);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_pulloutDisplacement_editingFinished()
{
    VDisp = ui->pulloutDisplacement->value();

    int sliderPosition = nearbyint(100.*VDisp/MAX_DISP);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->pulloutDisplacementSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_pulloutDisplacementSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    VDisp = MAX_DISP * sliderRatio;

    ui->pulloutDisplacement->setValue(VDisp);

    this->doAnalysis();
    this->updateSystemPlot();
}






void MainWindow::on_surfaceDisplacement_editingFinished()
{
    surfaceDisp = ui->surfaceDisplacement->value();

    int sliderPosition = nearbyint(100.*surfaceDisp/MAX_DISP);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->surfaceDisplacementSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_surfaceDisplacementSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    surfaceDisp = MAX_DISP * sliderRatio;

    ui->surfaceDisplacement->setValue(surfaceDisp);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_Interface12_editingFinished()
{
    percentage12 = ui->Interface12->value();

    int sliderPosition = nearbyint(percentage12);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->Interface12Slider->setSliderPosition(sliderPosition);
}

void MainWindow::on_Interface12Slider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    percentage12 = sliderRatio;

    ui->Interface12->setValue(percentage12);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_Interface23_editingFinished()
{
    percentage23 = ui->Interface23->value();

    int sliderPosition = nearbyint(percentage23);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->Interface23Slider->setSliderPosition(sliderPosition);
}

void MainWindow::on_Interface23Slider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    percentage23 = sliderRatio;

    ui->Interface23->setValue(percentage23);

    this->doAnalysis();
    this->updateSystemPlot();
}

void MainWindow::on_BaseDisplacement_editingFinished()
{
    percentageBase = ui->BaseDisplacement->value();

    int sliderPosition = nearbyint(percentageBase);
    if (sliderPosition >  100) sliderPosition= 100;
    if (sliderPosition < -100) sliderPosition=-100;

    ui->BaseDisplacementSlider->setSliderPosition(sliderPosition);
}

void MainWindow::on_BaseDisplacementSlider_valueChanged(int value)
{
    double sliderRatio;

    // slider moved -- the number of steps (100) is a parameter to the slider in mainwindow.ui
    sliderRatio = double(value)/100.0;

    percentageBase = sliderRatio;

    ui->BaseDisplacement->setValue(percentageBase);

    this->doAnalysis();
    this->updateSystemPlot();
}