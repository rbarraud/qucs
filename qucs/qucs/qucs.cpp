/***************************************************************************
                                 qucs.cpp
                                ----------
    begin                : Thu Aug 28 2003
    copyright            : (C) 2003, 2004, 2005, 2006 by Michael Margraf
    email                : michael.margraf@alumni.tu-berlin.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <limits.h>

#include <qaccel.h>
#include <qimage.h>
#include <qsplitter.h>
#include <qvbox.h>
#include <qhbox.h>
#include <qlabel.h>
#include <qmessagebox.h>
#include <qdir.h>
#include <qpainter.h>
#include <qfiledialog.h>
#include <qinputdialog.h>
#include <qclipboard.h>
#include <qfont.h>
#include <qtextedit.h>
#include <qcheckbox.h>

#include "main.h"
#include "qucs.h"
#include "qucsview.h"
#include "wire.h"
#include "components/components.h"
#include "paintings/paintings.h"
#include "diagrams/diagrams.h"
#include "dialogs/messagebox.h"
#include "dialogs/newprojdialog.h"
#include "dialogs/settingsdialog.h"
#include "dialogs/qucssettingsdialog.h"
#include "dialogs/simmessage.h"
#include "dialogs/sweepdialog.h"



QDir QucsWorkDir;  // current project path
QDir QucsHomeDir;  // Qucs user directory where all projects are located


// IconView without dragging icon bitmap
class myIconView : public QIconView
{
public:
  myIconView(QWidget* parent_) : QIconView(parent_, 0, 0) {};
 ~myIconView() {};

protected:
  QDragObject *dragObject() {
    QIconViewItem *Item = currentItem();
    if(!Item) return 0;

    // no picture during dragging, but bounding rectangles in QListView
    QIconDrag *DragPic = new QIconDrag( viewport() );
    DragPic->setPixmap( QPixmap(empty_xpm), QPoint(0, 0) );
    DragPic->append( QIconDragItem(),
        QRect( Item->pixmapRect().width() / -2,
               Item->pixmapRect().height() / -2,
               Item->pixmapRect().width(), Item->pixmapRect().height() ),
        QRect( Item->textRect().width() / -2,
               Item->pixmapRect().height() / 2 + 5,
               Item->textRect().width(), Item->textRect().height() ) );
    return DragPic;
  };
};



QucsApp::QucsApp()
{
  setCaption("Qucs " PACKAGE_VERSION);

  QucsFileFilter = tr("Schematic")+" (*.sch);;"+
                   tr("Data Display")+" (*.dpl);;"+
		   tr("Qucs Documents")+" (*.sch *.dpl);;"+
		   tr("Any File")+" (*)";
  QucsWorkDir.setPath(QDir::homeDirPath()+QDir::convertSeparators ("/.qucs"));
  QucsHomeDir.setPath(QDir::homeDirPath()+QDir::convertSeparators ("/.qucs"));

  move  (QucsSettings.x,  QucsSettings.y);
  resize(QucsSettings.dx, QucsSettings.dy);
//  resize(maximumSize());

  initView();
  Init.perform(this);  // initializes toolbar, statusbar, menubar, actions
  Acts.init(this);
  initCursorMenu();


  // default settings of the printer
  Printer = new QPrinter(QPrinter::PrinterResolution);
  Printer->setOrientation(QPrinter::Landscape);
  Printer->setColorMode(QPrinter::Color);

  Acts.select->setOn(true);  // switch on the 'select' action
  HierarchyHistory.setAutoDelete(true);

  // creates a document called "untitled"
  view->Docs.append(new QucsDoc(this, ""));

  // load documents given as command line arguments
  for(int z=1; z<qApp->argc(); z++)
    if(*(qApp->argv()[z]) != '-')
      gotoPage(qApp->argv()[z]);
}

QucsApp::~QucsApp()
{
  delete Printer;
}


// #########################################################################
// ##########                                                     ##########
// ##########       Creates the view area (QTabWidget etc.)       ##########
// ##########                                                     ##########
// #########################################################################
void QucsApp::initView()
{
  // set application icon
  setIcon (QPixmap(QucsSettings.BitmapDir + "big.qucs.xpm"));
  QVBox *all = new QVBox(this);   // only to fill the entire view area
  QSplitter *Hsplit = new QSplitter(QSplitter::Horizontal, all);

  TabView  = new QTabWidget(Hsplit);    // tabs on the left side
  QVBox *WorkGroup = new QVBox(Hsplit);
  WorkView = new QTabBar(WorkGroup);    // tab on the right side
  view = new QucsView(WorkGroup);    // work area with documents

  connect(WorkView, SIGNAL(selected(int)), SLOT(slotChangeView(int)));

  Hsplit->setResizeMode(TabView, QSplitter::KeepSize);
  setCentralWidget(all);

  // ----------------------------------------------------------
  // "Project Tab" of the left QTabWidget
  QVBox *ProjGroup = new QVBox(this);
  QHBox *ProjButts = new QHBox(ProjGroup);
  QPushButton *ProjNew   = new QPushButton(tr("New"),ProjButts);
  connect(ProjNew, SIGNAL(clicked()), SLOT(slotProjNewButt()));
  QPushButton *ProjOpen  = new QPushButton(tr("Open"),ProjButts);
  connect(ProjOpen, SIGNAL(clicked()), SLOT(slotProjOpenButt()));
  QPushButton *ProjDel   = new QPushButton(tr("Delete"),ProjButts);
  connect(ProjDel, SIGNAL(clicked()), SLOT(slotProjDelButt()));

  Projects = new QListBox(ProjGroup);
  TabView->addTab(ProjGroup, tr("Projects"));
  TabView->setTabToolTip(TabView->page(0),
			 tr("content of the project directory"));

  connect(Projects, SIGNAL(doubleClicked(QListBoxItem*)),
		    SLOT(slotOpenProject(QListBoxItem*)));

  // ----------------------------------------------------------
  // "Content Tab" of the left QTabWidget
  Content = new QListView(this);
  Content->setRootIsDecorated(true); // open/close decoration for root items
  Content->setSorting(-1);    // no sorting
  Content->addColumn(tr("Content of"));
  Content->addColumn(tr("Note"));
  Content->setColumnWidthMode(0,QListView::Manual);
  Content->setColumnWidth(0, 150);

  ConOthers     = new QListViewItem(Content, tr("Others"));
  ConDatasets   = new QListViewItem(Content, tr("Datasets"));
  ConDisplays   = new QListViewItem(Content, tr("Data Displays"));
  ConSchematics = new QListViewItem(Content, tr("Schematics"));
  TabView->addTab(Content,tr("Content"));
  TabView->setTabToolTip(TabView->page(1), tr("content of the open project"));

  connect(Content, SIGNAL(doubleClicked(QListViewItem*)),
		   SLOT(slotOpenContent(QListViewItem*)));
  connect(Content, SIGNAL(clicked(QListViewItem*)),
		   SLOT(slotSelectSubcircuit(QListViewItem*)));

  // ----------------------------------------------------------
  // "Component Tab" of the left QTabWidget
  QVBox *CompGroup  = new QVBox(this);
  CompChoose = new QComboBox(CompGroup);
  CompComps  = new myIconView(CompGroup);
  TabView->addTab(CompGroup,tr("Components"));
  TabView->setTabToolTip(TabView->page(2), tr("components and diagrams"));
  fillComboBox(true);

  slotSetCompView(0);
  connect(CompChoose, SIGNAL(activated(int)), SLOT(slotSetCompView(int)));
  connect(CompComps, SIGNAL(clicked(QIconViewItem*)),
		     SLOT(slotSelectComponent(QIconViewItem*)));


  // ---------------------------------------------------------------------
  readProjects();   // reads all projects and inserts them into the ListBox
}

// ----------------------------------------------------------
void QucsApp::fillComboBox(bool setAll)
{
  CompChoose->clear();
  if(setAll) {
    CompChoose->insertItem(tr("lumped components"));
    CompChoose->insertItem(tr("sources"));
    CompChoose->insertItem(tr("transmission lines"));
    CompChoose->insertItem(tr("nonlinear components"));
    CompChoose->insertItem(tr("digital components"));
    CompChoose->insertItem(tr("file components"));
    CompChoose->insertItem(tr("simulations"));
    CompChoose->insertItem(tr("diagrams"));
  }
  CompChoose->insertItem(tr("paintings"));
}

// ######################################################################
// The following arrays contains the components that appear in the
// component listview.
typedef Element*  (*pInfoFunc) (QString&, char* &, bool);
pInfoFunc lumpedComponents[] =
  {&Resistor::info, &Resistor::info_us, &Capacitor::info, &Inductor::info,
   &Ground::info, &SubCirPort::info, &Transformer::info, &symTrafo::info,
   &dcBlock::info, &dcFeed::info, &BiasT::info, &Attenuator::info,
   &Amplifier::info, &Isolator::info, &Circulator::info,
   &Gyrator::info, &Phaseshifter::info, &Coupler::info, &iProbe::info,
   &vProbe::info, &Mutual::info, &Mutual2::info, &Switch::info,
   &Relais::info, 0};

pInfoFunc Sources[] =
  {&Volt_dc::info, &Ampere_dc::info, &Volt_ac::info, &Ampere_ac::info,
   &Source_ac::info, &Volt_noise::info, &Ampere_noise::info, &VCCS::info,
   &CCCS::info, &VCVS::info, &CCVS::info, &vPulse::info, &iPulse::info,
   &vRect::info, &iRect::info, &Noise_ii::info, &Noise_vv::info,
   &Noise_iv::info, &AM_Modulator::info, &PM_Modulator::info, 0};

pInfoFunc TransmissionLines[] =
  {&TLine::info, &CoaxialLine::info, &Substrate::info, &MSline::info,
   &MScoupled::info, &MScorner::info, &MSmbend::info, &MSstep::info,
   &MStee::info, &MScross::info, &MSopen::info, &MSgap::info, &MSvia::info,
   &Coplanar::info, &CPWopen::info, &CPWshort::info, &CPWgap::info,
   &CPWstep::info, 0};

pInfoFunc nonlinearComps[] =
  {&Diode::info, &BJT::info, &BJT::info_pnp, &BJTsub::info,
   &BJTsub::info_pnp, &JFET::info, &JFET::info_p,
   &MOSFET::info, &MOSFET::info_p, &MOSFET::info_depl,
   &MOSFET_sub::info, &MOSFET_sub::info_p, &MOSFET_sub::info_depl,
   &OpAmp::info, 0};

pInfoFunc digitalComps[] =
  {&Digi_Source::info, &Logical_Inv::info, &Logical_OR::info,
   &Logical_NOR::info, &Logical_AND::info, &Logical_NAND::info,
   &Logical_XOR::info, &Logical_XNOR::info, &RS_FlipFlop::info,
   &D_FlipFlop::info, &JK_FlipFlop::info, 0};

pInfoFunc Simulations[] =
  {&DC_Sim::info, &TR_Sim::info, &AC_Sim::info, &SP_Sim::info,
   &HB_Sim::info, &Param_Sweep::info, &Digi_Sim::info, 0};

pInfoFunc FileComponents[] =
  {&SpiceFile::info, &SParamFile::info1, &SParamFile::info2,
   &SParamFile::info, 0};

pInfoFunc Diagrams[] =
  {&RectDiagram::info, &PolarDiagram::info, &TabDiagram::info,
   &SmithDiagram::info, &SmithDiagram::info_y, &PSDiagram::info,
   &PSDiagram::info_sp, &Rect3DDiagram::info, &CurveDiagram::info,
   &TimingDiagram::info, &TruthDiagram::info, 0};

pInfoFunc Paintings[] =
  {&GraphicLine::info, &Arrow::info, &GraphicText::info,
   &Ellipse::info, &Rectangle::info, &Ellipse::info_filled,
   &Rectangle::info_filled, &EllipseArc::info, 0};

// Order of the component groups in the ComboBox
pInfoFunc *ComponentGroups[] =
  {lumpedComponents, Sources, TransmissionLines, nonlinearComps,
   digitalComps, FileComponents, Simulations, Diagrams, 0};

// #######################################################################
// Whenever the Component Library ComboBox is changed, this slot fills the
// Component IconView with the appropriat components.
void QucsApp::slotSetCompView(int index)
{
  view->editText->setHidden(true); // disable text edit of component property

  char *File;
  QString Name;
  pInfoFunc *Infos = 0;

  CompComps->clear();   // clear the IconView
  if((index+1) >= CompChoose->count())  // because of symbol edit mode
    Infos = &Paintings[0];
  else
    Infos = ComponentGroups[index];

  while(*Infos != 0) {
    (**Infos) (Name, File, false);
    new QIconViewItem(CompComps, Name,
		QImage(QucsSettings.BitmapDir+QString(File)+".png"));
    Infos++;
  }
}

// ----------------------------------------------------------------------
// Is called when the mouse is clicked within the Component QIconView.
void QucsApp::slotSelectComponent(QIconViewItem *item)
{
  view->editText->setHidden(true); // disable text edit of component property

  // delete previously selected elements
  if(view->selElem != 0)  delete view->selElem;
  view->selElem  = 0;   // no component/diagram/painting selected

  if(view->drawn) view->viewport()->repaint();  // don't use update() here !!!
  view->drawn = false;

  if(item == 0) {   // mouse button pressed not over an item ?
    CompComps->clearSelection();  // deselect component in ViewList
    return;
  }

  // toggle last toolbar button off
  if(activeAction) {
    activeAction->blockSignals(true); // do not call toggle slot
    activeAction->setOn(false);       // set last toolbar button off
    activeAction->blockSignals(false);
  }
  activeAction = 0;


  view->MouseMoveAction = &QucsView::MMoveElement;
  view->MousePressAction = &QucsView::MPressElement;
  view->MouseReleaseAction = 0;
  view->MouseDoubleClickAction = 0;

  pInfoFunc Infos = 0;
  int i = CompComps->index(item);
  if((CompChoose->currentItem()+1) >= CompChoose->count())
    Infos = Paintings[i];   // the only one in "symbol-painting" mode
  else
    Infos = *(ComponentGroups[CompChoose->currentItem()] + i);

  char *Dummy2;
  QString Dummy1;
  if(Infos) view->selElem = (*Infos) (Dummy1, Dummy2, true);
}

// ----------------------------------------------------------
// Menu that appears if right mouse button is pressed on a file in the
// "Content" ListView.
void QucsApp::initCursorMenu()
{
  ContentMenu = new QPopupMenu(Content);
  ContentMenu->insertItem(tr("Open"), this, SLOT(slotCMenuOpen()));
  ContentMenu->insertItem(tr("Rename"), this, SLOT(slotCMenuRename()));
  ContentMenu->insertItem(tr("Delete"), this, SLOT(slotCMenuDelete()));
  ContentMenu->insertItem(tr("Delete Group"), this, SLOT(slotCMenuDelGroup()));

  connect(Content,
	  SIGNAL(contextMenuRequested(QListViewItem*, const QPoint&, int)),
	  SLOT(slotShowContentMenu(QListViewItem*, const QPoint&, int)));
}

// ----------------------------------------------------------
// Is called if right mouse button is pressed on a file in the "Content"
// ListView. It shows a menu.
void QucsApp::slotShowContentMenu(QListViewItem *item, const QPoint& point, int)
{
  if(item)
    if(item->parent() != 0) {   // no component, but item "schematic", ...
      if(item->parent()->nextSibling()) // "Others" section in listview ?
        ContentMenu->setItemEnabled(ContentMenu->idAt(3), true);
      else
        ContentMenu->setItemEnabled(ContentMenu->idAt(3), false);
      ContentMenu->popup(point);
    }
}

// ----------------------------------------------------------
// for menu that appears if right mouse button is pressed on a file in the "Content" ListView.
void QucsApp::slotCMenuOpen()
{
  QListViewItem *i = Content->selectedItem();
  if(i == 0) return;

  slotOpenContent(i);
}

// ----------------------------------------------------------
// for menu that appears if right mouse button is pressed on a file in the "Content" ListView.
void QucsApp::slotCMenuRename()
{
  QListViewItem *i = Content->selectedItem();
  if(i == 0) return;

  QucsDoc *dc = view->Docs.current();
  // search, if file is open
  for(QucsDoc *d = view->Docs.first(); d!=0; d = view->Docs.next()) {
    if(d->DocName == QucsWorkDir.filePath(i->text(0))) {
      QMessageBox::critical(this, tr("Error"),
				  tr("Cannot rename an open file!"));
      view->Docs.findRef(dc);
      return;
    }
  }
  view->Docs.findRef(dc);

  QString Name   = i->text(0);
  QString Suffix = Name.right(4);    // remember suffix

  bool ok;
  QString s = QInputDialog::getText(tr("Rename file"), tr("Enter new name:"),
		QLineEdit::Normal, Name.left(Name.length()-4), &ok, this);
  if(!ok) return;
  if(s.isEmpty()) return;

  QDir file(QucsWorkDir.path());
  if(!file.rename(Name, s+Suffix)) {
    QMessageBox::critical(this, tr("Error"), tr("Cannot rename file: ")+Name);
    return;
  }
  i->setText(0, s+Suffix);
}

// ----------------------------------------------------------
// for menu that appears if right mouse button is pressed on a file in
// the "Content" ListView.
void QucsApp::slotCMenuDelete()
{
  QListViewItem *item = Content->selectedItem();
  if(item == 0) return;
  QString FileName = QucsWorkDir.filePath(item->text(0));

  QucsDoc *dc = view->Docs.current();
  // search, if file is open
  for(QucsDoc *d = view->Docs.first(); d!=0; d = view->Docs.next()) {
    if(d->DocName == FileName) {
      QMessageBox::critical(this, tr("Error"),
                   tr("Cannot delete an open file!"));
      view->Docs.findRef(dc);
      return;
    }
  }
  view->Docs.findRef(dc);  // back to the real current document

  int n = QMessageBox::warning(this, tr("Warning"),
                 tr("This will delete the file permanently! Continue ?"),
                 tr("No"), tr("Yes"));
  if(n != 1) return;

  if(!QFile::remove(FileName)) {
    QMessageBox::critical(this, tr("Error"),
		tr("Cannot delete schematic: ")+item->text(0));
    return;
  }
//  if(ConSchematics == item->parent())
//    ConSchematics->takeItem(item);
//  else if(ConDisplays == item->parent())
//    ConDisplays->takeItem(item);
//  else
//    ConDatasets->takeItem(item);

  item->parent()->takeItem(item);
  delete item;
}

// ----------------------------------------------------------
// for menu that appears if right mouse button is pressed on a file in
// the "Content" ListView
// Deletes all files with that name (with suffix "sch", "dpl", "dat").
void QucsApp::slotCMenuDelGroup()
{
  QListViewItem *item = Content->selectedItem();
  if(item == 0) return;
  QString s = item->text(0);
  s = s.left(s.length()-4);  // cut off suffix from file name

  QString NameSCH = QucsWorkDir.filePath(s+".sch");
  QString NameDPL = QucsWorkDir.filePath(s+".dpl");
  QString NameDAT = QucsWorkDir.filePath(s+".dat");

  QucsDoc *dc = view->Docs.current();
  // search, if files are open
  for(QucsDoc *d = view->Docs.first(); d!=0; d = view->Docs.next()) {
    if(d->DocName == NameSCH) {
      QMessageBox::critical(this, tr("Error"),
                   tr("Cannot delete the open file: ")+NameSCH);
      view->Docs.findRef(dc);
      return;
    }
    if(d->DocName == NameDPL) {
      QMessageBox::critical(this, tr("Error"),
                   tr("Cannot delete the open file: ")+NameDPL);
      view->Docs.findRef(dc);
      return;
    }
  }
  view->Docs.findRef(dc);  // back to the real current document

  bool SCH_exists = QFile::exists(NameSCH);
  bool DPL_exists = QFile::exists(NameDPL);
  bool DAT_exists = QFile::exists(NameDAT);

  QString Str;
  if(SCH_exists)  Str += s+".sch\n";
  if(DPL_exists)  Str += s+".dpl\n";
  if(DAT_exists)  Str += s+".dat\n";

  int n = QMessageBox::warning(this, tr("Warning"),
	  tr("This will delete the files\n")+Str+tr("permanently! Continue ?"),
	  tr("No"), tr("Yes"));
  if(n != 1) return;

  // remove files .................
  if(SCH_exists)
    if(!QFile::remove(NameSCH)) {
      QMessageBox::critical(this, tr("Error"),
		tr("Cannot delete schematic: ")+s+".sch");
      return;
    }
  if(DPL_exists)
    if(!QFile::remove(NameDPL)) {
      QMessageBox::critical(this, tr("Error"),
		tr("Cannot delete schematic: ")+s+".dpl");
      return;
    }
  if(DAT_exists)
    if(!QFile::remove(NameDAT)) {
      QMessageBox::critical(this, tr("Error"),
		tr("Cannot delete schematic: ")+s+".dat");
      return;
    }

  // remove items from listview ........
  if(SCH_exists) {
    item = Content->findItem(s+".sch", 0);
    if(item) {
      item->parent()->takeItem(item);
      delete item;
    }
  }
  if(DPL_exists) {
    item = Content->findItem(s+".dpl", 0);
    if(item) {
      item->parent()->takeItem(item);
      delete item;
    }
  }
  if(DAT_exists) {
    item = Content->findItem(s+".dat", 0);
    if(item) {
      item->parent()->takeItem(item);
      delete item;
    }
  }
}

// ----------------------------------------------------------
// Checks situation and reads all existing Qucs projects
void QucsApp::readProjects()
{
  QDir ProjDir(QDir::homeDirPath());
  if(!ProjDir.cd(".qucs")) // work directory exists ?
    if(!ProjDir.mkdir(".qucs")) { // no, then create it
      QMessageBox::warning(this, tr("Warning"),
			tr("Cannot create work directory !"));
      return;
    }
  ProjDir.cd(".qucs");

  // get all directories
  QStringList PrDirs = ProjDir.entryList("*", QDir::Dirs, QDir::Name);
  PrDirs.pop_front(); // delete "." from list
  PrDirs.pop_front(); // delete ".." from list

  Projects->clear();
  QStringList::iterator it;
  // inserts all project directories
  for(it = PrDirs.begin(); it != PrDirs.end(); it++)
     if ((*it).right(4) == "_prj") {   // project directories end with "_prj"
      (*it) = (*it).left((*it).length()-4);// remove "_prj" from project name
      Projects->insertItem(*it);
     }
}

// #########################################################################
bool QucsApp::closeAllFiles()
{
  int  Result = 0;
  bool notForAll = true;
  CloseMessageBox *m = new CloseMessageBox(tr("Closing Qucs document"),
	tr("This document contains unsaved changes!\n"
	   "Do you want to save the changes before closing?"),this);

  // close all files and ask to save changed ones
  for(QucsDoc *ptr = view->Docs.first(); ptr != 0; ) {
    nextDocument(false);
    if(ptr->DocChanged) {
      if(notForAll)  Result = m->exec();
      switch(Result) {
	case 1: if(!saveCurrentFile())  return false;  // save button
		break;
	case 2: Result = 1;         // save all button
		notForAll = false;
		if(!saveCurrentFile())  return false;
		break;
	case 4: Result = 3;         // discard all button
		notForAll = false;
		break;
	case 5: return false;       // cancel button
      }
    }

    view->Docs.remove();
    ptr = view->Docs.current();
  }

  delete m;
  switchEditMode(true);   // set schematic edit mode
  return true;
}

// ########################################################################
// Switches to the next document. Is called when closing a document.
void QucsApp::nextDocument(bool loadDiagrams)
{
  // make new document the current
  WorkView->setCurrentTab(WorkView->tabAt(view->Docs.at()));

  QucsDoc *d = view->Docs.current();
  view->resizeContents (int(d->Scale*double(d->ViewX2-d->ViewX1)),
			int(d->Scale*double(d->ViewY2-d->ViewY1)));
  view->setContentsPos(d->PosX, d->PosY);   // set view area

  if(loadDiagrams)
    view->Docs.current()->reloadGraphs();  // load recent simulation data
  view->viewport()->update();
  view->drawn = false;

  QString *ps = d->UndoStack.current();
  if(ps != d->UndoStack.getFirst())  undo->setEnabled(true);
  else  undo->setEnabled(false);
  if(ps != d->UndoStack.getLast())  redo->setEnabled(true);
  else  redo->setEnabled(false);

  HierarchyHistory.clear();   // no subcircuit history
  popH->setEnabled(false);
}

// ########################################################################
// Is called when another document is selected via the TabBar.
void QucsApp::slotChangeView(int id)
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *d = view->Docs.current();
  d->PosX = view->contentsX();    // save position for old document
  d->PosY = view->contentsY();

  d = view->Docs.at(WorkView->indexOf(id));   // new current document
  view->resizeContents(int(d->Scale*double(d->ViewX2-d->ViewX1)),
                       int(d->Scale*double(d->ViewY2-d->ViewY1)));
  view->setContentsPos(d->PosX, d->PosY);

  // which mode: schematic or symbol editor ?
  if((CompChoose->count() > 1) == d->symbolMode)
    changeSchematicSymbolMode(d);

  view->Docs.current()->reloadGraphs();  // load recent simulation data
  view->viewport()->update();
  view->drawn = false;


  QString *ps = d->UndoStack.current();
  if(ps != d->UndoStack.getFirst())  undo->setEnabled(true);
  else  undo->setEnabled(false);
  if(ps != d->UndoStack.getLast())  redo->setEnabled(true);
  else  redo->setEnabled(false);

  if(!HierarchyHistory.isEmpty())
    if(*(HierarchyHistory.getLast()) != "*") {
      HierarchyHistory.clear();   // no subcircuit history anymore
      popH->setEnabled(false);
    }
}

// #######################################################################
// Changes to the document "Name" if possible.
bool QucsApp::gotoPage(const QString& Name)
{
  QucsDoc *d;
  int cNo, No = view->Docs.at();

  // search, if page is already loaded
  for(d = view->Docs.first(); d!=0; d = view->Docs.next())
    if(d->DocName == Name) break;

  if(d != 0) {   // open page found ?
    view->Docs.current()->reloadGraphs();   // load recent simulation data
    // make new document the current
    WorkView->setCurrentTab(WorkView->tabAt(view->Docs.at()));
    return true;
  }

  d = new QucsDoc(this, Name);
  view->Docs.append(d);   // create new page

  if(!d->load()) {    // load document if possible
    view->Docs.remove();
    view->Docs.at(No);
    view->viewport()->update();
    view->drawn = false;
    return false;
  }

  cNo = view->Docs.at();
  view->Docs.at(No);  // back to the last current
  // make new document the current (calls "slotChangeView(int)" indirectly)
  WorkView->setCurrentTab(WorkView->tabAt(cNo));
  // must be done here, in order to call the change slot !

  // if only an untitled document was open -> close it
  if(view->Docs.count() == 2)
    if(view->Docs.getFirst()->DocName.isEmpty())
      if(!view->Docs.getFirst()->DocChanged)
        view->Docs.removeRef(view->Docs.getFirst());

  d->sizeOfAll(d->UsedX1, d->UsedY1, d->UsedX2, d->UsedY2);
  view->viewport()->update();
  view->drawn = false;
  return true;
}

// #######################################################################
// Changes to the next document in the TabBar.
void QucsApp::slotNextTab()
{
  int No = view->Docs.at() + 1;
  if(view->Docs.current() == view->Docs.getLast())
    No = 0;

  // make new document the current (calls "slotChangeView(int)" indirectly)
  WorkView->setCurrentTab(WorkView->tabAt(No));
  view->viewport()->update();
  view->drawn = false;
}

// #######################################################################
void QucsApp::slotFileSettings()
{
  view->editText->setHidden(true); // disable text edit of component property

  SettingsDialog *d = new SettingsDialog(view->Docs.current(), this);
  d->exec();
  view->viewport()->update();
  view->drawn = false;
}

// --------------------------------------------------------------
void QucsApp::slotApplSettings()
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsSettingsDialog *d = new QucsSettingsDialog(this);
  d->exec();
  view->viewport()->update();
  view->drawn = false;
}

// --------------------------------------------------------------
void QucsApp::slotFileNew()
{
  statusBar()->message(tr("Creating new schematic..."));
  view->editText->setHidden(true); // disable text edit of component property

  view->Docs.append(new QucsDoc(this, ""));
  // make new document the current
  WorkView->setCurrentTab(WorkView->tabAt(view->Docs.at()));

  statusBar()->message(tr("Ready."));
}

// --------------------------------------------------------------
void QucsApp::slotFileOpen()
{
  static QString lastDir;  // to remember last directory and file
  view->editText->setHidden(true); // disable text edit of component property

  statusBar()->message(tr("Opening file..."));

  QString s = QFileDialog::getOpenFileName(
	lastDir.isEmpty() ? QString(".") : lastDir,
	QucsFileFilter, this, 0, tr("Enter a Schematic Name"));

  if(s.isEmpty())
    statusBar()->message(tr("Opening aborted"), 2000);
  else {
    gotoPage(s);
    lastDir = s;   // remember last directory and file
    statusBar()->message(tr("Ready."));
  }
}


// ########################################################################
void QucsApp::updatePortNumber(int No)
{
  if(No<0) return;
  QString pathName = view->Docs.current()->DocName;
  QFileInfo Info(pathName);
  QString File, Name = Info.fileName();

  // enter new port number into ListView
  QListViewItem *p;
  for(p = ConSchematics->firstChild(); p!=0; p = p->nextSibling()) {
    if(p->text(0) == Name) {
      if(No == 0) p->setText(1,"");
      else p->setText(1,QString::number(No)+tr("-port"));
      break;
    }
  }
  if(No == 0) return;

  // update all occurencies of subcircuit in all open documents
  QucsDoc *d;
  Component *pc_tmp;
  int DocNo = view->Docs.at();
  for(d = view->Docs.first(); d!=0; d = view->Docs.next())

    // start from the last to avoid re-appended components
    for(Component *pc=d->Comps->last(); pc!=0; ) {
      if(pc->Model == "Sub") {
        File = pc->Props.getFirst()->Value;
        if((File == pathName) || (File == Name)) {
          pc_tmp = d->Comps->prev();
          d->recreateComponent(pc);  // delete and re-append component
          if(!pc_tmp)  break;
          d->Comps->findRef(pc_tmp);
          pc = d->Comps->current();
          continue;
        }
      }
      pc = d->Comps->prev();
    }

  view->Docs.at(DocNo);  // back to the last current document
}


// ######################################################################
bool QucsApp::saveCurrentFile()
{
  if(view->Docs.current()->DocName.isEmpty())
    return saveAs();

  view->Docs.current()->PosX = view->contentsX();
  view->Docs.current()->PosY = view->contentsY();

  int Result = view->Docs.current()->save();   // SAVE
  if(Result < 0)  return false;

  updatePortNumber(Result);
  return true;
}

// ######################################################################
void QucsApp::slotFileSave()
{
  statusBar()->message(tr("Saving file..."));
  view->blockSignals(true);   // no user interaction during that time
  view->editText->setHidden(true); // disable text edit of component property

  if(!saveCurrentFile()) {
    view->blockSignals(false);
    statusBar()->message(tr("Saving aborted"), 2000);
    statusBar()->message(tr("Ready."));
    return;
  }

  view->blockSignals(false);
  statusBar()->message(tr("Ready."));
}

// #######################################################################
bool QucsApp::saveAs()
{
  static QString lastDir;  // to remember last directory and file

  int n = -1;
  bool intoView = true;
  QString s, Filter;
  QFileInfo Info;
  while(true) {
    intoView = true;
    s = view->Docs.current()->DocName;
    Info.setFile(s);
    if(s.isEmpty()) {   // which is default directory ?
      if(ProjName.isEmpty()) {
        if(lastDir.isEmpty())  s = QDir::currentDirPath();
        else  s = lastDir;
      }
      else s = QucsWorkDir.path();
    }

    s = QFileDialog::getSaveFileName(s,
	Info.extension() == "dpl" ? tr("Data Display")+" (*.dpl)"
				  : QucsFileFilter,
	this, "", tr("Enter a Document Name"), &Filter);
    if(s.isEmpty())  return false;
    Info.setFile(s);                 // try to guess the best extension ...
    if(Info.extension(false).isEmpty())  // ... if no one was specified
      if(Filter.right(7) == "(*.dpl)")  s += ".dpl";
      else s += ".sch";

    Info.setFile(s);
    if(QFile::exists(s)) {
      n = QMessageBox::warning(this, tr("Warning"),
		tr("The file '")+Info.fileName()+tr("' already exists!\n")+
		tr("Saving will overwrite the old one! Continue?"),
		tr("No"), tr("Yes"), tr("Cancel"));
      if(n == 2) return false;    // cancel
      if(n == 0) continue;
      intoView = false;    // file already exists
    }

    QString ext = Info.extension(false);
    if(ext != "sch") if(ext != "dpl") {
      n = QMessageBox::warning(this, tr("Warning"),
		tr("Only the extensions '.sch' and '.dpl'\n")+
		tr("will appear in the content browser! Continue?"),
		tr("No"), tr("Yes"), tr("Cancel"));
      if(n == 2) return false;    // cancel
      if(n == 0) continue;
    }

    // search, if document is open
    QucsDoc *d;
    int No = view->Docs.at();
    for(d = view->Docs.first(); d!=0; d = view->Docs.next())
      if(d->DocName == s) break;
    view->Docs.at(No);   // back to old current document
    if(d) {
      QMessageBox::information(this, tr("Info"),
		tr("Cannot overwrite an open document"));
      return false;
    }

    break;
  }
  view->Docs.current()->setName(s);
  lastDir = Info.dirPath(true);  // remember last directory and file

  if(intoView) {    // insert new name in Content ListView ?
//    Info.setFile(s);
    if(Info.dirPath(true) == QucsWorkDir.absPath())
      if(!ProjName.isEmpty()) {
	s = Info.fileName();  // remove path from file name
	if(Info.extension(false) == "dpl")
	  Content->setSelected(new QListViewItem(ConDisplays, s), true);
	else
	  Content->setSelected(new QListViewItem(ConSchematics, s), true);
      }
  }

  view->Docs.current()->PosX = view->contentsX();
  view->Docs.current()->PosY = view->contentsY();
  n = view->Docs.current()->save();   // SAVE
  if(n < 0)  return false;

  updatePortNumber(n);
  return true;
}

// #######################################################################
void QucsApp::slotFileSaveAs()
{
  statusBar()->message(tr("Saving file under new filename..."));
  view->blockSignals(true);   // no user interaction during the time
  view->editText->setHidden(true); // disable text edit of component property

  if(!saveAs()) {
    view->blockSignals(false);
    statusBar()->message(tr("Saving aborted"), 3000);
    statusBar()->message(tr("Ready."));
    return;
  }

  view->blockSignals(false);
  statusBar()->message(tr("Ready."));
}


// #######################################################################
void QucsApp::slotFileSaveAll()
{
  statusBar()->message(tr("Saving all files..."));
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *tmp = view->Docs.current();  // remember the current
  view->blockSignals(true);   // no user interaction during the time

  for(QucsDoc *ptr = view->Docs.first(); ptr != 0; ptr = view->Docs.next()) {
    if(ptr->DocName.isEmpty())  // make document the current ?
      WorkView->setCurrentTab(WorkView->tabAt(view->Docs.at()));
    saveCurrentFile();
  }

  view->Docs.findRef(tmp);
  view->blockSignals(false);
  // back to the current document
  WorkView->setCurrentTab(WorkView->tabAt(view->Docs.at()));

  statusBar()->message(tr("Ready."));
}

// #######################################################################
void QucsApp::slotFileClose()
{
  statusBar()->message(tr("Closing file..."));
  view->editText->setHidden(true); // disable text edit of component property

  if(view->Docs.current()->DocChanged) {
    switch(QMessageBox::warning(this,tr("Closing Qucs document"),
      tr("The document contains unsaved changes!\n")+
      tr("Do you want to save the changes before closing?"),
      tr("&Save"), tr("&Discard"), tr("Cancel"), 0, 2)) {
      case 0 : slotFileSave();
               break;
      case 2 : return;
    }
  }

  view->Docs.remove();

  if(view->Docs.isEmpty())  // if no document left, create an untitled
    view->Docs.append(new QucsDoc(this, ""));

  nextDocument(true);

  QucsDoc *d = view->Docs.current();
  // which mode: schematic or symbol editor ?
  if((CompChoose->count() > 1) == d->symbolMode)
    changeSchematicSymbolMode(d);

  statusBar()->message(tr("Ready."));
}

// ######################################################################
void QucsApp::slotFilePrint()
{
  statusBar()->message(tr("Printing..."));
  view->editText->setHidden(true); // disable text edit of component property

  if (Printer->setup(this))  // print dialog
  {
    QPainter painter;
    painter.begin(Printer);
    view->Docs.current()->print(&painter, true);
    painter.end();
  };

  statusBar()->message(tr("Ready."));
}

// ######################################################################
void QucsApp::slotFilePrintSelected()
{
  statusBar()->message(tr("Printing..."));
  view->editText->setHidden(true); // disable text edit of component property

  if (Printer->setup(this))  // print dialog
  {
    QPainter painter;
    painter.begin(Printer);
    view->Docs.current()->print(&painter, false);
    painter.end();
  };

  statusBar()->message(tr("Ready."));
}

// --------------------------------------------------------------------
// Exits the application.
void QucsApp::slotFileQuit()
{
  statusBar()->message(tr("Exiting application..."));
  view->editText->setHidden(true); // disable text edit of component property

  int exit = QMessageBox::information(this, tr("Quit..."),
				      tr("Do you really want to quit?"),
				      tr("Yes"), tr("No"));
  if(exit == 0)
    if(closeAllFiles()) {
      emit signalKillEmAll();   // kill all subprocesses
      qApp->quit();
    }

  statusBar()->message(tr("Ready."));
}

//-----------------------------------------------------------------
// To get all close events.
void QucsApp::closeEvent(QCloseEvent* Event)
{
  Event->ignore();
  if(closeAllFiles()) {
    emit signalKillEmAll();   // kill all subprocesses
    qApp->quit();
  }
}

// --------------------------------------------------------------------
void QucsApp::slotEditCut()
{
  statusBar()->message(tr("Cutting selection..."));
  view->editText->setHidden(true); // disable text edit of component property

  QClipboard *cb = QApplication::clipboard();   // get system clipboard
  QString s = view->Docs.current()->copySelected(true);
  if(!s.isEmpty()) {
    cb->setText(s, QClipboard::Clipboard);
    view->viewport()->update();
  }

  statusBar()->message(tr("Ready."));
}

// ######################################################################
void QucsApp::slotEditCopy()
{
  statusBar()->message(tr("Copying selection to clipboard..."));
  view->editText->setHidden(true); // disable text edit of component property

  QClipboard *cb = QApplication::clipboard();   // get system clipboard
  QString s = view->Docs.current()->copySelected(false);
  if(!s.isEmpty())
    cb->setText(s, QClipboard::Clipboard);

  statusBar()->message(tr("Ready."));
}

// ########################################################################
// Is called when the toolbar button is pressed to go into a subcircuit.
void QucsApp::slotIntoHierarchy()
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *Doc = view->Docs.current();
  Component *pc = view->Docs.current()->searchSelSubcircuit();
  if(pc == 0) return;

  QString *ps = new QString("*");
  HierarchyHistory.append(ps);    // sign to not clear HierarchyHistory

  QString s = QucsWorkDir.filePath(pc->Props.getFirst()->Value);
  if(!gotoPage(s)) return;

  *(HierarchyHistory.getLast()) = Doc->DocName; // remember for the way back
  popH->setEnabled(true);
}

// ########################################################################
// Is called when the toolbar button is pressed to leave a subcircuit.
void QucsApp::slotPopHierarchy()
{
  view->editText->setHidden(true); // disable text edit of component property

  if(HierarchyHistory.count() == 0) return;

  QString Doc = *(HierarchyHistory.getLast());
  *(HierarchyHistory.last()) = "*";  // sign to not clear HierarchyHistory

  if(!gotoPage(Doc)) return;

  HierarchyHistory.remove();
  if(HierarchyHistory.count() == 0) popH->setEnabled(false);
}

// ########################################################################
void QucsApp::slotShowAll()
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *d = view->Docs.current();
  int x1 = d->UsedX1;
  int y1 = d->UsedY1;
  int x2 = d->UsedX2;
  int y2 = d->UsedY2;

  if(x1 == INT_MAX) return;
  if(y1 == INT_MAX) return;
  if(x2 == INT_MIN) return;
  if(y2 == INT_MIN) return;
  x1 -= 40;  y1 -= 40;
  x2 += 40;  y2 += 40;

  float xScale = float(view->visibleWidth()) / float(x2-x1);
  float yScale = float(view->visibleHeight()) / float(y2-y1);
  if(xScale > yScale) xScale = yScale;
  xScale /= d->Scale;

  d->ViewX1 = x1;
  d->ViewY1 = y1;
  d->ViewX2 = x2;
  d->ViewY2 = y2;
  view->Zoom(xScale);

  view->viewport()->update();
  view->drawn = false;
}

// -----------------------------------------------------------
// Sets the scale factor to 1.
void QucsApp::slotShowOne()
{
  view->editText->setHidden(true); // disable text edit of component property
  QucsDoc *d = view->Docs.current();

  d->Scale = 1.0;

  int x1 = d->UsedX1;
  int y1 = d->UsedY1;
  int x2 = d->UsedX2;
  int y2 = d->UsedY2;

//  view->Docs.current()->sizeOfAll(x1, y1, x2, y2);
  if(x2==0) if(y2==0) if(x1==0) if(y1==0) x2 = y2 = 800;

  d->ViewX1 = x1-40;
  d->ViewY1 = y1-40;
  d->ViewX2 = x2+40;
  d->ViewY2 = y2+40;
  view->resizeContents(x2-x1+80, y2-y1+80);
  view->viewport()->update();
  view->drawn = false;
}

void QucsApp::slotZoomOut()
{
  view->editText->setHidden(true); // disable text edit of component property
  
  view->Zoom(0.5);
  view->viewport()->update();
  view->drawn = false;
}

// -----------------------------------------------------------------------
// Is called when the simulate toolbar button is pressed.
void QucsApp::slotSimulate()
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *d = view->Docs.current();
  if(d->DocName.isEmpty())         // if document 'untitled' ...
    if(!saveCurrentFile()) return; // ... save schematic before

  Init.slotResetWarnings();
  SimMessage *sim = new SimMessage(view->Docs.current(), this);
  // disconnect is automatically performed, if one of the involved objects
  // is destroyed !
  connect(sim, SIGNAL(SimulationEnded(int, SimMessage*)), this,
		SLOT(slotAfterSimulation(int, SimMessage*)));
  connect(sim, SIGNAL(displayDataPage(QString)),
		this, SLOT(slotChangePage(QString)));

  sim->show();
  if(!sim->startProcess()) return;

  // to kill it before qucs ends
  connect(this, SIGNAL(signalKillEmAll()), sim, SLOT(slotClose()));
}

// ------------------------------------------------------------------------
// Is called after the simulation process terminates.
void QucsApp::slotAfterSimulation(int Status, SimMessage *sim)
{
  if(Status != 0) return;  // errors ocurred ?

  if(sim->ErrText->lines() > 1)   // were there warnings ?
    Init.slotShowWarnings();

  int Index = view->Docs.containsRef(sim->Doc);
  if(sim->showBias == 0) {  // paint dc bias into schematic ?
    sim->slotClose();   // close and delete simulation window
    if(Index > 0) {  // schematic still open ?
      SweepDialog *Dia = new SweepDialog(sim->Doc);
      Dia->show();
    }
  }
  else if(sim->SimOpenDpl) {
    // switch to data display
    slotChangePage(sim->DataDisplay);
    sim->slotClose();   // close and delete simulation window
  }
  else if(Index > 0)
    sim->Doc->reloadGraphs(); // load recent simulation data (if doc still open)

  view->viewport()->update();

  // put all dataset files into "Content"-ListView (update)
/*  QStringList Elements = ProjDir.entryList("*.dat", QDir::Files, QDir::Name);
  for(it = Elements.begin(); it != Elements.end(); ++it)
    new QListViewItem(ConDatasets, (*it).ascii());*/
}

