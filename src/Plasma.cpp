/*
 * =====================================================================================
 *
 *       Filename: Plasma.cpp
 *
 *    Description: Properties of plasma
 *
 *         Author: Paul P. Hilscher (2009-), 
 *
 *        License: GPLv3+
 * =====================================================================================
 */

#include "Plasma.h"

// plasma defined global
Plasma  *plasma;
Species *species;

Plasma::Plasma(Setup *setup, FileIO *fileIO, Geometry *geo, const int _nfields) : nfields(_nfields) 
{

  species = new Species[SPECIES_MAX+1];

  // Set global parameters 
  debye2  = setup->get("Plasma.Debye2", 0. ); 
  B0      = setup->get("Plasma.B0"    , 1. );
       
  beta    = setup->get("Plasma.Beta"  , 0. );
  w_p     = setup->get("Plasma.w_p"   , 0. );
  global  = setup->get("Plasma.Global", 0  );
  cs      = setup->get("Plasma.cs"    , 1. );
 
  // Set reference lengths
    n_ref = setup->get("Plasma.ReferenceDensity    ", 1.);
    c_ref = setup->get("Plasma.ReferenceC          ", 1.);
    T_ref = setup->get("Plasma.ReferenceTemperature", 1.);
    L_ref = setup->get("Plasma.ReferenceLength     ", 1.);
  rho_ref = setup->get("Plasma.ReferenceLength     ", 1.);
     
  nfields = ((beta > 0.e0)  ? 2 : 1);
  nfields = (setup->get("Plasma.Bp", 0 ) == 1) ? 3 : nfields;
  Nq      = nfields;
      
  ///////////////// set adiabatic species //////////////////////////////////////////////////////
  
  std::string species_name = setup->get("Plasma.Species0.Name", "Unnamed") + "(ad.)";
  snprintf(species[0].name, sizeof(char) * std::min((size_t) 64, species_name.length()), "%s", species_name.c_str());
      
  species[0].n0     = setup->get("Plasma.Species0.Density" , 0. );
  species[0].T0     = setup->get("Plasma.Species0.Temperature" , 1. );
  species[0].q      = setup->get("Plasma.Species0.Charge" , 1. );
  species[0].m      = 0.;

  // this is dummy for flux average
  species[0].doGyro = setup->get("Plasma.Species0.FluxAverage", 0 );
  species[0].w_n    = setup->get("Plasma.Species0.Phase"      , 0.0 );
  species[0].w_T    = 0.;
      
  //////////////////////////  set kinetic species   ////////////////////////////////////////////
  
  for(int s = 1; s <= SPECIES_MAX; s++) { 

    std::string key          = "Plasma.Species" + Setup::num2str(s);

    std::string species_name = setup->get(key + ".Name"  , "Unnamed");
    snprintf(species[s].name, sizeof(char) * std::min((size_t) 64, species_name.length()), "%s", species_name.c_str());

    species[s].m         = setup->get(key + ".Mass"       , 1. );
    species[s].n0        = setup->get(key + ".Density"    , 0. );
    species[s].T0        = setup->get(key + ".Temperature", 1. );
    species[s].q         = setup->get(key + ".Charge"     , 1. );
    species[s].gyroModel = setup->get(key + ".gyroModel"  , (Nm > 1) ? "Gyro" : "Gyro-1" );
    
    species[s].doGyro = (species[s].gyroModel == "Gyro") ? 1 : 0;

    if  (species[s].doGyro) species[s].f0_str = setup->get(key + ".F0"      , "n/(pi*T)^1.5*exp(-v^2/T)*exp(-m/T)" );
    else                    species[s].f0_str = setup->get(key + ".F0"      , "n/(pi*T)^1.5*exp(-v^2/T)*T/Nm" );
        

    if(species[s].m < 1.e-10) check(-1, DMESG(std::string("Mass for species ") + std::string(species[s].name) + std::string(" choosen too low")));
  
        
    if(global) { 
    
      snprintf(species[s].n_name, 64, "%s", setup->get(key + ".Density"    , "0." ).c_str());
      snprintf(species[s].T_name, 64, "%s", setup->get(key + ".Temperature", "1." ).c_str());
        
      // Set Temperature and Density Gradient
      FunctionParser n_parser = setup->getFParser(); 
      FunctionParser T_parser = setup->getFParser();  
        
      check(((n_parser.Parse(species[s].n_name, "x") == -1) ? 1 : -1), DMESG("Parsing error of Initial condition n(x)"));
      check(((T_parser.Parse(species[s].T_name, "x") == -1) ? 1 : -1), DMESG("Parsing error of Initial condition T(x)"));

      // we not to normalize N, so that total density is equal in gyro-simulations 
      for(int x = NxLlB; x <= NxLuB; x++) { 
        species[s].n[x]   = n_parser.Eval(&X[x]);
        species[s].T[x]   = T_parser.Eval(&X[x]);
      //species[s].w_T[x] = T_parser.Eval(&X[x]);
      //species[s].w_n[x] = T_parser.Eval(&X[x]);
      //species[s].w_n[x] = T_parser.Eval(&X[x]);
      //species[s].w_n[x] = T_parser.Eval(&X[x]);

      } 
    } 
    else {
    
      species[s].w_T = setup->get(key + ".w_T", 0.0 );
      species[s].w_n = setup->get(key + ".w_n", 0.0 );
      species[s].n[NxLlB:NxLB] = species[s].n0;
      species[s].T[NxLlB:NxLB] = species[s].T0;
      //snprintf(species[s].n_name, 64, "%f", species[s].n0);
      //snprintf(species[s].T_name, 64, "%f", species[s].n0);
    }

    species[s].update(geo, cs);
  }   

  ////////////////////////   // make some simple checks
  
  //Total charge density
  double rho0_tot = 0.;
  
  for(int s = 0; s <= NsGuD; s++) rho0_tot += species[s].q * species[s].n0;

  if(rho0_tot > 1.e-8) check(setup->get("Plasma.checkTotalCharge", -1), DMESG("VIOLATING charge neutrality, check species q * n and TOTAL_SPECIES! Exciting...")); 

  initData(fileIO);
   
}


