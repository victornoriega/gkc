/*
 * =====================================================================================
 *
 *       Filename:  Vlasov_2D.cpp
 *
 *    Description: Vlasov Solver Implementation for 2D Geometry
 *                 or other special types.
 *
 *         Author: Paul P. Hilscher (2009-), 
 *
 *        License: GPLv3+
 * =====================================================================================
 */

#include "Vlasov/Vlasov_Island.h"
#include "Special/RootFinding.h"


VlasovIsland::VlasovIsland(Grid *_grid, Parallel *_parallel, Setup *_setup, FileIO *fileIO, 
                           Geometry *_geo, FFTSolver *fft, Benchmark *_bench, Collisions *_coll)    
: VlasovAux(_grid, _parallel, _setup, fileIO, _geo, fft, _bench, _coll) 
{

  /// Set Magnetic Island structure
  width = setup->get("Island.Width"  , 0. ); 
  i     = setup->get("Island.Mode"   , 1  ); 
  omega = setup->get("Island.Omega"  , 0.0); 
  Ap_ky = setup->get("Island.Ap_ky"  , 0.0); 

  //// Setup Magnetic Island structure
  ArrayX    = nct::allocate(nct::Range(NxGlB-2, Nx+8))(&MagIs, &dMagIs_dx);
  ArrayY    = nct::allocate(grid->RkyLD)(&ky_filter);
  ArrayPhi0 = nct::allocate(grid->RzLB, grid->RkyLD, grid->RxLB4)(&Phi0, &Psi0);
    
  const double ky0             = setup->get("Island.Filter.ky0"     , 1.2); 
  const double filter_gradient = setup->get("Island.Filter.Gradient", 10.); 
  const double signf           = setup->get("Island.Filter.Sign"    , 0.5); 
    
  for(int y_k = NkyLlD; y_k <= NkyLuD; y_k++) {  
    ky_filter[y_k] = 0.5 + signf*tanh(filter_gradient*(fft->ky(y_k)-ky0));
  }  
  
  const double p[] = { 0.13828847,  0.70216594, -0.01033686 };
  auto psi         = [=](double x) -> double { return (1. + p[0]*pow(pow2(x),p[1])) * exp( p[2] * pow2(x)); };
    
  // Calculates the island width for given scale factor
  // We integrate from the separatrix until Ly/2, which gives
  // us half the size of the island.
  auto IslandWidth = [=](double scale) -> double {

    if(scale == 0.) return 0.;
    
    // \partial f(x,y) / \partial y
    auto IslandForm = [=](double x, double y) -> double {
      return scale * psi(x) * sin(2.*M_PI/Ly * y) * (2.*M_PI/Ly);
    };

    double x_np1 = 0., x_n = 0.   , ///< Start integration at y=0 
           y_np1 = 0., y_n = 1.e-2; ///< Use small offset at  x 0

    int nSteps =  1024;              ///< Number of integration steps
    double ds  = Ly/2. / nSteps;    ///< Step size

    // Use Trapezoidal rule to integrate over line
    // Note that the role of (x,y) is switched
    for(int step = 0; step < nSteps; step++) {

        x_np1 = x_n + ds;
        y_np1 = y_n + 0.5 * ds * (IslandForm(y_n, x_n) + IslandForm(y_n, x_np1));

        // Update
        y_n = y_np1;
        x_n = x_np1;
    }
    return 2. * y_n; /// We measure the full-width of the island
  };
    
  double width_scale = RootFinding::BiSection([=](double w)-> double { return IslandWidth(w) - width; }, 0., 100.);
  

  // below mess starts 

  // helper function to calculate first order derivative using second order central differences 
  auto CD2 = [](std::function<double (double)> func, double x)-> double {
         const double eps = 1.e-8 * x; // "most accurate" sqrt(dp) * x
         return (func(x + eps) - func(x - eps)) / (2.*eps);  
  };
  auto CD4 = [](std::function<double (double)> func, double x)-> double {
         const double eps = 1.e-8 * x; // "most accurate" sqrt(dp) * x
         return 
           (8. * (func(x +    eps) - func(x -    eps)) 
            -    (func(x + 2.*eps) - func(x - 2.*eps))) / (12.*eps);  
  };

  // setup magnetic island arrays
  for(int x = NxGlD-4; x <= NxGuD+4; x++) { 

    // The poloidal island term is directly included per mode-mode coupling
    // multiply 0.5 as : cos(y) = 0.5 * [exp(-ky y) - exp(ky y)]
    MagIs    [x] = 0.5 * width_scale * psi(X[x]);
    dMagIs_dx[x] = 0.5 * width_scale * CD4(psi, X[x]);
  }    
   
  // if electro-magnetic version is used initialize fields
  if(Nq >= 2) {
  
    ArrayXi(&Xi_lin);
    ArrayG(&G_lin );

    [=](CComplex Psi0[NzLB][Nky][NxLB+4]) {

      for(int z = NzLlD  ; z <= NzLuD  ; z++) { for(int y_k = NkyLlD; y_k <= NkyLuD; y_k++) { 
      for(int x = NxLlB-2; x <= NxLuB+2; x++) {

       Psi0[z][y_k][x] = - ((y_k == 1 ) ? 1. : 0.) * MagIs[x];

      } } }
      }( (A3zz) Psi0);
  }

  initDataOutput(setup, fileIO);

}


void VlasovIsland::solve(std::string equation_type, Fields *fields, CComplex *f_in, CComplex *f_out, double dt, int rk_step, const double rk[3]) 
{
  // do I need both, we can stick to e-m ? Speed penalty ?
  if(0) ;  
  else if(equation_type == "2D_Island") 
    
      Vlasov_2D_Island((A6zz) f_in, (A6zz) f_out, (A6zz) f0, (A6zz) f, 
                       (A6zz) ft  , (A6zz) Coll, (A6zz) fields->Field, (A3zz) nonLinearTerm,
                       MagIs, dMagIs_dx, X, V, M, (A3zz) Psi0, (A4zz) fields->Field0, dt, rk_step, rk);
  
  else if(equation_type == "2D_Island_Orig") 
    
      Vlasov_2D_Island((A6zz) f_in, (A6zz) f_out, (A6zz) f0, (A6zz) f, 
                       (A6zz) ft  , (A6zz) Coll, (A6zz) fields->Field, (A3zz) nonLinearTerm,
                       MagIs, dMagIs_dx, X, V, M, (A3zz) Psi0, (A4zz) fields->Field0, dt, rk_step, rk);
  
  else if(equation_type == "2D_Island_EM") 
    
      Vlasov_2D_Island_EM   ((A6zz) f_in, (A6zz) f_out, (A6zz) f0, (A6zz) f,
                   (A6zz) ft, (A6zz) Coll, (A6zz) fields->Field, (A3zz) nonLinearTerm,
                   (A4zz) Xi, (A4zz) G, (A4zz) Xi_lin, (A4zz) G_lin, (A3zz) Psi0, (A4zz) fields->Field0, dt, rk_step, rk);

  else if(equation_type == "2D_Island_Filter") 
    
      Vlasov_2D_Island_filter((A6zz) f_in, (A6zz) f_out, (A6zz) f0, (A6zz) f, 
                       (A6zz) ft  , (A6zz) Coll, (A6zz) fields->Field, (A3zz) nonLinearTerm,
                       MagIs, dMagIs_dx, X, V, M, dt, rk_step, rk);

  else if(equation_type == "2D_Island_Equi")

      Vlasov_2D_Island_Equi((A6zz) f_in, (A6zz) f_out, (A6zz) f0, (A6zz) f, 
                       (A6zz) ft  , (A6zz) Coll, (A6zz) fields->Field, (A3zz) nonLinearTerm,
                       X, V, M, dt, rk_step, rk);

  else   check(-1, DMESG("No Such Equation"));


  return;

}