// ------------------------------------------------------------------------
void QucsApp::slotDCbias()
{
  view->Docs.current()->showBias = 0;
  slotSimulate();
}

// ------------------------------------------------------------------------
// Changes to the corresponding data display page or vice versa.
void QucsApp::slotChangePage(QString Name)
{
  if(Name.isEmpty())  return;

  QucsDoc   *d;
  QFileInfo Info;
  QucsDoc   *Doc = view->Docs.current();

  // search, if page is already loaded
  for(d = view->Docs.first(); d!=0; d = view->Docs.next())
    if(d->DocName == Name) break;

  if(d == 0) {   // no open page found ?
    Info.setFile(Name);
    if(Info.isRelative())
      Name = QucsWorkDir.filePath(Name);
    QFile file(Name);
    if(!file.open(IO_ReadOnly))       // load page
      if(!file.open(IO_ReadWrite)) {  // if it doesn't exist, create
        view->Docs.findRef(Doc);
        QMessageBox::critical(this, tr("Error"),
			tr("Cannot create ")+Info.dirPath(true)+
			QDir::separator()+Name);
        return;
      }
      else new QListViewItem(ConDisplays, Info.fileName()); // add new name
    file.close();

    d = new QucsDoc(this, Name);
    view->Docs.append(d);   // create new page

    if(!d->load()) {
      view->Docs.remove();
      view->Docs.findRef(Doc);
      view->viewport()->update();
      view->drawn = false;
      return;
    }
  }

  int cNo = view->Docs.at();
  view->Docs.findRef(Doc);  // back to the last current
  if(WorkView->indexOf(WorkView->currentTab()) == cNo)  // if page not ...
    view->Docs.current()->reloadGraphs();       // ... changes, reload here !
  WorkView->setCurrentTab(WorkView->tabAt(cNo));  // make new doc the current
  // must be done before the next step, in order to call the change slot !

  d->sizeOfAll(d->UsedX1, d->UsedY1, d->UsedX2, d->UsedY2);

  TabView->setCurrentPage(2);   // switch to "Component"-Tab
  if(Name.right(4) == ".dpl") {
    cNo = sizeof(ComponentGroups)/sizeof(pInfoFunc) - 2;
    CompChoose->setCurrentItem(cNo);   // switch to diagrams
    slotSetCompView(cNo);
  }
}