Plasma::~Plasma()
{
  delete[] species;
}


void Plasma::printOn(std::ostream &output) const 
{
         
  output << "Type       | " << (global ? " Global" : "Local") << " Cs   : " << cs << std::endl 
         << "Species  0 | ";

  // print adiabatic species
  if(species[0].n0 != 0.) {

    output << species[0].name 
           <<        " n : " << species[0].n0  << " q : " << species[0].q 
           <<        " T : " << species[0].T0 
           << " FluxAvrg : " << (species[0].doGyro ? "Yes" : "No") 
           <<    " Phase : " << species[0].w_n << " (adiabatic)" << std::endl; 

  } else output << "-- no adiabatic species --" << std::endl;
            
  // print kinetic species
  for(int s = 1; s <= Ns; s++) {

    output << "         " << s << " | "        << std::setw(12) << species[s].name  
           <<                    std::setprecision(2) 
           << "  n : " << species[s].n0  << "  q : " << species[s].q   
           << "  m : " << species[s].m   << "  T : " << species[s].T0  
           << " ωn : " << species[s].w_n << " ωT : " << species[s].w_T 
           << " Model : " << species[s].gyroModel       << std::endl;
  } 
}

void Plasma::initData(FileIO *fileIO) 
{
  
  hid_t plasmaGroup = fileIO->newGroup("Plasma");

  check(H5LTset_attribute_double(plasmaGroup, ".", "Debye2",  &debye2, 1), DMESG("HDF-5 Error"));
  check(H5LTset_attribute_double(plasmaGroup, ".", "beta"  ,  &beta  , 1), DMESG("HDF-5 Error"));
  check(H5LTset_attribute_double(plasmaGroup, ".", "B0"    ,  &B0    , 1), DMESG("HDF-5 Error"));
         
  //////////////////////// Set Table for species.
         
  
#pragma warning (disable : 1875) // ignore warnings about non-POD types
  size_t spec_offset[] = { HOFFSET( Species, name ), HOFFSET( Species, q  ), HOFFSET( Species, m ), HOFFSET( Species, n0), 
                           HOFFSET( Species, w_T  ), HOFFSET( Species, w_n) };
#pragma warning (enable  : 1875)

  size_t spec_sizes[]  = { sizeof(species[0].name ), sizeof(species[0].q  ), sizeof(species[0].m ), sizeof(species[0].n0), 
                           sizeof(species[0].w_T  ), sizeof(species[0].w_n) };

  hid_t spec_type[]    = { fileIO->str_tid         , H5T_NATIVE_DOUBLE     , H5T_NATIVE_DOUBLE    , H5T_NATIVE_DOUBLE    , 
                           H5T_NATIVE_DOUBLE       , H5T_NATIVE_DOUBLE      };

  char *spec_names[]   = { "Name"                  , "Charge"              , "Mass"               , "Density"            , 
                           "w_T"                   , "w_n"                  };

  // Note : +1 for adiabatic species
  check(H5TBmake_table("SpeciesTable", fileIO->getFileID(), "Species", (hsize_t) 6, (hsize_t) Ns+1, sizeof(Species), spec_names,
                       spec_offset, spec_type, Ns+1, NULL, 0, &species[0] ), DMESG("HDF-5 Error"));
        
  /////////////////////
         
  H5Gclose(plasmaGroup);
}

