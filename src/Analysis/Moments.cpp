/*
 * =====================================================================================
 *
 *       Filename: Moments.cpp
 *
 *    Description: Calculation of the Moments of the Phase-space function
 *
 *         Author: Paul P. Hilscher (2012-), 
 *
 *        License: GPLv3+
 * =====================================================================================
 */

#include "Analysis/Moments.h"
  
  
Moments::Moments(Setup *setup, Vlasov *_vlasov, Fields *_fields, Grid *_grid, Parallel *_parallel) 
: vlasov(_vlasov), fields(_fields), grid(_grid), parallel(_parallel) 
{

  doFieldCorrections = setup->get("Moments.FieldCorrections", 1);

}

void Moments::getMoments(const CComplex     f    [NsLD][NmLD][NzLB][Nky][NxLB  ][NvLB],
                         const CComplex Field0[Nq][NzLD][Nky][NxLD],
                               CComplex Mom[8][NsLD][NzLD][Nky][NxLD]) 
{

  getMoment(f, Field0, Mom, 0, 0, 0); 
  getMoment(f, Field0, Mom, 2, 0, 1); 
  getMoment(f, Field0, Mom, 0, 2, 2); 

  getMoment(f, Field0, Mom, 1, 0, 3); 
  getMoment(f, Field0, Mom, 3, 0, 4); 
  getMoment(f, Field0, Mom, 1, 2, 5); 
  
  getMoment(f, Field0, Mom, 2, 2, 6); 
  getMoment(f, Field0, Mom, 0, 4, 7); 

}

void Moments::getMoment(const CComplex     f    [NsLD][NmLD][NzLB][Nky][NxLB][NvLB],
                        const CComplex  Field0[Nq][NzLD][Nky][NxLD],
                        CComplex Mom[8][NsLD][NzLD][Nky][NxLD], const int a, const int b, const int idx)
{
  Mom[idx][:][:][:][:] = 0.;
 
  // temporary arrays for integration over \mu
  // Need Nq in order to let gyro-averaging work for Nq > 1
  CComplex Mom_m[Nq][NzLD][Nky][NxLD];
  
  // We have set to zero, because gyro-averaging operator currently requires size [Nq][...].
  // and surplus arrays may contain NaN
  if(Nq > 1) Mom_m[1:Nq-1][:][:][:] = 0.;

  for(int s = NsLlD; s <= NsLuD; s++) {
    
  // Scaling terms (1) 
  const double rho_L_ref = plasma->rho_ref / plasma->L_ref; 
  const double d_pre     = rho_L_ref * plasma->n_ref * species[s].n0 * pow(plasma->c_ref * species[s].v_th, a+b);
  
  // integrate over the first adiabatic invariant \mu
  for(int m = NmLlD; m <= NmLuD; m++) {
  
  const double d_DK = d_pre * M_PI * dv * grid->dm[m] * pow(plasma->B0, b/2);
  
  // calculate drift-kinetic moments (in gyro-coordinates) 
  for(int z = NzLlD; z <= NzLuD; z++) { for(int y_k = 0; y_k < Nky-1; y_k++) { 
  for(int x = NxLlD; x <= NxLuD; x++) { 
     
    Mom_m[0][z-NzLlD][y_k][x-NxLlD] = __sec_reduce_add(pow(M[m], b/2.) * pow(V[NvLlD:NvLD],a) 
                                                       * f[s][m][z][y_k][x][NvLlD:NvLD]);
  } } } // z, y_k, x
  
    // gyro-average moments (push back to drift-coordinates)
    #pragma omp parallel
    fields->gyroAverage(Mom_m, Mom_m, m, s, false, true);

    Mom[idx][s-NsLlD][:][:][:] += Mom_m[0][:][:][:] * d_DK;

  } // m

  // all operators are linear in v,m thus we sum here 
  parallel->reduce(&Mom[idx][s-NsLlD][0][0][0], Op::sum, DIR_VM, NzLD * Nky * NxLD); 


  //////////////////////////////////////////////////////////////////////////
  // add field corrections (necessary for k_perp^2 > 1)
  if(doFieldCorrections) {
 
    // pre-factors for field corrections (note : x-dependence of T neglected)
    const double d_FC = d_pre;// * species[s].n0/species[s].T0;
    
    double bT_q_B2vth = plasma->beta * species[s].T0/pow2(plasma->B0) 
                        / (species[s].q  * species[s].v_th);

    CComplex AAphi[Nq][NzLD][Nky][NxLD],  phi0[Nq][NzLD][Nky][NxLD],
             j0_par[NzLD][Nky][NxLD];

    phi0[0][:][:][:] = Field0[Field::phi][NzLlD:NzLD][:][NxLlD:NxLD];

    // The equilibrium current forms the equilibrium magnetic field.
    // As this current is stationary and handles the background magnetic field,
    // we can set it to zero. D. Told (PhD thesis, p.31) has some discussions about it.
    j0_par[:][:][:] = 0.;

    // calculate double gyro-average 
    fields->doubleGyroExp(phi0, AAphi, b/2, s);
    
    // add field corrections from gyro-kinetic effects 
    Mom[idx][s-NsLlD][:][:][:] -= d_FC * (Y(a) + Y(a+1) * bT_q_B2vth * j0_par[:][:][:]) *
                          (species[s].q * (phi0[0][:][:][:] -  AAphi[0][:][:][:]));

  } // doFieldCorrections
  } // s
}