// ------------------------------------------------------------------------
// Changes to the data display of current page.
void QucsApp::slotToPage()
{
  QString Name = view->Docs.current()->DataDisplay;
  if(Name.isEmpty()) {
    QMessageBox::critical(this, tr("Error"), tr("No page set !"));
    return;
  }

  QFileInfo Info(view->Docs.current()->DocName);
  slotChangePage(Info.dirPath() + QDir::separator() + Name);
}

// #######################################################################
// Is called when a double click is made in the content ListView.
void QucsApp::slotOpenContent(QListViewItem *item)
{
  view->editText->setHidden(true); // disable text edit of component property

  if(item == 0) return;   // no item was double clicked
  if(item->parent() == 0) return;  // no component, but item "schematic", ...

/*  
  QucsWorkDir.setPath(QucsHomeDir.path());
  QString p = ProjName+"_prj";
  if(!QucsWorkDir.cd(p)) {
    QMessageBox::critical(this, tr("Error"),
			  tr("Cannot access project directory: ")+
			  QucsWorkDir.path()+QDir::separator()+p);
    return;
  }*/

  QFileInfo Info(QucsWorkDir.filePath(item->text(0)));
  QString Suffix = Info.extension(false);

  if((Suffix == "sch") || (Suffix == "dpl")) {
    gotoPage(Info.absFilePath());

    if(item->text(1).isEmpty()) return;  // is subcircuit ?
    // switch on the 'select' action
    Acts.select->blockSignals(true);
    Acts.select->setOn(true);
    Acts.select->blockSignals(false);
    Acts.slotSelect(true);
    return;
  }

  if(Suffix == "dat") {
    Acts.editFile(Info.absFilePath());  // open datasets with text editor
    return;
  }


  // File is no Qucs file, so go through list and search a program
  // to open it.
  QStringList com;
  QStringList::Iterator it = QucsSettings.FileTypes.begin();
  while(it != QucsSettings.FileTypes.end()) {
    if(Suffix == (*it).section('/',0,0)) {
      com = QStringList::split(" ", (*it).section('/',1,1));
      com << Info.absFilePath();
      QProcess *Program = new QProcess(com);
      Program->setCommunication(0);
      if(!Program->start()) {
        QMessageBox::critical(this, tr("Error"),
               tr("Cannot start \"%1\"!").arg(Info.absFilePath()));
        delete Program;
      }
      return;
    }
    it++;
  }

  // If no appropriate program was found, open as text file.
  Acts.editFile(Info.absFilePath());  // open datasets with text editor
}