void VlasovIsland::Vlasov_2D_Island(
                           CComplex fs        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll[NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex nonLinearTerm                  [Nky][NxLD  ][NvLD],
                           const double MagIs[NxGB], const double dMagIs_dx[NxGB], 
                           const double X[NxGB+4], const double V[NvGB], const double M[NmGB],
                           const CComplex Psi0                    [NzLB][Nky][NxLB+4]      ,
                           CComplex Field0[Nq][NzLD][Nky][NxLD]   ,
                           const double dt, const int rk_step, const double rk[3])
{ 

  if((Nq > 1)) {
   Field0[Field::Ap][NzLlD:NzLD][:][NxLlD:NxLD] = Psi0[NzLlD:NzLD][:][NxLlD:NxLD];
  }


    //static double Time = 0.;  if(rk_step == 1) Time += dt;
    //const CComplex IslandPhase = cexp(_imag * Time * omega);

  for(int s = NsLlD; s <= NsLuD; s++) {
        
    // small abbrevations
    const double w_n   = species[s].w_n;
    const double w_T   = species[s].w_T;
    const double alpha = species[s].alpha;
    const double sigma = species[s].sigma;
    const double Temp  = species[s].T0;
    const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;

    const double kw_T = 1./Temp;

    bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      
    const double rho_t2 = species[s].T0 * species[s].m / (pow2(species[s].q) * plasma->B0); 

  for(int m = NmLlD; m <= NmLuD; m++) { for(int z = NzLlD; z <= NzLuD; z++) {  
        
    //calculate non-linear term (rk_step == 0 for eigenvalue calculations)
    if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(nullptr, nullptr, fs, Fields, z, m, s, nonLinearTerm, Xi_max, false); 
  
  // don't evolve Nyquist
  #pragma omp for
  for(int y_k = 0; y_k < Nky-1; y_k++) {

    // Note : for negative modes we need to use complex conjugate value
    const CComplex iky     = _imag * fft->ky(y_k);
        
    // We need to take care of boundaries :
    //  if y_k > Nky -1 : ky = 0.
    //  if y_k < 0      : ky = - ky(y_k)
    const CComplex iky_1   = _imag * fft->ky(1);
    const CComplex iky_p1  = ((y_k+1) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+1);
    const CComplex iky_m1  = _imag * fft->ky(y_k-1); 
    
    const double ky_4     = pow4(fft->ky(1));

  for(int x = NxLlD; x <= NxLuD; x++) {  

    const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
          
  /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
        
  
    // Note : if y_k is at Nyquist or above, we have no coupling with higher frequencies (=0)
    const CComplex phi_p1 = ((y_k+1) >= Nky-1) ? 0.                                          : Fields[Field::phi][s][m][z][y_k+1][x] ;
    const CComplex phi_m1 = ((y_k-1) <  0    ) ? conj(Fields[Field::phi][s][m][z][1-y_k][x]) : Fields[Field::phi][s][m][z][y_k-1][x] ;

    // X-derivative (first derivative with CD-4) of phi for poloidal mode +1, take care of Nyquist frequency
    const CComplex dphi_dx_p1 = ( (y_k+1) >= Nky-1) 
        ? 0.
        : (8.*(Fields[Field::phi][s][m][z][y_k+1][x+1] - Fields[Field::phi][s][m][z][y_k+1][x-1]) 
            - (Fields[Field::phi][s][m][z][y_k+1][x+2] - Fields[Field::phi][s][m][z][y_k+1][x-2])) * _kw_12_dx  ;

    // X-derivative (first derivative CD-4 )of phi for poloidal mode -1, take care of complex conjugate relation for y_k=-1
    const CComplex dphi_dx_m1 = ( (y_k-1) < 0 ) 
    
        ?  conj(8.*(Fields[Field::phi][s][m][z][1-y_k][x+1] - Fields[Field::phi][s][m][z][1-y_k][x-1]) 
                 - (Fields[Field::phi][s][m][z][1-y_k][x+2] - Fields[Field::phi][s][m][z][1-y_k][x-2])) * _kw_12_dx
        :      (8.*(Fields[Field::phi][s][m][z][y_k-1][x+1] - Fields[Field::phi][s][m][z][y_k-1][x-1]) 
                 - (Fields[Field::phi][s][m][z][y_k-1][x+2] - Fields[Field::phi][s][m][z][y_k-1][x-2])) * _kw_12_dx ;
        
    // The magnetic island
    
    //  For the latter term, the intrigate derivative is the \partial_y * Island
    //  remember the island structure is 
    //  \partial_y (e^{imx} + e^{-imx}) = (i m) * ( e^{imx} - e^{-imx} )
    //
    const CComplex Island_phi = dMagIs_dx[x] * ( iky_m1 * phi_m1 + iky_p1 * phi_p1) - MagIs[x] *  iky_1 * (dphi_dx_m1 -  dphi_dx_p1); 
    ///////////////////////////////////////////////////////////////////////////////
            
    const CComplex ikp = geo->get_kp(x, iky, z);
   
  // velocity space magic
  simd_for(int v = NvLlD; v <= NvLuD; v++) {

  const CComplex g   = fs[s][m][z][y_k][x][v];
  const CComplex f0_ = f0[s][m][z][y_k][x][v];

      /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
      
      // X-derivative of f1 (1-CD4) for poloidal mode +1, take care of Nyquist frequency
      const CComplex dfs_dx_p1  =  ((y_k+1) >= Nky-1)
        ? 0.
        : (8.*(fs[s][m][z][y_k+1][x+1][v] - fs[s][m][z][y_k+1][x-1][v])  
            - (fs[s][m][z][y_k+1][x+2][v] - fs[s][m][z][y_k+1][x-2][v])) * _kw_12_dx;

      // X-derivative of f1 (1-CD4) for poloidal mode -1, take care of complex conjugate relation for y_k=-1 
      const CComplex dfs_dx_m1  =  ( (y_k-1) < 0  )

         ? conj(8.*(fs[s][m][z][1-y_k][x+1][v] - fs[s][m][z][1-y_k][x-1][v])  
                 - (fs[s][m][z][1-y_k][x+2][v] - fs[s][m][z][1-y_k][x-2][v])) * _kw_12_dx 
         :     (8.*(fs[s][m][z][y_k-1][x+1][v] - fs[s][m][z][y_k-1][x-1][v])  
                 - (fs[s][m][z][y_k-1][x+2][v] - fs[s][m][z][y_k-1][x-2][v])) * _kw_12_dx;

      // Note Nky-1 is the maximum mode number Nky = 6 i-> [ 0, 1, 2, 3, 4, 5] 
      const CComplex fs_p1 = ((y_k+1) >= Nky-1) ? 0.                             : fs[s][m][z][y_k+1][x][v] ;
      const CComplex fs_m1 = ((y_k-1) <  0    ) ? conj(fs[s][m][z][1-y_k][x][v]) : fs[s][m][z][y_k-1][x][v] ;
         
  // Coupling of phase-space with Island mode-mode coupling
  const CComplex Island_g = dMagIs_dx[x] * (iky_m1 * fs_m1 + iky_p1 * fs_p1) - MagIs[x] * iky_1 * (dfs_dx_m1 - dfs_dx_p1);

      const CComplex d4_dx_fs = (-39. * (fs[s][m][z][y_k][x+1][v] - fs[s][m][z][y_k][x-1][v])  
                                + 12. * (fs[s][m][z][y_k][x+2][v] - fs[s][m][z][y_k][x-2][v]) 
                                + 56. *  fs[s][m][z][y_k][x  ][v]) * _kw_16_dx4;
      
      const CComplex hypvisc_xy = hyp_visc[DIR_X] *  d4_dx_fs + hyp_visc[DIR_Y] * ky_4 * g;

    /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
             
  CComplex dg_dt =
   
    //-  IslandPhase * alpha ... 
    - alpha * V[v] * (Island_g + sigma * Island_phi * f0_)           // Island term
    + nonLinearTerm[y_k][x][v]                                        // Non-linear ( array is zero for linear simulations) 
    - iky* (w_n + w_T * (((V[v]*V[v])+ M[m])*kw_T  - sub)) * f0_ * 
        phi_ 
        //(phi_ - (y_k == 1 ? Ap_ky * V[v] * MagIs[x] : 0.  )  )                 // Source term (Temperature/Density gradient)
    - alpha * V[v]* ikp  * ( g + sigma * phi_ * f0_)                 // Linear Landau damping
    + Coll[s][m][z][y_k][x][v]                                        // Collisional operator
;//       +  hypvisc_xy        ;                                       // Hyperviscosity for stabilizing the scheme
         
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        
    if(y_k == 0    ) dg_dt = creal(dg_dt);


    //////////////////////////// Vlasov End ////////////////////////////
    //  time-integrate the distribution function    
    ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] + rk[1] * dg_dt             ;
    fss[s][m][z][y_k][x][v] = f1[s][m][z][y_k][x][v]         + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;
      
    } } } } }
   
  } // s

}


