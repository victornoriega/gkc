/*
 * =====================================================================================
 *
 *       Filename: Visualization.h
 *
 *    Description: Writes only slides of potnetial to disk.
 *
 *         Author: Paul P. Hilscher (2011-), 
 *
 *        License: GPLv3+
 * =====================================================================================
 */

// ToDO : Use Fields instead of phi , Ap, Bp


#ifndef VISUALIZATION_DATA_H_
#define VISUALIZATION_DATA_H_


#include "Visualization.h"

#include "Global.h"
#include "Setup.h"


#include "Plasma.h"

#include "Vlasov.h"
#include "Fields.h"
#include "Timing.h"
#include "Parallel.h"
#include "FileIO.h"
#include "Grid.h"


/**
*   
*   @brief Class to produce visual of snapshots
*
*   Writes out certain slides of the domain, which
*   reduced file size and can latter be used
*   for Visualization.
*
*
**/
class Visualization_Data : public Visualization {

   FileAttr *FA_slphi, *FA_slAp, *FA_slBp, *FA_sldn, *FA_sln, *FA_slT, *FA_slphiTime, *FA_slF1, *FA_slF1Time, *FA_XV ;
  
   Array3z V3;
   Array3z ArrXV;

   bool visXV;

  public:

   Visualization_Data(Grid *grid, Parallel *parallel, Setup *setup, FileIO *fileIO, Vlasov *_vlasov, Fields *_fields);
    
   ~Visualization_Data();

   void writeData(Timing timing, double dt, bool force=false) ;

};


#endif // VISUALIZATION_DATA_H_