// ########################################################################
// Is called when the close project menu is called.
void QucsApp::slotMenuCloseProject()
{
  view->editText->setHidden(true); // disable text edit of component property

  if(!closeAllFiles()) return;   // close files and ask for saving them
  QucsDoc *d = new QucsDoc(this, "");
  view->Docs.append(d);   // create 'untitled' file
  view->resizeContents(int(d->Scale*double(d->ViewX2-d->ViewX1)),
                       int(d->Scale*double(d->ViewY2-d->ViewY1)));
  view->setContentsPos(d->PosX, d->PosY);
  view->viewport()->update();
  view->drawn = false;

  Init.slotResetWarnings();
  setCaption("Qucs " PACKAGE_VERSION + tr(" - Project: "));
  QucsWorkDir.setPath(QDir::homeDirPath()+QDir::convertSeparators ("/.qucs"));

  Content->setColumnText(0,tr("Content of")); // column text

  Content->clear();   // empty content view
  ConOthers     = new QListViewItem(Content, tr("Others"));
  ConDatasets   = new QListViewItem(Content, tr("Datasets"));
  ConDisplays   = new QListViewItem(Content, tr("Data Displays"));
  ConSchematics = new QListViewItem(Content, tr("Schematics"));

  TabView->setCurrentPage(0);   // switch to "Projects"-Tab
  ProjName = "";
}