void VlasovIsland::initDataOutput(Setup *setup, FileIO *fileIO) 
{
    
   hid_t islandGroup = fileIO->newGroup("Islands");
          
   // Length scale
   check(H5LTset_attribute_double(islandGroup, ".", "MagIs"    , &MagIs[NxGlD]    , Nx), DMESG("Attribute"));
   check(H5LTset_attribute_double(islandGroup, ".", "dMagIs_dx", &dMagIs_dx[NxGlD], Nx), DMESG("Attribute"));
   
   check(H5LTset_attribute_double(islandGroup, ".", "Width", &width, 1), DMESG("Attribute"));
   check(H5LTset_attribute_double(islandGroup, ".", "Omega", &omega, 1), DMESG("Attribute"));
   check(H5LTset_attribute_int   (islandGroup, ".", "Mode" , &i    , 1), DMESG("Attribute"));
         
    H5Gclose(islandGroup); 

}   

void VlasovIsland::printOn(std::ostream &output) const 
{
         Vlasov::printOn(output);
         output   << "Island     |  Width : " << width << " mode : " << i << " ω : " << omega << std::endl;

}


void VlasovIsland::Vlasov_2D_Island_Equi(
                           CComplex fs       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss      [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0 [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1 [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll      [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex nonLinearTerm                  [Nky][NxLD  ][NvLD],
                           const double X[NxGB+4], const double V[NvGB], const double M[NmGB],
                           const double dt, const int rk_step, const double rk[3])
{ 


    for(int s = NsLlD; s <= NsLuD; s++) {
        
      // small abbrevations
      const double w_n   = species[s].w_n;
      const double w_T   = species[s].w_T;
      const double alpha = species[s].alpha;
      const double sigma = species[s].sigma;
      const double Temp  = species[s].T0;
      const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;

      const double kw_T = 1./Temp;

      bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      
      const double rho_t2 = species[s].T0 * species[s].m / (pow2(species[s].q) * plasma->B0); 


      for(int m=NmLlD; m<=NmLuD; m++) { for(int z=NzLlD; z<= NzLuD;z++) {  
        
        //calculate non-linear term (rk_step == 0 for eigenvalue calculations)
        if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(nullptr, nullptr, fs, Fields, z, m, s, nonLinearTerm, Xi_max, false); 
        
        for(int y_k=NkyLlD; y_k<= NkyLuD;y_k++) {

        // Note : for negative modes we need to use complex conjugate value
        const CComplex ky = _imag * fft->ky(y_k) ;
        
        for(int x = NxLlD; x <= NxLuD;x++) {  
          
          const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
          
          const CComplex dphi_dx  = (8.*(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])
                                      - (Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2])) * _kw_12_dx  ;  

        const CComplex kp = geo->get_kp(x, ky, z) - dMagIs_dx[x] * ky;
           
        CComplex half_eta_kperp2_phi = 0;

        
        // velocity space magic
        simd_for(int v=NvLlD; v<= NvLuD;v++) {

            const CComplex g    = fs[s][m][z][y_k][x][v];
            const CComplex f0_  = f0[s][m][z][y_k][x][v];


        /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
      

        /////////// Collisions ////////////////////////////////////////////////////////////////////

        const CComplex dfs_dv    = (8.  *(fs[s][m][z][y_k][x][v+1] - fs[s][m][z][y_k][x][v-1]) - 
                                         (fs[s][m][z][y_k][x][v+2] - fs[s][m][z][y_k][x][v-2])) * _kw_12_dv;

        const CComplex ddfs_dvv  = (16. *(fs[s][m][z][y_k][x][v+1] + fs[s][m][z][y_k][x][v-1]) -
                                         (fs[s][m][z][y_k][x][v+2] + fs[s][m][z][y_k][x][v-2]) 
                                    - 30.*fs[s][m][z][y_k][x][v  ]) * _kw_12_dv_dv;

        //// Hypervisocisty to stabilize simulation
        /*
        const double hypvisc_phi_val = -1.e-5;
        
        const CComplex d4_dx_phi    = (-39. *(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])  
                                      + 12. *(Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2]) 
                                      + 56. * Fields[Field::phi][s][m][z][y_k][x  ])/pow4(dx);

        const CComplex hypvisc_phi    = hypvisc_phi_val * ( d4_dx_phi + pow4(ky) * phi_);
*/

        /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
             
        const CComplex dg_dt =
             +  nonLinearTerm[y_k][x][v]                                             // Non-linear ( array is zero for linear simulations) 
             +  ky* (-(w_n + w_T * (((V[v]*V[v])+ M[m])*kw_T  - sub)) * f0_ * phi_     // Driving term (Temperature/Density gradient)
             -  half_eta_kperp2_phi * f0_)                                            // Contributions from gyro-1 (0 if not neq Gyro-1)
             -  alpha  * V[v]* kp  * ( g + sigma * phi_ * f0_)                        // Linear Landau damping
             +  Coll[s][m][z][y_k][x][v]  ;                                           // Collisional operator
         
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        


        //////////////////////////// Vlasov End ////////////////////////////
        //  time-integrate the distribution function    
        ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] + rk[1] * dg_dt             ;
        fss[s][m][z][y_k][x][v] = f1[s][m][z][y_k][x][v]         + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;
      
        }}} }}
   }
//  std::cout << "OUT" << std::flush;  

}


void VlasovIsland::Vlasov_2D_Island_filter(
                           CComplex fs        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll[NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex nonLinearTerm                  [Nky][NxLD  ][NvLD],
                           const double MagIs[NxGB], const double dMagIs_dx[NxGB], 
                           const double X[NxGB+4], const double V[NvGB], const double M[NmGB],
                           const double dt, const int rk_step, const double rk[3])
{ 

  
    for(int s = NsLlD; s <= NsLuD; s++) {
        
      // small abbrevations
      const double w_n   = species[s].w_n;
      const double w_T   = species[s].w_T;
      const double alpha = species[s].alpha;
      const double sigma = species[s].sigma;
      const double Temp  = species[s].T0;
      const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;

      const double kw_T = 1./Temp;

      bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      
      const double rho_t2 = species[s].T0 * species[s].m / (pow2(species[s].q) * plasma->B0); 


      for(int m=NmLlD; m<=NmLuD; m++) { for(int z=NzLlD; z<= NzLuD;z++) {  
        
        //calculate non-linear term (rk_step == 0 for eigenvalue calculations)
        if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(nullptr, nullptr, fs, Fields, z, m, s, nonLinearTerm, Xi_max, false); 
        
        for(int y_k=NkyLlD; y_k<= NkyLuD;y_k++) {

        
        // Note : for negative modes we need to use complex conjugate value
        const CComplex ky     = _imag * fft->ky(y_k);
        
        // We need to take care of boundaries. For poloidal numbers y_k > N_k-1, we  use zero.
        // For y_k < 0, the corresponding complex conjugate value is used.
        // is ky_p1 = ky or not ?!
        const CComplex ky_1   = _imag * fft->ky(i);
        const CComplex ky_p1  = ((y_k+i) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+i);
        const CComplex ky_m1  = _imag * fft->ky(y_k-i); 
        
        //const CComplex ky_1   = _imag * fft->ky(1);
        //const CComplex ky_m1  = _imag * (y_k == 0    ) ? -fft-ky_1 : _imag * fft->ky(y_k-i); 
        //const CComplex ky_p1  = ((y_k+i) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+i);
        //const CComplex ky_m1  = _imag * fft->ky(y_k-i);

        
        for(int x=NxLlD; x<= NxLuD;x++) {  

          
          const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
          
          const CComplex dphi_dx  = (8.*(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])
                                      - (Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2])) * _kw_12_dx  ;  

          /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
        
          // NOTE :  at the Nyquist frequency we have no coupling with higher frequencies (actually phi(m=Ny) = 0. anyway)

          const CComplex     phi_p1 = ((y_k+i) >= Nky-1) ? 0.                                          : Fields[Field::phi][s][m][z][y_k+i][x] ;
          const CComplex     phi_m1 = ((y_k-i) <  0    ) ? conj(Fields[Field::phi][s][m][z][i-y_k][x]) : Fields[Field::phi][s][m][z][y_k-i][x] ;
          //const CComplex     phi_m1 = ( y_k ==  0   ) ? (Fields[Field::phi][s][m][z][1][x]) : Fields[Field::phi][s][m][z][y_k-1][x] ;

          
          // X-derivative (First Derivative with Central Difference 4th) of phi for poloidal mode +1, take care of Nyquist frequency
          const CComplex dphi_dx_p1 = ( (y_k+i) >= Nky-1) 
                                   ? 0.

                                   :      (8.*(Fields[Field::phi][s][m][z][y_k+i][x+1] - Fields[Field::phi][s][m][z][y_k+i][x-1]) 
                                            - (Fields[Field::phi][s][m][z][y_k+i][x+2] - Fields[Field::phi][s][m][z][y_k+i][x-2])) * _kw_12_dx  ;

          // X-derivative (1st deriv. CD-4 )of phi for poloidal mode -1, take care of complex conjugate relation for y_k=-1
          const CComplex dphi_dx_m1 = ( (y_k-i) < 0 ) 
                                   ?  conj(8.*(Fields[Field::phi][s][m][z][i-y_k][x+1] - Fields[Field::phi][s][m][z][i-y_k][x-1]) 
                                            - (Fields[Field::phi][s][m][z][i-y_k][x+2] - Fields[Field::phi][s][m][z][i-y_k][x-2])) * _kw_12_dx
                                   :      (8.*(Fields[Field::phi][s][m][z][y_k-i][x+1] - Fields[Field::phi][s][m][z][y_k-i][x-1]) 
                                            - (Fields[Field::phi][s][m][z][y_k-i][x+2] - Fields[Field::phi][s][m][z][y_k-i][x-2])) * _kw_12_dx ;
        
       
        // The magnetic island
    
        //  For the latter term, the intrigate derivative is the \partial_y * Island
        //  remember the island structure is 
        //  \partial_y (e^{imx} + e^{-imx}) = (i m) * ( e^{imx} - e^{-imx} )
        //
        const CComplex Island_phi = dMagIs_dx[x] * (ky_m1 * phi_m1 + ky_p1 * phi_p1) - MagIs[x] *  ky_1 * (dphi_dx_m1 - dphi_dx_p1);
        
             ///////////////////////////////////////////////////////////////////////////////
            
        const CComplex kp = geo->get_kp(x, ky, z);
           
        CComplex half_eta_kperp2_phi = 0;

        if(isGyro1) { // first order approximation for gyro-kinetics
             const CComplex ddphi_dx_dx = (16. *(Fields[Field::phi][s][m][z][y_k][x+1] + Fields[Field::phi][s][m][z][y_k][x-1]) 
                                              - (Fields[Field::phi][s][m][z][y_k][x+2] + Fields[Field::phi][s][m][z][y_k][x-2]) 
                                         - 30.*phi_) * _kw_12_dx_dx;

             half_eta_kperp2_phi     = rho_t2 * 0.5 * w_T  * ( (ky*ky) * phi_ + ddphi_dx_dx ) ; 
         }
             
        
        // velocity space magic
        simd_for(int v=NvLlD; v<= NvLuD;v++) {

            const CComplex g    = fs[s][m][z][y_k][x][v];
            const CComplex f0_  = f0[s][m][z][y_k][x][v];


        /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
      
          
        // X-derivative of f1 (1-CD4) for poloidal mode +1, take care of Nyquist frequency
        const CComplex dfs_dx_p1  =  ((y_k+i) >= Nky-i)

                            ? 0.

                         //   : (8. *(fs[s][m][z][y_k+i][x+1][v] - fs[s][m][z][y_k+i][x-1][v])  
                         //        - (fs[s][m][z][y_k+i][x+2][v] - fs[s][m][z][y_k+i][x-2][v])) * _kw_12_dx;
                            : (-    fs[s][m][z][y_k+i][x+2][v] + 6. * fs[s][m][z][y_k+i][x+1][v]  
                               - 3.*fs[s][m][z][y_k+i][x  ][v] - 2. * fs[s][m][z][y_k+i][x-1][v]) / (6. * dx);


        // X-derivative of f1 (1-CD4) for poloidal mode -1, take care of complex conjugate relation for y_k=-1 
        const CComplex dfs_dx_m1  =  ( (y_k-i) < 0  )

                    //    ? conj(8. *(fs[s][m][z][i-y_k][x+1][v] - fs[s][m][z][i-y_k][x-1][v])  
                    //             - (fs[s][m][z][i-y_k][x+2][v] - fs[s][m][z][i-y_k][x-2][v])) * _kw_12_dx 
                          ? conj(-    fs[s][m][z][i-y_k][x+2][v] + 6. * fs[s][m][z][i-y_k][x+1][v]  
                                 - 3.*fs[s][m][z][i-y_k][x  ][v] - 2. * fs[s][m][z][i-y_k][x-1][v]) / (6. * dx)

                        //:     (8. *(fs[s][m][z][y_k-i][x+1][v] - fs[s][m][z][y_k-i][x-1][v])  
                        //         - (fs[s][m][z][y_k-i][x+2][v] - fs[s][m][z][y_k-i][x-2][v])) * _kw_12_dx;
                          :     (-    fs[s][m][z][i-y_k][x+2][v] + 6. * fs[s][m][z][i-y_k][x+1][v]  
                                 - 3.*fs[s][m][z][i-y_k][x  ][v] - 2. * fs[s][m][z][i-y_k][x-1][v]) / (6. * dx);

        // Note Nky-1 is the maximum mode number Nky = 6 i-> [ 0, 1, 2, 3, 4, 5] 
        const CComplex fs_p1      = ((y_k+i) >= Nky-1) ? 0.                             : fs[s][m][z][y_k+i][x][v] ;
        const CComplex fs_m1      = ((y_k-i) <  0    ) ? conj(fs[s][m][z][i-y_k][x][v]) : fs[s][m][z][y_k-i][x][v] ;
        //const CComplex fs_m1      = (y_k ==  0   ) ? (fs[s][m][z][1][x][v]) : fs[s][m][z][y_k-1][x][v] ;
         
        // Coupling of phase-space with Island mode-mode coupling
        const CComplex Island_g =  dMagIs_dx[x] * (ky_m1 * fs_m1  + ky_p1 * fs_p1 )  -  MagIs[x]  * ky_1 *  (dfs_dx_m1  - dfs_dx_p1 )  ;

        
        /////////// Collisions ////////////////////////////////////////////////////////////////////

        const CComplex dfs_dv    = (8.  *(fs[s][m][z][y_k][x][v+1] - fs[s][m][z][y_k][x][v-1]) - 
                                         (fs[s][m][z][y_k][x][v+2] - fs[s][m][z][y_k][x][v-2])) * _kw_12_dv;

        const CComplex ddfs_dvv  = (16. *(fs[s][m][z][y_k][x][v+1] + fs[s][m][z][y_k][x][v-1]) -
                                         (fs[s][m][z][y_k][x][v+2] + fs[s][m][z][y_k][x][v-2]) 
                                    - 30.*fs[s][m][z][y_k][x][v  ]) * _kw_12_dv_dv;

        //// Hypervisocisty to stabilize simulation
        /*
        const double hypvisc_phi_val = -1.e-5;
        
        const CComplex d4_dx_phi    = (-39. *(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])  
                                      + 12. *(Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2]) 
                                      + 56. * Fields[Field::phi][s][m][z][y_k][x  ])/pow4(dx);

        const CComplex hypvisc_phi    = hypvisc_phi_val * ( d4_dx_phi + pow4(ky) * phi_);
*/

        /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
             
        const CComplex dg_dt =
          ky_filter[y_k] * (                                                          // filter artifically remove parts
             -  alpha * V[v] * (Island_g + sigma * Island_phi * f0_) +                // Island term
             +  nonLinearTerm[y_k][x][v]                                             // Non-linear ( array is zero for linear simulations) 
             +  ky* (-(w_n + w_T * (((V[v]*V[v])+ M[m])*kw_T  - sub)) * f0_ * phi_    // Driving term (Temperature/Density gradient)
             -  half_eta_kperp2_phi * f0_)                                            // Contributions from gyro-1 (0 if not neq Gyro-1)
             -  alpha  * V[v]* kp  * ( g + sigma * phi_ * f0_)                        // Linear Landau damping
             +  Coll[s][m][z][y_k][x][v]                                             // Collisional operator
             );
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        


        //////////////////////////// Vlasov End ////////////////////////////
        //  time-integrate the distribution function    
        ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] + rk[1] * dg_dt             ;
        fss[s][m][z][y_k][x][v] = f1[s][m][z][y_k][x][v]         + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;
      
        }}} }}
   }

}