// ########################################################################
// Is called when the open project menu is called.
void QucsApp::slotMenuOpenProject()
{
  QFileDialog *d = new QFileDialog(QucsHomeDir.path());
  d->setCaption(tr("Choose Project Directory for Opening"));
  d->setShowHiddenFiles(true);
  d->setMode(QFileDialog::DirectoryOnly);
  if(d->exec() != QDialog::Accepted) return;

  QString s = d->selectedFile();
  if(s.isEmpty()) return;

  s = s.left(s.length()-1);   // cut off trailing '/'
  int i = s.findRev('/');
  if(i > 0) s = s.mid(i+1);   // cut out the last subdirectory
  s.remove("_prj");
  OpenProject(d->selectedFile(), s);
}

// #######################################################################
// Is called when project is double-clicked.
void QucsApp::slotOpenProject(QListBoxItem *item)
{
  OpenProject(QucsHomeDir.filePath(item->text()+"_prj"), item->text());
}

// ########################################################################
// Checks whether this file is a qucs file and whether it is an subcircuit.
// It returns the number of subcircuit ports.
int QucsApp::testFile(const QString& DocName)
{
  QFile file(DocName);
  if(!file.open(IO_ReadOnly)) {
    return -1;
  }

  QString Line;
  // *****************************************************************
  // To strongly speed up the file read operation the whole file is
  // read into the memory in one piece.
  QTextStream ReadWhole(&file);
  QString FileString = ReadWhole.read();
  file.close();
  QTextStream stream(&FileString, IO_ReadOnly);


  // read header **************************
  do {
    if(stream.atEnd()) {
      file.close();
      return -2;
    }
    Line = stream.readLine();
    Line = Line.stripWhiteSpace();
  } while(Line.isEmpty());
  
  if(Line.left(16) != "<Qucs Schematic ") {  // wrong file type ?
    file.close();
    return -3;
  }

  QString s = PACKAGE_VERSION;
  s.remove('.');
  Line = Line.mid(16, Line.length()-17);
  Line.remove('.');
  if(Line > s) {  // wrong version number ? (only backward compatible)
    file.close();
    return -4;
  }

  // read content *************************
  while(!stream.atEnd()) {
    Line = stream.readLine();
    if(Line == "<Components>") break;
  }

  int z=0;
  while(!stream.atEnd()) {
    Line = stream.readLine();
    if(Line == "</Components>") {
      file.close();
      return z;       // return number of ports
    }

    Line = Line.stripWhiteSpace();
    s    = Line.section(' ',0,0);    // component type
    if(s == "<Port") z++;
  }
  return -5;  // component field not closed
}

// #######################################################################
// Reads all files in the project directory and sort them into the
// content ListView
void QucsApp::readProjectFiles()
{
  Content->clear();   // empty content view
  ConOthers     = new QListViewItem(Content, tr("Others"));
  ConDatasets   = new QListViewItem(Content, tr("Datasets"));
  ConDisplays   = new QListViewItem(Content, tr("Data Displays"));
  ConSchematics = new QListViewItem(Content, tr("Schematics"));

  int n;
  // put all files into "Content"-ListView
  QStringList Elements = QucsWorkDir.entryList("*", QDir::Files, QDir::Name);
  QStringList::iterator it;
  QString Str;
  for(it = Elements.begin(); it != Elements.end(); ++it) {
    Str = (*it).right(4);
    if(Str == ".sch") {
      n = testFile(QucsWorkDir.filePath((*it).ascii()));
      if(n >= 0) {
        if(n > 0)
          new QListViewItem(ConSchematics, (*it).ascii(),
				QString::number(n)+tr("-port"));
        else new QListViewItem(ConSchematics, (*it).ascii());
      }
    }
    else if(Str == ".dpl")
      new QListViewItem(ConDisplays, (*it).ascii());
    else if(Str == ".dat")
      new QListViewItem(ConDatasets, (*it).ascii());
    else
      new QListViewItem(ConOthers, (*it).ascii());
  }
}