void VlasovIsland::Vlasov_2D_Island_EM(
                           CComplex fs       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss      [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0 [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1 [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll      [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex    nonLinearTerm               [Nky][NxLD  ][NvLD],
                           CComplex Xi       [NzLB][Nky][NxLB+4][NvLB],
                           CComplex G        [NzLB][Nky][NxLB][NvLB],
                           CComplex Xi_lin       [NzLB][Nky][NxLB+4][NvLB],
                           CComplex G_lin        [NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Psi0  [NzLB][Nky][NxLB+4]      ,
                           CComplex Field0[Nq][NzLD][Nky][NxLD]   ,
                           const double dt, const int rk_step, const double rk[3])
{ 

  // Add modified Ap from the Island to the field equations (gyro-averaging neglected)
   for(int s = NsLlD; s <= NsLuD; s++) { for(int m = NmLlD; m <= NmLuD; m++) { 
  
   Fields[Field::Ap][s][m][NzLlB:NzLB][0:Nky][NxLlB-2:NxLB+4] = Psi0[NzLlB:NzLB][0:Nky][NxLlB-2:NxLB+4];
   
   } }
   
   Field0[Field::Ap][NzLlD:NzLD][0:Nky][NxLlD:NxLD] = Psi0[NzLlD:NzLD][0:Nky][NxLlD:NxLD];


   ////
   for(int s = NsLlD; s <= NsLuD; s++) {
        
      const double w_n   = species[s].w_n;
      const double w_T   = species[s].w_T;
      const double alpha = species[s].alpha;
      const double sigma = species[s].sigma;
      const double Temp  = species[s].T0;
    
      const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;
      
      bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      

    for(int m = NmLlD; m <= NmLuD; m++) { 
 
          if(doNonLinear) setupXiAndG    (fs, f0 , Fields, Xi    , G    , (A3zz) Phi0, m , s);
          else            setupXiAndG_lin(fs, f0 , Fields, Xi_lin, G_lin, (A3zz) Phi0, m , s);
       
    for(int z = NzLlD; z <= NzLuD; z++) { 
      
          if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(G    , Xi    , nullptr, nullptr, z, m, s, nonLinearTerm, Xi_max, true);
          else                              calculateExBNonLinearity(G_lin, Xi_lin, nullptr, nullptr, z, m, s, nonLinearTerm, Xi_max, true);
    
    #pragma omp for
    for(int y_k = NkyLlD; y_k <= NkyLuD; y_k++) { 
    for(int x   = NxLlD ; x   <= NxLuD ;   x++) { 

      const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
               
      const CComplex dphi_dx = (8.*(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1]) 
                                 - (Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2])) * _kw_12_dx;

      const CComplex ky = _imag *  std::abs(fft->ky(y_k));
      const CComplex kp = geo->get_kp(x, ky, z);

    #pragma ivdep
    #pragma vector always 
    for(int v = NvLlD; v <= NvLuD; v++) {
           
      // sign has no influence on result ...
      const CComplex kp = geo->get_kp(x, ky, z);

      const CComplex g    = fs[s][m][z][y_k][x][v];
      const CComplex f0_  = f0[s][m][z][y_k][x][v];

      const CComplex G_   =  G[z][y_k][x][v];
      const CComplex Xi_  = Xi[z][y_k][x][v];

    
    /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
   
            
    //const CComplex dg_dt = 
    const CComplex dg_dt = 
    
    +  nonLinearTerm[y_k][x][v]                                          // Non-linear ( array is zero for linear simulations) 
    -  ky* (w_n + w_T * ((V[v]*V[v]+ M[m])/Temp  - sub)) * f0_ *    // Driving term (Temperature/Density gradient)
        phi_                  // Source term (Temperature/Density gradient)
        //(phi_ - (y_k == 1 ? Ap_ky * V[v] * MagIs[x] : 0.  )  )                 // Source term (Temperature/Density gradient)
    -  alpha  * V[v]* kp  * ( g + sigma * phi_ * f0_)                    // Linear Landau damping
    +  Coll[s][m][z][y_k][x][v]  ;                                       // Collisional operator
         
 
    //////////////////////////// Vlasov End ////////////////////////////
  
    //  time-integrate the distribution function    
    ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] +  rk[1] * dg_dt             ;
    fss[s][m][z][y_k][x][v] =         f1[s][m][z][y_k][x][v] + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;

   } } } 
   } }
   }
}

void VlasovIsland::setupXiAndG_lin(
                           const CComplex g          [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0         [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields [Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex Xi                           [NzLB][Nky][NxLB+4][NvLB],
                           CComplex G                            [NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Phi0                   [NzLB][Nky][NxLB+4]      ,
                           const int m, const int s) 
{
  const double alpha = species[s].alpha;
  const double sigma = species[s].sigma;
  
  const double aeb   =  alpha * geo->eps_hat * plasma->beta; 
  const double saeb  =  sigma * aeb;

  const bool useAp   = (Nq >= 2);
  const bool useBp   = (Nq >= 3);


  // ICC vectorizes useAp/useBp into separate lopps, check for any speed penelity ? 
  #pragma omp for collapse(2)
  for(int z = NzLlB; z <= NzLuB; z++) {      for(int y_k = NkyLlD; y_k <= NkyLuD; y_k++) { 
  for(int x = NxLlB; x <= NxLuB; x++) { simd_for(int v   = NvLlB ;   v <= NvLuB ;   v++) { 

    // Magnetic Island
    Xi[z][y_k][x][v] = Phi0[z][y_k][x] - aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][x]; 

    G [z][y_k][x][v] = g[s][m][z][y_k][x][v] + sigma * Fields[Field::phi][s][m][z][y_k][x] * f0[s][m][z][y_k][x][v];

  } } // v, x
     
  // Note we have extended boundaries in X (NxLlB-2 -- NxLuB+2) for fields
  // Intel Inspector complains about useAp ? ... memory violation, is it true? 
  for(int v = NvLlB; v <= NvLuB; v++) {

    Xi[z][y_k][NxLlB-2:2][v] =  - aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][NxLlB-2:2];

    Xi[z][y_k][NxLuB+1:2][v] =  - aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][NxLuB+1:2];

  }
  
  } } // y_k, z

}

/* 
void VlasovIsland::Vlasov_2D_Island(
                           CComplex fs        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll[NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex nonLinearTerm                  [Nky][NxLD  ][NvLD],
                           const double MagIs[NxGB], const double dMagIs_dx[NxGB], 
                           const double X[NxGB+4], const double V[NvGB], const double M[NmGB],
                           const CComplex Psi0                    [NzLB][Nky][NxLB+4]      ,
                           CComplex Field0[Nq][NzLD][Nky][NxLD]   ,
                           const double dt, const int rk_step, const double rk[3])
{ 

  if((Nq > 1)) {
   Field0[Field::Ap][NzLlD:NzLD][:][NxLlD:NxLD] = Psi0[NzLlD:NzLD][:][NxLlD:NxLD];
  }

  for(int s = NsLlD; s <= NsLuD; s++) {
        
      // small abbrevations
      const double w_n   = species[s].w_n;
      const double w_T   = species[s].w_T;
      const double alpha = species[s].alpha;
      const double sigma = species[s].sigma;
      const double Temp  = species[s].T0;
      const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;

      const double kw_T = 1./Temp;

      bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      
      const double rho_t2 = species[s].T0 * species[s].m / (pow2(species[s].q) * plasma->B0); 


      for(int m=NmLlD; m<=NmLuD; m++) { for(int z=NzLlD; z<= NzLuD;z++) {  
        
        //calculate non-linear term (rk_step == 0 for eigenvalue calculations)
        if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(nullptr, nullptr, fs, Fields, z, m, s, nonLinearTerm, Xi_max, false); 
        
        #pragma omp for
        for(int y_k=NkyLlD; y_k<= NkyLuD;y_k++) {

        
        // Note : for negative modes we need to use complex conjugate value
        const CComplex ky     = _imag * fft->ky(y_k);
        
        // We need to take care of boundaries. For poloidal numbers y_k > N_k-1, we  use zero.
        // For y_k < 0, the corresponding complex conjugate value is used.
        // is ky_p1 = ky or not ?!
        const CComplex ky_1   = _imag * fft->ky(i);
        const CComplex ky_p1  = ((y_k+i) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+i);
        const CComplex ky_m1  = _imag * fft->ky(y_k-i); 
        
        //const CComplex ky_1   = _imag * fft->ky(1);
        //const CComplex ky_m1  = _imag * (y_k == 0    ) ? -fft-ky_1 : _imag * fft->ky(y_k-i); 
        //const CComplex ky_p1  = ((y_k+i) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+i);
        //const CComplex ky_m1  = _imag * fft->ky(y_k-i);

        
        for(int x = NxLlD; x <= NxLuD; x++) {  

          
          const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
          
          const CComplex dphi_dx  = (8.*(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])
                                      - (Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2])) * _kw_12_dx  ;  

          /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
        
          // NOTE :  at the Nyquist frequency we have no coupling with higher frequencies (actually phi(m=Ny) = 0. anyway)

          const CComplex     phi_p1 = ((y_k+i) >= Nky-1) ? 0.                                          : Fields[Field::phi][s][m][z][y_k+i][x] ;
          const CComplex     phi_m1 = ((y_k-i) <  0    ) ? conj(Fields[Field::phi][s][m][z][i-y_k][x]) : Fields[Field::phi][s][m][z][y_k-i][x] ;
          //const CComplex     phi_m1 = ( y_k ==  0   ) ? (Fields[Field::phi][s][m][z][1][x]) : Fields[Field::phi][s][m][z][y_k-1][x] ;

          
          // X-derivative (First Derivative with Central Difference 4th) of phi for poloidal mode +1, take care of Nyquist frequency
          const CComplex dphi_dx_p1 = ( (y_k+i) >= Nky-1) 
                                   ? 0.

                                   :      (8.*(Fields[Field::phi][s][m][z][y_k+i][x+1] - Fields[Field::phi][s][m][z][y_k+i][x-1]) 
                                            - (Fields[Field::phi][s][m][z][y_k+i][x+2] - Fields[Field::phi][s][m][z][y_k+i][x-2])) * _kw_12_dx  ;

          // X-derivative (1st deriv. CD-4 )of phi for poloidal mode -1, take care of complex conjugate relation for y_k=-1
          const CComplex dphi_dx_m1 = ( (y_k-i) < 0 ) 
                                   ?  conj(8.*(Fields[Field::phi][s][m][z][i-y_k][x+1] - Fields[Field::phi][s][m][z][i-y_k][x-1]) 
                                            - (Fields[Field::phi][s][m][z][i-y_k][x+2] - Fields[Field::phi][s][m][z][i-y_k][x-2])) * _kw_12_dx
                                   :      (8.*(Fields[Field::phi][s][m][z][y_k-i][x+1] - Fields[Field::phi][s][m][z][y_k-i][x-1]) 
                                            - (Fields[Field::phi][s][m][z][y_k-i][x+2] - Fields[Field::phi][s][m][z][y_k-i][x-2])) * _kw_12_dx ;
        
       
        // The magnetic island
    
        //  For the latter term, the intrigate derivative is the \partial_y * Island
        //  remember the island structure is 
        //  \partial_y (e^{imx} + e^{-imx}) = (i m) * ( e^{imx} - e^{-imx} )
        //
        const CComplex Island_phi =   dMagIs_dx[x] * ( ky_m1 * phi_m1 + ky_p1 * phi_p1) - MagIs[x] *  ky_1 * ( dphi_dx_m1 -  dphi_dx_p1);
        
             ///////////////////////////////////////////////////////////////////////////////
            
        const CComplex kp = geo->get_kp(x, ky, z);
           
        CComplex half_eta_kperp2_phi = 0;

        if(isGyro1) { // first order approximation for gyro-kinetics
             const CComplex ddphi_dx_dx = (16. *(Fields[Field::phi][s][m][z][y_k][x+1] + Fields[Field::phi][s][m][z][y_k][x-1]) 
                                              - (Fields[Field::phi][s][m][z][y_k][x+2] + Fields[Field::phi][s][m][z][y_k][x-2]) 
                                         - 30.*phi_) * _kw_12_dx_dx;

             half_eta_kperp2_phi     = rho_t2 * 0.5 * w_T  * ( (ky*ky) * phi_ + ddphi_dx_dx ) ; 
         }
             
        
        // velocity space magic
        simd_for(int v=NvLlD; v<= NvLuD;v++) {

            const CComplex g    = fs[s][m][z][y_k][x][v];
            const CComplex f0_  = f0[s][m][z][y_k][x][v];


        /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
      
          
        // X-derivative of f1 (1-CD4) for poloidal mode +1, take care of Nyquist frequency
        const CComplex dfs_dx_p1  =  ((y_k+i) >= Nky-i)

                            ? 0.

                            : (8. *(fs[s][m][z][y_k+i][x+1][v] - fs[s][m][z][y_k+i][x-1][v])  
                                 - (fs[s][m][z][y_k+i][x+2][v] - fs[s][m][z][y_k+i][x-2][v])) * _kw_12_dx;


        // X-derivative of f1 (1-CD4) for poloidal mode -1, take care of complex conjugate relation for y_k=-1 
        const CComplex dfs_dx_m1  =  ( (y_k-i) < 0  )

                        ? conj(8. *(fs[s][m][z][i-y_k][x+1][v] - fs[s][m][z][i-y_k][x-1][v])  
                                 - (fs[s][m][z][i-y_k][x+2][v] - fs[s][m][z][i-y_k][x-2][v])) * _kw_12_dx 

                        :     (8. *(fs[s][m][z][y_k-i][x+1][v] - fs[s][m][z][y_k-i][x-1][v])  
                                 - (fs[s][m][z][y_k-i][x+2][v] - fs[s][m][z][y_k-i][x-2][v])) * _kw_12_dx;

        // Note Nky-1 is the maximum mode number Nky = 6 i-> [ 0, 1, 2, 3, 4, 5] 
        const CComplex fs_p1      = ((y_k+i) >= Nky-1) ? 0.                             : fs[s][m][z][y_k+i][x][v] ;
        const CComplex fs_m1      = ((y_k-i) <  0    ) ? conj(fs[s][m][z][i-y_k][x][v]) : fs[s][m][z][y_k-i][x][v] ;
        //const CComplex fs_m1      = (y_k ==  0   ) ? (fs[s][m][z][1][x][v]) : fs[s][m][z][y_k-1][x][v] ;
         
        // Coupling of phase-space with Island mode-mode coupling
        const CComplex Island_g =  dMagIs_dx[x] * (ky_m1 * fs_m1  + ky_p1 * fs_p1 )  -  MagIs[x]  * ky_1 *  (dfs_dx_m1  - dfs_dx_p1 )  ;


        //// Hypervisocisty to stabilize simulation

        /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
             
        const CComplex dg_dt =
  //           +  hypvisc_phi
             -  alpha * V[v] * (Island_g + sigma * Island_phi * f0_) +                // Island term
             +  nonLinearTerm[y_k][x][v]                                             // Non-linear ( array is zero for linear simulations) 
             +  ky* (-(w_n + w_T * (((V[v]*V[v])+ M[m])*kw_T  - sub)) * f0_ * 
//                 phi_    )                     // Source term (Temperature/Density gradient)
//                (phi_ - (y_k == 1 ? V[v] * MagIs[x] : 0.  )  ) )                       // Source term (Temperature/Density gradient)
             phi_)                        // Source term (Temperature/Density gradient)
     //        -  half_eta_kperp2_phi * f0_)                                            // Contributions from gyro-1 (0 if not neq Gyro-1)
             -  alpha  * V[v]* kp  * ( g + sigma * phi_ * f0_)                        // Linear Landau damping
             +  Coll[s][m][z][y_k][x][v]  ;                                           // Collisional operator
         
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        


        //////////////////////////// Vlasov End ////////////////////////////
        //  time-integrate the distribution function    
        ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] + rk[1] * dg_dt             ;
        fss[s][m][z][y_k][x][v] = f1[s][m][z][y_k][x][v]         + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;
      
        }}} }}
   }

}

 * */

void VlasovIsland::Vlasov_2D_Island_orig(
                           CComplex fs        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex fss       [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f1  [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           CComplex ft        [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Coll[NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields[Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex nonLinearTerm                  [Nky][NxLD  ][NvLD],
                           const double MagIs[NxGB], const double dMagIs_dx[NxGB], 
                           const double X[NxGB+4], const double V[NvGB], const double M[NmGB],
                           const CComplex Psi0                    [NzLB][Nky][NxLB+4]      ,
                           CComplex Field0[Nq][NzLD][Nky][NxLD]   ,
                           const double dt, const int rk_step, const double rk[3])
{ 

  if((Nq > 1)) {
   Field0[Field::Ap][NzLlD:NzLD][:][NxLlD:NxLD] = Psi0[NzLlD:NzLD][:][NxLlD:NxLD];
  }


  for(int s = NsLlD; s <= NsLuD; s++) {
        
    // small abbreviations
    const double w_n   = species[s].w_n;
    const double w_T   = species[s].w_T;
    const double alpha = species[s].alpha;
    const double sigma = species[s].sigma;
    const double Temp  = species[s].T0;
    const double sub   = (species[s].doGyro) ? 3./2. : 1./2.;

    const double kw_T = 1./Temp;

    bool isGyro1 = (species[s].gyroModel == "Gyro-1");
      
    const double rho_t2 = species[s].T0 * species[s].m / (pow2(species[s].q) * plasma->B0); 

  for(int m=NmLlD; m<=NmLuD; m++) { for(int z=NzLlD; z<= NzLuD;z++) {  
        
 
    //calculate non-linear term (rk_step == 0 for eigenvalue calculations)
    if(doNonLinear && (rk_step != 0)) calculateExBNonLinearity(nullptr, nullptr, fs, Fields, z, m, s, nonLinearTerm, Xi_max, false); 
        
  #pragma omp for
  for(int y_k=NkyLlD; y_k<= NkyLuD;y_k++) {

    // Note : for negative modes we need to use complex conjugate value
    const CComplex ky     = _imag * fft->ky(y_k);
        
    // We need to take care of boundaries. For poloidal numbers y_k > N_k-1, we  use zero.
    // For y_k < 0, the corresponding complex conjugate value is used.
    // is ky_p1 = ky or not ?!
    const CComplex ky_1   = _imag * fft->ky(1);
    const CComplex ky_p1  = ((y_k+1) >= Nky-1) ? 0.    : _imag * fft->ky(y_k+1);
    const CComplex ky_m1  = _imag * fft->ky(y_k-1); 
        
  for(int x = NxLlD; x <= NxLuD; x++) {  

    const CComplex phi_ = Fields[Field::phi][s][m][z][y_k][x];
          
  /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
        
  
  // NOTE :  at the Nyquist frequency we have no coupling with higher frequencies (actually phi(m=Ny) = 0. anyway)

  
    const CComplex phi_p1 = ((y_k+1) >= Nky-1) ? 0.                                          : Fields[Field::phi][s][m][z][y_k+1][x] ;
    const CComplex phi_m1 = ((y_k-1) <  0    ) ? conj(Fields[Field::phi][s][m][z][1-y_k][x]) : Fields[Field::phi][s][m][z][y_k-1][x] ;

    // X-derivative (First Derivative with Central Difference 4th) of phi for poloidal mode +1, take care of Nyquist frequency
    const CComplex dphi_dx_p1 = ( (y_k+1) >= Nky-1) 
        ? 0.
        :      (8.*(Fields[Field::phi][s][m][z][y_k+1][x+1] - Fields[Field::phi][s][m][z][y_k+1][x-1]) 
                 - (Fields[Field::phi][s][m][z][y_k+1][x+2] - Fields[Field::phi][s][m][z][y_k+1][x-2])) * _kw_12_dx  ;

    // X-derivative (1st deriv. CD-4 )of phi for poloidal mode -1, take care of complex conjugate relation for y_k=-1
    const CComplex dphi_dx_m1 = ( (y_k-1) < 0 ) 
    
        ?  conj(8.*(Fields[Field::phi][s][m][z][1-y_k][x+1] - Fields[Field::phi][s][m][z][1-y_k][x-1]) 
                 - (Fields[Field::phi][s][m][z][1-y_k][x+2] - Fields[Field::phi][s][m][z][1-y_k][x-2])) * _kw_12_dx
        :      (8.*(Fields[Field::phi][s][m][z][y_k-1][x+1] - Fields[Field::phi][s][m][z][y_k-1][x-1]) 
                 - (Fields[Field::phi][s][m][z][y_k-1][x+2] - Fields[Field::phi][s][m][z][y_k-1][x-2])) * _kw_12_dx ;
        
    // The magnetic island
    
    //  For the latter term, the intrigate derivative is the \partial_y * Island
    //  remember the island structure is 
    //  \partial_y (e^{imx} + e^{-imx}) = (i m) * ( e^{imx} - e^{-imx} )
    //
    const CComplex Island_phi =  - dMagIs_dx[x] * ( ky_m1 * phi_m1 + ky_p1 * phi_p1) + MagIs[x] *  ky_1 * ( dphi_dx_m1 -  dphi_dx_p1);
        
    ///////////////////////////////////////////////////////////////////////////////
            
    const CComplex kp = geo->get_kp(x, ky, z);
   
    /* 
    CComplex half_eta_kperp2_phi = 0;
        if(isGyro1) { // first order approximation for gyro-kinetics
             const CComplex ddphi_dx_dx = (16. *(Fields[Field::phi][s][m][z][y_k][x+1] + Fields[Field::phi][s][m][z][y_k][x-1]) 
                                              - (Fields[Field::phi][s][m][z][y_k][x+2] + Fields[Field::phi][s][m][z][y_k][x-2]) 
                                         - 30.*phi_) * _kw_12_dx_dx;
             half_eta_kperp2_phi     = rho_t2 * 0.5 * w_T  * ( (ky*ky) * phi_ + ddphi_dx_dx ) ; 
         }
     * */
             
     
    // velocity space magic
    simd_for(int v = NvLlD; v <= NvLuD; v++) {

      const CComplex g    = fs[s][m][z][y_k][x][v];
      const CComplex f0_  = f0[s][m][z][y_k][x][v];

      /////////////////////////////////////////////////// Magnetic Island Contribution    /////////////////////////////////////////
      
      // X-derivative of f1 (1-CD4) for poloidal mode +1, take care of Nyquist frequency
      const CComplex dfs_dx_p1  =  ((y_k+1) >= Nky-1)

        ? 0.
        
        : (8. *(fs[s][m][z][y_k+1][x+1][v] - fs[s][m][z][y_k+1][x-1][v])  
             - (fs[s][m][z][y_k+1][x+2][v] - fs[s][m][z][y_k+1][x-2][v])) * _kw_12_dx;

      // X-derivative of f1 (1-CD4) for poloidal mode -1, take care of complex conjugate relation for y_k=-1 
      const CComplex dfs_dx_m1  =  ( (y_k-1) < 0  )

         ? conj(8. *(fs[s][m][z][1-y_k][x+1][v] - fs[s][m][z][1-y_k][x-1][v])  
                  - (fs[s][m][z][1-y_k][x+2][v] - fs[s][m][z][1-y_k][x-2][v])) * _kw_12_dx 

         :     (8. *(fs[s][m][z][y_k-1][x+1][v] - fs[s][m][z][y_k-1][x-1][v])  
                  - (fs[s][m][z][y_k-1][x+2][v] - fs[s][m][z][y_k-1][x-2][v])) * _kw_12_dx;

      // Note Nky-1 is the maximum mode number Nky = 6 i-> [ 0, 1, 2, 3, 4, 5] 
      const CComplex fs_p1 = ((y_k+1) >= Nky-1) ? 0.                             : fs[s][m][z][y_k+1][x][v] ;
      const CComplex fs_m1 = ((y_k-1) <  0    ) ? conj(fs[s][m][z][1-y_k][x][v]) : fs[s][m][z][y_k-1][x][v] ;
         
      // Coupling of phase-space with Island mode-mode coupling
      //const CComplex Island_g =  dMagIs_dx[x] * (ky_m1 * fs_m1  + ky_p1 * fs_p1 ) -  MagIs[x]  * ky_1 *  (dfs_dx_m1  - dfs_dx_p1 )  ;
      const CComplex Island_g =  - dMagIs_dx[x] * (ky_m1 * fs_m1  + ky_p1 * fs_p1 ) +  MagIs[x]  * ky_1 *  (dfs_dx_m1  - dfs_dx_p1 )  ;
      //signconst CComplex Island_g =  dMagIs_dx[x] * (ky_m1 * fs_m1  + ky_p1 * fs_p1 )  +  MagIs[x]  * ky_1 *  (dfs_dx_m1  - dfs_dx_p1 )  ;


      //// Hypervisocisty to stabilize simulation
      //  const double hypvisc_phi_val = -1.e-5;
      //  
      //  const CComplex d4_dx_phi    = (-39. *(Fields[Field::phi][s][m][z][y_k][x+1] - Fields[Field::phi][s][m][z][y_k][x-1])  
      //                                + 12. *(Fields[Field::phi][s][m][z][y_k][x+2] - Fields[Field::phi][s][m][z][y_k][x-2]) 
      //                                + 56. * Fields[Field::phi][s][m][z][y_k][x  ])/pow4(dx);
      //
      //  const CComplex hypvisc_phi    = hypvisc_phi_val * ( d4_dx_phi + pow4(ky) * phi_);

    /////////////// Finally the Vlasov equation calculate the time derivatve      //////////////////////
             
    const CComplex dg_dt =
    //           +  hypvisc_phi
       -  alpha * V[v] * (Island_g + sigma * Island_phi * f0_) +          // Island term
       +  nonLinearTerm[y_k][x][v]                                        // Non-linear ( array is zero for linear simulations) 
       -  ky* (w_n + w_T * (((V[v]*V[v])+ M[m])*kw_T  - sub)) * f0_ * 
    //  (phi_ - (y_k == 1 ? V[v] * MagIs[x] : 0.  )  ) )                  // Source term (Temperature/Density gradient)
          phi_                                                           // Source term (Temperature/Density gradient)
       -  alpha  * V[v]* kp  * ( g + sigma * phi_ * f0_)                  // Linear Landau damping
       +  Coll[s][m][z][y_k][x][v]  ;                                     // Collisional operator
     //        -  half_eta_kperp2_phi * f0_)                              // Contributions from gyro-1 (0 if not neq Gyro-1)
         
        //////////////////////////////////////////////////////////////////////////////////////////////////////
        


    //////////////////////////// Vlasov End ////////////////////////////
    //  time-integrate the distribution function    
    ft [s][m][z][y_k][x][v] = rk[0] * ft[s][m][z][y_k][x][v] + rk[1] * dg_dt             ;
    fss[s][m][z][y_k][x][v] = f1[s][m][z][y_k][x][v]         + (rk[2] * ft[s][m][z][y_k][x][v] + dg_dt) * dt;
      
    
    } } } } }
   
  } // s

}


void VlasovIsland::setupXiAndG(
                           const CComplex g          [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex f0         [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Fields [Nq][NsLD][NmLD][NzLB][Nky][NxLB+4]      ,
                           CComplex Xi                           [NzLB][Nky][NxLB+4][NvLB],
                           CComplex G                            [NzLB][Nky][NxLB  ][NvLB],
                           const CComplex Phi0                   [NzLB][Nky][NxLB+4]      ,
                           const int m, const int s) 
{
  const double alpha = species[s].alpha;
  const double sigma = species[s].sigma;
  
  const double aeb   =  alpha * geo->eps_hat * plasma->beta; 
  const double saeb  =  sigma * aeb;

  const bool useAp   = (Nq >= 2);
  const bool useBp   = (Nq >= 3);


  // ICC vectorizes useAp/useBp into separate lopps, check for any speed penelity ? 
  #pragma omp for collapse(2)
  for(int z = NzLlB; z <= NzLuB; z++) {      for(int y_k = NkyLlD; y_k <= NkyLuD; y_k++) { 
  for(int x = NxLlB; x <= NxLuB; x++) { simd_for(int v   = NvLlB ;   v <= NvLuB ;   v++) { 

     Xi[z][y_k][x][v] = Fields[Field::phi][s][m][z][y_k][x] - (useAp ? aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][x] : 0.) 
                                                            - (useBp ? aeb*M[m]*Fields[Field::Bp][s][m][z][y_k][x] : 0.);

    // Island testing    
    G [z][y_k][x][v] = g[s][m][z][y_k][x][v] + sigma * Fields[Field::phi][s][m][z][y_k][x] * f0[s][m][z][y_k][x][v];

     // substract canonical momentum to get "real" f1 (not used "yet")
     // f1[z][y_k][x][v] = g[s][m][z][y_k][x][v] - (useAp ? saeb * V[v] * f0[s][n][z][y_k][x][v] * Ap[s][m][z][y_k][x] : 0.);
      
  } } // v, x
     
  // Note we have extended boundaries in X (NxLlB-2 -- NxLuB+2) for fields
  // Intel Inspector complains about useAp ? ... memory violation, is it true? 
  simd_for(int v = NvLlB; v <= NvLuB; v++) {

    Xi[z][y_k][NxLlB-2:2][v] = Fields[Field::phi][s][m][z][y_k][NxLlB-2:2]  - (useAp ? aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][NxLlB-2:2] : 0.)
                                                                            - (useBp ? aeb*M[m]*Fields[Field::Bp][s][m][z][y_k][NxLlB-2:2] : 0.);

    Xi[z][y_k][NxLuB+1:2][v] = Fields[Field::phi][s][m][z][y_k][NxLuB+1:2]  - (useAp ? aeb*V[v]*Fields[Field::Ap][s][m][z][y_k][NxLuB+1:2] : 0.) 
                                                                            - (useBp ? aeb*M[m]*Fields[Field::Bp][s][m][z][y_k][NxLuB+1:2] : 0.);
  }
  
  } } // y_k, z

};



void VlasovIsland::loadMHDFields(std::string mhd_filename, 
     LinearInterpolation<double, CComplex> *inter_phi, LinearInterpolation<double, CComplex> *inter_psi) 
{
    
    // open and access file
    hid_t file    = H5Fopen(mhd_filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);

    // Load fields (phi and psi) assuming that they are the same shape !
    //hid_t dataset_phi = H5Dopen(file, "/Field/Phi", H5P_DEFAULT);
    hid_t dataset_psi = H5Dopen(file, "/Field/Psi", H5P_DEFAULT);
 
    hid_t filespace = H5Dget_space(dataset_psi);   
    int rank        = H5Sget_simple_extent_ndims(filespace);
    
    hsize_t dims[rank];
    herr_t status        = H5Sget_simple_extent_dims(filespace, dims, NULL);

    int N = dims[1];

    hsize_t dims_chunk[] = { dims[1] };
    hid_t memspace       = H5Screate_simple(1, dims_chunk, NULL);
    
    // Define offset and count to access last time-step
    hsize_t offset[] = {1, 0, dims[2]-1};
    hsize_t count [] = {1, N, 1        };
  
    CComplex data_psi[N], data_phi[N]; double X[N];

    /// Load data
    H5LTget_attribute_double(file, "Init", "X", X );

    // Note : We only load m=1 mode !
    status = H5Sselect_hyperslab(filespace, H5S_SELECT_SET, offset, NULL, count, NULL);
    status = H5Dread(dataset_psi, H5T_NATIVE_DOUBLE, memspace, filespace, H5P_DEFAULT, data_psi);

    // Setup Interpolation routine
    inter_phi = new LinearInterpolation<double, CComplex>(N, X, data_psi);


    H5Dclose(dataset_psi);
    H5Sclose(filespace);
    H5Sclose(memspace);
    H5Fclose(file);

  return;


}