// #######################################################################
// Opens an existing project.
void QucsApp::OpenProject(const QString& Path, const QString& Name)
{
  view->editText->setHidden(true); // disable text edit of component property

  if(!closeAllFiles()) return;   // close files and ask for saving them
  QucsDoc *d = new QucsDoc(this, "");
  view->Docs.append(d);   // create 'untitled' file
  view->resizeContents(int(d->Scale*double(d->ViewX2-d->ViewX1)),
                       int(d->Scale*double(d->ViewY2-d->ViewY1)));
  view->setContentsPos(d->PosX, d->PosY);
  view->viewport()->update();
  view->drawn = false;

  Init.slotResetWarnings();

  QDir ProjDir(QDir::cleanDirPath(Path));
  if(!ProjDir.exists() || !ProjDir.isReadable()) { // check project directory
    QMessageBox::critical(this, tr("Error"),
                          tr("Cannot access project directory: ")+Path);
    return;
  }
  QucsWorkDir.setPath(ProjDir.path());

  Content->setColumnText(0,tr("Content of '")+Name+tr("'")); // column text
//  Content->setColumnWidth(0, Content->width()-5);

  readProjectFiles();

  TabView->setCurrentPage(1);   // switch to "Content"-Tab
  Content->firstChild()->setOpen(true);  // show schematics
  ProjName = Name;   // remember the name of project

  // show name in title of main window
  setCaption("Qucs " PACKAGE_VERSION + tr(" - Project: ")+Name);
}

// #######################################################################
// Is called, when "Create New Project"-Button is pressed.
void QucsApp::slotProjNewButt()
{
  view->editText->setHidden(true); // disable text edit of component property

  NewProjDialog *d = new NewProjDialog(this);
  if(d->exec() != QDialog::Accepted) return;

  QDir projDir(QucsHomeDir.path());
  if(projDir.mkdir(d->ProjName->text()+"_prj")) {
    Projects->insertItem(d->ProjName->text(),0);  // at first position
    if(d->OpenProj->isChecked()) slotOpenProject(Projects->firstItem());
  }
  else QMessageBox::information(this, tr("Info"),
                    tr("Cannot create project directory !"));
}

// #######################################################################
// Is called, when "Open Project"-Button is pressed.
void QucsApp::slotProjOpenButt()
{
  view->editText->setHidden(true); // disable text edit of component property

  QListBoxItem *item = Projects->selectedItem();
  if(item) slotOpenProject(item);
  else QMessageBox::information(this, tr("Info"),
				tr("No project is selected !"));
}

// #######################################################################
bool QucsApp::DeleteProject(const QString& Path, const QString& Name)
{
  view->editText->setHidden(true); // disable text edit of component property

  if(Name == ProjName) {
    QMessageBox::information(this, tr("Info"),
			tr("Cannot delete an open project !"));
    return false;
  }

  // first ask, if really delete project ?
  if(QMessageBox::warning(this, tr("Warning"),
     tr("This will destroy all the project files permanently ! Continue ?"),
     tr("&Yes"), tr("&No"), 0,1,1))  return false;

  QDir projDir = QDir(Path);
  QStringList ProjFiles = projDir.entryList("*", QDir::Files);  // all files

  // removes every file, remove("*") does not work
  QStringList::iterator it;
  for(it = ProjFiles.begin(); it != ProjFiles.end(); it++) {
     if(!projDir.remove(*it)) {
       QMessageBox::information(this, tr("Info"),
				tr("Cannot remove project file: ")+(*it));
       return false;
     }
  }

  projDir.cdUp();  // leave project directory for deleting
  if(!projDir.rmdir(Name+"_prj")) {
    QMessageBox::information(this, tr("Info"),
			     tr("Cannot remove project directory !"));
    return false;
  }

  return true;
}

// ########################################################################
// Is called, when "Delete Project"-menu is pressed.
void QucsApp::slotMenuDelProject()
{
  QFileDialog *d = new QFileDialog(QucsHomeDir.path());
  d->setCaption(tr("Choose Project Directory for Deleting"));
  d->setShowHiddenFiles(true);
  d->setMode(QFileDialog::DirectoryOnly);
  if(d->exec() != QDialog::Accepted) return;

  QString s = d->selectedFile();
  if(s.isEmpty()) return;

  s = s.left(s.length()-1);   // cut off trailing '/'
  int i = s.findRev('/');
  if(i > 0) s = s.mid(i+1);   // cut out the last subdirectory
  s.remove("_prj");
  DeleteProject(d->selectedFile(), s);
  readProjects();   // re-reads all projects and inserts them into the ListBox
}

// #######################################################################
// Is called, when "Delete Project"-Button is pressed.
void QucsApp::slotProjDelButt()
{
  QListBoxItem *item = Projects->selectedItem();
  if(!item) {
    QMessageBox::information(this, tr("Info"),
			     tr("No project is selected !"));
    return;
  }

  if(!DeleteProject(QucsHomeDir.filePath(item->text()+"_prj"),
	item->text()))  return;
  Projects->removeItem(Projects->currentItem());  // remove from project list
}

// -----------------------------------------------------------------------
// Is called when the mouse is clicked within the Content QListView.
void QucsApp::slotSelectSubcircuit(QListViewItem *item)
{
  view->editText->setHidden(true); // disable text edit of component property

  if(item == 0) {   // mouse button pressed not over an item ?
    Content->clearSelection();  // deselect component in ListView
    if(view->drawn) view->viewport()->update();
    view->drawn = false;
    return;
  }

  if(item->parent() == 0) return;
  if(item->parent()->text(0) != tr("Schematics"))
    return;   // return, if not clicked on schematic
  if(item->text(1).isEmpty()) return;   // return, if not a subcircuit

  // delete previously selected elements
  if(view->selElem != 0)  delete view->selElem;
  view->selElem = 0;

  // toggle last toolbar button off
  if(activeAction) {
    activeAction->blockSignals(true); // do not call toggle slot
    activeAction->setOn(false);       // set last toolbar button off
    activeAction->blockSignals(false);
  }
  activeAction = 0;

  Component *Comp = new Subcircuit();
  Comp->Props.first()->Value = item->text(0);
  Comp->recreate(0);
  view->selElem = Comp;

  if(view->drawn) view->viewport()->update();
  view->drawn = false;
  view->MouseMoveAction = &QucsView::MMoveElement;
  view->MousePressAction = &QucsView::MPressElement;
  view->MouseReleaseAction = 0;
  view->MouseDoubleClickAction = 0;
}

// #######################################################################
void QucsApp::switchEditMode(bool SchematicMode)
{
  if(SchematicMode) {
    symEdit->setMenuText(tr("Edit Circuit Symbol"));
    symEdit->setStatusTip(tr("Edits the symbol for this schematic"));
    symEdit->setWhatsThis(
	tr("Edit Circuit Symbol\n\nEdits the symbol for this schematic"));
  }
  else {
    symEdit->setMenuText(tr("Edit Schematic"));
    symEdit->setStatusTip(tr("Edits the schematic"));
    symEdit->setWhatsThis(tr("Edit Schematic\n\nEdits the schematic"));
  }


  fillComboBox(SchematicMode);
  slotSetCompView(0);

  Acts.editActivate->setEnabled(SchematicMode);
  Acts.insEquation->setEnabled(SchematicMode);
  Acts.insGround->setEnabled(SchematicMode);
  Acts.insPort->setEnabled(SchematicMode);
  Acts.insWire->setEnabled(SchematicMode);
  Acts.insLabel->setEnabled(SchematicMode);
  Acts.setMarker->setEnabled(SchematicMode);
  simulate->setEnabled(SchematicMode);
}

// #######################################################################
void QucsApp::changeSchematicSymbolMode(QucsDoc *d)
{
  if(!d->symbolMode) {
    switchEditMode(true);

    d->Comps  = &(d->DocComps);
    d->Wires  = &(d->DocWires);
    d->Nodes  = &(d->DocNodes);
    d->Diags  = &(d->DocDiags);
    d->Paints = &(d->DocPaints);

    QString *ps = d->UndoStack.current();
    if(ps != d->UndoStack.getFirst())  undo->setEnabled(true);
    else  undo->setEnabled(false);
    if(ps != d->UndoStack.getLast())  redo->setEnabled(true);
    else  redo->setEnabled(false);
  }
  else {
    // go into select modus to avoid placing a forbidden element
    Acts.select->setOn(true);

    switchEditMode(false);

    d->Comps  = &SymbolComps;
    d->Wires  = &SymbolWires;
    d->Nodes  = &SymbolNodes;
    d->Diags  = &SymbolDiags;
    d->Paints = &(d->SymbolPaints);

    // If the number of ports is not equal, remove or add some.
    unsigned int countPort = d->adjustPortNumbers();

    // If a symbol does not yet exist, create one.
    if(d->SymbolPaints.count() == countPort) {
      int h = 30*((countPort-1)/2) + 10;
      d->SymbolPaints.prepend(new ID_Text(-20, h+4));

      d->SymbolPaints.append(
	new GraphicLine(-20, -h, 40,  0, QPen(QPen::darkBlue,2)));
      d->SymbolPaints.append(
	new GraphicLine( 20, -h,  0,2*h, QPen(QPen::darkBlue,2)));
      d->SymbolPaints.append(
	new GraphicLine(-20,  h, 40,  0, QPen(QPen::darkBlue,2)));
      d->SymbolPaints.append(
	new GraphicLine(-20, -h,  0,2*h, QPen(QPen::darkBlue,2)));
//  Texts.append(new Text( -7,  0,"sub"));

      unsigned int i=0, y = 10-h;
      while(i<countPort) {
	i++;
	d->SymbolPaints.append(
	  new GraphicLine(-30, y, 10, 0, QPen(QPen::darkBlue,2)));
	d->SymbolPaints.at(i)->setCenter(-30,  y);

	if(i == countPort)  break;
	i++;
	d->SymbolPaints.append(
	  new GraphicLine( 20, y, 10, 0, QPen(QPen::darkBlue,2)));
	d->SymbolPaints.at(i)->setCenter(30,  y);
	y += 60;
      }
      d->sizeOfAll(d->UsedX1, d->UsedY1, d->UsedX2, d->UsedY2);
    }

    QString *ps = d->UndoSymbol.current();
    if(ps != d->UndoSymbol.getFirst())  undo->setEnabled(true);
    else  undo->setEnabled(false);
    if(ps != d->UndoSymbol.getLast())  redo->setEnabled(true);
    else  redo->setEnabled(false);
  }
}

// #######################################################################
// Is called if the "symEdit" action is activated, i.e. if the user
// switches between the two painting mode: Schematic and (subcircuit)
// symbol.
void QucsApp::slotSymbolEdit()
{
  view->editText->setHidden(true); // disable text edit of component property

  QucsDoc *d = view->Docs.current();
  d->symbolMode = !(d->symbolMode);  // change mode
  d->switchPaintMode();   // twist the view coordinates
  changeSchematicSymbolMode(d);

  // This can only be true when switching to the symbol the first time.
  if(d->UndoSymbol.isEmpty()) {
    d->setChanged(false, true); // "not changed" state, but put on undo stack
    d->UndoSymbol.current()->at(1) = 'i';  // state of being unchanged
  }

  view->resizeContents(int(d->Scale*double(d->ViewX2-d->ViewX1)),
                       int(d->Scale*double(d->ViewY2-d->ViewY1)));
  view->setContentsPos(d->PosX, d->PosY);
  view->viewport()->update();
  view->drawn = false;
}
